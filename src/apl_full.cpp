// Full APL backend (--language apl --apl-api full): a GNU APL native function plus a GNU APL script.
//
// GNU APL has no namespaces: every binding is a defined function named PREFIX∆... (PREFIX is the
// module name or --apl-prefix): PREFIX∆f (free functions, PREFIX∆sub∆f in policy modules),
// PREFIX∆Class∆new (constructors), PREFIX∆Class∆subclass (an APL implementation of the class's
// virtual methods), PREFIX∆Class∆method, PREFIX∆Class∆field and PREFIX∆Class∆field∆set,
// PREFIX∆Class∆plus (operators), PREFIX∆Enum∆name (enumerators) and PREFIX∆constant. C++ names are
// ASCII identifiers, which are APL names too; `∆` never occurs in them, so the joined names never
// collide with each other.
//
// Calling convention: a function's right argument is its one argument when every overload takes
// exactly one; otherwise a vector of its arguments (a scalar, a character vector, a handle or a
// function reference is one argument; ⍬ is none). Methods take the object handle on the left
// (methods without parameters take it on the right). Overloads and default arguments are resolved
// at run time in C++. Objects are handles ('Class' id) released with PREFIX∆Delete; callbacks are
// function references ('⍙fn' 'NAME', made by PREFIX∆fn) called re-entrantly under ⎕EC. C++
// exceptions are APL errors signalled by ⎕ES with messages 'PREFIX: kind: what'.
//
// Every function carries its documentation as ⍝ lines (Markdown) right after its header, the APL
// convention mep's APL server (`mep-apl-lsp --doc NAME`) reads.
//
// The C++ side is the library every full C ABI backend shares (cabi_full.hpp,
// lisp_full_runtime.hpp) with apl_full_runtime.hpp appended: the main source (and parts) build
// libMODULE_gnu_apl.so against GNU APL's configured headers.
#include "rosetta/apl.hpp"
#include "apl_full_runtime.hpp"
#include "cabi_full.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

// An APL character vector literal.
std::string apl_string(const std::string& text) {
    std::string result = "'";
    for (char c : text) result += c == '\'' ? std::string("''") : std::string(1, c);
    return result + "'";
}
// A name piece: C++ identifiers as they are; anything else (policy renames) made an APL name.
std::string apl_piece(const std::string& text) {
    std::string result;
    for (char c : text) result += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result[0]))) result = "_" + result;
    return result;
}
// A C++ numeric literal as an APL number (¯ for the sign, E for the exponent).
std::string apl_number(std::string text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '-') result += "¯";
        else if (c == 'e' || c == 'E') result += 'E';
        else if (c == '+' && i > 0 && (text[i - 1] == 'e' || text[i - 1] == 'E')) continue;
        else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') result += c;
    }
    return result.empty() ? text : result;
}
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        lines.push_back(line);
    }
    return lines;
}
std::string error_kind_of(const std::string& cpp) {
    const auto bare = normalize(cpp);
    if (bare == "std::invalid_argument") return "invalid_argument";
    if (bare == "std::out_of_range") return "out_of_range";
    if (bare == "std::domain_error") return "domain_error";
    if (bare == "std::length_error") return "length_error";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "arithmetic_error";
    if (bare == "std::bad_alloc") return "allocation";
    return "native";
}
std::string event_of(const std::string& kind) {
    if (kind == "out_of_range") return "INDEX ERROR";
    if (kind == "length_error") return "LENGTH ERROR";
    if (kind == "allocation") return "WS FULL";
    return "DOMAIN ERROR";
}

// ⍝ documentation lines (Markdown).
struct Doc {
    std::vector<std::string> lines;
    void line(const std::string& text = {}) { lines.push_back(text); }
    void blank() { if (!lines.empty() && !lines.back().empty()) lines.emplace_back(); }
    void text(const std::string& text, const std::string& indent = {}) { for (const auto& l : split_lines(text)) lines.push_back(l.empty() ? l : indent + l); }
    std::string render() const {
        std::vector<std::string> trimmed = lines;
        while (!trimmed.empty() && trimmed.back().empty()) trimmed.pop_back();
        std::string out;
        for (const auto& l : trimmed) out += "⍝" + (l.empty() ? std::string() : " " + l) + "\n";
        return out;
    }
};
std::string summary(const Documentation& doc, const std::string& fallback) {
    if (!doc.brief.empty()) return one_line(doc.brief);
    for (const auto& block : doc.details) if (block.kind == "text") return one_line(block.text);
    return fallback;
}
// The description after the summary: text, code blocks and headings.
void describe(Doc& doc, const Documentation& documentation) {
    bool first = documentation.brief.empty(), heading = false;
    for (const auto& block : documentation.details) {
        if (first && block.kind == "text") { first = false; continue; }
        first = false;
        if (!heading) doc.blank();
        if (block.kind == "code" || block.kind == "math") {
            doc.line("```");
            doc.text(block.text);
            doc.line("```");
        } else if (block.kind == "heading") doc.line("**" + one_line(block.text) + "**");
        else doc.text(block.text);
        heading = block.kind == "heading";
    }
}

// How a callable's right argument holds its arguments (the native function's mode).
enum Mode { whole = 0, vector = 1, none = 2 };

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const AplFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedAplFullSource run();

