// Prolog backend (--language prolog): one SWI-Prolog 9 module (.pl) over the shared C ABI library
// of the full C ABI backends (cabi_full.hpp, lisp_full_runtime.hpp). The library's first source
// also carries the SWI-Prolog foreign glue (prolog_full_runtime.hpp), which converts terms to the
// library's value encoding and back; the module loads it with load_foreign_library/2.
//
// Every C++ function is a family of deterministic predicates, one per argument count, with the
// result last (add(1, R), add(1, 2, R)); C++ resolves overloads at run time and applies its
// default arguments to those left out. Methods take the object first (circle_area(C, A)); classes
// prefix their predicates (circle_new/2, vec2_set_x/2 for a field). Objects are blobs released by
// atom garbage collection (or close_object/1). Values: integers, floats, strings, the atoms true,
// false and null, lists, Key-Value pairs for maps and pairs, atoms for enumerators. A non-const
// reference parameter is an input and an output argument (scale(Values0, F, Values)). Extended
// objects (CLASS_extend(Module, Data, ..., Object)) implement virtual methods by Module's
// override/1 clauses. Doxygen becomes PlDoc.
#include "rosetta/prolog.hpp"
#include "cabi_full.hpp"
#include "prolog_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// snake_case words of a C++ name (camelCase, PascalCase, URLPath and snake_case alike): always a
// plain Prolog atom (a lowercase letter first).
std::string snake_words(const std::string& name) {
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!is_alnum(c)) {
            if (!result.empty() && result.back() != '_') result += '_';
            continue;
        }
        if (is_upper(c) && !result.empty() && result.back() != '_') {
            const char previous = name[i - 1];
            const bool next_lower = i + 1 < name.size() && is_lower(name[i + 1]);
            if (is_lower(previous) || is_digit(previous) || (is_upper(previous) && next_lower)) result += '_';
        }
        result += lower(c);
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    while (!result.empty() && result.front() == '_') result.erase(result.begin());
    if (result.empty()) return "x";
    if (is_digit(result[0])) result = "x" + result;
    return result;
}
// A Prolog variable name (PascalCase of the words).
std::string variable_name(const std::string& name) {
    const auto words = snake_words(name);
    std::string result;
    bool start = true;
    for (const char c : words) {
        if (c == '_') { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    return result;
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
bool starts(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }
std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) result += (i ? separator : "") + items[i];
    return result;
}
// A Prolog string literal ("...").
std::string prolog_string(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') { result += "\\n"; continue; }
        result += c;
    }
    return result + "\"";
}

const std::set<std::string>& system_predicates() {
    static const std::set<std::string> names = [] {
        std::set<std::string> result;
        std::istringstream in(detail::prolog_system_predicates);
        for (std::string item; in >> item;) result.insert(item);
        return result;
    }();
    return names;
}

// Predicate names in the module: name/arity keys.
struct Preds {
    std::set<std::string> taken;
    static std::string key(const std::string& name, std::size_t arity) { return name + "/" + std::to_string(arity); }
    bool free(const std::string& name, const std::vector<std::size_t>& arities) const {
        for (const auto arity : arities)
            if (taken.count(key(name, arity)) || system_predicates().count(key(name, arity))) return false;
        return true;
    }
    void reserve(const std::string& name, std::size_t arity) { taken.insert(key(name, arity)); }
    // The wanted name, else with a number (count_2), free at every arity.
    std::string claim(const std::string& wanted, const std::vector<std::size_t>& arities) {
        auto candidate = wanted;
        for (int i = 2; !free(candidate, arities); ++i) candidate = wanted + "_" + std::to_string(i);
        for (const auto arity : arities) reserve(candidate, arity);
        return candidate;
    }
};

// ---------------------------------------------------------------- PlDoc
// Inline Markdown (`code` stays, **bold** is *bold*).
std::string doc_inline(const std::string& text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "**") == 0) {
            const auto end = text.find("**", i + 2);
            if (end != std::string::npos && end > i + 2) { out += "*" + text.substr(i + 2, end - i - 2) + "*"; i = end + 2; continue; }
        }
        out += text[i];
        ++i;
    }
    return out;
}
// The paragraphs of a PlDoc comment, as lines (without the "%   " prefix).
struct Doc {
    std::vector<std::vector<std::string>> blocks;
    void add(std::vector<std::string> lines) { if (!lines.empty()) blocks.push_back(std::move(lines)); }
    void line(const std::string& text) { if (!text.empty()) blocks.push_back({text}); }
    void paragraph(const std::string& text) {
        std::istringstream in(text);
        std::vector<std::string> lines, items;
        const auto flush_lines = [&] { add(lines); lines.clear(); };
        const auto flush_items = [&] {
            if (items.empty()) return;
            std::vector<std::string> block;
            for (const auto& item : items) {
                std::istringstream parts(item);
                bool first = true;
                for (std::string part; std::getline(parts, part);) { block.push_back((first ? "  - " : "    ") + part); first = false; }
            }
            add(block);
            items.clear();
        };
        for (std::string line; std::getline(in, line);) {
            const auto stripped = trim(line);
            if (stripped.empty()) { flush_lines(); flush_items(); continue; }
            if (starts(stripped, "* ") || starts(stripped, "- ")) { flush_lines(); items.push_back(doc_inline(trim(stripped.substr(2)))); continue; }
            if (!items.empty()) { items.back() += "\n" + doc_inline(stripped); continue; }
            lines.push_back(doc_inline(stripped));
        }
        flush_lines();
        flush_items();
    }
    void heading(const std::string& text) { line("*" + doc_inline(one_line(trim(text))) + "*"); }
    // A code block (PlDoc's == fences).
    void code(const std::string& text) {
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        if (body.empty()) return;
        std::vector<std::string> block = {"=="};
        for (const auto& line : body) block.push_back(line);
        block.push_back("==");
        add(block);
    }
    std::string render() const {
        std::string out;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (i) out += "%\n";
            for (const auto& line : blocks[i]) out += line.empty() ? "%\n" : "%   " + line + "\n";
        }
        return out;
    }
};

