// Full Fortran backend (--fortran-api full): a library-shaped Fortran 2018 module over the shared C
// ABI library (cabi_full.hpp). Native classes become derived types extending rosetta_object, with
// type-bound procedures, C++ inheritance as `extends` and reference-counted copies; overloads and
// default arguments become specific procedures behind generic interfaces (checked to be
// distinguishable); constructors are generic functions named after the type; containers become
// arrays and small generated derived types; enums are named integer constants; callbacks are
// procedure arguments; Fortran extensions of director classes override virtual methods; and every
// procedure carries the C++ documentation as FORD `!>` comments.
//
// It writes the C++ library source (and --parts) and one Fortran source with the runtime module
// (fortran_full_runtime_f90.hpp), the main module and one module per policy submodule. Arguments
// are encoded and results decoded statically from the declared types.
#include "rosetta/fortran.hpp"
#include "cabi_full.hpp"
#include "fortran_full_runtime_f90.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\n");
    const auto last = text.find_last_not_of(" \t\n");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
// Names Fortran accepts: ASCII letters, digits and _, a letter first, at most 63 characters.
std::string fortran_identifier(const std::string& text) {
    std::string name;
    for (const char c : text) name += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
    if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0]))) name = "x" + name;
    return name;
}
std::string fit(std::string name) {
    if (name.size() <= 63) return name;
    unsigned long hash = 5381;
    for (const char c : name) hash = hash * 33 + static_cast<unsigned char>(c);
    char suffix[16];
    std::snprintf(suffix, sizeof suffix, "_%08lx", hash & 0xffffffffUL);
    return name.substr(0, 63 - 9) + suffix;
}
// A Fortran character literal.
std::string f_string(const std::string& text) {
    std::string result = "'";
    for (const char c : text) {
        if (c == '\'') result += "''";
        else if (c == '\n') result += ' ';
        else result += c;
    }
    return result + "'";
}
// Intrinsic procedures the generated procedures call: no generated name may hide them. Intrinsic
// type names cannot name derived types.
bool hides_intrinsic(const std::string& name) {
    static const std::set<std::string> names = {"size", "present", "allocated", "trim", "int", "real", "len", "integer",
        "logical", "complex", "character", "doubleprecision", "doublecomplex", "byte", "type", "class", "allocate", "status"};
    return names.count(lower(name)) != 0;
}
// Names the generated code uses from iso_c_binding, the runtime and the module itself.
bool reserved_name(const std::string& name) {
    const auto small = lower(name);
    static const std::set<std::string> names = {"c_double", "c_float", "c_int64_t", "rosetta_error", "rosetta_string",
        "rosetta_object", "rosetta_raise", "rosetta_same", "iso_c_binding", "iso_fortran_env"};
    return names.count(small) || small.rfind("rs_", 0) == 0 || hides_intrinsic(small);
}

// A user name that may not be reserved: rs_* names get an x prefix, others a trailing _.
std::string escape(std::string name) {
    if (lower(name).rfind("rs_", 0) == 0) name = "x" + name;
    while (reserved_name(name)) name += "_";
    return name;
}

// Case-insensitive name registries (a module's names, or a type's bindings).
struct Names {
    std::set<std::string> taken;
    bool has(const std::string& name) const { return taken.count(lower(name)) != 0; }
    void add(const std::string& name) { taken.insert(lower(name)); }
    // `wanted`, or wanted_, wanted__, ... (at most 63 characters).
    // Internal names (rs_...) are claimed as they are; names made from C++ names are escaped, so
    // they never start with rs_ or hide what generated code uses.
    std::string claim(const std::string& wanted, bool internal = false) {
        auto name = fit(internal ? wanted : escape(wanted));
        while (has(name) || (!internal && reserved_name(name))) name = fit(name.size() < 63 ? name + "_" : name.substr(0, 50) + "_" + std::to_string(taken.size()));
        add(name);
        return name;
    }
};

// ------------------------------------------------------------------ the Fortran view of C++ types
enum class K { none, integer, int64, real, real32, logical, complex, complex32, character, string, enumeration, object, vector,
               map, tuple, optional, callback, function };
struct FT {
    K kind = K::none;
    std::string name;         // object/function: Fortran type; enumeration: qualified C++ enum; callback: interface
    std::vector<FT> items;    // vector, optional: element; map: key, value; tuple: items; callback: result (none = void), arguments
    bool big = false;         // enumeration: values need integer(c_int64_t)
    bool pair = false;        // tuple: from std::pair (first, second)
    bool nullable = false;    // object: a pointer (an optional argument; a null result is an unset object)
    bool writeback = false;   // a non-const lvalue reference parameter: C++ updates it
    int sig = 0;              // callback: signature index
    std::string helper;       // map, tuple, optional, vector element (vector of a container), function: the generated type
};
bool composite(const FT& t) { return t.kind == K::vector || t.kind == K::map || t.kind == K::tuple || t.kind == K::optional; }

// Splits a spelled template argument list ("std::vector<std::int64_t>" -> {"std::int64_t"}).
std::vector<std::string> spelled_arguments(const std::string& spelling) {
    std::vector<std::string> result;
    const auto open = spelling.find('<');
    const auto close = spelling.rfind('>');
    if (open == std::string::npos || close == std::string::npos || close < open) return result;
    int depth = 0;
    std::string current;
    for (std::size_t i = open + 1; i < close; ++i) {
        const char c = spelling[i];
        if (c == '<' || c == '(') ++depth;
        if (c == '>' || c == ')') --depth;
        if (c == ',' && depth == 0) { result.push_back(trim(current)); current.clear(); continue; }
        current += c;
    }
    if (!trim(current).empty()) result.push_back(trim(current));
    return result;
}
// Integers spelled as 64-bit types keep 64 bits; every other integer is a default integer.
bool wide_spelling(const std::string& spelling) {
    for (const char* wide : {"int64_t", "long long", "intptr_t", "ptrdiff_t", "intmax_t", "ssize_t", "uintmax_t"})
        if (spelling.find(wide) != std::string::npos) return true;
    return false;
}

// ------------------------------------------------------------------ documentation (FORD)
std::string doc_block(const DocBlock& block) {
    if (block.kind == "code" || block.kind == "math") return "```" + (block.kind == "math" ? std::string("math") : block.language) + "\n" + block.text + "\n```";
    if (block.kind == "heading") return "**" + block.text + "**";
    return block.text;
}
// Text as `!>` lines (prose wrapped at 100 columns; code blocks kept), `indent` before each.
std::string comment_lines(const std::string& text, const std::string& indent) {
    std::string out;
    std::istringstream lines(text);
    bool code = false;
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("```", 0) == 0) code = !code;
        if (code || line.rfind("```", 0) == 0 || line.size() <= 100) {
            out += indent + "!>" + (line.empty() ? "" : " " + line) + "\n";
            continue;
        }
        std::string lead;
        while (lead.size() < line.size() && line[lead.size()] == ' ') lead += ' ';
        std::string rest = line;
        bool first = true;
        while (rest.size() > 100) {
            auto split = rest.rfind(' ', 100);
            if (split == std::string::npos || split <= lead.size()) split = rest.find(' ', 100);
            if (split == std::string::npos) break;
            out += indent + "!> " + rest.substr(0, split) + "\n";
            rest = (first && rest.rfind("- ", lead.size()) == lead.size() ? lead + "  " : lead) + trim(rest.substr(split + 1));
            first = false;
        }
        out += indent + "!> " + rest + "\n";
    }
    return out;
}
// A declaration's documentation: summary, details, notes, see-also (parameters are added after).
struct Doc {
    std::string summary;
    std::vector<std::string> paragraphs;
    std::vector<std::pair<std::string, std::string>> params;
    std::string returns;
    std::vector<std::string> throws;
    std::string render(const std::string& indent) const {
        std::string text = summary;
        for (const auto& paragraph : paragraphs) text += "\n\n" + paragraph;
        std::string tags;
        for (const auto& [name, about] : params) tags += "\n@param " + name + (about.empty() ? "" : " " + about);
        if (!returns.empty()) tags += "\n@return " + returns;
        for (const auto& item : throws) tags += "\n@throws " + item;
        if (!tags.empty()) text += "\n" + tags;
        return comment_lines(text, indent);
    }
};
Doc describe(const Documentation& doc, const std::string& fallback) {
    Doc result;
    std::vector<std::string> details;
    for (const auto& block : doc.details) details.push_back(doc_block(block));
    if (!doc.brief.empty()) result.summary = one_line(doc.brief);
    else if (!doc.details.empty() && doc.details.front().kind == "text") { result.summary = one_line(doc.details.front().text); details.erase(details.begin()); }
    else result.summary = fallback;
    result.paragraphs = details;
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        result.paragraphs.push_back("@" + (item.name == "warning" || item.name == "note" || item.name == "deprecated" ? item.name + " " : "note **" + label + ":** ") + one_line(item.text));
    }
    if (!doc.see.empty()) {
        std::string see = "See also: ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + std::string("`") + doc.see[i] + "`";
        result.paragraphs.push_back(see + ".");
    }
    for (const auto& item : doc.throws)
        result.throws.push_back((item.name.empty() ? std::string() : item.name + (item.text.empty() ? "" : " ")) + one_line(item.text));
    return result;
}

// Keeps free-form lines within 132 columns (comments are already short).
std::string wrapped(const std::string& source) {
    std::istringstream lines(source);
    std::ostringstream out;
    std::string line;
    while (std::getline(lines, line)) {
        const auto code = line.find_first_not_of(' ');
        if (code != std::string::npos && line[code] == '!') { out << line << '\n'; continue; }
        while (line.size() > 120) {
            char quoted = 0;
            std::size_t split = std::string::npos;
            for (std::size_t i = 0; i < std::min<std::size_t>(line.size(), 116); ++i) {
                if (quoted) { if (line[i] == quoted) quoted = 0; }
                else if (line[i] == '\'' || line[i] == '"') quoted = line[i];
                else if (line[i] == ' ' || line[i] == ',') split = i + 1;
            }
            if (split == std::string::npos || split < 5) throw BindingError("cannot wrap generated Fortran source line");
            out << line.substr(0, split) << " &\n";
            line = "    & " + line.substr(split);
        }
        out << line << '\n';
    }
    return out.str();
}

// ------------------------------------------------------------------ distinguishability (F2018 15.4.3.4.5)
struct Dummy {
    std::string name;
    std::string type;     // integer, real, complex, logical, character, or the derived type
    int kind = 0;
    int rank = 0;
    bool poly = false;    // class(T)
    bool optional = false;
    bool procedure = false;
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const FortranFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedFortranFullSource run();

private:
    FortranFullOptions options_;
    std::string fmodule_;                        // the main Fortran module
    Names names_;                                // the main module's names
    std::map<std::string, std::string> parents_; // lower(derived type) -> lower(parent type)
    std::map<std::string, std::string> enum_values_type_; // qualified enum -> integer declaration

    // --- hooks
    std::string language() const override { return "Fortran"; }
    std::string target_name(const std::string& cpp) const override { return fortran_identifier(cpp); }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override {
        static const std::map<std::string, std::pair<const char*, const char*>> names = {
            {"operator+", {"+", "+"}}, {"operator-", {"-", "-"}}, {"operator*", {nullptr, "*"}}, {"operator/", {nullptr, "/"}},
            {"operator==", {nullptr, "=="}}, {"operator!=", {nullptr, "/="}}, {"operator<", {nullptr, "<"}},
            {"operator<=", {nullptr, "<="}}, {"operator>", {nullptr, ">"}}, {"operator>=", {nullptr, ">="}},
            {"operator!", {".not.", nullptr}}};
        if (cpp == "operator[]") return "item";
        if (cpp == "operator()") return "invoke";
        const auto found = names.find(cpp);
        if (found == names.end() || arity > 1) return {};
        const char* result = arity == 0 ? found->second.first : found->second.second;
        return result ? result : "";
    }
    bool free_operator(const std::string& name) const override { return name != "item" && name != "invoke"; }
    std::string static_constant_name(const ClassPlan& plan, const std::string& cpp) const override { return plan.name + "_" + cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override;
    std::string signatures(const Group& group, bool is_method) const override;
    std::string constructor_signatures(const ClassPlan& plan) const override;
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: extend it in Fortran and call " + lower(plan.name) + "_init on the extension";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: extend it in Fortran and call " + lower(plan.name) + "_init on the extension";
    }
    std::string symbol_prefix() const override { return "rosetta_f90_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_FORTRAN"; }
    std::string module_prefix() const override { return "rosetta_fortran_module_"; }