private:
    AplFullOptions options_;
    std::string prefix_;   // PREFIX of every APL name
    std::string native_;   // PREFIX⍙native
    GeneratedAplFullSource result_;
    std::set<std::string> taken_;
    std::ostringstream script_;

    std::string language() const override { return "APL"; }
    std::string target_name(const std::string& cpp) const override { return apl_piece(cpp); }
    std::string type_name(const std::string& cpp) const override { return apl_piece(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return apl_piece(name); }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override {
        if (cpp == "operator()") return "call";
        static const std::map<std::string, std::pair<const char*, const char*>> names = {
            {"operator+", {nullptr, "plus"}}, {"operator-", {"negate", "minus"}}, {"operator*", {nullptr, "times"}},
            {"operator/", {nullptr, "divide"}}, {"operator%", {nullptr, "mod"}}, {"operator==", {nullptr, "eq"}},
            {"operator!=", {nullptr, "ne"}}, {"operator<", {nullptr, "lt"}}, {"operator<=", {nullptr, "le"}},
            {"operator>", {nullptr, "gt"}}, {"operator>=", {nullptr, "ge"}}, {"operator!", {"not", nullptr}},
            {"operator&&", {nullptr, "and"}}, {"operator||", {nullptr, "or"}}, {"operator[]", {nullptr, "at"}}};
        const auto found = names.find(cpp);
        if (found == names.end() || arity > 1) return {};
        const char* result = arity == 0 ? found->second.first : found->second.second;
        return result ? result : "";
    }
    bool free_operator(const std::string& name) const override { return name != "at" && name != "call"; }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return apl_piece(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken{"Z", "B", "H", "A"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : cpp;
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool is_method) const override {
        std::string text;
        for (const auto& line : usages(group, group_path(group, is_method), is_method)) text += "    " + line + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& line : constructor_usages(plan, false)) text += "    " + line + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return prefix_ + "∆" + plan.name + " is abstract: implement it in APL with " + prefix_ + "∆" + plan.name + "∆subclass";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return prefix_ + "∆" + plan.name + " constructor is protected: use " + prefix_ + "∆" + plan.name + "∆subclass";
    }
    std::string symbol_prefix() const override { return "rosetta_apl_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_APL"; }
    std::string module_prefix() const override { return "rosetta_apl_module_"; }

    // ------------------------------------------------------------ naming
    std::string module_path(const std::string& submodule) const {
        std::string path = prefix_;
        for (std::size_t start = 0; !submodule.empty();) {
            const auto dot = submodule.find('.', start);
            path += "∆" + apl_piece(submodule.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return path;
    }
    std::string class_path(const std::string& owner) const { return prefix_ + "∆" + classes_.at(owner).name; }
    // Free and static functions keep their group's name; methods are found through their class.
    mutable std::map<const Group*, std::string> paths_;
    std::string group_path(const Group& group, bool) const {
        const auto found = paths_.find(&group);
        return found != paths_.end() ? found->second : module_path(group.module) + "∆" + group.name;
    }
    bool claim(const std::string& name) { return taken_.insert(name).second; }

    // ------------------------------------------------------------ types and defaults
    std::string apl_type(const Type& type) const;
    std::string default_text(const DefaultValue& value, const Type& type) const;
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    static bool returns_void(const Group& group) {
        return std::all_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) { return is_void(o.function->result); });
    }
    bool has_writebacks(const Group& group) const {
        for (const auto& overload : group.overloads) {
            const std::size_t first = overload.receives_self ? 1 : 0;
            for (std::size_t i = first; i < overload.function->parameters.size(); ++i) if (writeback(overload.function->parameters[i])) return true;
        }
        return false;
    }
    // Parameters an overload takes from the right argument (`self` excluded).
    static std::size_t count_of(const Overload& overload) { return overload.function->parameters.size() - (overload.receives_self ? 1 : 0); }
    static std::size_t required_of(const Overload& overload) {
        const auto count = count_of(overload);
        return count - std::min(count, overload.function->default_arguments);
    }
    static Mode mode_of(const std::vector<Overload>& overloads) {
        std::size_t most = 0;
        bool one = true;
        for (const auto& overload : overloads) {
            most = std::max(most, count_of(overload));
            if (count_of(overload) != 1 || required_of(overload) != 1) one = false;
        }
        if (most == 0) return none;
        return one ? whole : vector;
    }
    // The call forms of a group, one per overload and arity.
    std::vector<std::string> usages(const Group& group, const std::string& callee, bool is_method) const {
        return forms(group.overloads, callee, is_method, has_writebacks(group) || !returns_void(group) ? "Z←" : "", "");
    }
    std::vector<std::string> forms(const std::vector<Overload>& overloads, const std::string& callee, bool is_method,
                                   const std::string& result, const std::string& left) const {
        std::vector<std::string> lines;
        std::set<std::string> seen;
        const auto mode = mode_of(overloads);
        for (const auto& overload : overloads) {
            const auto names = parameter_names(overload);
            for (std::size_t arity = required_of(overload); arity <= count_of(overload); ++arity) {
                std::string line = result;
                if (!left.empty()) line += left + " ";
                if (is_method && mode != none) line += "obj ";
                line += callee;
                if (mode == none) line += is_method ? " obj" : "";
                else if (arity == 0) line += " ⍬";
                else for (std::size_t i = 0; i < arity; ++i) line += " " + names[i];
                if (seen.insert(line).second) lines.push_back(line);
            }
        }
        return lines;
    }
    std::vector<std::string> constructor_usages(const ClassPlan& plan, bool subclass) const {
        return forms(plan.constructors, class_path(plan.record->qualified_name()) + (subclass ? "∆subclass" : "∆new"), false, "obj←",
                     subclass ? "'Impl'" : "");
    }
    std::string cpp_signature(const Function& function) const {
        std::string text = function.is_constructor ? std::string() : function.result.spelling + " ";
        text += function.qualified_name() + "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            text += (i ? ", " : "") + function.parameters[i].spelling;
            const auto& cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            if (!cpp.empty()) text += " " + cpp;
            if (i < function.default_values.size() && !function.default_values[i].kind.empty()) {
                const auto& value = function.default_values[i];
                text += " = " + (value.kind == "char" ? "'" + value.value + "'" : value.kind == "default" ? std::string("{}") : value.value);
            } else if (i + function.default_arguments >= function.parameters.size()) text += " = ...";
        }
        text += ")";
        if (function.is_const) text += " const";
        return text;
    }
    void sections(Doc& doc, const std::vector<Overload>& overloads, const Overload& documented, bool with_result,
                  const std::string& result_type, const std::string& result_text) const;
    void errors(Doc& doc, const Documentation& documentation, bool any) const;
    Doc group_doc(const Group& group, const std::string& callee, bool is_method, const std::string& what) const;

    // ------------------------------------------------------------ emission
    void emit(const std::string& header, const Doc& doc, const std::string& body) {
        script_ << "\n∇" << header << "\n" << doc.render() << body << "∇\n";
    }
    std::string call(const std::string& a, const std::string& b, bool result) const {
        return " (⍙M ⍙E " + std::string(result ? "Z" : "⍙Z") + ")←(" + a + ") " + native_ + " " + b + "\n ⍙M ⎕ES ⍙E\n";
    }
    void emit_group(const Group& group, const std::string& path, bool is_method, const std::string& what);
    void emit_class(const std::string& owner);
    void emit_enum(const std::string& qualified, const std::string& name);
    void emit_runtime();
    std::string header_comment(const std::string& what) const { return "Generated by Rosetta from " + module_.header + ": " + what + "."; }
};