std::string exception_kind(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "invalid_argument";
    if (bare == "std::out_of_range") return "out_of_range";
    if (bare == "std::domain_error") return "domain_error";
    if (bare == "std::length_error") return "length_error";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "arithmetic_error";
    return "native";
}

// C++ operators as predicate names ("" = not bound).
std::string prolog_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "call";
    static const std::map<std::string, std::pair<const char*, const char*>> table = {
        {"operator+", {"pos", "add"}}, {"operator-", {"neg", "subtract"}}, {"operator*", {nullptr, "multiply"}},
        {"operator/", {nullptr, "divide"}}, {"operator%", {nullptr, "modulo"}}, {"operator==", {nullptr, "equals"}},
        {"operator!=", {nullptr, "not_equals"}}, {"operator<", {nullptr, "less"}}, {"operator<=", {nullptr, "less_equal"}},
        {"operator>", {nullptr, "greater"}}, {"operator>=", {nullptr, "greater_equal"}}, {"operator&", {nullptr, "bit_and"}},
        {"operator|", {nullptr, "bit_or"}}, {"operator^", {nullptr, "bit_xor"}}, {"operator~", {"complement", nullptr}},
        {"operator!", {"not", nullptr}}, {"operator<<", {nullptr, "shift_left"}}, {"operator>>", {nullptr, "shift_right"}},
        {"operator[]", {nullptr, "at"}}, {"operator+=", {nullptr, "add_assign"}}, {"operator-=", {nullptr, "subtract_assign"}},
        {"operator*=", {nullptr, "multiply_assign"}}, {"operator/=", {nullptr, "divide_assign"}}};
    const auto found = table.find(name);
    if (found == table.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// One parameter position of a predicate family (every overload's parameter there).
struct Param {
    std::vector<std::string> types;
    std::string name;          // Prolog variable
    std::string description;
    bool byref = false;        // an input and an output argument (Name0, Name)
    bool function = false;     // a std::function: a closure (meta argument)
    int closure_arity = -1;    // its extra arguments (meta_predicate), -1 unknown
    bool closure_result = true;
};
struct Sig {
    std::vector<Param> params;
    std::size_t required = 0;
    std::vector<std::string> result_types;
    bool returns = false;
};
// One predicate family: a group, a class's constructors, ...
struct Family {
    std::string name;
    Sig sig;
    std::string receiver;      // the object's variable ("" = none)
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const PrologOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedPrologSource run();

private:
    PrologOptions options_;
    std::string module_name_, library_;
    Preds preds_;
    std::set<std::string> prefixes_;                       // class predicate prefixes
    std::map<std::string, std::vector<std::string>> enum_atoms_;
    std::map<const Group*, Family> families_;
    std::map<std::string, Family> constructors_, extenders_;   // class -> family
    std::map<std::string, std::pair<std::string, std::string>> field_preds_;   // class::field -> getter, setter ("" = read-only)
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::string> submodule_prefixes_;
    std::map<std::string, std::string> links_;             // C++ name -> name/arity
    std::vector<std::string> exports_;

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Prolog"; }
    std::string target_name(const std::string& cpp) const override { return snake_words(cpp); }
    std::string method_name(const std::string& cpp) const override { return snake_words(cpp); }
    std::string type_name(const std::string& cpp) const override { return cpp; }
    std::string rename_target(const std::string& name, const std::string&) const override { return snake_words(name); }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return prolog_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken = {"Result", "Object"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "Arg" + std::to_string(i - first + 1) : variable_name(cpp);
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool is_method) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + overload_signature(overload, group.name, is_method && !group.is_static) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, plan.name + "_new", false) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: create an extended object (" + plan.name + "_extend) whose module's override/1 implements its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: create an extended object (" + plan.name + "_extend)";
    }
    std::string symbol_prefix() const override { return "rosetta_prolog_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_PROLOG"; }
    std::string module_prefix() const override { return "rosetta_prolog_module_"; }
    std::string enum_label(const std::string& qualified, std::size_t index) const override { return enum_atoms_.at(qualified).at(index); }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    const Type* function_type(const Type& type) const {
        const auto& target = referent(type);
        if (target.kind == TypeKind::template_instance && normalize(target.declaration) == "std::function" && !target.arguments.empty() &&
            target.arguments[0].kind == TypeKind::function)
            return &target.arguments[0];
        return nullptr;
    }
    std::string prolog_type(const Type& type) const;
    std::string prolog_spelled(std::string cpp) const;
    std::string prolog_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name, bool method) const;
    Sig signature_of(const std::vector<Overload>& overloads) const;

    // ------------------------------------------------------------ naming and emission
    std::vector<std::size_t> arities(const Sig& sig, std::size_t fixed) const;
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    std::string link(const std::string& cpp) const;
    Doc doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Doc& doc, const Documentation& documentation) const;
    std::string mode_lines(const Family& family, const std::vector<std::string>& leading, const std::string& result_name) const;
    Doc family_doc(const std::vector<Overload>& overloads, const Sig& sig, const std::string& what) const;
    void emit_family(std::ostream& out, const Family& family, const std::vector<Overload>& overloads, const std::string& what,
                     const std::string& call, bool virtual_method, const std::string& method);
    void emit_constructor(std::ostream& out, const ClassPlan& plan, const Family& family, bool extend);
    void emit_class(std::ostream& out, const std::string& owner);
    void emit_enum(std::ostream& out, const std::string& qualified);
    void export_predicate(const std::string& name, std::size_t arity) { exports_.push_back(name + "/" + std::to_string(arity)); }
    std::string file_doc() const;
};