    // --- types
    FT convert(const Type& type, Use use, std::string& why, const std::string& spelling = {});
    FT spelled(std::string cpp, Use use, std::string& why);
    std::string mangle(const FT& t) const;
    std::string helper_for(FT& t);               // the generated type of a map, tuple, optional or vector element
    std::string scalar_decl(const FT& t) const;
    struct Decl { std::string spec, dims; };
    Decl value_decl(const FT& t) const;          // a result, component or local
    Decl array_decl(const FT& element) const;    // an allocatable array of values
    Decl param_decl(const FT& t) const;          // a dummy argument
    Dummy dummy(const FT& t, const std::string& name) const;
    std::string shown(const FT& t) const;        // in messages
    std::string helpers_;                        // generated helper type definitions
    std::map<std::string, std::string> helper_names_; // mangled -> name
    bool extends(const std::string& derived, const std::string& base) const;
    bool tkr(const Dummy& a, const Dummy& b) const;
    bool distinguishable(const Dummy& a, const Dummy& b) const { return a.procedure != b.procedure || (!a.procedure && !tkr(a, b) && !tkr(b, a)); }
    bool distinguishable(const std::vector<Dummy>& p, const std::vector<Dummy>& q) const;

    // --- callbacks
    struct Signature { FT type; std::string iface, store, add, dispatch; };
    std::vector<Signature> signatures_;
    std::map<std::string, int> signature_index_;
    int callback(FT& t);

    // --- encoding and decoding (statements)
    struct Body {
        std::ostringstream code;
        int loops = 0;                           // nesting of rs_i<n>/rs_n<n> loop variables in use
        int max_loop = 0;
        std::set<std::string> locals;
        std::string indent = "    ";
        void line(const std::string& text) { code << indent << text << "\n"; }
        int open() { ++loops; max_loop = std::max(max_loop, loops); return loops; }
        void close() { --loops; }
        std::set<int> counts;                    // loop levels using rs_n<n>
        std::string loop_decls() const {
            std::string text;
            for (int i = 1; i <= max_loop; ++i)
                text += "    integer :: rs_i" + std::to_string(i) + (counts.count(i) ? ", rs_n" + std::to_string(i) : std::string()) + "\n";
            return text;
        }
    };
    void encode(Body& b, const FT& t, const std::string& expr, bool param, int argument, const std::string& writer = "rs_w");
    void decode(Body& b, const FT& t, const std::string& target, const std::string& reader = "rs_r");
    std::string decl_line(const Decl& d, const std::string& name) const { return d.spec + " :: " + name + d.dims; }

    // --- procedures
    struct Arg { std::string name; FT type; };
    struct Specific {
        const Overload* overload = nullptr;
        std::size_t arity = 0;
        std::vector<Arg> args;
        std::vector<Dummy> dummies;              // without self, with status
        bool function = false;
        FT result;
        std::string proc;                        // module procedure
        std::string binding;                     // type-bound specific binding
    };
    // Specifics of a group (one per overload and arity), unsupported and ambiguous ones dropped.
    std::vector<Specific> specifics(const std::vector<Overload>& overloads, const std::string& context, bool self, bool is_operator,
                                    bool constructor, std::vector<Specific>* compare = nullptr);
    std::string interface_text(const Specific& s, const std::string& self) const;
    struct Call {
        std::string proc;
        std::string self_class;                  // "" = no self
        std::string self_spec;                   // the self declaration's attributes
        std::size_t index = 0;
        const Specific* specific = nullptr;
        int mode = 0;                            // 0 call, 1 constructor, 2 director init, 3 field getter, 4 field setter, 5 constant, 6 native function
        FT result;                               // getters and constants
        FT value;                                // setters
        bool status = true;
        std::string doc;
        std::string result_type;                 // constructors and init: the class
    };
    std::string emit_call(const Call& call);
    Doc specific_doc(const Specific& s, const std::string& fallback) const;

    // --- output
    std::ostringstream types_, interfaces_, procedures_, publics_;
    struct ModuleOut { std::string name; std::ostringstream interfaces, procedures; std::set<std::string> uses; Names names; std::vector<std::string> publics; };
    std::map<std::string, ModuleOut> submodules_;
    std::map<const Function*, std::string> override_binding_;   // method -> binding the dispatcher calls
    std::map<std::string, std::map<std::string, std::string>> bindings_; // class -> lower(binding) -> interface ("" generic, "*" reserved)
    std::map<std::string, std::vector<std::string>> binding_specifics_;  // class/generic -> specific bindings
    void plan_class(const std::string& owner, std::ostringstream& members, std::ostringstream& generics);
    void emit_overrides(std::ostringstream& out);
    std::map<std::string, std::vector<std::pair<std::string, Specific>>> operator_specifics_; // operator -> (class, specific)
    std::vector<std::string> native_function_types_;
    std::string emit_native_function(FT& t);
    std::string type_of(const std::string& owner) const { return classes_.at(owner).name; }
};

// ------------------------------------------------------------------ type conversion
FT Generator::convert(const Type& type, Use use, std::string& why, const std::string& spelling_hint) {
    FT t;
    const bool parameter = use == Use::parameter;
    const auto& target = referent(type);
    if (type.kind == TypeKind::lvalue_reference && !target.is_const && parameter && !bound(target) && target.kind != TypeKind::pointer) {
        auto inner = convert(target, use, why, spelling_hint.empty() ? target.spelling : spelling_hint);
        if (inner.kind == K::callback) { why = "references to callbacks are unsupported in Fortran"; return {}; }
        inner.writeback = true;
        return inner;
    }
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") { t.kind = K::string; return t; }
        if (bound(pointee)) { t = convert(pointee, use, why); t.nullable = true; return t; }
        why = "unsupported Fortran pointer type " + type.canonical;
        return {};
    }
    if (is_void(target)) return t;
    const auto converted = conversion(target);
    if (!converted.empty()) return spelled(policy_.conversions.at(converted), use, why);
    const auto bare = normalize(strip_cv(target.canonical));
    const auto spelling = spelling_hint.empty() ? target.spelling : spelling_hint;
    switch (target.kind) {
    case TypeKind::builtin:
        if (bare == "bool") t.kind = K::logical;
        else if (bare == "char") t.kind = K::character;
        else if (bare == "double" || bare == "long double") t.kind = K::real;
        else if (bare == "float") t.kind = K::real32;
        else if (bare == "long long" || bare == "unsigned long long" || ((bare == "long" || bare == "unsigned long") && wide_spelling(spelling)))
            t.kind = K::int64;
        else if (bare.find("int") != std::string::npos || bare.find("short") != std::string::npos || bare.find("long") != std::string::npos ||
                 bare.find("char") != std::string::npos)
            t.kind = K::integer;
        else why = "unsupported Fortran value type " + type.canonical;
        return t;
    case TypeKind::enumeration:
        t.kind = K::enumeration;
        t.name = target.declaration;
        if (!enum_names_.count(t.name)) { why = "enum " + t.name + " is not exported to Fortran"; return {}; }
        t.big = enum_values_type_[t.name] != "integer";
        return t;
    case TypeKind::record:
        if (bound(target)) { t.kind = K::object; t.name = classes_.at(target.declaration).name; return t; }
        if (bare == "std::filesystem::path") { t.kind = K::string; return t; }
        why = "class " + strip_cv(target.canonical) + " is not exported to Fortran";
        return {};
    case TypeKind::template_instance: {
        const auto declaration = normalize(target.declaration);
        const auto hints = spelled_arguments(spelling);
        const auto arg = [&](std::size_t i, Use inner) {
            if (i >= target.arguments.size()) { why = "incomplete template arguments in " + target.canonical; return FT{}; }
            auto item = convert(target.arguments[i], inner, why, i < hints.size() ? hints[i] : std::string());
            return item;
        };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") { t.kind = K::string; return t; }
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::set" ||
            declaration == "std::unordered_set" || declaration == "std::valarray" || declaration == "std::array") {
            t.kind = K::vector;
            t.items.push_back(arg(0, Use::element));
            if (!why.empty()) return {};
            if (t.items[0].kind == K::none || t.items[0].kind == K::callback || t.items[0].kind == K::function) { why = "unsupported Fortran element type in " + target.canonical; return {}; }
            if (t.items[0].nullable) t.items[0].nullable = false;
            if (composite(t.items[0])) helper_for(t.items[0]);
            return t;
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") {
            t.kind = K::map;
            t.items.push_back(arg(0, Use::element));
            t.items.push_back(arg(1, Use::element));
            if (!why.empty()) return {};
            for (const auto& item : t.items)
                if (item.kind == K::none || item.kind == K::callback || item.kind == K::function) { why = "unsupported Fortran map type " + target.canonical; return {}; }
            helper_for(t);
            return t;
        }
        if (declaration == "std::pair" || declaration == "std::tuple") {
            t.kind = K::tuple;
            t.pair = declaration == "std::pair";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) {
                t.items.push_back(arg(i, Use::element));
                if (!why.empty()) return {};
                if (t.items.back().kind == K::none || t.items.back().kind == K::callback || t.items.back().kind == K::function) {
                    why = "unsupported Fortran tuple type " + target.canonical;
                    return {};
                }
            }
            if (t.items.empty()) { why = "empty tuples are unsupported in Fortran"; return {}; }
            helper_for(t);
            return t;
        }
        if (declaration == "std::optional") {
            auto item = arg(0, Use::element);
            if (!why.empty()) return {};
            if (item.kind == K::none || item.kind == K::callback || item.kind == K::function || item.kind == K::optional) {
                why = "unsupported Fortran optional type " + target.canonical;
                return {};
            }
            t.kind = K::optional;
            t.items.push_back(item);
            if (use != Use::parameter) helper_for(t);
            return t;
        }
        if (declaration == "std::complex") {
            const auto inner = target.arguments.empty() ? std::string() : strip_cv(target.arguments[0].canonical);
            t.kind = inner == "float" ? K::complex32 : K::complex;
            return t;
        }
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            if (target.arguments.empty() || !bound(target.arguments[0])) { why = "smart pointers require an exported class"; return {}; }
            t = convert(target.arguments[0], use, why);
            return t;
        }
        if (declaration == "std::function") {
            if (target.arguments.empty() || target.arguments[0].kind != TypeKind::function) { why = "unsupported std::function signature"; return {}; }
            if (use == Use::element || use == Use::callback_argument || use == Use::callback_result) {
                why = "callbacks inside containers or callbacks are unsupported in Fortran";
                return {};
            }
            const auto& signature = target.arguments[0];
            t.kind = use == Use::parameter ? K::callback : K::function;
            std::string inner_why;
            t.items.push_back(signature.arguments.empty() ? FT{} : convert(signature.arguments[0], Use::callback_result, inner_why));
            for (std::size_t i = 1; i < signature.arguments.size() && inner_why.empty(); ++i)
                t.items.push_back(convert(signature.arguments[i], Use::callback_argument, inner_why));
            if (!inner_why.empty()) { why = "callback: " + inner_why; return {}; }
            for (std::size_t i = 1; i < t.items.size(); ++i)
                if (t.items[i].kind == K::none) { why = "unsupported callback argument"; return {}; }
            if (t.kind == K::callback) callback(t);
            else emit_native_function(t);
            return t;
        }
        if (declaration == "std::chrono::duration") { t.kind = K::real; return t; }
        why = "unsupported Fortran template type " + target.canonical;
        return {};
    }
    default:
        why = "unsupported Fortran value type " + type.canonical;
        return {};
    }
}