std::string Generator::apl_type(const Type& original) const {
    const Type& type = referent(original);
    if (type.kind == TypeKind::pointer && !type.arguments.empty()) {
        const auto& pointee = type.arguments[0];
        if (bound(pointee)) return classes_.at(pointee.declaration).name + " handle, or ⍬";
        return "character vector";
    }
    const auto bare = normalize(strip_cv(type.canonical));
    if (type.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(type.declaration);
        return found != enum_names_.end() ? "integer (" + prefix_ + "∆" + found->second + "∆…, or the enumerator's name)" : "integer";
    }
    if (bound(type)) return classes_.at(type.declaration).name + " handle";
    if (bare == "bool") return "boolean (0 or 1)";
    if (bare == "char") return "character";
    if (bare == "float" || bare == "double" || bare == "long double") return "number";
    if (type.kind == TypeKind::builtin) return "integer";
    if (bare == "std::filesystem::path") return "character vector (path)";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "⍬";
    if (!conversion(type).empty()) return "value convertible to " + policy_.conversions.at(conversion(type));
    if (type.kind != TypeKind::template_instance) return strip_cv(type.spelling);
    const auto declaration = normalize(type.declaration);
    const auto argument = [&](std::size_t i) { return i < type.arguments.size() ? apl_type(type.arguments[i]) : std::string("value"); };
    if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "character vector";
    if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::valarray" ||
        declaration == "std::array" || declaration == "std::set" || declaration == "std::unordered_set") {
        const auto element = argument(0);
        if (element == "number") return "numeric vector";
        if (element == "integer") return "integer vector";
        if (element == "boolean (0 or 1)") return "boolean vector";
        if (element == "numeric vector") return "numeric matrix (one row per element)";
        if (element == "character vector") return "vector of character vectors";
        return "vector of (" + element + ")";
    }
    if (declaration == "std::optional") return argument(0) + ", or ⍬ for none";
    if (declaration == "std::map" || declaration == "std::unordered_map")
        return "2-column matrix (keys: " + argument(0) + "; values: " + argument(1) + ")";
    if (declaration == "std::pair" || declaration == "std::tuple") {
        std::string text = "vector (";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? "; " : "") + argument(i);
        return text + ")";
    }
    if (declaration == "std::variant") {
        std::string text;
        for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? " or " : "") + argument(i);
        return text;
    }
    if (declaration == "std::complex") return "complex number (1J2)";
    if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return argument(0) + ", or ⍬";
    if (declaration == "std::function") return "function reference (" + prefix_ + "∆fn 'NAME')";
    if (declaration == "std::chrono::duration") return "number (seconds)";
    return strip_cv(type.spelling);
}

std::string Generator::default_text(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "float") return apl_number(value.value);
    if (value.kind == "constant") return value.value;
    if (value.kind == "bool") return value.value == "true" ? "1" : "0";
    if (value.kind == "null") return "⍬";
    if (value.kind == "char") return apl_string(value.value);
    if (value.kind == "string") {
        auto text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
        return apl_string(text);
    }
    if (value.kind == "enum") {
        const auto found = enum_names_.find(referent(type).declaration);
        return found != enum_names_.end() ? prefix_ + "∆" + found->second + "∆" + apl_piece(value.value) : value.value;
    }
    if (value.kind == "default") return "the C++ default";
    return {};
}

void Generator::errors(Doc& doc, const Documentation& documentation, bool any) const {
    if (!documentation.throws.empty()) {
        doc.blank();
        doc.line("**Errors:**");
        for (const auto& item : documentation.throws) {
            const auto kind = error_kind_of(item.name);
            doc.line("- " + event_of(kind) + " `" + prefix_ + ": " + kind + ": …`" + (item.text.empty() ? "" : ": " + one_line(item.text)) +
                     (item.name.empty() ? "" : " (C++ `" + item.name + "`)"));
        }
    } else if (any) {
        doc.blank();
        doc.line("**Errors:** C++ exceptions signal `" + prefix_ + ": kind: message` (DOMAIN ERROR; INDEX ERROR for out_of_range).");
    }
    bool notes = false;
    for (const auto& item : documentation.notes) {
        if (!notes) doc.blank();
        notes = true;
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        doc.line("- **" + label + ":** " + one_line(item.text));
    }
}