// ---------------------------------------------------------------- types
// A type as PlDoc describes it (a Prolog term).
std::string Generator::prolog_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "optional(string)";
        if (is_void(pointee)) return "any";
        return "optional(" + prolog_type(pointee) + ")";
    }
    if (is_void(target)) return "null";
    if (!conversion(target).empty()) return prolog_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "boolean";
        if (bare == "char") return "char";
        if (bare == "float" || bare == "double" || bare == "long double") return "float";
        if (bare == "void") return "null";
        return "integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found == enum_names_.end() ? "integer" : found->second;
    }
    if (bound(target)) return classes_.at(target.declaration).name;
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "null";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration_name = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? prolog_type(target.arguments[i]) : std::string("any"); };
        if (declaration_name == "std::basic_string" || declaration_name == "std::basic_string_view") return "string";
        if (declaration_name == "std::vector" || declaration_name == "std::deque" || declaration_name == "std::list" || declaration_name == "std::array" ||
            declaration_name == "std::valarray" || declaration_name == "std::span" || declaration_name == "std::set" ||
            declaration_name == "std::unordered_set" || declaration_name == "std::multiset" || declaration_name == "std::unordered_multiset")
            return "list(" + argument(0) + ")";
        if (declaration_name == "std::map" || declaration_name == "std::unordered_map" || declaration_name == "std::multimap" ||
            declaration_name == "std::unordered_multimap")
            return "list(pair(" + argument(0) + ", " + argument(1) + "))";
        if (declaration_name == "std::optional" || declaration_name == "std::shared_ptr" || declaration_name == "std::unique_ptr")
            return target.arguments.empty() ? std::string("any") : "optional(" + argument(0) + ")";
        if (declaration_name == "std::complex") return "complex";
        if (declaration_name == "std::pair") return "pair(" + argument(0) + ", " + argument(1) + ")";
        if (declaration_name == "std::tuple") {
            std::vector<std::string> items;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) items.push_back(argument(i));
            return items.size() == 2 ? "pair(" + join(items, ", ") + ")" : "list";
        }
        if (function_type(target)) return "callable";
        if (declaration_name == "std::chrono::duration") return "float";
    }
    return "any";
}

// A type a policy conversion spells in C++.
std::string Generator::prolog_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    if (cpp == "bool") return "boolean";
    if (cpp == "float" || cpp == "double" || cpp == "long double") return "float";
    if (cpp == "char" || cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return "string";
    static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int", "long",
        "unsigned long", "long long", "unsigned long long", "std::size_t", "size_t", "std::int64_t", "std::uint64_t", "std::int32_t",
        "std::uint32_t", "std::int16_t", "std::uint16_t", "std::int8_t", "std::uint8_t"};
    if (integers.count(cpp)) return "integer";
    for (const auto& [qualified, name] : enum_names_) if (normalize(qualified) == cpp) return name;
    for (const auto& [qualified, plan] : classes_) if (normalize(qualified) == cpp) return plan.name;
    if (starts(cpp, "std::vector<") || starts(cpp, "std::map<")) return "list";
    if (starts(cpp, "std::pair<")) return "pair";
    return "any";
}

std::string Generator::prolog_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "null";
    if (value.kind == "char") return value.value.size() == 1 ? prolog_string(value.value) : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos)
            return prolog_string(text.substr(1, text.size() - 2));
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        if (!enum_atoms_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return enum_atoms_.at(qualified)[i];
    }
    return {};
}

std::string Generator::overload_signature(const Overload& overload, const std::string& name, bool method) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    if (method) parameters.push_back("+Object");
    std::vector<std::string> outputs;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto type = prolog_type(function.parameters[i]);
        auto text = "+" + names[i - first] + (writeback(function.parameters[i]) ? "0" : "") + ":" + type;
        if (i >= function.parameters.size() - function.default_arguments) {
            const auto shown = prolog_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            text += " = " + (shown.empty() ? std::string("C++ default") : shown);
        }
        parameters.push_back(text);
        if (writeback(function.parameters[i])) outputs.push_back("-" + names[i - first] + ":" + type);
    }
    for (const auto& output : outputs) parameters.push_back(output);
    if (function.is_constructor) parameters.push_back("-Object");
    else if (!is_void(function.result)) parameters.push_back("-Result:" + prolog_type(function.result));
    return name + "(" + join(parameters, ", ") + ")";
}

// The parameters of overloads called through one predicate family: position i takes every
// overload's parameter there; families exist from the fewest arguments any overload needs.
Sig Generator::signature_of(const std::vector<Overload>& overloads) const {
    Sig sig;
    std::size_t required = std::string::npos, positions = 0;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto count = function.parameters.size() - first;
        required = std::min(required, count - std::min(count, function.default_arguments));
        positions = std::max(positions, count);
    }
    if (required == std::string::npos) required = 0;
    sig.required = required;
    sig.params.resize(positions);
    std::vector<bool> all_byref(positions, true), all_function(positions, true);
    std::vector<std::size_t> having(positions, 0);
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto names = parameter_names(overload);
        for (std::size_t i = first, p = 0; i < function.parameters.size(); ++i, ++p) {
            auto& param = sig.params[p];
            const auto type = prolog_type(function.parameters[i]);
            if (std::find(param.types.begin(), param.types.end(), type) == param.types.end()) param.types.push_back(type);
            if (param.name.empty()) param.name = names[i - first];
            all_byref[p] = all_byref[p] && writeback(function.parameters[i]) && !function.is_constructor;
            if (const auto* fn = function_type(function.parameters[i])) {
                const int arity = static_cast<int>(fn->arguments.size()) - 1 + (is_void(fn->arguments[0]) ? 0 : 1);
                if (param.closure_arity >= 0 && param.closure_arity != arity) param.closure_arity = -2;
                else if (param.closure_arity != -2) param.closure_arity = arity;
                param.closure_result = !is_void(fn->arguments[0]);
            } else {
                all_function[p] = false;
            }
            ++having[p];
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            if (param.description.empty())
                for (const auto& item : function.documentation.params)
                    if (item.name == cpp && !item.text.empty()) param.description = one_line(item.text);
        }
        if (!function.is_constructor && !is_void(function.result)) {
            sig.returns = true;
            const auto type = prolog_type(function.result);
            if (std::find(sig.result_types.begin(), sig.result_types.end(), type) == sig.result_types.end()) sig.result_types.push_back(type);
        }
    }
    std::set<std::string> taken = {"Result", "Object", "Module", "Data", "Writebacks"};
    for (auto& param : sig.params)
        while (!taken.insert(param.name).second || (!param.name.empty() && taken.count(param.name + "0"))) param.name += "_";
    for (std::size_t p = 0; p < positions; ++p) {
        sig.params[p].byref = all_byref[p] && having[p] == overloads.size();
        sig.params[p].function = all_function[p] && having[p] == overloads.size();
        if (sig.params[p].byref) taken.insert(sig.params[p].name + "0");
    }
    return sig;
}