// A policy conversion's target type, spelled as C++ text ("std::vector<double>").
FT Generator::spelled(std::string cpp, Use use, std::string& why) {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (cpp.rfind("const ", 0) == 0) cpp = trim(cpp.substr(6));
    if (cpp.rfind("::", 0) == 0) cpp = cpp.substr(2);
    FT t;
    static const std::map<std::string, K> scalars = {{"bool", K::logical}, {"char", K::character}, {"signed char", K::integer},
        {"unsigned char", K::integer}, {"short", K::integer}, {"unsigned short", K::integer}, {"int", K::integer}, {"unsigned int", K::integer},
        {"unsigned", K::integer}, {"long", K::integer}, {"unsigned long", K::integer}, {"long long", K::int64}, {"unsigned long long", K::int64},
        {"float", K::real32}, {"double", K::real}, {"long double", K::real}, {"std::size_t", K::integer}, {"size_t", K::integer},
        {"std::int64_t", K::int64}, {"int64_t", K::int64}, {"std::uint64_t", K::int64}, {"uint64_t", K::int64}, {"std::int32_t", K::integer},
        {"int32_t", K::integer}, {"std::uint32_t", K::integer}, {"std::string", K::string}, {"std::string_view", K::string},
        {"std::filesystem::path", K::string}, {"std::complex<double>", K::complex}, {"std::complex<float>", K::complex32}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) { t.kind = found->second; return t; }
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') { why = "unsupported Fortran conversion target " + cpp; return {}; }
    const auto name = cpp.substr(0, open);
    const auto arguments = spelled_arguments(cpp);
    const auto arg = [&](std::size_t i) { return i < arguments.size() ? spelled(arguments[i], Use::element, why) : FT{}; };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::valarray" ||
        name == "std::set" || name == "std::unordered_set") {
        t.kind = K::vector;
        t.items.push_back(arg(0));
        if (!why.empty()) return {};
        if (composite(t.items[0])) helper_for(t.items[0]);
        return t;
    }
    if (name == "std::map" || name == "std::unordered_map") {
        t.kind = K::map;
        t.items = {arg(0), arg(1)};
        if (!why.empty()) return {};
        helper_for(t);
        return t;
    }
    if (name == "std::pair" || name == "std::tuple") {
        t.kind = K::tuple;
        t.pair = name == "std::pair";
        for (std::size_t i = 0; i < arguments.size() && why.empty(); ++i) t.items.push_back(arg(i));
        if (!why.empty() || t.items.empty()) { if (why.empty()) why = "empty tuple"; return {}; }
        helper_for(t);
        return t;
    }
    if (name == "std::optional") {
        t.kind = K::optional;
        t.items.push_back(arg(0));
        if (!why.empty()) return {};
        if (use != Use::parameter) helper_for(t);
        return t;
    }
    why = "unsupported Fortran conversion target " + cpp;
    return {};
}

// A name fragment for generated type names (vector_real, map_string_int, ...).
std::string Generator::mangle(const FT& t) const {
    switch (t.kind) {
    case K::integer: return "int";
    case K::int64: return "int64";
    case K::real: return "real";
    case K::real32: return "real32";
    case K::logical: return "logical";
    case K::complex: return "complex";
    case K::complex32: return "complex32";
    case K::character: return "char";
    case K::string: return "string";
    case K::enumeration: return lower(enum_names_.at(t.name));
    case K::object: case K::function: return lower(t.name);
    case K::vector: return "vector_" + mangle(t.items[0]);
    case K::map: return "map_" + mangle(t.items[0]) + "_" + mangle(t.items[1]);
    case K::optional: return "optional_" + mangle(t.items[0]);
    case K::tuple: {
        std::string text = t.pair ? "pair" : "tuple";
        for (const auto& item : t.items) text += "_" + mangle(item);
        return text;
    }
    case K::callback: {
        std::string text = "fn_" + (t.items.empty() || t.items[0].kind == K::none ? std::string("void") : mangle(t.items[0]));
        for (std::size_t i = 1; i < t.items.size(); ++i) text += "_" + mangle(t.items[i]);
        return text;
    }
    default: return "value";
    }
}

std::string Generator::scalar_decl(const FT& t) const {
    switch (t.kind) {
    case K::integer: return "integer";
    case K::int64: return "integer(c_int64_t)";
    case K::real: return "real(c_double)";
    case K::real32: return "real(c_float)";
    case K::logical: return "logical";
    case K::complex: return "complex(c_double)";
    case K::complex32: return "complex(c_float)";
    case K::character: return "character(len=1)";
    case K::enumeration: return t.big ? "integer(c_int64_t)" : "integer";
    case K::string: return "character(len=:), allocatable";
    case K::object: case K::function: return "type(" + t.name + ")";
    default: return "type(" + t.helper + ")";
    }
}

Generator::Decl Generator::array_decl(const FT& e) const {
    if (e.kind == K::string) return {"type(rosetta_string), allocatable", "(:)"};
    if (e.kind == K::object) return {"type(" + e.name + "), allocatable", "(:)"};
    if (composite(e)) return {"type(" + e.helper + "), allocatable", "(:)"};
    return {scalar_decl(e) + ", allocatable", "(:)"};
}

Generator::Decl Generator::value_decl(const FT& t) const {
    if (t.kind == K::vector) return array_decl(t.items[0]);
    return {scalar_decl(t), ""};
}

Generator::Decl Generator::param_decl(const FT& t) const {
    if (t.writeback) {
        auto d = value_decl(t);
        if (t.kind == K::optional) d = {"type(" + t.helper + ")", ""};
        d.spec += ", intent(inout)";
        return d;
    }
    switch (t.kind) {
    case K::string: return {"character(len=*), intent(in)", ""};
    case K::object: return {"class(" + t.name + "), intent(in)" + std::string(t.nullable ? ", optional" : ""), ""};
    case K::vector: {
        const auto& e = t.items[0];
        if (e.kind == K::string) return {"character(len=*), intent(in)", "(:)"};
        if (e.kind == K::object) return {"class(" + e.name + "), intent(in)", "(:)"};
        if (composite(e)) return {"type(" + e.helper + "), intent(in)", "(:)"};
        return {scalar_decl(e) + ", intent(in)", "(:)"};
    }
    case K::map: case K::tuple: return {"type(" + t.helper + "), intent(in)", ""};
    case K::optional: {
        auto d = param_decl(t.items[0]);
        if (d.spec.find(", optional") == std::string::npos) d.spec += ", optional";
        return d;
    }
    case K::callback: return {"procedure(" + t.name + ")", ""};
    default: return {scalar_decl(t) + ", intent(in)", ""};
    }
}

Dummy Generator::dummy(const FT& t, const std::string& name) const {
    Dummy d;
    d.name = lower(name);
    const auto base = [&](const FT& x, Dummy& out) {
        switch (x.kind) {
        case K::integer: out.type = "integer"; out.kind = 4; break;
        case K::int64: out.type = "integer"; out.kind = 8; break;
        case K::enumeration: out.type = "integer"; out.kind = x.big ? 8 : 4; break;
        case K::real: out.type = "real"; out.kind = 8; break;
        case K::real32: out.type = "real"; out.kind = 4; break;
        case K::complex: out.type = "complex"; out.kind = 8; break;
        case K::complex32: out.type = "complex"; out.kind = 4; break;
        case K::logical: out.type = "logical"; out.kind = 4; break;
        case K::character: case K::string: out.type = "character"; out.kind = 1; break;
        case K::object: case K::function: out.type = lower(x.name); out.poly = true; break;
        default: out.type = lower(x.helper); break;
        }
    };
    if (t.kind == K::callback) { d.procedure = true; return d; }
    if (t.kind == K::optional && !t.writeback) { base(t.items[0], d); d.optional = true; if (t.items[0].kind == K::vector) { base(t.items[0].items[0], d); d.rank = 1; } return d; }
    if (t.kind == K::vector) {
        base(t.items[0], d);
        if (t.items[0].kind == K::string && t.writeback) { d.type = "rosetta_string"; d.kind = 0; }
        if (t.items[0].kind == K::object && t.writeback) d.poly = false;
        d.rank = 1;
        return d;
    }
    base(t, d);
    if (t.kind == K::object && t.writeback) d.poly = false;
    d.optional = t.nullable && !t.writeback;
    return d;
}

std::string Generator::shown(const FT& t) const {
    const auto d = param_decl(t);
    auto spec = d.spec;
    for (const char* attribute : {", intent(in)", ", intent(inout)", ", optional", ", allocatable"}) {
        for (std::size_t i; (i = spec.find(attribute)) != std::string::npos;) spec.erase(i, std::string(attribute).size());
    }
    return spec + d.dims;
}

bool Generator::extends(const std::string& derived, const std::string& base) const {
    auto current = lower(derived);
    for (int depth = 0; depth < 128; ++depth) {
        if (current == lower(base)) return true;
        const auto found = parents_.find(current);
        if (found == parents_.end()) return false;
        current = found->second;
    }
    return false;
}
bool Generator::tkr(const Dummy& a, const Dummy& b) const {
    if (a.procedure || b.procedure) return a.procedure && b.procedure;
    if (a.rank != b.rank || a.kind != b.kind) return false;
    if (a.type == b.type) return true;
    return a.poly && extends(b.type, a.type);
}
// F2018 C1514 (1) and (3) for two specifics' non-passed dummy arguments.
bool Generator::distinguishable(const std::vector<Dummy>& p, const std::vector<Dummy>& q) const {
    const auto count_rule = [&](const std::vector<Dummy>& x, const std::vector<Dummy>& y) {
        for (const auto& d : x) {
            if (d.procedure) continue;
            std::size_t mine = 0, theirs = 0;
            for (const auto& e : x) if (!e.optional && !e.procedure && tkr(d, e)) ++mine;
            for (const auto& e : y) if (!e.procedure && !distinguishable(d, e)) ++theirs;
            if (mine > theirs) return true;
        }
        return false;
    };
    if (count_rule(p, q) || count_rule(q, p)) return true;
    const auto position_rule = [&](const std::vector<Dummy>& x, const std::vector<Dummy>& y) {
        std::size_t by_position = x.size();
        for (std::size_t i = 0; i < x.size() && by_position == x.size(); ++i)
            if (!x[i].optional && (i >= y.size() || distinguishable(x[i], y[i]))) by_position = i;
        if (by_position == x.size()) return false;
        for (std::size_t i = by_position; i < x.size(); ++i) {
            if (x[i].optional) continue;
            const auto other = std::find_if(y.begin(), y.end(), [&](const Dummy& e) { return e.name == x[i].name; });
            if (other == y.end() || distinguishable(x[i], *other)) return true;
        }
        return false;
    };
    return position_rule(p, q) || position_rule(q, p);
}

// ------------------------------------------------------------------ generated helper types
std::string Generator::helper_for(FT& t) {
    if (t.kind == K::vector) {
        // A vector as an element of another container: a type holding the array in `v`.
        const auto key = mangle(t);
        auto found = helper_names_.find(key);
        if (found == helper_names_.end()) {
            const auto name = names_.claim(key);
            found = helper_names_.emplace(key, name).first;
            const auto d = array_decl(t.items[0]);
            helpers_ += "\n!> A `" + key + "` element: the array is `v`.\ntype, public :: " + name + "\n    !> The elements.\n    " + decl_line(d, "v") + "\nend type\n";
        }
        t.helper = found->second;
        return t.helper;
    }
    if (t.kind == K::optional && t.items[0].kind == K::vector && t.items[0].helper.empty()) helper_for(t.items[0]);
    const auto key = mangle(t);
    auto found = helper_names_.find(key);
    if (found != helper_names_.end()) { t.helper = found->second; return t.helper; }
    const auto name = names_.claim(key);
    helper_names_.emplace(key, name);
    t.helper = name;
    std::string body;
    if (t.kind == K::map) {
        body = "!> A C++ map: `keys(i)` maps to `values(i)`.\ntype, public :: " + name + "\n    !> The keys.\n    " + decl_line(array_decl(t.items[0]), "keys") +
               "\n    !> The values: `values(i)` belongs to `keys(i)`.\n    " + decl_line(array_decl(t.items[1]), "values") + "\nend type\n";
    } else if (t.kind == K::optional) {
        auto value = value_decl(t.items[0]);
        body = "!> A C++ optional: `value` is set when `has_value`.\ntype, public :: " + name + "\n    !> Whether there is a value.\n    logical :: has_value = .false.\n    !> The value (set when `has_value`).\n    " +
               decl_line(value, "value") + "\nend type\n";
    } else {
        body = std::string("!> A C++ ") + (t.pair ? "pair" : "tuple") + ".\ntype, public :: " + name + "\n";
        for (std::size_t i = 0; i < t.items.size(); ++i)
            body += "    !> Element " + std::to_string(i + 1) + ".\n    " + decl_line(value_decl(t.items[i]), t.pair ? (i == 0 ? "first" : "second") : "item" + std::to_string(i + 1)) + "\n";
        body += "end type\n";
    }
    helpers_ += "\n" + body;
    return name;
}