// Arguments (of a single overload), result.
void Generator::sections(Doc& doc, const std::vector<Overload>& overloads, const Overload& documented, bool with_result,
                         const std::string& result_type, const std::string& result_text) const {
    const auto& function = *documented.function;
    if (overloads.size() == 1 && count_of(documented) > 0) {
        doc.blank();
        doc.line("**Arguments:**");
        const std::size_t first = documented.receives_self ? 1 : 0;
        const auto names = parameter_names(documented);
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            std::string text;
            for (const auto& item : function.documentation.params) if (!cpp.empty() && item.name == cpp) text = item.text;
            std::string shown = i < function.default_values.size() ? default_text(function.default_values[i], function.parameters[i]) : std::string();
            if (shown.empty() && i + function.default_arguments >= function.parameters.size()) shown = "the C++ default";
            doc.line("- `" + names[i - first] + "` " + apl_type(function.parameters[i]) + (shown.empty() ? "" : ", default " + shown) +
                     (writeback(function.parameters[i]) ? " (updated: returned in Z)" : "") + (text.empty() ? "" : ": " + one_line(text)));
        }
    }
    if (with_result) {
        doc.blank();
        doc.line("**Result:** " + result_type + (result_text.empty() ? "" : ": " + one_line(result_text)));
    }
}

Doc Generator::group_doc(const Group& group, const std::string& callee, bool is_method, const std::string& what) const {
    const Overload* documented = &group.overloads.front();
    for (const auto& overload : group.overloads) if (!overload.function->documentation.empty()) { documented = &overload; break; }
    const auto& function = *documented->function;
    // An undocumented override shows the documentation of the method it overrides.
    const Documentation* inherited = nullptr;
    if (function.documentation.empty() && is_method && classes_.count(documented->owner)) {
        std::function<void(const std::string&)> walk = [&](const std::string& cls) {
            for (const auto& base : classes_.at(cls).bases) {
                for (const auto& other : classes_.at(base).methods)
                    for (const auto& overload : other.overloads)
                        if (!inherited && overload.function->name == function.name && !overload.function->documentation.empty())
                            inherited = &overload.function->documentation;
                if (!inherited) walk(base);
            }
        };
        walk(documented->owner);
    }
    const auto& documentation = inherited ? *inherited : function.documentation;
    Doc doc;
    doc.line(summary(documentation, what + " (C++ `" + function.qualified_name() + "`)."));
    describe(doc, documentation);
    doc.blank();
    doc.line("**Usage:**");
    doc.line("```apl");
    for (const auto& line : usages(group, callee, is_method)) doc.line(line);
    doc.line("```");
    const auto mode = mode_of(group.overloads);
    if (mode == vector) doc.line("The right argument is the vector of arguments (one argument may be given alone; ⍬ for none).");
    const bool writebacks = has_writebacks(group);
    const bool single = group.overloads.size() == 1;
    std::string result_type = single && !is_void(function.result) ? apl_type(function.result) : std::string("depends on the overload");
    if (writebacks) result_type = (is_void(function.result) ? std::string("the updated arguments") : "the result, then the updated arguments") +
                                  " (one value alone, several as a vector)";
    sections(doc, group.overloads, *documented, writebacks || !returns_void(group), result_type, single ? documentation.returns : std::string());
    errors(doc, documentation, false);
    doc.blank();
    if (group.overloads.size() > 1) doc.line("Overloads are chosen at run time from the arguments:");
    for (const auto& overload : group.overloads) doc.line("- C++: `" + cpp_signature(*overload.function) + "`");
    if (!documentation.see.empty()) {
        std::string see;
        for (const auto& target : documentation.see) {
            auto name = target;
            const auto colons = name.rfind("::");
            if (colons != std::string::npos) name = name.substr(colons + 2);
            see += (see.empty() ? "" : ", ") + std::string("`") + prefix_ + "∆" + name + "`";
        }
        doc.blank();
        doc.line("**See also:** " + see);
    }
    return doc;
}

// One callable (free function, static method, method or operator).
void Generator::emit_group(const Group& group, const std::string& path, bool is_method, const std::string& what) {
    const auto mode = mode_of(group.overloads);
    const bool result = has_writebacks(group) || !returns_void(group);
    std::string header = result ? "Z←" : "";
    std::string a = std::to_string(group.index) + " " + std::to_string(mode);
    std::string b = "B";
    if (is_method) {
        if (mode == none) { header += path + " H"; b = "⍬"; }
        else header += "H " + path + " B";
        a += " H";
    } else if (mode == none) { header += path; b = "⍬"; }
    else header += path + " B";
    header += std::string(";⍙M;⍙E") + (result ? "" : ";⍙Z");
    emit(header, group_doc(group, path, is_method, what), call(a, b, result));
}