// The arities of a family: `fixed` leading arguments (the object; Module and Data), then the
// parameters given, their outputs and the result.
std::vector<std::size_t> Generator::arities(const Sig& sig, std::size_t fixed) const {
    std::vector<std::size_t> result;
    for (std::size_t count = sig.required; count <= sig.params.size(); ++count) {
        std::size_t outputs = 0;
        for (std::size_t p = 0; p < count; ++p) outputs += sig.params[p].byref ? 1 : 0;
        result.push_back(fixed + count + outputs + (sig.returns ? 1 : 0));
    }
    return result;
}

// ---------------------------------------------------------------- naming
void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    for (const auto& [name, arity] : std::vector<std::pair<const char*, std::size_t>>{
             {"close_object", 1}, {"object_closed", 1}, {"object_class", 2}, {"object_is_a", 2}, {"object_data", 2}, {"object_set_data", 2}})
        preds_.reserve(name, arity);
    // Enumerations: a predicate of their atoms and values.
    for (auto& [qualified, name] : enum_names_) name = preds_.claim(name, {2});
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        const auto& prefix = cls.name;
        const auto receiver = variable_name(prefix);
        // Constructors: CLASS_new, and CLASS_extend for classes with virtual methods.
        std::vector<Overload> overloads;
        const bool constructible = !cls.record->abstract;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) overloads.push_back(overload);
        if (!overloads.empty()) {
            Family family{"", signature_of(overloads), ""};
            family.sig.returns = true;
            family.sig.result_types = {prefix};
            family.name = preds_.claim(prefix + "_new", arities(family.sig, 0));
            constructors_[owner] = family;
            if (cls.director) {
                family.name = preds_.claim(prefix + "_extend", arities(family.sig, 2));
                extenders_[owner] = family;
            }
        }
        for (auto& group : cls.methods) {
            Family family{"", signature_of(group.overloads), group.is_static ? std::string() : receiver};
            family.name = preds_.claim(prefix + "_" + group.name, arities(family.sig, group.is_static ? 0 : 1));
            families_[&group] = family;
        }
        for (auto& group : cls.operators) {
            const auto& front = group.overloads.front();
            if (front.extension && !front.receives_self) continue;   // a free operator with the object on the right
            Family family{"", signature_of(group.overloads), receiver};
            family.name = preds_.claim(prefix + "_" + group.name, arities(family.sig, 1));
            families_[&group] = family;
        }
        for (const auto& field : cls.fields) {
            const auto& f = *field.field;
            const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
            const auto wanted = snake_words(field.name);
            auto member = wanted;
            for (int i = 2; !preds_.free(prefix + "_" + member, {2}) || (writable && !preds_.free(prefix + "_set_" + member, {2})); ++i)
                member = wanted + "_" + std::to_string(i);
            preds_.reserve(prefix + "_" + member, 2);
            if (writable) preds_.reserve(prefix + "_set_" + member, 2);
            field_preds_[owner + "::" + f.name] = {prefix + "_" + member, writable ? prefix + "_set_" + member : std::string()};
        }
        for (auto& constant : cls.constants) constant_names_[&constant] = preds_.claim(prefix + "_" + snake_words(constant.name), {1});
    }
    for (auto& group : functions) {
        Family family{"", signature_of(group.overloads), ""};
        const auto prefix = group.module.empty() ? std::string() : submodule_prefixes_.at(group.module);
        family.name = preds_.claim(prefix + snake_words(group.name), arities(family.sig, 0));
        families_[&group] = family;
    }
    for (auto& constant : constants) constant_names_[&constant] = preds_.claim(snake_words(constant.name), {1});
    // PlDoc references.
    const auto first_arity = [&](const Family& family, std::size_t fixed) { return family.name + "/" + std::to_string(arities(family.sig, fixed).front()); };
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        for (const auto& group : cls.methods) {
            if (!families_.count(&group)) continue;
            const auto reference = first_arity(families_.at(&group), group.is_static ? 0 : 1);
            links_[owner + "::" + group.overloads.front().function->name] = reference;
            links_.emplace(cls.record->name + "::" + group.overloads.front().function->name, reference);
        }
        if (constructors_.count(owner)) {
            links_[owner] = first_arity(constructors_.at(owner), 0);
            links_.emplace(cls.record->name, links_[owner]);
        }
    }
    for (const auto& group : functions) {
        const auto reference = first_arity(families_.at(&group), 0);
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = reference;
            if (group.module.empty()) links_.emplace(overload.function->name, reference);
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = name + "/2";
        links_.emplace(enums_.at(qualified)->name, name + "/2");
    }
}

// ---------------------------------------------------------------- documentation
std::string Generator::link(const std::string& cpp) const {
    const auto found = links_.find(normalize(cpp));
    return found != links_.end() ? found->second : "`" + cpp + "`";
}
void Generator::add_notes(Doc& doc, const Documentation& documentation) const {
    for (const auto& item : documentation.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        doc.line("*" + label + ":* " + doc_inline(one_line(item.text)));
    }
}
Doc Generator::doc_of(const Documentation& documentation, const std::string& lead) const {
    Doc doc;
    auto details = documentation.details;
    auto brief = documentation.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    doc.paragraph(brief.empty() ? lead : brief);
    if (!brief.empty() && !lead.empty()) doc.paragraph(lead);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") doc.code(block.text);
        else if (block.kind == "heading") doc.heading(block.text);
        else doc.paragraph(block.text);
    }
    return doc;
}