// A Fortran procedure signature C++ may call through a std::function parameter.
int Generator::callback(FT& t) {
    const auto key = mangle(t);
    const auto found = signature_index_.find(key);
    if (found != signature_index_.end()) {
        t.sig = found->second;
        t.name = signatures_[static_cast<std::size_t>(t.sig - 1)].iface;
        return t.sig;
    }
    Signature s;
    s.iface = names_.claim(key);
    t.name = s.iface;
    t.sig = static_cast<int>(signatures_.size()) + 1;
    s.store = "rs_cb" + std::to_string(t.sig);
    s.add = s.store + "_add";
    s.type = t;
    signature_index_[key] = t.sig;
    signatures_.push_back(s);
    return t.sig;
}

// A std::function C++ returns: a type with a type-bound `invoke`.
std::string Generator::emit_native_function(FT& t) {
    FT signature = t;
    signature.kind = K::callback;
    const auto key = "native_" + mangle(signature);
    const auto found = helper_names_.find(key);
    if (found != helper_names_.end()) { t.name = found->second; return t.name; }
    const auto name = names_.claim(key);
    helper_names_[key] = name;
    t.name = name;
    native_function_types_.push_back(name);
    // The type and its procedure are emitted with the classes (run()).
    std::ostringstream type;
    const bool returns = !t.items.empty() && t.items[0].kind != K::none;
    type << "\n!> A C++ `std::function` returned to Fortran; call it with `invoke`.\ntype, extends(rosetta_object), public :: " << name
         << "\ncontains\n    !> Calls the C++ function.\n    procedure :: invoke => " << name << "_invoke\nend type\n";
    types_ << type.str();
    Body b;
    std::string dummies;
    std::vector<std::string> decls;
    for (std::size_t i = 1; i < t.items.size(); ++i) {
        const auto arg = "a" + std::to_string(i);
        dummies += ", " + arg;
        decls.push_back(decl_line(param_decl(t.items[i]), arg));
    }
    b.line("call rs_start(rs_w, " + std::to_string(t.items.size()) + ")");
    b.line("call rs_put_object(rs_w, self, 0)");
    for (std::size_t i = 1; i < t.items.size(); ++i) encode(b, t.items[i], "a" + std::to_string(i), true, static_cast<int>(i));
    b.line(returns ? "call rs_call(0, rs_w, rs_r, .true.)" : "call rs_call(0, rs_w, rs_r)");
    if (returns) decode(b, t.items[0], "rs_res");
    b.line("call rs_end(rs_r, status)");
    std::ostringstream proc;
    proc << "\n!> Calls the C++ function.\n" << (returns ? "function " : "subroutine ") << name << "_invoke(self" << dummies << ", status)"
         << (returns ? " result(rs_res)" : "") << "\n    class(" << name << "), intent(in) :: self\n";
    for (const auto& decl : decls) proc << "    " << decl << "\n";
    proc << "    type(rosetta_error), intent(out), optional :: status\n";
    if (returns) proc << "    " << decl_line(value_decl(t.items[0]), "rs_res") << "\n";
    proc << "    type(rs_writer) :: rs_w\n    type(rs_reader) :: rs_r\n";
    proc << b.loop_decls();
    proc << b.code.str() << "end " << (returns ? "function" : "subroutine") << "\n";
    procedures_ << proc.str();
    return name;
}

// ------------------------------------------------------------------ encoding and decoding
void Generator::encode(Body& b, const FT& t, const std::string& x, bool param, int argument, const std::string& w) {
    switch (t.kind) {
    case K::integer: case K::int64: case K::enumeration: b.line("call rs_put_int(" + w + ", " + x + ")"); return;
    case K::real: case K::real32: b.line("call rs_put_real(" + w + ", " + x + ")"); return;
    case K::complex: case K::complex32: b.line("call rs_put_complex(" + w + ", " + x + ")"); return;
    case K::logical: b.line("call rs_put_bool(" + w + ", " + x + ")"); return;
    case K::character: b.line("call rs_put_string(" + w + ", " + x + ")"); return;
    case K::string:
        if (param && !t.writeback) b.line("call rs_put_string(" + w + ", " + x + ")");
        else {
            b.line("if (allocated(" + x + ")) then");
            b.line("    call rs_put_string(" + w + ", " + x + ")");
            b.line("else");
            b.line("    call rs_put_string(" + w + ", '')");
            b.line("end if");
        }
        return;
    case K::object: case K::function:
        if (t.nullable && param) {
            b.line("if (present(" + x + ")) then");
            b.line("    call rs_put_object(" + w + ", " + x + ", " + std::to_string(argument) + ")");
            b.line("else");
            b.line("    call rs_put_nil(" + w + ")");
            b.line("end if");
        } else b.line("call rs_put_object(" + w + ", " + x + ", " + std::to_string(argument) + ")");
        return;
    case K::callback:
        b.line("call rs_put_function(" + w + ", " + signatures_[static_cast<std::size_t>(t.sig - 1)].add + "(" + x + "))");
        return;
    case K::optional: {
        if (param && !t.writeback) {
            b.line("if (present(" + x + ")) then");
            const auto saved = b.indent;
            b.indent += "    ";
            encode(b, t.items[0], x, true, argument, w);
            b.indent = saved;
        } else {
            b.line("if (" + x + "%has_value) then");
            const auto saved = b.indent;
            b.indent += "    ";
            encode(b, t.items[0], x + "%value", false, -1, w);
            b.indent = saved;
        }
        b.line("else");
        b.line("    call rs_put_nil(" + w + ")");
        b.line("end if");
        return;
    }
    case K::vector: {
        const auto& e = t.items[0];
        const bool array_value = !param || t.writeback;  // an allocatable array
        const auto saved = b.indent;
        if (array_value) {
            b.line("if (.not. allocated(" + x + ")) then");
            b.line("    call rs_put_list(" + w + ", 0)");
            b.line("else");
            b.indent += "    ";
        }
        if (e.kind == K::real) b.line("call rs_put_reals(" + w + ", " + x + ")");
        else if (e.kind == K::real32) b.line("call rs_put_reals(" + w + ", real(" + x + ", c_double))");
        else if (e.kind == K::integer || e.kind == K::int64 || e.kind == K::enumeration) b.line("call rs_put_ints(" + w + ", " + x + ")");
        else if (e.kind == K::logical) b.line("call rs_put_bools(" + w + ", " + x + ")");
        else if (e.kind == K::string && !array_value) b.line("call rs_put_chars(" + w + ", " + x + ")");
        else if (e.kind == K::string) b.line("call rs_put_strings(" + w + ", " + x + ")");
        else {
            const auto i = "rs_i" + std::to_string(b.open());
            b.line("call rs_put_list(" + w + ", size(" + x + "))");
            b.line("do " + i + " = 1, size(" + x + ")");
            b.indent += "    ";
            encode(b, e, x + "(" + i + ")" + (e.kind == K::vector ? "%v" : ""), false, -1, w);
            b.indent.resize(b.indent.size() - 4);
            b.line("end do");
            b.close();
        }
        if (array_value) { b.indent = saved; b.line("end if"); }
        return;
    }
    case K::map: {
        const auto i = "rs_i" + std::to_string(b.open());
        b.line("if (.not. allocated(" + x + "%keys) .or. .not. allocated(" + x + "%values)) then");
        b.line("    call rs_put_map(" + w + ", 0)");
        b.line("else");
        b.line("    call rs_put_map(" + w + ", min(size(" + x + "%keys), size(" + x + "%values)))");
        b.line("    do " + i + " = 1, min(size(" + x + "%keys), size(" + x + "%values))");
        const auto saved = b.indent;
        b.indent += "        ";
        const auto element = [&](const FT& e, const std::string& at) {
            if (e.kind == K::string) {
                b.line("if (allocated(" + at + "%s)) then");
                b.line("    call rs_put_string(" + w + ", " + at + "%s)");
                b.line("else");
                b.line("    call rs_put_string(" + w + ", '')");
                b.line("end if");
            } else if (e.kind == K::vector) encode(b, e, at + "%v", false, -1, w);
            else encode(b, e, at, false, -1, w);
        };
        element(t.items[0], x + "%keys(" + i + ")");
        element(t.items[1], x + "%values(" + i + ")");
        b.indent = saved;
        b.line("    end do");
        b.line("end if");
        b.close();
        return;
    }
    case K::tuple:
        b.line("call rs_put_list(" + w + ", " + std::to_string(t.items.size()) + ")");
        for (std::size_t i = 0; i < t.items.size(); ++i)
            encode(b, t.items[i], x + "%" + (t.pair ? (i == 0 ? "first" : "second") : "item" + std::to_string(i + 1)), false, -1, w);
        return;
    default: return;
    }
}

void Generator::decode(Body& b, const FT& t, const std::string& x, const std::string& r) {
    switch (t.kind) {
    case K::integer: b.line(x + " = rs_get_int(" + r + ")"); return;
    case K::int64: b.line(x + " = rs_get_int64(" + r + ")"); return;
    case K::enumeration: {
        const auto index = std::distance(enum_names_.begin(), enum_names_.find(t.name)) + 1;
        b.line(x + " = rs_enum" + std::to_string(index) + "(" + r + ")");
        return;
    }
    case K::real: b.line(x + " = rs_get_real(" + r + ")"); return;
    case K::real32: b.line(x + " = rs_get_real32(" + r + ")"); return;
    case K::complex: b.line(x + " = rs_get_complex(" + r + ")"); return;
    case K::complex32: b.line(x + " = rs_get_complex32(" + r + ")"); return;
    case K::logical: b.line(x + " = rs_get_bool(" + r + ")"); return;
    case K::character: b.line(x + " = rs_get_char(" + r + ")"); return;
    case K::string: b.line("call rs_get_string(" + r + ", " + x + ")"); return;
    case K::object: case K::function: b.line("call rs_get_object(" + r + ", " + x + ")"); return;
    case K::optional: {
        b.line("if (rs_take_nil(" + r + ")) then");
        b.line("    " + x + "%has_value = .false.");
        b.line("else");
        b.line("    " + x + "%has_value = .true.");
        const auto saved = b.indent;
        b.indent += "    ";
        decode(b, t.items[0], x + "%value", r);
        b.indent = saved;
        b.line("end if");
        return;
    }
    case K::vector: {
        const auto& e = t.items[0];
        if (e.kind == K::real) { b.line("call rs_get_reals(" + r + ", " + x + ")"); return; }
        if (e.kind == K::real32) { b.line("call rs_get_reals32(" + r + ", " + x + ")"); return; }
        if (e.kind == K::integer || e.kind == K::int64) { b.line("call rs_get_ints(" + r + ", " + x + ")"); return; }
        if (e.kind == K::logical) { b.line("call rs_get_bools(" + r + ", " + x + ")"); return; }
        if (e.kind == K::string) { b.line("call rs_get_strings(" + r + ", " + x + ")"); return; }
        const auto level = std::to_string(b.open());
        b.counts.insert(b.loops);
        const auto i = "rs_i" + level, n = "rs_n" + level;
        b.line(n + " = rs_get_count(" + r + ")");
        b.line("if (allocated(" + x + ")) deallocate(" + x + ")");
        b.line("allocate(" + x + "(" + n + "))");
        b.line("do " + i + " = 1, " + n);
        b.indent += "    ";
        if (e.kind == K::vector) decode(b, e, x + "(" + i + ")%v", r);
        else decode(b, e, x + "(" + i + ")", r);
        b.indent.resize(b.indent.size() - 4);
        b.line("end do");
        b.close();
        return;
    }
    case K::map: {
        const auto level = std::to_string(b.open());
        b.counts.insert(b.loops);
        const auto i = "rs_i" + level, n = "rs_n" + level;
        b.line(n + " = rs_get_map_count(" + r + ")");
        b.line("if (allocated(" + x + "%keys)) deallocate(" + x + "%keys)");
        b.line("if (allocated(" + x + "%values)) deallocate(" + x + "%values)");
        b.line("allocate(" + x + "%keys(" + n + "), " + x + "%values(" + n + "))");
        b.line("do " + i + " = 1, " + n);
        b.indent += "    ";
        const auto element = [&](const FT& e, const std::string& at) {
            if (e.kind == K::string) decode(b, e, at + "%s", r);
            else if (e.kind == K::vector) decode(b, e, at + "%v", r);
            else decode(b, e, at, r);
        };
        element(t.items[0], x + "%keys(" + i + ")");
        element(t.items[1], x + "%values(" + i + ")");
        b.indent.resize(b.indent.size() - 4);
        b.line("end do");
        b.close();
        return;
    }
    case K::tuple:
        b.line("call rs_get_tuple(" + r + ", " + std::to_string(t.items.size()) + ")");
        for (std::size_t i = 0; i < t.items.size(); ++i)
            decode(b, t.items[i], x + "%" + (t.pair ? (i == 0 ? "first" : "second") : "item" + std::to_string(i + 1)), r);
        return;
    default: return;
    }
}