void Generator::emit_class(const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto path = class_path(owner);
    // The class description goes with the constructor (or the first function of the class).
    Doc about;
    about.line(summary(record.documentation, "C++ class `" + owner + "`."));
    describe(about, record.documentation);
    if (!plan.bases.empty()) {
        std::string bases;
        for (const auto& base : plan.bases) bases += (bases.empty() ? "" : ", ") + std::string("`") + class_path(base) + "`";
        about.blank();
        about.line("Derives from " + bases + ": their functions accept " + plan.name + " handles.");
    }
    if (record.abstract) {
        about.blank();
        about.line(std::string("Abstract in C++: ") + (plan.director ? "implement it in APL with `" + path + "∆subclass`." : "C++ returns objects of derived classes."));
    }
    std::vector<std::string> members;
    for (const auto& field : plan.fields) members.push_back("`" + path + "∆" + field.name + "` (field)");
    for (const auto& group : plan.methods) members.push_back("`" + path + "∆" + group.name + "`" + (group.is_static ? " (static)" : ""));
    if (!members.empty()) {
        about.blank();
        std::string list;
        for (const auto& member : members) list += (list.empty() ? "" : ", ") + member;
        about.line("**Members:** " + list + ".");
    }
    about.blank();
    about.line("Handles are `('" + plan.name + "' id)`; release them with `" + prefix_ + "∆Delete obj`.");

    // Constructor.
    if (!plan.constructor.empty() || plan.constructors.empty()) {
        Doc doc = about;
        const Function* function = plan.constructors.empty() ? nullptr : plan.constructors.front().function;
        for (const auto& overload : plan.constructors) if (!overload.function->documentation.empty()) { function = overload.function; break; }
        doc.blank();
        doc.line("**Usage:**");
        doc.line("```apl");
        for (const auto& line : constructor_usages(plan, false)) doc.line(line);
        if (plan.constructors.empty()) doc.line("obj←" + path + "∆new ⍬");
        doc.line("```");
        if (mode_of(plan.constructors) == vector) doc.line("The right argument is the vector of arguments (one argument may be given alone; ⍬ for none).");
        if (function) {
            if (!function->documentation.brief.empty()) { doc.blank(); doc.line(one_line(function->documentation.brief)); }
            const Overload* documented = &plan.constructors.front();
            for (const auto& overload : plan.constructors) if (overload.function == function) documented = &overload;
            sections(doc, plan.constructors, *documented, true, plan.name + " handle", "");
            errors(doc, function->documentation, false);
            doc.blank();
            for (const auto& overload : plan.constructors) doc.line("- C++: `" + cpp_signature(*overload.function) + "`");
        } else {
            doc.blank();
            doc.line("It has no public constructor: C++ functions return its objects.");
        }
        const auto mode = mode_of(plan.constructors);
        if (plan.constructor.empty()) {
            emit("Z←" + path + "∆new B", doc, " ('" + prefix_ + ": binding: " + (record.abstract ? abstract_message(plan) : path + " has no public constructor") +
                 "') ⎕ES 5 4\n");
        } else {
            emit("Z←" + path + "∆new B;⍙M;⍙E", doc,
                 call("¯2 " + std::to_string(mode) + " " + std::to_string(plan.constructor_index), mode == none ? "⍬" : "B", true));
        }
    }
    if (plan.director && !plan.constructor.empty()) {
        Doc doc;
        doc.line("An APL implementation of `" + path + "`: C++ calls the APL functions Impl∆method for its virtual methods.");
        doc.blank();
        doc.line("**Usage:**");
        doc.line("```apl");
        for (const auto& line : constructor_usages(plan, true)) doc.line(line);
        doc.line("```");
        doc.line("`Impl` is a name prefix: for each overridable method below, a defined function `Impl∆method` overrides it,");
        doc.line("called as `Z←obj Impl∆method args` (`Z←Impl∆method obj` without arguments, the same conventions as the");
        doc.line("generated functions). Methods without an APL function run the C++ implementation.");
        doc.blank();
        doc.line("**Overridable:**");
        for (const auto& item : overridable(plan)) doc.line("- `" + item.name + "`" + (item.pure ? " (abstract: required)" : "") +
                                                             " — C++ `" + cpp_signature(*item.method) + "`");
        doc.blank();
        doc.line("The object lives until `" + prefix_ + "∆Delete obj`, or while C++ owns it.");
        const auto mode = mode_of(plan.constructors);
        emit("Z←P " + path + "∆subclass B;⍙M;⍙E", doc,
             call("¯2 " + std::to_string(mode) + " " + std::to_string(plan.constructor_index) + " P", mode == none ? "⍬" : "B", true));
    }
    // Fields.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        Doc get;
        get.line(summary(f.documentation, "Field `" + f.name + "` of " + plan.name + "."));
        describe(get, f.documentation);
        get.blank();
        get.line("**Usage:** `Z←" + path + "∆" + field.name + " obj`" + (writable ? "; set it with `obj " + path + "∆" + field.name + "∆set value`" : " (read-only)"));
        get.blank();
        get.line("**Result:** " + apl_type(f.type));
        get.blank();
        get.line("- C++: `" + strip_cv(f.type.spelling) + " " + owner + "::" + f.name + "`");
        emit("Z←" + path + "∆" + field.name + " H;⍙M;⍙E", get, call(std::to_string(field.getter_index) + " 2 H", "⍬", true));
        if (writable) {
            Doc set;
            set.line("Sets field `" + f.name + "` of " + plan.name + ".");
            set.blank();
            set.line("**Usage:** `obj " + path + "∆" + field.name + "∆set value`");
            set.blank();
            set.line("**Arguments:**");
            set.line("- `value` " + apl_type(f.type));
            set.blank();
            set.line("- C++: `" + strip_cv(f.type.spelling) + " " + owner + "::" + f.name + "`");
            emit("H " + path + "∆" + field.name + "∆set B;⍙M;⍙E;⍙Z", set, call(std::to_string(field.setter_index) + " 0 H", "B", false));
        }
    }
    for (const auto& constant : plan.constants) {
        Doc doc;
        doc.line(summary(*constant.documentation, "Class constant `" + normalize(constant.expression) + "`."));
        doc.blank();
        doc.line("**Usage:** `Z←" + path + "∆" + constant.name + "`");
        doc.blank();
        doc.line("**Result:** " + apl_type(*constant.type));
        doc.blank();
        doc.line("- C++: `" + normalize(constant.expression) + "`");
        emit("Z←" + path + "∆" + constant.name + ";⍙M;⍙E", doc, call(std::to_string(constant.index) + " 2", "⍬", true));
    }
    for (const auto& group : plan.methods)
        emit_group(group, group_path(group, !group.is_static), !group.is_static, (group.is_static ? "Static method " : "Method ") + group.name + " of " + plan.name);
    // Operators: one function per name; when two groups share it (the class first or second), the
    // left argument's class picks.
    std::map<std::string, std::vector<const Group*>> operators;
    std::vector<std::string> order;
    for (const auto& group : plan.operators) {
        if (!operators.count(group.name)) order.push_back(group.name);
        operators[group.name].push_back(&group);
    }
    for (const auto& name : order) {
        const auto& groups = operators[name];
        const auto& group = *groups.front();
        const auto opath = group_path(group, true);
        if (groups.size() == 1) {
            emit_group(group, opath, true, "Operator " + name + " of " + plan.name);
            continue;
        }
        const Group* first = nullptr;
        const Group* second = nullptr;
        for (const auto* g : groups) (g->operand == 0 ? first : second) = g;
        if (!first || !second) { emit_group(group, opath, true, "Operator " + name + " of " + plan.name); continue; }
        Doc doc = group_doc(*first, opath, true, "Operator " + name + " of " + plan.name);
        doc.blank();
        doc.line("Also with a " + plan.name + " on the right:");
        for (const auto& overload : second->overloads) doc.line("- C++: `" + cpp_signature(*overload.function) + "`");
        const std::size_t klass = static_cast<std::size_t>(std::find(class_order_.begin(), class_order_.end(), owner) - class_order_.begin());
        emit("Z←H " + opath + " B;⍙M;⍙E;⍙I", doc,
             " ⍙I←(" + std::to_string(first->index) + " " + std::to_string(second->index) + ")[1+0=3⊃(¯4 " + std::to_string(klass) + ") " + native_ + " H]\n" +
             call("⍙I 0 H", "B", true));
    }
}