// The mode lines of a family: one per arity.
std::string Generator::mode_lines(const Family& family, const std::vector<std::string>& leading, const std::string& result_name) const {
    std::string out;
    const auto& sig = family.sig;
    for (std::size_t count = sig.required; count <= sig.params.size(); ++count) {
        std::vector<std::string> items = leading, outputs;
        for (std::size_t p = 0; p < count; ++p) {
            const auto& param = sig.params[p];
            const auto type = param.types.size() == 1 ? param.types.front() : "(" + join(param.types, ";") + ")";
            items.push_back((param.function ? ":" : "+") + param.name + (param.byref ? "0" : "") + ":" + type);
            if (param.byref) outputs.push_back("-" + param.name + ":" + type);
        }
        for (const auto& output : outputs) items.push_back(output);
        if (sig.returns) {
            const auto type = sig.result_types.size() == 1 ? sig.result_types.front() : "(" + join(sig.result_types, ";") + ")";
            items.push_back("-" + result_name + ":" + type);
        }
        out += "%!  " + family.name + (items.empty() ? std::string() : "(" + join(items, ", ") + ")") + " is det.\n";
    }
    return out;
}

Doc Generator::family_doc(const std::vector<Overload>& overloads, const Sig& sig, const std::string& what) const {
    const Function* documented = overloads.front().function;
    for (const auto& overload : overloads)
        if (!overload.function->documentation.brief.empty() || !overload.function->documentation.details.empty()) { documented = overload.function; break; }
    const auto& documentation = documented->documentation;
    Doc doc = doc_of(documentation, "Calls " + what + ".");
    if (overloads.size() > 1) {
        std::vector<std::string> items = {"Overloads (C++ picks one by the arguments' types):"};
        for (const auto& overload : overloads)
            items.push_back("  - `" + overload_signature(overload, overload.function->name, false) + "`");
        doc.add(items);
    }
    if (overloads.size() == 1) {
        const auto& function = *overloads.front().function;
        const std::size_t first = overloads.front().receives_self ? 1 : 0;
        const auto names = parameter_names(overloads.front());
        std::vector<std::string> defaults;
        for (std::size_t i = function.parameters.size() - function.default_arguments; i < function.parameters.size(); ++i) {
            if (i < first) continue;
            const auto shown = prolog_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            const auto cpp = i < function.default_values.size() ? function.default_values[i].value : std::string();
            defaults.push_back(names[i - first] + " = " + (shown.empty() ? cpp.empty() ? std::string("C++ default") : "`" + cpp + "`" : shown));
        }
        if (!defaults.empty()) doc.line("Arguments left out take the C++ defaults: " + join(defaults, ", ") + ".");
    }
    std::vector<std::string> tags;
    for (const auto& param : sig.params) {
        std::string text = param.description;
        if (param.byref) text += std::string(text.empty() ? "" : "; ") + "C++ writes it back: " + param.name + " is the value after the call";
        if (param.function)
            text += std::string(text.empty() ? "" : "; ") + "called with the arguments" + (param.closure_result ? " and a result to bind" : "");
        if (!text.empty()) tags.push_back("@arg " + param.name + (param.byref ? "0" : "") + " " + doc_inline(text));
    }
    if (sig.returns && !one_line(documentation.returns).empty()) tags.push_back("@arg Result " + doc_inline(one_line(documentation.returns)));
    std::set<std::string> thrown;
    for (const auto& overload : overloads)
        for (const auto& item : overload.function->documentation.throws) {
            const auto kind = exception_kind(item.name);
            if (!thrown.insert(kind + item.text).second) continue;
            tags.push_back("@throws error(native_error(" + kind + ", Message), _)" + (item.name.empty() ? "" : " (C++ `" + item.name + "`)") +
                           (item.text.empty() ? "" : ": " + doc_inline(one_line(item.text))));
        }
    std::vector<std::string> see;
    for (const auto& item : documentation.see) see.push_back(link(item));
    if (!see.empty()) tags.push_back("@see " + join(see, ", "));
    add_notes(doc, documentation);
    doc.add(tags);
    return doc;
}

// ---------------------------------------------------------------- emission
// The clauses of a family calling `call` (a native function index, as text): one per arity.
// `virtual_method`: a virtual method's predicate, which runs the override of an extended object.
void Generator::emit_family(std::ostream& out, const Family& family, const std::vector<Overload>& overloads, const std::string& what,
                            const std::string& call, bool virtual_method, const std::string& method) {
    const auto& sig = family.sig;
    std::vector<std::string> leading;
    if (!family.receiver.empty()) leading.push_back("+" + family.receiver);
    out << "\n" << mode_lines(family, leading, "Result") << "%\n" << family_doc(overloads, sig, what).render();
    // Meta-predicate declarations (closures are qualified with the caller's module).
    bool meta = false;
    for (const auto& param : sig.params) meta = meta || param.function;
    for (std::size_t count = sig.required; count <= sig.params.size(); ++count) {
        std::vector<std::string> head, arguments, outputs, specs, outputs_spec;
        if (!family.receiver.empty()) { head.push_back(family.receiver); arguments.push_back(family.receiver); specs.push_back("+"); }
        std::vector<std::string> bindings;
        std::size_t slot = 0;
        for (std::size_t p = 0; p < count; ++p) {
            const auto& param = sig.params[p];
            const auto input = param.name + (param.byref ? "0" : "");
            head.push_back(input);
            specs.push_back(param.function ? (param.closure_arity >= 0 && param.closure_arity <= 9 ? std::to_string(param.closure_arity) : ":") : "+");
            if (param.byref) {
                const auto number = std::to_string(slot++);
                arguments.push_back("'$rosetta_slot'(" + number + ", " + input + ")");
                outputs.push_back(param.name);
                outputs_spec.push_back("-");
                bindings.push_back("'$rosetta_out'(Writebacks, " + number + ", " + input + ", " + param.name + ")");
            } else if (param.function) {
                arguments.push_back("'$rosetta_fn'(" + input + ", " + (param.closure_result ? "true" : "false") + ")");
            } else {
                arguments.push_back(input);
            }
        }
        for (const auto& output : outputs) head.push_back(output);
        for (const auto& spec : outputs_spec) specs.push_back(spec);
        if (sig.returns) { head.push_back("Result"); specs.push_back("-"); }
        const auto arity = head.size();
        if (meta) out << ":- meta_predicate " << family.name << "(" << join(specs, ", ") << ").\n";
        const auto result = sig.returns ? std::string("Result") : std::string("_");
        std::string native = bindings.empty() ? "'$rosetta_call'(" + call + ", [" + join(arguments, ", ") + "], " + result + ")"
                                              : "'$rosetta_call'(" + call + ", [" + join(arguments, ", ") + "], " + result + ", Writebacks)";
        out << family.name << (head.empty() ? std::string() : "(" + join(head, ", ") + ")") << " :-\n";
        if (virtual_method) {
            out << "    '$rosetta_dispatch'(" << family.receiver << ", " << method << ", [" << join(head, ", ") << "],\n        "
                << (bindings.empty() ? native : "(" + native + ", " + join(bindings, ", ") + ")") << ").\n";
        } else {
            out << "    " << native;
            for (const auto& binding : bindings) out << ",\n    " << binding;
            out << ".\n";
        }
        export_predicate(family.name, arity);
    }
}