// ------------------------------------------------------------------ specifics
std::vector<std::string> Generator::parameter_names(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    std::vector<std::string> names;
    std::set<std::string> taken{"self", "status", "value"};
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        auto name = cpp.empty() ? "arg" + std::to_string(i - first + 1) : fit(escape(fortran_identifier(cpp)));
        // Dummy names must not hide the types, kinds and intrinsics their procedure uses.
        while (reserved_name(name) || names_.has(name) || !taken.insert(lower(name)).second) name = fit(name + "_");
        names.push_back(name);
    }
    return names;
}

std::vector<Generator::Specific> Generator::specifics(const std::vector<Overload>& overloads, const std::string& context, bool self,
                                                      bool is_operator, bool constructor, std::vector<Specific>* compare) {
    std::vector<Specific> kept;
    std::vector<Specific> all_kept;
    if (compare) all_kept = *compare;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t count = function.parameters.size();
        const std::size_t required = count - function.default_arguments;
        const auto names = parameter_names(overload);
        std::string why;
        std::vector<Arg> args;
        for (std::size_t i = first; i < count && why.empty(); ++i) args.push_back({names[i - first], convert(function.parameters[i], Use::parameter, why)});
        FT result;
        if (why.empty() && !constructor) result = convert(function.result, Use::result, why);
        if (why.empty() && is_operator) {
            for (const auto& arg : args)
                if (arg.type.writeback || arg.type.kind == K::callback || arg.type.kind == K::optional || arg.type.nullable) why = "operator operands must be plain values in Fortran";
            if (result.kind == K::none) why = "operators must return a value in Fortran";
        }
        if (!why.empty()) { reject(context, why); continue; }
        for (std::size_t arity = std::max(required, first); arity <= count; ++arity) {
            Specific s;
            s.overload = &overload;
            s.arity = arity;
            s.args.assign(args.begin(), args.begin() + static_cast<std::ptrdiff_t>(arity - first));
            s.result = result;
            s.function = constructor || result.kind != K::none;
            for (const auto& arg : s.args) s.dummies.push_back(dummy(arg.type, arg.name));
            if (is_operator && self) {
                Dummy operand;
                operand.name = "self";
                operand.type = lower(classes_.at(overload.owner).name);
                operand.poly = true;
                s.dummies.insert(s.dummies.begin(), operand);
            }
            if (!is_operator) {
                Dummy status;
                status.name = "status";
                status.type = "rosetta_error";
                status.optional = true;
                s.dummies.push_back(status);
            }
            bool keep = true;
            for (const auto& other : all_kept) {
                if (other.function != s.function) {
                    reject(context, "mixes subroutines (void) and functions under one Fortran generic name; dropped");
                    keep = false;
                    break;
                }
                if (distinguishable(other.dummies, s.dummies)) continue;
                // The same Fortran signature reaches the same C++ overload set: C++ chooses at run time.
                bool same = other.dummies.size() == s.dummies.size();
                for (std::size_t i = 0; same && i < s.dummies.size(); ++i) {
                    const auto& a = other.dummies[i];
                    const auto& b = s.dummies[i];
                    same = a.type == b.type && a.kind == b.kind && a.rank == b.rank && a.optional == b.optional && a.procedure == b.procedure && a.poly == b.poly;
                }
                if (!same)
                    reject(context, "overload " + function.identity() + " (" + std::to_string(arity - first) +
                                        " arguments) is ambiguous with another under Fortran's generic rules; dropped");
                keep = false;
                break;
            }
            if (!keep) continue;
            all_kept.push_back(s);
            kept.push_back(s);
        }
    }
    return kept;
}

std::string Generator::interface_text(const Specific& s, const std::string& self) const {
    std::string text = self + "|";
    for (const auto& arg : s.args) text += arg.name + ":" + decl_line(param_decl(arg.type), "") + ";";
    if (s.function) text += "->" + decl_line(value_decl(s.result), "");
    return text;
}

std::string Generator::signatures(const Group& group, bool is_method) const {
    std::string text;
    for (const auto& overload : group.overloads) {
        const auto names = parameter_names(overload);
        const auto& function = *overload.function;
        std::string line = "    " + (is_method && !overload.extension ? std::string("self%") : std::string()) + group.name + "(";
        for (std::size_t i = overload.receives_self ? 1 : 0, k = 0; i < function.parameters.size(); ++i, ++k) {
            std::string why;
            auto& self = const_cast<Generator&>(*this);
            const auto t = self.convert(function.parameters[i], Use::parameter, why);
            line += (k ? ", " : "") + names[k] + (why.empty() ? ": " + shown(t) : "");
        }
        text += line + ")\n";
    }
    return text;
}
std::string Generator::constructor_signatures(const ClassPlan& plan) const {
    Group group;
    group.name = plan.name;
    group.overloads = plan.constructors;
    return signatures(group, false);
}

Doc Generator::specific_doc(const Specific& s, const std::string& fallback) const {
    const auto& function = *s.overload->function;
    auto doc = describe(function.documentation, fallback);
    const std::size_t first = s.overload->receives_self ? 1 : 0;
    const auto names = parameter_names(*s.overload);
    std::string omitted;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : function.documentation.params) if (item.name == cpp) text = one_line(item.text);
        if (i < s.arity) { doc.params.emplace_back(names[i - first], text); continue; }
        const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
        std::string shown_value = value.value;
        if (value.kind == "float") shown_value = shortest_double(value.value);
        if (value.kind == "bool") shown_value = value.value == "true" ? ".true." : ".false.";
        if (value.kind == "char") shown_value = "'" + value.value + "'";
        omitted += (omitted.empty() ? "" : ", ") + std::string("`") + names[i - first] + (shown_value.empty() ? "" : " = " + shown_value) + "`";
    }
    if (!omitted.empty()) doc.paragraphs.push_back("Uses the C++ default for " + omitted + ".");
    if (!function.is_constructor && s.function && !function.documentation.returns.empty()) doc.returns = one_line(function.documentation.returns);
    return doc;
}

// ------------------------------------------------------------------ one generated procedure
std::string Generator::emit_call(const Call& c) {
    Body b;
    std::ostringstream head;
    const auto* s = c.specific;
    const bool function = c.mode == 1 || c.mode == 3 || c.mode == 5 || (s && s->function && c.mode != 2);
    std::vector<std::pair<std::string, std::string>> dummies; // name, declaration
    if (!c.self_class.empty()) dummies.emplace_back("self", c.self_spec + " :: self");
    if (s) for (const auto& arg : s->args) dummies.emplace_back(arg.name, decl_line(param_decl(arg.type), arg.name));
    if (c.mode == 4) dummies.emplace_back("value", decl_line(param_decl(c.value), "value"));
    // The call: [constructor subclass id] [self] arguments.
    std::size_t count = (c.self_class.empty() || c.mode == 2 ? 0 : 1) + (s ? s->args.size() : 0) + (c.mode == 1 || c.mode == 2 ? 1 : 0) + (c.mode == 4 ? 1 : 0);
    if (c.mode == 2) b.line("rs_id = rs_attach(self)");
    b.line("call rs_start(rs_w, " + std::to_string(count) + ")");
    int position = 0;
    if (c.mode == 1) { b.line("call rs_put_int(rs_w, 0_c_int64_t)"); ++position; }
    if (c.mode == 2) { b.line("call rs_put_int(rs_w, int(rs_id, c_int64_t))"); ++position; }
    if (!c.self_class.empty() && c.mode != 2) { b.line("call rs_put_object(rs_w, self, 0)"); ++position; }
    std::vector<std::pair<int, const Arg*>> writebacks;
    if (s) {
        for (const auto& arg : s->args) {
            if (arg.type.writeback) {
                b.line("call rs_put_ref(rs_w, " + std::to_string(writebacks.size()) + ")");
                writebacks.emplace_back(static_cast<int>(writebacks.size()), &arg);
            }
            encode(b, arg.type, arg.name, true, position);
            ++position;
        }
    }
    if (c.mode == 4) encode(b, c.value, "value", true, position);
    // Functions may be referenced in an output list: their call must not flush output_unit.
    b.line("call rs_call(" + std::to_string(c.index) + ", rs_w, rs_r" + (function ? ", .true.)" : ")"));
    FT result = c.mode == 3 || c.mode == 5 ? c.result : s ? s->result : FT{};
    if (c.mode == 1) b.line("call rs_get_object(rs_r, rs_res)");
    else if (c.mode == 2) b.line("call rs_adopt(rs_r, self)");
    else if (function) decode(b, result, "rs_res");
    else if (!writebacks.empty()) b.line("call rs_skip(rs_r)");
    if (!writebacks.empty()) {
        b.line("do rs_k = 1, rs_get_u32(rs_r)");
        b.line("    select case (rs_get_u32(rs_r))");
        for (const auto& [slot, arg] : writebacks) {
            b.line("    case (" + std::to_string(slot) + ")");
            const auto saved = b.indent;
            b.indent += "        ";
            decode(b, arg->type, arg->name);
            b.indent = saved;
        }
        b.line("    case default");
        b.line("        call rs_skip(rs_r)");
        b.line("    end select");
        b.line("end do");
    }
    if (c.status) b.line("call rs_end(rs_r, status)");
    else b.line("call rs_end(rs_r)");

    std::string list;
    for (const auto& [name, decl] : dummies) list += (list.empty() ? "" : ", ") + name;
    if (c.status) list += (list.empty() ? "" : ", ") + std::string("status");
    head << "\n" << c.doc << (function ? "function " : "subroutine ") << c.proc << "(" << list << ")" << (function ? " result(rs_res)" : "") << "\n";
    for (const auto& [name, decl] : dummies) head << "    " << decl << "\n";
    if (c.status) head << "    type(rosetta_error), intent(out), optional :: status\n";
    if (c.mode == 1) head << "    type(" << c.result_type << ") :: rs_res\n";
    else if (function) head << "    " << decl_line(value_decl(result), "rs_res") << "\n";
    head << "    type(rs_writer) :: rs_w\n    type(rs_reader) :: rs_r\n";
    if (c.mode == 2) head << "    integer :: rs_id\n";
    if (!writebacks.empty()) head << "    integer :: rs_k\n";
    head << b.loop_decls();
    head << b.code.str() << "end " << (function ? "function" : "subroutine") << "\n";
    return head.str();
}