void Generator::emit_enum(const std::string& qualified, const std::string& name) {
    const auto& enumeration = *enums_.at(qualified);
    const auto path = prefix_ + "∆" + name;
    std::vector<std::pair<std::string, std::string>> entries;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
        entries.emplace_back(apl_piece(renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i])),
                             apl_number(enumeration.enumerator_values[i]));
    Doc table;
    table.line(summary(enumeration.documentation, "C++ enumeration `" + qualified + "`."));
    describe(table, enumeration.documentation);
    table.blank();
    table.line("**Usage:** `Z←" + path + "` (a 2-column matrix: names, values). Each enumerator is a niladic function:");
    for (const auto& [label, value] : entries) table.line("- `" + path + "∆" + label + "` = " + value);
    table.blank();
    table.line("Enumerators are numbers; functions also accept their names as character vectors.");
    table.blank();
    table.line(std::string("- C++: `enum ") + (enumeration.scoped ? "class " : "") + qualified + "`");
    // A strand of (,'name') value pairs, reshaped (,'x' keeps a 1-character name a vector).
    std::string body = " Z←" + std::to_string(entries.size()) + " 2⍴";
    for (const auto& [label, value] : entries) body += "(," + apl_string(label) + ") " + value + " ";
    if (entries.empty()) body = " Z←0 2⍴0 ";
    body.back() = '\n';
    if (claim(path)) emit("Z←" + path, table, body);
    else reject(qualified, "APL name " + path + " is taken");
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto epath = path + "∆" + entries[i].first;
        if (!claim(epath)) { reject(qualified + "::" + enumeration.enumerators[i], "APL name " + epath + " is taken"); continue; }
        Doc doc;
        doc.line("Enumerator `" + qualified + "::" + enumeration.enumerators[i] + "` = " + entries[i].second + ".");
        doc.blank();
        doc.line("**Usage:** `Z←" + epath + "`");
        emit("Z←" + epath, doc, " Z←" + entries[i].second + "\n");
    }
}