void Generator::emit_constructor(std::ostream& out, const ClassPlan& plan, const Family& family, bool extend) {
    const auto owner = plan.record->qualified_name();
    const auto& overloads_ = [&]() -> std::vector<Overload> {
        std::vector<Overload> result;
        const bool constructible = !plan.record->abstract;
        for (const auto& overload : plan.constructors)
            if (plan.director || (constructible && overload.function->access == "public")) result.push_back(overload);
        return result;
    }();
    const auto& sig = family.sig;
    std::vector<std::string> leading;
    if (extend) leading = {"+Module:atom", "+Data"};
    out << "\n" << mode_lines(family, leading, "Object");
    auto doc = family_doc(overloads_, sig, "the C++ constructor `" + owner + "`");
    if (extend)
        doc.line("Creates an extended object: a C++ subclass whose virtual methods run the clauses of `Module:override/1` for them "
                 "(`override(area(Self, Area)) :- ...`), and the C++ implementation otherwise. Data is the object's data "
                 "(object_data/2). Inside an override, calling the method's predicate on the object runs the C++ implementation.");
    out << "%\n" << doc.render();
    for (std::size_t count = sig.required; count <= sig.params.size(); ++count) {
        std::vector<std::string> head, arguments;
        if (extend) { head.push_back("Module"); head.push_back("Data"); }
        for (std::size_t p = 0; p < count; ++p) {
            const auto& param = sig.params[p];
            head.push_back(param.name);
            arguments.push_back(param.function ? "'$rosetta_fn'(" + param.name + ", " + (param.closure_result ? "true" : "false") + ")" : param.name);
        }
        head.push_back("Object");
        out << family.name << "(" << join(head, ", ") << ") :-\n    '$rosetta_construct'(" << plan.constructor_index << ", [" << join(arguments, ", ")
            << "], " << (extend ? "Module, Data" : "[], []") << ", Object).\n";
        export_predicate(family.name, head.size());
    }
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& atoms = enum_atoms_.at(qualified);
    out << "\n%!  " << name << "(?Name:atom, ?Value:integer) is nondet.\n%\n";
    auto doc = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    doc.line("Its enumerators cross as atoms (" + (atoms.empty() ? std::string("x") : atoms.front()) + "); C++ also takes their integer values.");
    std::vector<std::string> items;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
        items.push_back("  - " + atoms[i] + " = " + enumeration.enumerator_values[i] + " (C++ `" + enumeration.enumerators[i] + "`)");
    doc.add(items);
    add_notes(doc, enumeration.documentation);
    out << doc.render();
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) out << name << "(" << atoms[i] << ", " << enumeration.enumerator_values[i] << ").\n";
    export_predicate(name, 2);
}