// ------------------------------------------------------------------ classes
void Generator::plan_class(const std::string& owner, std::ostringstream& members, std::ostringstream& generics) {
    auto& plan = classes_.at(owner);
    const auto type = plan.name;
    const auto small = lower(type);
    auto& bindings = bindings_[owner];
    if (!plan.bases.empty()) bindings = bindings_[plan.bases.front()];
    else for (const char* reserved : {"close", "is_closed", "class_name", "rs_assign", "rs_slot", "rs_key", "rosetta_object"}) bindings[reserved] = "*";
    bindings[small] = "*"; // the parent component of extensions
    const auto bind_name = [&](const std::string& wanted) {
        auto name = fit(escape(wanted));
        while (bindings.count(lower(name)) || reserved_name(name)) name = fit(name + "_");
        return name;
    };
    const std::string self_in = "class(" + type + "), intent(in)";
    const std::string self_any = "class(" + type + ")";

    // Instance methods (and the call/subscript operators).
    std::vector<const Group*> groups;
    for (const auto& group : plan.methods) groups.push_back(&group);
    for (const auto& group : plan.operators) if (group.name == "item" || group.name == "invoke") groups.push_back(&group);
    for (const auto* group : groups) {
        const bool instance = !group->is_static;
        if (!instance) continue;
        // Overloads called on an object: methods and extensions taking the object first.
        std::vector<Overload> overloads;
        for (const auto& overload : group->overloads) if (!overload.extension || overload.receives_self) overloads.push_back(overload);
        auto list = specifics(overloads, owner + "::" + group->name, true, false, false);
        if (list.empty()) continue;
        bool is_virtual = false;
        for (const auto& item : list) if (item.overload->function->is_virtual) is_virtual = true;
        const auto self_spec = is_virtual ? self_any : self_in;
        std::vector<std::string> ifaces;
        for (const auto& item : list) ifaces.push_back(interface_text(item, is_virtual ? "class" : "class, intent(in)"));
        auto name = group->name;
        const auto existing = bindings.find(lower(name));
        bool override_names = false;
        if (existing != bindings.end()) {
            const auto& specs = binding_specifics_[lower(name) + "@" + existing->second];
            if (list.size() == 1 && existing->second == ifaces[0]) override_names = true;
            else if (list.size() > 1 && existing->second.rfind("generic:", 0) == 0) {
                std::string joined = "generic:";
                for (const auto& iface : ifaces) joined += iface + "#";
                override_names = existing->second == joined && specs.size() == list.size();
            }
            if (!override_names) name = bind_name(name + "_" + small);
        }
        std::vector<std::string> specific_names;
        if (list.size() == 1) {
            list[0].binding = name;
            bindings[lower(name)] = ifaces[0];
        } else {
            std::string joined = "generic:";
            for (const auto& iface : ifaces) joined += iface + "#";
            const auto& previous = override_names ? binding_specifics_[lower(name) + "@" + joined] : std::vector<std::string>{};
            for (std::size_t i = 0; i < list.size(); ++i) {
                list[i].binding = override_names ? previous[i] : bind_name(name + "_" + std::to_string(i + 1));
                bindings[lower(list[i].binding)] = ifaces[i];
                specific_names.push_back(list[i].binding);
            }
            bindings[lower(name)] = joined;
            binding_specifics_[lower(name) + "@" + joined] = specific_names;
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            auto& item = list[i];
            item.proc = names_.claim(small + "_" + item.binding);
            auto doc = specific_doc(item, "C++ `" + item.overload->function->qualified_name() + "`.");
            if (item.overload->function->is_virtual && plan.director)
                doc.paragraphs.push_back("Virtual in C++: a Fortran extension of `" + type + "` may override binding `" + item.binding +
                                         "` (same dummy names), also when C++ calls it.");
            Call call;
            call.proc = item.proc;
            call.self_class = type;
            call.self_spec = self_spec;
            call.index = group->index;
            call.specific = &item;
            call.doc = doc.render("");
            const auto summary = comment_lines(doc.summary, "    ");
            members << summary << "    procedure :: " << item.binding << " => " << item.proc << "\n";
            procedures_ << emit_call(call);
            if (item.arity == item.overload->function->parameters.size()) override_binding_[item.overload->function] = item.binding;
        }
        if (list.size() > 1 && !override_names) {
            members << comment_lines(describe(list[0].overload->function->documentation, "C++ `" + list[0].overload->function->qualified_name() + "`.").summary, "    ")
                    << "    generic :: " << name << " => ";
            for (std::size_t i = 0; i < specific_names.size(); ++i) members << (i ? ", " : "") << specific_names[i];
            members << "\n";
        }
    }
    // Fields: get_<name> and set_<name>.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        std::string why;
        const auto t = convert(f.type, Use::result, why);
        if (!why.empty() || t.kind == K::none) { reject(owner + "::" + f.name, why.empty() ? "unsupported Fortran field type" : why); continue; }
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        auto about = describe(f.documentation, "C++ field `" + owner + "::" + f.name + "`.");
        for (const bool setter : {false, true}) {
            if (setter && !writable) continue;
            const auto wanted = std::string(setter ? "set_" : "get_") + field.name;
            const auto iface = std::string(setter ? "set|" : "get|") + decl_line(setter ? param_decl(t) : value_decl(t), "");
            auto name = wanted;
            const auto existing = bindings.find(lower(name));
            if (existing != bindings.end() && existing->second != iface) name = bind_name(wanted + "_" + small);
            bindings[lower(name)] = iface;
            Call call;
            call.proc = names_.claim(small + "_" + name);
            call.self_class = type;
            call.self_spec = self_in;
            call.index = setter ? field.setter_index : field.getter_index;
            call.mode = setter ? 4 : 3;
            call.result = t;
            call.value = t;
            Doc doc = about;
            doc.summary = (setter ? "Sets " : "Gets ") + std::string("C++ field `") + f.name + "`: " + about.summary;
            if (setter) doc.params.emplace_back("value", "the new value");
            call.doc = doc.render("");
            members << comment_lines(doc.summary, "    ") << "    procedure :: " << name << " => " << call.proc << "\n";
            procedures_ << emit_call(call);
        }
    }
    // Static methods (and extensions without the object): module generics <type>_<name>.
    for (const auto& group : plan.methods) {
        std::vector<Overload> overloads;
        for (const auto& overload : group.overloads)
            if (group.is_static || (overload.extension && !overload.receives_self)) overloads.push_back(overload);
        if (overloads.empty()) continue;
        auto list = specifics(overloads, owner + "::" + group.name, false, false, false);
        if (list.empty()) continue;
        const auto generic = names_.claim(small + "_" + group.name);
        std::ostringstream specs;
        for (std::size_t i = 0; i < list.size(); ++i) {
            auto& item = list[i];
            item.proc = names_.claim(generic + "_" + std::to_string(i + 1));
            Call call;
            call.proc = item.proc;
            call.index = group.index;
            call.specific = &item;
            call.doc = specific_doc(item, "C++ `" + item.overload->function->qualified_name() + "`.").render("");
            procedures_ << emit_call(call);
            specs << (i ? ", " : "") << item.proc;
        }
        generics << "\n" << comment_lines(describe(list[0].overload->function->documentation, "C++ `" + owner + "::" + group.name + "`.").summary, "")
                 << "interface " << generic << "\n    module procedure " << specs.str() << "\nend interface\npublic :: " << generic << "\n";
    }
    // Class constants: functions <type>_<name>.
    for (const auto& constant : plan.constants) {
        std::string why;
        const auto t = convert(*constant.type, Use::result, why);
        if (!why.empty() || t.kind == K::none) { reject(constant.expression, why.empty() ? "unsupported constant type" : why); continue; }
        const auto generic = names_.claim(constant.name);
        Call call;
        call.proc = names_.claim(generic + "_value");
        call.index = constant.index;
        call.mode = 5;
        call.result = t;
        call.status = false;
        call.doc = describe(*constant.documentation, "C++ constant `" + constant.expression + "`.").render("");
        procedures_ << emit_call(call);
        generics << "\n" << comment_lines(describe(*constant.documentation, "C++ constant `" + constant.expression + "`.").summary, "")
                 << "interface " << generic << "\n    module procedure " << call.proc << "\nend interface\npublic :: " << generic << "\n";
    }
    // Constructors: a generic function named after the type; directors: <type>_init.
    if (!plan.constructors.empty()) {
        auto list = specifics(plan.constructors, owner + "::" + plan.record->name, false, false, true);
        const bool concrete = !plan.record->abstract;
        std::vector<Specific> plain;
        for (const auto& item : list) if (concrete && item.overload->function->access == "public") plain.push_back(item);
        if (!plain.empty()) {
            std::ostringstream specs;
            for (std::size_t i = 0; i < plain.size(); ++i) {
                auto& item = plain[i];
                item.proc = names_.claim(small + "_new_" + std::to_string(i + 1));
                Call call;
                call.proc = item.proc;
                call.index = plan.constructor_index;
                call.specific = &item;
                call.mode = 1;
                call.result_type = type;
                call.doc = specific_doc(item, "Creates a C++ `" + owner + "`.").render("");
                procedures_ << emit_call(call);
                specs << (i ? ", " : "") << item.proc;
            }
            generics << "\n!> Creates a C++ `" << owner << "` (`" << lower(type) << " = " << type << "(...)`).\ninterface " << type
                     << "\n    module procedure " << specs.str() << "\nend interface\n";
        }
        if (plan.director && !list.empty()) {
            const auto generic = names_.claim(small + "_init");
            std::ostringstream specs;
            for (std::size_t i = 0; i < list.size(); ++i) {
                auto& item = list[i];
                item.proc = names_.claim(generic + "_" + std::to_string(i + 1));
                Call call;
                call.proc = item.proc;
                call.self_class = type;
                call.self_spec = "class(" + type + "), target, intent(inout)";
                call.index = plan.constructor_index;
                call.specific = &item;
                call.mode = 2;
                auto doc = specific_doc(item, "Creates the C++ `" + owner + "` of a Fortran extension.");
                doc.paragraphs.insert(doc.paragraphs.begin(),
                    "Creates the C++ object of `self`, an object of a Fortran type extending `" + type +
                    "`: C++ calls of virtual methods then run the extension's overriding bindings. `self` must stay at its address "
                    "(a `target`, `save`d or allocated variable) while C++ uses it; the last copy of it going out of scope (or "
                    "`close`) releases the C++ object.");
                doc.params.insert(doc.params.begin(), {"self", "the Fortran object (it must have the `target` attribute)"});
                call.doc = doc.render("");
                procedures_ << emit_call(call);
                specs << (i ? ", " : "") << item.proc;
            }
            generics << "\n!> Creates the C++ object of a Fortran extension of `" << type << "` (`call " << generic << "(self, ...)`).\ninterface "
                     << generic << "\n    module procedure " << specs.str() << "\nend interface\npublic :: " << generic << "\n";
        }
    }
    // Symbolic operators: module-level operator interfaces (collected across classes).
    for (const auto& group : plan.operators) {
        if (group.name == "item" || group.name == "invoke") continue;
        std::vector<Overload> members_ops, free_ops;
        for (const auto& overload : group.overloads) (overload.extension ? free_ops : members_ops).push_back(overload);
        auto& all = operator_specifics_[group.name];
        std::vector<Specific> existing;
        for (const auto& item : all) existing.push_back(item.second);
        for (const bool member : {true, false}) {
            const auto& overloads = member ? members_ops : free_ops;
            if (overloads.empty()) continue;
            auto list = specifics(overloads, owner + "::operator" + group.name, member, true, false, &existing);
            for (auto& item : list) {
                item.proc = names_.claim(small + "_op_" + std::to_string(all.size() + 1));
                Call call;
                call.proc = item.proc;
                call.self_class = member ? type : std::string();
                call.self_spec = self_in;
                call.index = group.index;
                call.specific = &item;
                call.status = false;
                auto doc = specific_doc(item, "C++ `" + item.overload->function->qualified_name() + "` (operator " + group.name + ").");
                doc.paragraphs.push_back("An error stops the program (operators take no `status`).");
                call.doc = doc.render("");
                procedures_ << emit_call(call);
                existing.push_back(item);
                all.emplace_back(owner, item);
            }
        }
    }
}