void Generator::emit_runtime() {
    const auto p = prefix_;
    const auto library = (options_.library_dir.empty() ? std::string(".") : options_.library_dir);
    const auto file = (library.back() == '/' ? library : library + "/") + "lib" + name_ + "_gnu_apl.so";
    std::string variable = name_ + "_APL_LIBRARY";
    for (auto& c : variable) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    script_ << "#!/usr/bin/env apl --script\n"
            << "⍝ " << header_comment("GNU APL bindings for " + name_ + " (full API)") << " Do not edit.\n"
            << "⍝\n"
            << "⍝ Load with: apl --script -f " << name_ << ".apl (or )COPY it). The native library is\n"
            << "⍝ " << file << ",\n"
            << "⍝ or the file named by the environment variable " << variable << ".\n"
            << "⍝\n"
            << "⍝ Every name starts with " << p << "∆. A function's right argument is its one argument when\n"
            << "⍝ every C++ overload takes exactly one; otherwise the vector of its arguments (a scalar, a\n"
            << "⍝ character vector, a handle or a function reference alone is one argument; ⍬ is none).\n"
            << "⍝ Methods take the object on the left (on the right when they take no arguments).\n"
            << "⍝ Objects are handles ('Class' id): release them with " << p << "∆Delete (GNU APL has no\n"
            << "⍝ finalizers). Callbacks are function references (" << p << "∆fn 'NAME'). C++ exceptions\n"
            << "⍝ signal APL errors ('" << p << ": kind: message'); " << p << "∆last_error returns the latest.\n\n"
            << "∇" << p << "⍙load;⍙L;⍙V;⍙Z\n"
            << " →(3=⎕NC '" << native_ << "')/0\n"
            << " ⍙L←" << apl_string(file) << "\n"
            << " ⍙V←⎕ENV " << apl_string(variable) << "\n"
            << " ⍙V←(⍙V[;1]∊⊂" << apl_string(variable) << ")⌿⍙V\n"
            << " →(0=↑⍴⍙V)/⍙FIX\n"
            << " ⍙L←⊃⍙V[1;2]\n"
            << "⍙FIX:⍙Z←⍙L ⎕FX '" << native_ << "'\n"
            << " →(3=⎕NC '" << native_ << "')/0\n"
            << " ('" << p << ": cannot load the native library ',⍙L) ⎕ES 5 4\n"
            << "∇\n" << p << "⍙load\n";
    Doc del;
    del.line("Releases objects: a handle, or a vector of handles.");
    del.blank();
    del.line("**Usage:** `" + p + "∆Delete obj` or `" + p + "∆Delete obj1 obj2`");
    del.blank();
    del.line("GNU APL has no finalizers: every handle a function returns stays alive until it is deleted. An object");
    del.line("borrowed from another (a reference C++ returns into it) keeps that object alive until it is deleted too.");
    del.line("Deleted handles signal `" + p + ": binding: native object is closed` when used.");
    emit(p + "∆Delete B;⍙M;⍙E;⍙Z", del, call("¯1", "B", false));
    Doc fn;
    fn.line("A function reference for a C++ `std::function` parameter.");
    fn.blank();
    fn.line("**Usage:** `Z←" + p + "∆fn 'NAME'` (Z is `('⍙fn' 'NAME')`)");
    fn.blank();
    fn.line("C++ calls the defined function NAME with the conventions of the generated functions: `NAME arg` with one");
    fn.line("argument, `NAME (a b ...)` with several, `NAME ⍬` with none. Its result converts to the C++ return type.");
    fn.line("APL errors in NAME become C++ exceptions, and signal again when control returns to APL.");
    emit("Z←" + p + "∆fn B", fn, " Z←'⍙fn' (,B)\n");
    Doc call_doc;
    call_doc.line("Calls a C++ `std::function` a function returned (a `('NativeFunction' id)` handle).");
    call_doc.blank();
    call_doc.line("**Usage:** `Z←f " + p + "∆call args` (the vector of arguments; ⍬ for none)");
    emit("Z←H " + p + "∆call B;⍙M;⍙E", call_doc, call("0 1 H", "B", true));
    Doc last;
    last.line("The kind and message of the latest error: `(kind message)`.");
    last.blank();
    last.line("**Usage:** `Z←" + p + "∆last_error`");
    last.blank();
    last.line("Kinds: invalid_argument, out_of_range, domain_error, length_error, arithmetic_error, allocation,");
    last.line("binding (a wrong argument or a closed object), callback (an APL error in a callback) and native.");
    emit("Z←" + p + "∆last_error;⍙M;⍙E", last, call("¯3", "⍬", true));
    Doc live;
    live.line("The number of live object handles (not yet deleted).");
    live.blank();
    live.line("**Usage:** `Z←" + p + "∆live`");
    emit("Z←" + p + "∆live;⍙M;⍙E", live, call("¯5", "⍬", true));
}