void Generator::emit_class(std::ostream& out, const std::string& owner) {
    auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& prefix = plan.name;
    auto doc = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    std::vector<std::string> bases;
    for (const auto& base : plan.bases) bases.push_back(classes_.at(base).name);
    if (!bases.empty())
        doc.line("Derives from " + join(bases, ", ") + ": the predicates of " + (bases.size() == 1 ? "that class" : "those classes") + " take its objects too.");
    if (plan.director)
        doc.line(std::string(record.abstract ? "Abstract in C++: create" : "Create") + " an extended object (" + prefix +
                 "_extend) to implement its virtual methods in Prolog.");
    else if (record.abstract)
        doc.line("Abstract in C++: C++ functions return objects of derived classes.");
    add_notes(doc, record.documentation);
    out << "\n% ---------------------------------------------------------------- " << prefix << "\n%\n" << doc.render();
    if (constructors_.count(owner)) emit_constructor(out, plan, constructors_.at(owner), false);
    if (extenders_.count(owner)) emit_constructor(out, plan, extenders_.at(owner), true);
    for (const auto& group : plan.methods) {
        const auto& front = group.overloads.front();
        const auto& function = *front.function;
        std::string what = front.extension ? "C++ `" + function.qualified_name() + "`" : "the C++ method `" + owner + "::" + function.name + "`";
        bool virtual_method = false;
        for (const auto& overload : group.overloads)
            virtual_method = virtual_method || (!group.is_static && !overload.extension && overload.function->is_virtual && policy_.directors != "none");
        emit_family(out, families_.at(&group), group.overloads, what, std::to_string(group.index), virtual_method, group.name);
    }
    for (const auto& group : plan.operators) {
        if (!families_.count(&group)) continue;
        const auto& function = *group.overloads.front().function;
        emit_family(out, families_.at(&group), group.overloads, "the C++ operator `" + owner + "::" + function.name + "`", std::to_string(group.index), false, "");
    }
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto& [getter, setter] = field_preds_.at(owner + "::" + f.name);
        const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        const auto type = prolog_type(f.type);
        const auto receiver = variable_name(prefix);
        const auto text = (about.empty() ? "The C++ field `" + owner + "::" + f.name + "`" : doc_inline(about)) + " (" + type + (setter.empty() ? ", read-only" : "") + ").";
        out << "\n%!  " << getter << "(+" << receiver << ", -Value:" << type << ") is det.\n%\n%   " << text << "\n"
            << getter << "(" << receiver << ", Value) :-\n    '$rosetta_call'(" << field.getter_index << ", [" << receiver << "], Value).\n";
        export_predicate(getter, 2);
        if (!setter.empty()) {
            const auto* fn = function_type(f.type);
            const auto value = fn ? "'$rosetta_fn'(Value, " + std::string(is_void(fn->arguments[0]) ? "false" : "true") + ")" : std::string("Value");
            out << "\n%!  " << setter << "(+" << receiver << ", +Value:" << type << ") is det.\n%\n%   Sets " << getter << "/2.\n";
            if (fn) {
                const int arity = static_cast<int>(fn->arguments.size()) - 1 + (is_void(fn->arguments[0]) ? 0 : 1);
                out << ":- meta_predicate " << setter << "(+, " << (arity <= 9 ? std::to_string(arity) : ":") << ").\n";
            }
            out << setter << "(" << receiver << ", Value) :-\n    '$rosetta_call'(" << field.setter_index << ", [" << receiver << ", " << value << "], _).\n";
            export_predicate(setter, 2);
        }
    }
    for (const auto& constant : plan.constants) {
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        const auto& name = constant_names_.at(&constant);
        out << "\n%!  " << name << "(-Value:" << prolog_type(*constant.type) << ") is det.\n%\n" << about.render() << name
            << "(Value) :-\n    '$rosetta_call'(" << constant.index << ", [], Value).\n";
        export_predicate(name, 1);
    }
}

std::string Generator::file_doc() const {
    auto environment = name_;
    for (auto& c : environment) c = upper(c);
    std::ostringstream doc;
    doc << "/** <module> Prolog interface to the C++ library " << name_ << " (" << header_name() << ")\n\n"
        << "Generated by Rosetta from `" << header_name() << "`. Every C++ function is a family of deterministic predicates, one per "
        << "argument count, with the result last: add(1, R) and add(1, 2, R) call one C++ function, which picks the overload by the "
        << "arguments' types at run time and applies its default arguments to those left out. Methods take the object first "
        << "(circle_area(Circle, Area)); constructors are CLASS_new/N, fields CLASS_FIELD/2 and CLASS_set_FIELD/2, constants "
        << "predicates of one argument. A predicate of a base class takes the objects of derived classes.\n\n"
        << "Values: integers (unbounded: C++ checks the range) and floats are Prolog numbers, strings Prolog strings (atoms are taken "
        << "too), `bool` the atoms `true` and `false`, a missing value (`std::optional`, null pointers) the atom `null`, vectors and sets "
        << "lists, maps lists of Key-Value pairs, pairs Key-Value, tuples lists, `std::complex` complex(Re, Im), enumerators atoms, "
        << "`std::function` closures (called with the arguments and the result) and C++ objects blobs, released when atom garbage "
        << "collection reclaims them or by close_object/1. A C++ function a call returns is a closure: call(F, Arguments..., Result). "
        << "A non-const reference parameter is an input and an output argument: scale(Values0, Factor, Values).\n\n"
        << "Extended objects: a CLASS_extend(Module, Data, Arguments..., Object) predicate creates a C++ subclass whose virtual methods "
        << "run the clauses of Module's override/1 for them (`override(area(Self, Area)) :- ...`); calling the method's predicate inside "
        << "its own override runs the C++ implementation, and calls from Prolog run the override too.\n\n"
        << "Errors: C++ exceptions and values C++ does not accept raise error(native_error(Kind, Message), _), Kind one of binding (no "
        << "matching overload, a value that does not convert, a closed object), invalid_argument, out_of_range, domain_error, "
        << "length_error, arithmetic_error, native, allocation and callback (a Prolog callback failed). An exception a Prolog callback "
        << "or override raises crosses C++ unchanged.\n\n"
        << "The native library lib" << library_ << " is found through the " << environment << "_PROLOG_LIBRARY environment variable (a "
        << "path), beside this file, in lib/ or ../lib, else on the foreign search path.\n*/\n";
    return doc.str();
}

GeneratedPrologSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    module_name_ = options_.module.empty() ? snake_words(name_) : options_.module;
    {
        bool ok = !module_name_.empty() && is_lower(module_name_[0]);
        for (const char c : module_name_) ok = ok && (is_alnum(c) || c == '_');
        static const std::set<std::string> taken = {"system", "user", "prolog", "lists", "apply", "error", "debug", "pldoc", "plunit",
            "yall", "apply_macros", "readutil", "option", "pairs", "aggregate", "dicts", "strings", "ordsets", "assoc"};
        if (!ok || taken.count(module_name_))
            throw BindingError("Prolog module name must be a plain atom (a lowercase letter, then letters, digits and _) and not a library module: " + module_name_);
    }
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Prolog library name must be a plain file name: " + library_);
    GeneratedPrologSource result;
    prepare();
    // Class prefixes (alike but for case: point, point_2).
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto wanted = snake_words(plan.name);
        auto prefix = wanted;
        for (int i = 2; !prefixes_.insert(prefix).second; ++i) prefix = wanted + "_" + std::to_string(i);
        plan.name = prefix;
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = snake_words(renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = name;
        std::set<std::string> scope = {"true", "false", "null"};
        for (const auto& enumerator : enumeration.enumerators) {
            const auto wanted = snake_words(renamed(qualified + "::" + enumerator, enumerator));
            auto atom = wanted;
            for (int i = 2; !scope.insert(atom).second; ++i) atom = wanted + "_" + std::to_string(i);
            enum_atoms_[qualified].push_back(atom);
        }
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    for (const auto& group : functions) {
        if (group.module.empty() || submodule_prefixes_.count(group.module)) continue;
        std::string prefix;
        std::size_t start = 0;
        while (true) {
            const auto dot = group.module.find('.', start);
            prefix += snake_words(group.module.substr(start, dot == std::string::npos ? std::string::npos : dot - start)) + "_";
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        submodule_prefixes_[group.module] = prefix;
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Prolog module " + module_name_ + " (" + name_ + ").");
    std::string glue;
    for (std::size_t i = 0; i < detail::prolog_full_glue_count; ++i) glue += detail::prolog_full_glue_parts[i];
    std::string runtime;
    for (std::size_t i = 0; i < detail::prolog_full_runtime_count; ++i) runtime += detail::prolog_full_runtime_parts[i];
    auto environment = name_;
    for (auto& c : environment) c = upper(c);
    const auto replace = [&](std::string& text, const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    };
    for (auto* text : {&glue, &runtime}) {
        replace(*text, "@C@", "rosetta_" + name_);
        replace(*text, "@M@", module_name_);
        replace(*text, "@LIB@", library_);
        replace(*text, "@ENV@", environment + "_PROLOG_LIBRARY");
    }
    result.source += glue;

    // ------------------------------------------------------------ the Prolog module
    std::ostringstream body;
    for (const auto& [qualified, name] : enum_names_) emit_enum(body, qualified);
    for (const auto& owner : class_order_) emit_class(body, owner);
    std::string previous_module;
    for (const auto& group : functions) {
        const auto& function = *group.overloads.front().function;
        if (group.module != previous_module) {
            previous_module = group.module;
            std::string from;
            for (const auto& [ns, mapped] : policy_.modules) if (mapped == group.module) from = ns;
            body << "\n% ---------------------------------------------------------------- " << group.module << "\n%\n%   The functions of the C++ namespace `"
                 << from << "` (policy module " << group.module << "): predicates prefixed " << submodule_prefixes_.at(group.module) << ".\n";
        }
        emit_family(body, families_.at(&group), group.overloads, "C++ `" + function.qualified_name() + "`", std::to_string(group.index), false, "");
    }
    for (const auto& constant : constants) {
        auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
        const auto& name = constant_names_.at(&constant);
        body << "\n%!  " << name << "(-Value:" << prolog_type(*constant.type) << ") is det.\n%\n" << about.render() << name
             << "(Value) :-\n    '$rosetta_call'(" << constant.index << ", [], Value).\n";
        export_predicate(name, 1);
    }
    // The class table, and the predicates C++ calls for virtual methods of extended objects.
    body << "\n% ---------------------------------------------------------------- tables\n\n";
    std::vector<std::string> class_names;
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& plan = classes_.at(class_order_[i]);
        std::vector<std::string> bases;
        for (const auto& base : plan.bases) bases.push_back(std::to_string(std::find(class_order_.begin(), class_order_.end(), base) - class_order_.begin()));
        body << "'$rosetta_class'(" << i << ", " << plan.name << ", [" << join(bases, ", ") << "]).\n";
        class_names.push_back(plan.name);
    }
    if (class_order_.empty()) body << "'$rosetta_class'(_, _, _) :- fail.\n";
    std::map<std::size_t, std::pair<std::string, bool>> virtuals;   // generic -> (method, has result)
    std::vector<std::string> natives;
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& plan = classes_.at(class_order_[i]);
        if (!plan.director) continue;
        for (const auto& item : overridable(plan)) {
            const auto generic_found = generic_index_.find(item.name);
            if (generic_found == generic_index_.end()) continue;
            virtuals.emplace(generic_found->second, std::make_pair(item.name, !is_void(item.method->result)));
            // The predicate of the nearest class declaring the method (the class, then its bases).
            std::vector<std::string> queue = {class_order_[i]};
            std::string predicate;
            for (std::size_t q = 0; q < queue.size() && predicate.empty(); ++q) {
                const auto& candidate = classes_.at(queue[q]);
                for (const auto& group : candidate.methods)
                    for (const auto& overload : group.overloads)
                        if (predicate.empty() && !group.is_static && !overload.extension && overload.function->name == item.method->name &&
                            overload.function->is_virtual && families_.count(&group))
                            predicate = families_.at(&group).name;
                for (const auto& base : candidate.bases) queue.push_back(base);
            }
            if (!predicate.empty())
                natives.push_back("'$rosetta_native'(" + std::to_string(i) + ", " + std::to_string(generic_found->second) + ", " + predicate + ").");
        }
    }
    for (const auto& [generic, info] : virtuals)
        body << "'$rosetta_virtual'(" << generic << ", " << info.first << ", " << (info.second ? "true" : "false") << ").\n";
    if (virtuals.empty()) body << "'$rosetta_virtual'(_, _, _) :- fail.\n";
    std::sort(natives.begin(), natives.end());
    natives.erase(std::unique(natives.begin(), natives.end()), natives.end());
    for (const auto& native : natives) body << native << "\n";
    if (natives.empty()) body << "'$rosetta_native'(_, _, _) :- fail.\n";
    body << "\n:- '$rosetta_init'([" << join(class_names, ", ") << "]).\n";

    for (const char* item : {"close_object/1", "object_closed/1", "object_class/2", "object_is_a/2", "object_data/2", "object_set_data/2"})
        exports_.push_back(item);
    std::ostringstream prolog;
    prolog << "% Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n:- module(" << module_name_ << ", [\n";
    for (std::size_t i = 0; i < exports_.size(); ++i) prolog << "    " << exports_[i] << (i + 1 < exports_.size() ? ",\n" : "\n");
    prolog << "]).\n\n" << file_doc() << runtime << body.str();
    result.prolog = prolog.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

std::string prolog_module_name(const std::string& name) { return snake_words(name); }

GeneratedPrologSource generate_prolog(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                      const PrologOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Prolog C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