// The procedures C++ calls on director objects: decode the arguments, call the binding (the
// extension's override, or the generated one, which runs the C++ implementation), encode the result.
void Generator::emit_overrides(std::ostringstream& out) {
    std::map<std::size_t, std::vector<std::pair<std::string, std::string>>> cases; // generic -> (class, procedure)
    std::size_t counter = 0;
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director) continue;
        for (const auto& item : overridable(plan)) {
            const auto found = override_binding_.find(item.method);
            if (found == override_binding_.end()) continue;
            const auto& method = *item.method;
            Body b;
            std::vector<std::string> decls, actuals;
            std::string why;
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                auto t = convert(method.parameters[i], Use::parameter, why);
                const auto local = "rs_a" + std::to_string(i + 1);
                if (t.kind == K::optional || t.nullable) {
                    const auto& inner = t.kind == K::optional ? t.items[0] : t;
                    auto d = value_decl(inner);
                    if (d.spec.find("allocatable") == std::string::npos) d.spec += ", allocatable";
                    decls.push_back(decl_line(d, local));
                    b.line("if (.not. rs_take_nil(r)) then");
                    if (inner.kind != K::string && inner.kind != K::vector) b.line("    allocate(" + local + ")");
                    b.indent += "    ";
                    decode(b, inner, local, "r");
                    b.indent.resize(b.indent.size() - 4);
                    b.line("end if");
                    actuals.push_back(local);
                    continue;
                }
                auto local_type = t;
                local_type.writeback = false;
                decls.push_back(decl_line(t.kind == K::map || t.kind == K::tuple ? Decl{"type(" + t.helper + ")", ""} : value_decl(local_type), local));
                decode(b, local_type, local, "r");
                actuals.push_back(t.kind == K::vector && t.items[0].kind == K::string && !t.writeback ? "rs_chars(" + local + ")" : local);
            }
            std::string args;
            for (const auto& actual : actuals) args += actual + ", ";
            const auto result = convert(method.result, Use::result, why);
            const bool returns = result.kind != K::none;
            b.line("if (r%failed) return");
            if (returns) b.line("rs_res = self%" + found->second + "(" + args + "status=rs_st)");
            else b.line("call self%" + found->second + "(" + args + "status=rs_st)");
            b.line("if (rs_st%code /= 0) then");
            b.line("    if (allocated(rs_st%message)) then");
            b.line("        call rosetta_raise(rs_st%message)");
            b.line("    else");
            b.line("        call rosetta_raise('error in a Fortran override')");
            b.line("    end if");
            b.line("    return");
            b.line("end if");
            if (returns) encode(b, result, "rs_res", false, -1, "w");
            else b.line("call rs_put_nil(w)");
            const auto proc = names_.claim("rs_override_" + std::to_string(++counter), true);
            out << "\nsubroutine " << proc << "(self, r, w)\n    class(" << plan.name << ") :: self\n    type(rs_reader), intent(inout) :: r\n"
                << "    type(rs_writer), intent(inout) :: w\n    type(rosetta_error) :: rs_st\n";
            for (const auto& decl : decls) out << "    " << decl << "\n";
            if (returns) out << "    " << decl_line(value_decl(result), "rs_res") << "\n";
            out << b.loop_decls();
            out << b.code.str() << "end subroutine\n";
            cases[generic(item.name)].emplace_back(plan.name, proc);
        }
    }
    out << "\nsubroutine rs_dispatch_overrides(which, id, r, w)\n    integer, intent(in) :: which, id\n    type(rs_reader), intent(inout) :: r\n"
        << "    type(rs_writer), intent(inout) :: w\n    class(rosetta_object), pointer :: self\n    self => rs_self(id)\n";
    if (cases.empty()) out << "    if (associated(self) .and. which < 0) call rs_skip(r)\n    call rs_put_nil(w)\n";
    else {
        out << "    select case (which)\n";
        for (const auto& [index, list] : cases) {
            out << "    case (" << index << ")\n        select type (self)\n";
            for (const auto& [cls, proc] : list) out << "        class is (" << cls << ")\n            call " << proc << "(self, r, w)\n";
            out << "        class default\n            call rs_fail(r, 'the Fortran object does not implement this method')\n        end select\n";
        }
        out << "    case default\n        call rs_fail(r, 'unknown overridable method')\n    end select\n";
    }
    out << "    if (id < 0) call rs_skip(r)\nend subroutine\n";
}