GeneratedAplFullSource Generator::run() {
    if (!identifier(name_) || name_[0] == '_') throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    prefix_ = options_.prefix.empty() ? name_ : options_.prefix;
    if (!identifier(prefix_) || prefix_[0] == '_') throw BindingError("APL prefix must be an ASCII identifier starting with a letter: " + prefix_);
    native_ = prefix_ + "⍙native";
    prepare();
    for (const char* runtime : {"Delete", "fn", "call", "last_error", "live"}) taken_.insert(prefix_ + "∆" + runtime);

    // Classes, then enums (nested ones are Outer_Name).
    {
        std::set<std::string> collided, names;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!names.insert(plan.name).second) {
                if (!flat_) throw BindingError("APL name collision: class " + plan.name);
                reject(owner, "class name collides with another class");
                collided.insert(owner);
            }
        }
        drop_classes(collided);
    }
    std::set<std::string> enum_names;
    for (const auto& owner : class_order_) enum_names.insert(classes_.at(owner).name);
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, type_name(nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        if (!enum_names.insert(name).second) {
            if (!flat_) throw BindingError("APL name collision: " + name);
            reject(qualified, "enum name collides with another name");
            continue;
        }
        enum_names_[qualified] = name;
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, apl_piece(flat(variable.namespaces) ? variable.name : exported(qualified)));
    });

    // APL names: one flat namespace. Runtime names, classes' members, functions, constants.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto path = class_path(owner);
        claim(path + "∆new");
        if (plan.director) claim(path + "∆subclass");
        for (auto& group : plan.methods) {
            auto name = path + "∆" + group.name;
            while (!claim(name)) { group.name += "_"; name = path + "∆" + group.name; }
            paths_[&group] = name;
        }
        for (auto& field : plan.fields) {
            auto name = path + "∆" + field.name;
            while (!claim(name)) { field.name += "_"; name = path + "∆" + field.name; }
            claim(name + "∆set");
        }
        for (auto& constant : plan.constants) {
            auto name = path + "∆" + constant.name;
            while (!claim(name)) { constant.name += "_"; name = path + "∆" + constant.name; }
        }
        for (auto& group : plan.operators) {
            if (paths_.count(&group)) continue;
            auto name = path + "∆" + group.name;
            // Groups sharing an operator name share its function.
            bool shared = false;
            for (const auto& other : plan.operators) if (&other != &group && other.name == group.name && paths_.count(&other)) { name = paths_[&other]; shared = true; }
            if (!shared && !claim(name)) { name = path + "∆op∆" + group.name; claim(name); }
            paths_[&group] = name;
        }
    }
    for (auto& group : functions) {
        auto name = module_path(group.module) + "∆" + group.name;
        while (!claim(name)) { group.name += "_"; name = module_path(group.module) + "∆" + group.name; }
    }
    std::vector<ConstantPlan> kept;
    for (auto& constant : constants) {
        if (!claim(prefix_ + "∆" + constant.name)) { reject(normalize(constant.expression), "constant name collides with another APL name"); continue; }
        kept.push_back(constant);
    }
    constants = kept;
    check_diagnostics();
    emit_cpp(result_.source, result_.parts, functions, constants, "GNU APL bindings " + prefix_ + " (" + name_ + ", full API).");

    // ------------------------------------------------------------------ the native function glue
    std::vector<unsigned long long> masks(symbols_.size() + 1, 0);
    std::vector<int> voids(symbols_.size() + 1, 0);
    const auto plan_group = [&](const Group& group, bool is_method) {
        unsigned long long mask = 0;
        for (const auto& overload : group.overloads) {
            const std::size_t skip = overload.receives_self ? 1 : 0;
            const std::size_t base = (is_method && !overload.extension) || overload.receives_self ? 1 : 0;
            for (std::size_t i = skip; i < overload.function->parameters.size(); ++i)
                if (writeback(overload.function->parameters[i]) && base + i - skip < 64) mask |= 1ULL << (base + i - skip);
        }
        masks.at(group.index) = mask;
        voids.at(group.index) = returns_void(group) ? 1 : 0;
    };
    for (const auto& group : functions) plan_group(group, false);
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& group : plan.methods) plan_group(group, !group.is_static);
        for (const auto& group : plan.operators) plan_group(group, true);
        for (const auto& field : plan.fields) voids.at(field.setter_index) = 1;
    }
    // director_methods: per class and overridable method, the function running the C++ implementation.
    std::vector<std::size_t> director(class_order_.size() * std::max<std::size_t>(generic_order_.size(), 1), 0);
    for (std::size_t k = 0; k < class_order_.size(); ++k) {
        const auto& plan = classes_.at(class_order_[k]);
        if (!plan.director) continue;
        std::map<std::string, bool> pure;
        for (const auto& item : overridable(plan)) pure[item.name] = item.pure;
        for (std::size_t g = 0; g < generic_order_.size(); ++g) {
            const auto& name = generic_order_[g];
            if (!pure.count(name) || pure[name]) continue;
            std::function<std::size_t(const std::string&)> find = [&](const std::string& cls) -> std::size_t {
                for (const auto& group : classes_.at(cls).methods) if (!group.is_static && group.name == name) return group.index;
                for (const auto& base : classes_.at(cls).bases) if (const auto found = find(base)) return found;
                return 0;
            };
            director[k * generic_order_.size() + g] = find(class_order_[k]);
        }
    }
    std::ostringstream glue;
    glue << "\n// ---------------------------------------------------------------- GNU APL\n"
         << "#include <unordered_map>\n"
         << "namespace ROSETTA_LISP_MODULE { namespace rosetta_gnu {\n"
         << "inline const char* const prefix = " << quote(prefix_) << ";\n"
         << "inline const char* const classes[] = {";
    for (const auto& owner : class_order_) glue << quote(classes_.at(owner).name) << ", ";
    glue << "nullptr};\ninline constexpr std::size_t class_count = " << class_order_.size() << ";\n"
         << "inline const char* const generics[] = {";
    for (const auto& name : generic_order_) glue << quote(name) << ", ";
    glue << "nullptr};\ninline constexpr std::size_t generic_count = " << generic_order_.size() << ";\n"
         << "inline constexpr std::size_t function_count = sizeof(ROSETTA_LISP_MODULE::table) / sizeof(ROSETTA_LISP_MODULE::table[0]);\n"
         << "inline const unsigned long long out_masks[] = {";
    for (std::size_t i = 0; i < masks.size(); ++i) glue << (i ? ", " : "") << masks[i] << "ULL";
    glue << "};\ninline const unsigned char void_results[] = {";
    for (std::size_t i = 0; i < voids.size(); ++i) glue << (i ? ", " : "") << voids[i];
    glue << "};\ninline const std::size_t director_methods[] = {";
    for (std::size_t i = 0; i < director.size(); ++i) glue << (i ? ", " : "") << director[i];
    glue << "};\n"
         << "inline int init(int version, ROSETTA_LISP_MODULE::rosetta_lisp::InvokeFn invoke, ROSETTA_LISP_MODULE::rosetta_lisp::RetainFn retain) {\n"
         << "    return rosetta_" << name_ << "_init(version, invoke, retain);\n}\n"
         << "using ROSETTA_LISP_MODULE::table;\n"
         << "} }\n"
         << detail::apl_full_runtime;
    result_.source += glue.str();

    // ------------------------------------------------------------------ the script
    emit_runtime();
    for (const auto& [qualified, name] : enum_names_) emit_enum(qualified, name);
    for (const auto& owner : class_order_) emit_class(owner);
    for (const auto& group : functions) {
        const auto path = module_path(group.module) + "∆" + group.name;
        emit_group(group, path, false, "Function " + group.name);
    }
    for (const auto& constant : constants) {
        Doc doc;
        doc.line(summary(*constant.documentation, "Constant `" + normalize(constant.expression) + "`."));
        describe(doc, *constant.documentation);
        doc.blank();
        doc.line("**Usage:** `Z←" + prefix_ + "∆" + constant.name + "`");
        doc.blank();
        doc.line("**Result:** " + apl_type(*constant.type));
        doc.blank();
        doc.line("- C++: `" + normalize(constant.expression) + "`");
        emit("Z←" + prefix_ + "∆" + constant.name + ";⍙M;⍙E", doc, call(std::to_string(constant.index) + " 2", "⍬", true));
    }
    result_.script = script_.str();
    result_.diagnostics = diagnostics_;
    return std::move(result_);
}
} // namespace

GeneratedAplFullSource generate_apl_full(const Module& module, const std::string& name, bool skip_unsupported,
                                         const BindingPolicy& policy, const AplFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("APL source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