// ------------------------------------------------------------------ the whole binding
GeneratedFortranFullSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    fmodule_ = options_.fortran_module.empty() ? name_ : options_.fortran_module;
    if (!identifier(fmodule_) || fmodule_.size() > 54 || !std::isalpha(static_cast<unsigned char>(fmodule_[0])))
        throw BindingError("Fortran module name must be an identifier of at most 54 characters starting with a letter: " + fmodule_);
    if (reserved_name(fmodule_)) throw BindingError("reserved Fortran module name: " + fmodule_);
    GeneratedFortranFullSource result;
    names_.add(fmodule_);
    names_.add(fmodule_ + "_rosetta");
    prepare();
    // Submodule modules are named <module>_<submodule>.
    std::set<std::string> submodule_names;
    for (const auto& function : module_.functions) {
        const auto submodule = policy_module(policy_, function.namespaces);
        if (!submodule.empty()) submodule_names.insert(submodule);
    }
    for (const auto& submodule : submodule_names) {
        if (!identifier(submodule)) throw BindingError("Fortran submodule name must be an identifier: " + submodule);
        names_.add(fmodule_ + "_" + submodule);
    }
    // Class names: escaped, unique (case-insensitively) in the module.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = names_.claim(fortran_identifier(plan.name));
    }
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        parents_[lower(plan.name)] = plan.bases.empty() ? "rosetta_object" : lower(classes_.at(plan.bases.front()).name);
    }
    // Enums: integer parameters <enum>_<label>.
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        if (!enumeration.owner.empty() && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, fortran_identifier(!enumeration.owner.empty() || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (!enumeration.owner.empty()) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = name;
        bool big = false;
        for (const auto& value : enumeration.enumerator_values) {
            const bool negative = !value.empty() && value[0] == '-';
            const auto digits = negative ? value.substr(1) : value;
            if (digits.size() > 10 || (digits.size() == 10 && digits > (negative ? "2147483648" : "2147483647"))) big = true;
        }
        enum_values_type_[qualified] = big ? "integer(c_int64_t)" : "integer";
    }
    std::ostringstream enums;
    std::size_t enum_index = 0;
    std::ostringstream enum_decoders;
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        const auto type = enum_values_type_[qualified];
        ++enum_index;
        enums << "\n" << describe(enumeration.documentation, "C++ enumeration `" + qualified + "`.").render("");
        std::ostringstream decoder;
        decoder << "\nfunction rs_enum" << enum_index << "(r) result(value)\n    type(rs_reader), intent(inout) :: r\n    " << type
                << " :: value\n    character(len=:), allocatable :: label\n    integer(c_int64_t) :: number\n"
                << "    if (rs_get_enum(r, label, number)) then\n        select case (label)\n";
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
            const auto constant = names_.claim(fortran_identifier(name + "_" + label));
            auto value = enumeration.enumerator_values[i];
            const auto literal = value == "-9223372036854775808" ? std::string("(-9223372036854775807_c_int64_t - 1_c_int64_t)")
                               : type == "integer" ? value : value + "_c_int64_t";
            enums << "!> C++ `" << qualified << "::" << enumeration.enumerators[i] << "` (" << value << ").\n"
                  << type << ", parameter, public :: " << constant << " = " << literal << "\n";
            decoder << "        case (" << f_string(label) << ")\n            value = " << constant << "\n";
        }
        decoder << "        case default\n            value = 0\n            call rs_fail(r, 'unknown enumerator ' // label)\n        end select\n"
                << "    else\n        value = " << (type == "integer" ? "int(number)" : "number") << "\n    end if\nend function\n";
        enum_decoders << decoder.str();
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, fortran_identifier(flat(variable.namespaces) ? variable.name : exported(qualified)));
    });
    // Directors whose overridable methods Fortran cannot express are plain classes.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        if (!plan.director) continue;
        for (const auto& item : overridable(plan)) {
            std::string why;
            for (const auto& parameter : item.method->parameters) {
                const auto t = convert(parameter, Use::parameter, why);
                if (why.empty() && (t.writeback || t.kind == K::callback)) why = "reference or callback parameters cannot be overridden in Fortran";
            }
            if (why.empty()) convert(item.method->result, Use::result, why);
            if (!why.empty()) {
                reject(owner, "Fortran extensions cannot override " + item.method->name + " (" + why + "); not extensible in Fortran");
                plan.director = false;
                break;
            }
        }
    }
    // Plan the Fortran side (it reports ambiguous and unsupported overloads) before the C++ library.
    // The C++ library assigns the function indices, so procedures are emitted afterwards: plan here
    // only what may reject, by a dry run into scratch streams.
    {
        auto saved_names = names_;
        auto saved_helpers = helpers_;
        auto saved_helper_names = helper_names_;
        auto saved_signatures = signatures_;
        auto saved_signature_index = signature_index_;
        std::ostringstream members, generics;
        for (const auto& owner : class_order_) plan_class(owner, members, generics);
        for (const auto& group : functions) specifics(group.overloads, group.module.empty() ? group.name : group.module + "." + group.name, false, false, false);
        for (const auto& constant : constants) {
            std::string why;
            convert(*constant.type, Use::result, why);
            if (!why.empty()) reject(constant.expression, why);
        }
        names_ = saved_names;
        helpers_ = saved_helpers;
        helper_names_ = saved_helper_names;
        signatures_ = saved_signatures;
        signature_index_ = saved_signature_index;
        procedures_.str("");
        types_.str("");
        native_function_types_.clear();
        bindings_.clear();
        binding_specifics_.clear();
        override_binding_.clear();
        operator_specifics_.clear();
    }
    check_diagnostics();
    const auto diagnostics = diagnostics_;
    emit_cpp(result.source, result.parts, functions, constants, "Fortran module " + fmodule_ + " (" + name_ + ", full API).");

    // --------------------------------------------------------------- the Fortran modules
    std::ostringstream classes, generics;
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        std::ostringstream members, own_generics;
        plan_class(owner, members, own_generics);
        auto doc = describe(plan.record->documentation, "C++ class `" + owner + "`.");
        if (plan.bases.size() > 1) {
            std::string also;
            for (std::size_t i = 1; i < plan.bases.size(); ++i) also += (i > 1 ? ", " : "") + std::string("`") + classes_.at(plan.bases[i]).name + "`";
            doc.paragraphs.push_back("Also derives (in C++) from " + also + ": assign it to a variable of that type to use its procedures.");
        }
        if (plan.director)
            doc.paragraphs.push_back(std::string(plan.record->abstract ? "Abstract in C++: extend" : "Extend") + " it in Fortran, override its "
                                     "virtual bindings and call `" + lower(plan.name) + "_init(self, ...)`: C++ then calls the overrides.");
        else if (plan.record->abstract)
            doc.paragraphs.push_back("Abstract in C++: C++ returns objects of derived classes.");
        const auto parent = plan.bases.empty() ? std::string("rosetta_object") : classes_.at(plan.bases.front()).name;
        classes << "\n" << doc.render("") << "type, extends(" << parent << "), public :: " << plan.name << "\n";
        if (!members.str().empty()) classes << "contains\n" << members.str();
        classes << "end type\n";
        generics << own_generics.str();
    }
    // Module functions, constants and operators.
    ModuleOut main_functions;
    for (auto& group : functions) {
        auto list = specifics(group.overloads, group.module.empty() ? group.name : group.module + "." + group.name, false, false, false);
        if (list.empty()) continue;
        ModuleOut* target = nullptr;
        if (!group.module.empty()) {
            auto& sub = submodules_[group.module];
            if (sub.name.empty()) {
                sub.name = fmodule_ + "_" + group.module;
                // Its own names; only the types it imports from the main module are taken.
                for (const auto& owner : class_order_) sub.names.add(classes_.at(owner).name);
                for (const auto& [key, helper] : helper_names_) sub.names.add(helper);
                for (const auto& signature : signatures_) sub.names.add(signature.iface);
                sub.names.add(fmodule_);
                sub.names.add(fmodule_ + "_rosetta");
                sub.names.add(sub.name);
            }
            target = &sub;
        }
        auto& registry = target ? target->names : names_;
        const auto generic = registry.claim(group.name);
        std::ostringstream specs;
        for (std::size_t i = 0; i < list.size(); ++i) {
            auto& item = list[i];
            item.proc = registry.claim(generic + "_" + std::to_string(i + 1));
            if (target) names_.add(item.proc);
            Call call;
            call.proc = item.proc;
            call.index = group.index;
            call.specific = &item;
            call.doc = specific_doc(item, "C++ `" + item.overload->function->qualified_name() + "`.").render("");
            (target ? target->procedures : procedures_) << emit_call(call);
            specs << (i ? ", " : "") << item.proc;
            if (target) {
                for (const auto& arg : item.args) {
                    std::function<void(const FT&)> use = [&](const FT& t) {
                        if (t.kind == K::object || t.kind == K::function) target->uses.insert(t.name);
                        if (!t.helper.empty()) target->uses.insert(t.helper);
                        if (t.kind == K::callback) target->uses.insert(t.name);
                        for (const auto& item_type : t.items) use(item_type);
                    };
                    use(arg.type);
                }
                std::function<void(const FT&)> use = [&](const FT& t) {
                    if (t.kind == K::object || t.kind == K::function) target->uses.insert(t.name);
                    if (!t.helper.empty()) target->uses.insert(t.helper);
                    for (const auto& item_type : t.items) use(item_type);
                };
                use(item.result);
            }
        }
        (target ? target->interfaces : generics) << "\n" << comment_lines(describe(list[0].overload->function->documentation, "C++ `" + group.name + "`.").summary, "")
            << "interface " << generic << "\n    module procedure " << specs.str() << "\nend interface\npublic :: " << generic << "\n";
    }
    for (auto& constant : constants) {
        std::string why;
        const auto t = convert(*constant.type, Use::result, why);
        if (!why.empty() || t.kind == K::none) continue;
        const auto generic = names_.claim(constant.name);
        Call call;
        call.proc = names_.claim(generic + "_value");
        call.index = constant.index;
        call.mode = 5;
        call.result = t;
        call.status = false;
        call.doc = describe(*constant.documentation, "C++ constant `" + normalize(constant.expression) + "`.").render("");
        procedures_ << emit_call(call);
        generics << "\n" << comment_lines(describe(*constant.documentation, "C++ constant `" + normalize(constant.expression) + "`.").summary, "")
                 << "interface " << generic << "\n    module procedure " << call.proc << "\nend interface\npublic :: " << generic << "\n";
    }
    for (const auto& [op, list] : operator_specifics_) {
        const auto spelled_op = op.front() == '.' ? op : op;
        generics << "\n!> C++ operator" << (op == "/=" ? std::string("!=") : op == ".not." ? std::string("!") : op) << " on native classes.\ninterface operator("
                 << spelled_op << ")\n    module procedure ";
        for (std::size_t i = 0; i < list.size(); ++i) generics << (i ? ", " : "") << list[i].second.proc;
        generics << "\nend interface\npublic :: operator(" << spelled_op << ")\n";
    }

    // Callbacks: storage, registration and the dispatcher.
    std::ostringstream callback_types, callback_procs;
    for (std::size_t k = 0; k < signatures_.size(); ++k) {
        auto& s = signatures_[k];
        const auto index = std::to_string(k + 1);
        s.store = "rs_cb" + index;
        s.add = "rs_cb" + index + "_add";
    }
    std::ostringstream interfaces_block;
    for (std::size_t k = 0; k < signatures_.size(); ++k) {
        const auto& s = signatures_[k];
        const auto index = std::to_string(k + 1);
        const auto& t = s.type;
        const bool returns = !t.items.empty() && t.items[0].kind != K::none;
        interfaces_block << "\n!> A Fortran procedure C++ calls (a C++ `std::function`): ";
        std::string shape = returns ? "function f(" : "subroutine f(";
        for (std::size_t i = 1; i < t.items.size(); ++i) shape += (i > 1 ? ", " : "") + std::string("a") + std::to_string(i);
        interfaces_block << "`" << shape << ")`.\n!> Dummy arguments are `intent(in)`; an error is reported with `rosetta_raise`.\nabstract interface\n    "
                         << (returns ? "function " : "subroutine ") << s.iface << "(";
        for (std::size_t i = 1; i < t.items.size(); ++i) interfaces_block << (i > 1 ? ", " : "") << "a" << i;
        interfaces_block << ")" << (returns ? " result(r)" : "") << "\n        import\n";
        for (std::size_t i = 1; i < t.items.size(); ++i) {
            auto arg = t.items[i];
            arg.writeback = false;
            interfaces_block << "        " << decl_line(param_decl(arg), "a" + std::to_string(i)) << "\n";
        }
        if (returns) interfaces_block << "        " << decl_line(value_decl(t.items[0]), "r") << "\n";
        interfaces_block << "    end " << (returns ? "function" : "subroutine") << "\nend interface\npublic :: " << s.iface << "\n";
        callback_types << "type :: " << s.store << "\n    procedure(" << s.iface << "), pointer, nopass :: p => null()\nend type\n"
                       << "type(" << s.store << "), allocatable, save :: " << s.store << "s(:)\n";
        callback_procs << "\nfunction " << s.add << "(f) result(id)\n    procedure(" << s.iface << ") :: f\n    integer :: id\n    type(" << s.store
                       << "), allocatable :: grown(:)\n    id = rs_new_callback(" << index << ")\n    if (.not. allocated(" << s.store << "s)) allocate("
                       << s.store << "s(max(64, id)))\n    if (id > size(" << s.store << "s)) then\n        allocate(grown(max(2 * size(" << s.store
                       << "s), id)))\n        grown(1:size(" << s.store << "s)) = " << s.store << "s\n        call move_alloc(grown, " << s.store
                       << "s)\n    end if\n    " << s.store << "s(id)%p => f\nend function\n";
        // The dispatcher: decode, call, encode.
        Body b;
        std::vector<std::string> decls, actuals;
        for (std::size_t i = 1; i < t.items.size(); ++i) {
            auto arg = t.items[i];
            arg.writeback = false;
            const auto local = "a" + std::to_string(i);
            if (arg.kind == K::optional || arg.nullable) {
                const auto& inner = arg.kind == K::optional ? arg.items[0] : arg;
                auto d = value_decl(inner);
                if (d.spec.find("allocatable") == std::string::npos) d.spec += ", allocatable";
                decls.push_back(decl_line(d, local));
                b.line("if (.not. rs_take_nil(r)) then");
                if (inner.kind != K::string && inner.kind != K::vector) b.line("    allocate(" + local + ")");
                b.indent += "    ";
                decode(b, inner, local, "r");
                b.indent.resize(b.indent.size() - 4);
                b.line("end if");
            } else {
                decls.push_back(decl_line(arg.kind == K::map || arg.kind == K::tuple ? Decl{"type(" + arg.helper + ")", ""} : value_decl(arg), local));
                decode(b, arg, local, "r");
            }
            actuals.push_back(arg.kind == K::vector && arg.items[0].kind == K::string ? "rs_chars(" + local + ")" : local);
        }
        std::string args;
        for (std::size_t i = 0; i < actuals.size(); ++i) args += (i ? ", " : "") + actuals[i];
        b.line("if (r%failed) return");
        if (returns) {
            b.line("rs_res = f(" + args + ")");
            encode(b, t.items[0], "rs_res", false, -1, "w");
        } else {
            b.line("call f(" + args + ")");
            b.line("call rs_put_nil(w)");
        }
        callback_procs << "\nsubroutine rs_cb" << index << "_call(f, r, w)\n    procedure(" << s.iface << ") :: f\n    type(rs_reader), intent(inout) :: r\n"
                       << "    type(rs_writer), intent(inout) :: w\n";
        for (const auto& decl : decls) callback_procs << "    " << decl << "\n";
        if (returns) callback_procs << "    " << decl_line(value_decl(t.items[0]), "rs_res") << "\n";
        callback_procs << b.loop_decls();
        callback_procs << b.code.str() << "end subroutine\n";
    }
    callback_procs << "\nsubroutine rs_dispatch_callbacks(which, id, r, w)\n    integer, intent(in) :: which, id\n    type(rs_reader), intent(inout) :: r\n"
                   << "    type(rs_writer), intent(inout) :: w\n";
    if (signatures_.empty()) callback_procs << "    if (which < 0 .or. id < 0) call rs_skip(r)\n    call rs_fail(r, 'no Fortran callbacks')\n    call rs_put_nil(w)\n";
    else {
        callback_procs << "    select case (which)\n";
        for (std::size_t k = 0; k < signatures_.size(); ++k)
            callback_procs << "    case (" << k + 1 << ")\n        call rs_cb" << k + 1 << "_call(" << signatures_[k].store << "s(id)%p, r, w)\n";
        callback_procs << "    case default\n        call rs_fail(r, 'unknown callback signature')\n    end select\n";
    }
    callback_procs << "end subroutine\n";
    std::ostringstream overrides;
    emit_overrides(overrides);

    // Class names (for class_name()).
    std::ostringstream class_names;
    class_names << "\nfunction rs_class_names(klass) result(name)\n    integer, intent(in) :: klass\n    character(len=:), allocatable :: name\n"
                << "    select case (klass)\n";
    for (std::size_t i = 0; i < class_order_.size(); ++i)
        class_names << "    case (" << i << ")\n        name = " << f_string(classes_.at(class_order_[i]).name) << "\n";
    class_names << "    case (-2)\n        name = 'native function'\n    case default\n        name = 'native object'\n    end select\nend function\n";

    // --------------------------------------------------------------- assembly
    // Internal procedures the submodule modules also call.
    std::string internal = "rs_start";
    for (std::size_t i = 1; i <= enum_names_.size(); ++i) internal += ", rs_enum" + std::to_string(i);
    for (const auto& signature : signatures_) internal += ", " + signature.add;
    std::string runtime = detail::fortran_full_runtime_f90;
    const auto replace = [&](const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
    };
    replace("@M@", fmodule_ + "_rosetta");
    replace("@C@", "rosetta_" + name_);
    replace("@NAME@", fmodule_);
    std::ostringstream f;
    f << "! Generated by Rosetta: Fortran 2018 interface to native module `" << name_ << "` (full API).\n"
      << "! Link the program with the native library built from the generated C++ source.\n" << runtime;
    f << "\n!> Fortran interface to the C++ library `" << name_ << "` (generated by Rosetta).\n!>\n"
      << "!> Every procedure takes an optional trailing `type(rosetta_error) :: status`; without it an error stops the program.\n"
      << "!> Classes are derived types extending `rosetta_object`: `x = Type(...)` constructs, type-bound procedures call methods,\n"
      << "!> copies share the C++ object and the last scalar copy releases it.\n"
      << "module " << fmodule_ << "\nuse, intrinsic :: iso_c_binding, only: c_double, c_float, c_int64_t\nuse " << fmodule_ << "_rosetta\nimplicit none\nprivate\n"
      << "public :: rosetta_error, rosetta_string, rosetta_object, rosetta_raise, rosetta_same, c_double, c_float, c_int64_t\npublic :: " << internal << "\n"
      << enums.str() << classes.str() << types_.str() << helpers_ << interfaces_block.str() << generics.str() << "\n" << callback_types.str()
      << "\ncontains\n\n! Starts a call (and installs this module's dispatchers in the runtime).\nsubroutine rs_start(w, count)\n"
      << "    type(rs_writer), intent(inout) :: w\n    integer, intent(in) :: count\n    if (.not. associated(rs_class_name_fn)) then\n"
      << "        rs_callback_dispatch => rs_dispatch_callbacks\n        rs_override_dispatch => rs_dispatch_overrides\n"
      << "        rs_class_name_fn => rs_class_names\n    end if\n    call rs_begin(w, count)\nend subroutine\n"
      << enum_decoders.str() << class_names.str() << callback_procs.str() << overrides.str() << procedures_.str() << "end module " << fmodule_ << "\n";
    for (auto& [submodule, out] : submodules_) {
        std::string only;
        for (const auto& name : out.uses) only += ", " + name;
        f << "\n!> The functions of the C++ namespace mapped to submodule `" << submodule << "` of `" << name_ << "`.\nmodule " << out.name
          << "\nuse, intrinsic :: iso_c_binding, only: c_double, c_float, c_int64_t\nuse " << fmodule_ << "_rosetta\nuse " << fmodule_ << ", only: " << internal
          << only << "\nimplicit none\nprivate\n" << out.interfaces.str() << "\ncontains\n" << out.procedures.str() << "end module " << out.name << "\n";
    }
    // Native calls nest (callbacks and overrides call back into the module), so every module
    // procedure is recursive (gfortran does not make procedures recursive by default).
    std::string text;
    {
        std::istringstream lines(f.str());
        for (std::string line; std::getline(lines, line);) {
            for (const char* start : {"function ", "subroutine ", "logical function ", "integer function "})
                if (line.rfind(start, 0) == 0) { line = "recursive " + line; break; }
            text += line + "\n";
        }
    }
    result.interface = wrapped(text);
    result.diagnostics = diagnostics;
    return result;
}
} // namespace

GeneratedFortranFullSource generate_fortran_full(const Module& module, const std::string& name, bool skip_unsupported,
                                                 const BindingPolicy& policy, const FortranFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Fortran source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
