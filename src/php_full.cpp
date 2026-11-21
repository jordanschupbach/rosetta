// PHP backend (--language php): one PHP 8.1+ file calling the shared C ABI library of the full C
// ABI backends (cabi_full.hpp, lisp_full_runtime.hpp) through the FFI extension.
//
// Classes are PHP classes extending NativeObject (C++ inheritance is `extends` along the primary
// base; secondary bases are reached through asBase()). A PHP subclass overrides C++ virtual
// methods: its constructor creates a C++ subclass whose virtual methods call the PHP methods.
// Overloads of a name are one PHP method whose parameters are union types; C++ resolves the call
// at run time, and only the arguments a call passes cross, so C++ applies its default arguments.
// Fields are properties (__get/__set), enums backed enums, containers arrays, std::function
// callables, non-const reference parameters by-reference parameters, C++ exceptions subclasses of
// the SPL exceptions implementing NativeError. Doxygen becomes PHPDoc.
//
// The output is one file: the runtime (namespace NS and NS\Rosetta), the classes, enums and
// functions (namespace NS) and the policy modules' functions (NS\Module).
#include "rosetta/php.hpp"
#include "cabi_full.hpp"
#include "php_full_runtime.hpp"
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
std::string lowered(std::string text) { for (auto& c : text) c = lower(c); return text; }

// Words PHP reserves as class, enum and function names (compared case-insensitively).
bool php_reserved(const std::string& name) {
    static const std::set<std::string> words = {"__halt_compiler", "abstract", "and", "array", "as", "break", "callable", "case",
        "catch", "class", "clone", "const", "continue", "declare", "default", "die", "do", "echo", "else", "elseif", "empty",
        "enddeclare", "endfor", "endforeach", "endif", "endswitch", "endwhile", "enum", "eval", "exit", "extends", "final", "finally",
        "fn", "for", "foreach", "function", "global", "goto", "if", "implements", "include", "include_once", "instanceof", "insteadof",
        "interface", "isset", "list", "match", "namespace", "new", "or", "print", "private", "protected", "public", "readonly",
        "require", "require_once", "return", "static", "switch", "throw", "trait", "try", "unset", "use", "var", "while", "xor",
        "yield", "int", "float", "bool", "string", "true", "false", "null", "void", "iterable", "object", "mixed", "never", "self",
        "parent", "resource", "numeric"};
    return words.count(lowered(name)) != 0;
}

// snake_case words of a C++ name (camelCase, PascalCase, URLPath and snake_case alike).
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
// camelCase (methods, properties, parameters).
std::string camel(const std::string& name) {
    const auto words = snake_words(name);
    std::string result;
    bool start = false;
    for (const char c : words) {
        if (c == '_') { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    return result;
}
// PascalCase (classes, enums, enum cases).
std::string pascal(const std::string& name) {
    auto result = camel(name);
    result[0] = upper(result[0]);
    return result;
}
// A function name: snake_case, never a reserved word.
std::string snake(const std::string& name) {
    auto result = snake_words(name);
    if (php_reserved(result)) result += "_";
    return result;
}
// A single-quoted PHP string literal.
std::string php_string(const std::string& text) {
    std::string result = "'";
    for (const char c : text) {
        if (c == '\\' || c == '\'') result += '\\';
        result += c;
    }
    return result + "'";
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

// Names in one PHP scope; class, function and method names compare case-insensitively.
struct Names {
    bool insensitive = true;
    std::set<std::string> taken;
    std::string key(const std::string& name) const { return insensitive ? lowered(name) : name; }
    bool reserve(const std::string& name) { return taken.insert(key(name)).second; }
    bool free(const std::string& name) const { return !taken.count(key(name)); }
    // The wanted name, else with a number (getName2, area_of_2, ON_2).
    std::string claim(const std::string& wanted) {
        if (reserve(wanted)) return wanted;
        const bool separated = wanted.find('_') != std::string::npos || !std::any_of(wanted.begin(), wanted.end(), is_lower);
        for (int i = 2;; ++i) {
            const auto candidate = wanted + (separated ? "_" : "") + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
    }
    // A type name: the wanted name, else with a trailing underscore (String_), else a number.
    std::string claim_type(const std::string& wanted) {
        if (reserve(wanted)) return wanted;
        return claim(wanted + "_");
    }
};

// ---------------------------------------------------------------- doc comments
// The lines of a PHPDoc block ("" is a blank line); descriptions are Markdown.
struct Doc {
    std::vector<std::string> lines;
    std::vector<std::string> tags;
    void blank() { if (!lines.empty() && !lines.back().empty()) lines.push_back(""); }
    void paragraph(const std::string& text) {
        blank();
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const auto stripped = trim(line);
            if (stripped.empty()) blank();
            else if (starts(stripped, "* ")) lines.push_back("- " + trim(stripped.substr(2)));
            else lines.push_back(stripped);
        }
    }
    void heading(const std::string& text) {
        blank();
        lines.push_back("**" + one_line(trim(text)) + "**");
        lines.push_back("");
    }
    void code(const std::string& text) {
        blank();
        lines.push_back("```");
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        lines.insert(lines.end(), body.begin(), body.end());
        lines.push_back("```");
        lines.push_back("");
    }
    void list(const std::string& title, const std::vector<std::string>& items) {
        if (items.empty()) return;
        blank();
        if (!title.empty()) lines.push_back(title);
        for (const auto& item : items) lines.push_back("- " + item);
        lines.push_back("");
    }
    void tag(const std::string& text) { tags.push_back(text); }
    std::string render(const std::string& indent = {}) const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        if (!tags.empty()) {
            // In the usual order: @deprecated, @property, @param, @return, @throws, @see.
            auto sorted = tags;
            const auto rank = [](const std::string& tag) {
                for (const auto& [prefix, order] : std::vector<std::pair<std::string, int>>{
                         {"@deprecated", 0}, {"@property", 1}, {"@param", 2}, {"@return", 3}, {"@throws", 4}, {"@see", 5}})
                    if (starts(tag, prefix)) return order;
                return 6;
            };
            std::stable_sort(sorted.begin(), sorted.end(), [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
            if (!copy.empty()) copy.push_back("");
            copy.insert(copy.end(), sorted.begin(), sorted.end());
        }
        std::string out = indent + "/**\n";
        bool previous_blank = true;
        bool fenced = false;
        for (auto line : copy) {
            if (line.empty() && previous_blank && !fenced) continue;
            if (starts(line, "```")) fenced = !fenced;
            previous_blank = line.empty();
            for (std::size_t at = 0; (at = line.find("*/", at)) != std::string::npos;) line.replace(at, 2, "*\\/");
            if (!fenced && starts(line, "@") && !starts(line, "@param") && !starts(line, "@return") && !starts(line, "@throws") &&
                !starts(line, "@see") && !starts(line, "@property") && !starts(line, "@internal") && !starts(line, "@var"))
                line = "\\" + line;
            out += indent + " *" + (line.empty() ? "" : " " + line) + "\n";
        }
        return out + indent + " */\n";
    }
};

std::string exception_class(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "InvalidArgumentException";
    if (bare == "std::out_of_range") return "OutOfRangeException";
    if (bare == "std::domain_error") return "DomainException";
    if (bare == "std::length_error") return "LengthException";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "RangeException";
    return "NativeException";
}

// C++ operators and their PHP method names ("" = not bound).
std::string php_operator(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"pos", "add"}}, {"operator-", {"neg", "sub"}}, {"operator*", {nullptr, "mul"}}, {"operator/", {nullptr, "div"}},
        {"operator%", {nullptr, "mod"}}, {"operator==", {nullptr, "equals"}}, {"operator!=", {nullptr, "notEquals"}},
        {"operator<", {nullptr, "less"}}, {"operator<=", {nullptr, "lessEqual"}}, {"operator>", {nullptr, "greater"}},
        {"operator>=", {nullptr, "greaterEqual"}}, {"operator&", {nullptr, "bitAnd"}}, {"operator|", {nullptr, "bitOr"}},
        {"operator^", {nullptr, "bitXor"}}, {"operator~", {"complement", nullptr}}, {"operator!", {"not", nullptr}},
        {"operator<<", {nullptr, "shiftLeft"}}, {"operator>>", {nullptr, "shiftRight"}}, {"operator[]", {nullptr, "at"}},
        {"operator+=", {nullptr, "addAssign"}}, {"operator-=", {nullptr, "subAssign"}}, {"operator*=", {nullptr, "mulAssign"}},
        {"operator/=", {nullptr, "divAssign"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// A type as PHP sees it: the declaration's type (`hint`), the PHPDoc type, and for a result holding
// enumerations the runtime's conversion spec (a PHP expression).
struct PhpType {
    std::string hint, doc, spec;
    bool callable = false;  // std::function: passed through Runtime::callable
    bool bytes = false;     // a vector of char-sized integers: a string or a list
};

// Splits a type declaration into its members: ?X -> X, null.
std::vector<std::string> members(const std::string& hint) {
    std::vector<std::string> result;
    if (starts(hint, "?")) return {hint.substr(1), "null"};
    std::size_t start = 0;
    while (true) {
        const auto bar = hint.find('|', start);
        result.push_back(hint.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return result;
}
std::string declaration(std::vector<std::string> types) {
    std::vector<std::string> unique;
    for (const auto& type : types) {
        if (type == "mixed") return "mixed";
        if (std::find(unique.begin(), unique.end(), type) == unique.end()) unique.push_back(type);
    }
    const auto has_null = std::find(unique.begin(), unique.end(), "null") != unique.end();
    if (has_null) unique.erase(std::find(unique.begin(), unique.end(), "null"));
    if (unique.empty()) return "mixed";
    if (has_null && unique.size() == 1) return "?" + unique[0];
    if (has_null) unique.push_back("null");
    return join(unique, "|");
}
std::string doc_union(const std::vector<std::string>& docs) {
    std::vector<std::string> unique;
    for (const auto& doc : docs)
        if (std::find(unique.begin(), unique.end(), doc) == unique.end()) unique.push_back(doc);
    return join(unique, "|");
}

// One parameter position of a PHP method (every overload's parameter there).
struct Param {
    std::vector<std::string> types;   // declaration members
    std::vector<std::string> docs;
    std::string name;
    bool optional = false;
    bool byref = false;
    bool callable = false;
    bool bytes = false;
    std::string default_value;        // PHP literal ("" = required)
    std::string description;
};
// The PHP signature of a method or function: what PHP checks an override against.
struct Sig {
    bool is_static = false;
    std::vector<Param> params;
    std::vector<std::string> results;   // declaration members
    std::vector<std::string> result_docs;
    std::string spec;
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const PhpOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedPhpSource run();

private:
    PhpOptions options_;
    std::string namespace_, library_, runtime_;
    Names types_;                                           // class and enum names of the namespace
    std::map<std::string, Names> functions_;                // policy module ("" = main) -> function names
    std::map<std::string, Names> members_;                  // class -> method names (inherited ones included)
    std::map<std::string, std::map<std::string, Sig>> inherited_;  // class -> lowercase method -> signature
    std::map<const Group*, std::string> names_;             // group -> PHP name
    std::map<const Group*, Sig> sigs_;
    std::map<std::string, Sig> constructor_sigs_;           // class -> __construct signature
    std::map<std::string, std::vector<Overload>> constructor_overloads_;
    std::map<std::string, std::string> property_names_;     // class::field -> property
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::map<std::string, std::string> links_;              // C++ name -> PHPDoc reference
    std::set<std::string> loose_;                           // classes some class derives from not along primary bases
    std::map<std::size_t, std::string> generic_methods_;    // C++ virtual method number -> PHP method

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "PHP"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return php_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken = {"this", "rosettaWritebacks", "rosettaResult", "GLOBALS"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : camel(cpp);
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + overload_signature(overload, group.name) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, "new " + plan.name) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: extend it in PHP and implement its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: extend the class in PHP";
    }
    std::string symbol_prefix() const override { return "rosetta_php_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_PHP"; }
    std::string module_prefix() const override { return "rosetta_php_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    PhpType php_type(const Type& type, bool param) const;
    PhpType php_spelled(std::string cpp) const;
    PhpType class_type(const std::string& qualified) const;
    std::string php_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    // `skip`: the bound operand of a free operator (the receiver), not a PHP parameter.
    Sig signature_of(const std::vector<Overload>& overloads, bool is_static, std::size_t skip = std::string::npos) const;
    bool subtype(const std::string& type, const std::vector<std::string>& of) const;
    bool merge_override(Sig& own, const Sig& base) const;

    // ------------------------------------------------------------ naming and emission
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    std::string link(const std::string& cpp) const;
    Doc doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Doc& comment, const Documentation& doc) const;
    Doc group_doc(const std::vector<Overload>& overloads, const Sig& sig, const std::string& what, const std::string& name) const;
    std::string parameter_list(const Sig& sig) const;
    std::string call_arguments(const Sig& sig, const Overload* operand_overload, std::size_t operand, bool method) const;
    void emit_call(std::ostream& out, const Sig& sig, const std::string& call, const std::string& indent) const;
    void emit_group(std::ostream& out, const Group& group, const ClassPlan* plan, bool method, const std::string& indent) const;
    void emit_class(std::ostream& out, const std::string& owner) const;
    void emit_enum(std::ostream& out, const std::string& qualified) const;
    std::string file_doc() const;
};

// ---------------------------------------------------------------- types
PhpType Generator::class_type(const std::string& qualified) const {
    const auto& name = classes_.at(qualified).name;
    // PHP checks declarations along `extends`: a class some class derives from through a secondary
    // base is declared NativeObject (C++ checks the object).
    return {loose_.count(qualified) ? "NativeObject" : name, name, {}};
}

PhpType nullable(PhpType type) {
    type.hint = declaration([&] { auto m = members(type.hint); m.push_back("null"); return m; }());
    if (type.doc.find("null") == std::string::npos) type.doc += "|null";
    return type;
}

PhpType Generator::php_type(const Type& type, bool param) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return {"?string", "string|null", {}};
        if (bound(pointee)) return nullable(class_type(pointee.declaration));
        if (is_void(pointee)) return {"mixed", "mixed", {}};
        return nullable(php_type(pointee, param));
    }
    if (is_void(target)) return {"void", "void", {}};
    if (!conversion(target).empty()) return php_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return {"bool", "bool", {}};
        if (bare == "char") return {"string", "string", {}};
        if (bare == "float" || bare == "double" || bare == "long double") return {"float", "float", {}};
        if (bare == "void") return {"void", "void", {}};
        if (bare.find("int128") != std::string::npos) return {"int|string", "int|string", {}};
        return {"int", "int", {}};
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return {"int", "int", {}};
        return {found->second + "|int", found->second + "|int", found->second + "::class"};
    }
    if (bound(target)) return class_type(target.declaration);
    if (bare == "std::filesystem::path") return {"string", "string", {}};
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {"mixed", "null", {}};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration_name = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? php_type(target.arguments[i], param) : PhpType{"mixed", "mixed", {}}; };
        if (declaration_name == "std::basic_string" || declaration_name == "std::basic_string_view") return {"string", "string", {}};
        if (declaration_name == "std::vector" || declaration_name == "std::deque" || declaration_name == "std::list" || declaration_name == "std::array" ||
            declaration_name == "std::valarray" || declaration_name == "std::span" || declaration_name == "std::set" ||
            declaration_name == "std::unordered_set" || declaration_name == "std::multiset" || declaration_name == "std::unordered_multiset") {
            if (!target.arguments.empty() && referent(target.arguments[0]).kind == TypeKind::builtin) {
                const auto element = normalize(strip_cv(referent(target.arguments[0]).canonical));
                if ((element == "unsigned char" || element == "signed char") && declaration_name.find("set") == std::string::npos) {
                    PhpType bytes{param ? "string|array" : "array", param ? "string|list<int>" : "list<int>", {}};
                    bytes.bytes = param;
                    return bytes;
                }
            }
            const auto element = argument(0);
            return {"array", "list<" + element.doc + ">", element.spec.empty() ? std::string() : "['list', " + element.spec + "]"};
        }
        if (declaration_name == "std::map" || declaration_name == "std::unordered_map" || declaration_name == "std::multimap" ||
            declaration_name == "std::unordered_multimap") {
            const auto key = argument(0), value = argument(1);
            const bool keyed = (key.doc == "int" || key.doc == "string") && declaration_name.find("multi") == std::string::npos;
            const auto spec = key.spec.empty() && value.spec.empty() ? std::string()
                              : "['map', " + (key.spec.empty() ? "null" : key.spec) + ", " + (value.spec.empty() ? "null" : value.spec) + "]";
            return {"array", keyed ? "array<" + key.doc + ", " + value.doc + ">" : "list<array{" + key.doc + ", " + value.doc + "}>", spec};
        }
        if (declaration_name == "std::optional") return nullable(argument(0));
        if (declaration_name == "std::complex") return {"Complex", "Complex", {}};
        if (declaration_name == "std::shared_ptr" || declaration_name == "std::unique_ptr") {
            if (target.arguments.empty()) return {"mixed", "mixed", {}};
            return nullable(argument(0));
        }
        if (declaration_name == "std::pair" || declaration_name == "std::tuple") {
            std::vector<std::string> docs, specs;
            bool any = false;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) {
                const auto item = argument(i);
                docs.push_back(item.doc);
                specs.push_back(item.spec.empty() ? "null" : item.spec);
                any = any || !item.spec.empty();
            }
            return {"array", "array{" + join(docs, ", ") + "}", any ? "['tuple', " + join(specs, ", ") + "]" : std::string()};
        }
        if (declaration_name == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::vector<std::string> items;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) items.push_back(php_type(function.arguments[i], false).doc);
            const auto result = function.arguments.empty() ? std::string("void") : php_type(function.arguments[0], false).doc;
            PhpType callable{"callable", "callable(" + join(items, ", ") + "): " + result, {}};
            callable.callable = param;
            return callable;
        }
        if (declaration_name == "std::chrono::duration") return {"float", "float", {}};
    }
    return {"mixed", "mixed", {}};
}

// A type a policy conversion spells in C++.
PhpType Generator::php_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    if (cpp == "bool") return {"bool", "bool", {}};
    if (cpp == "float" || cpp == "double" || cpp == "long double") return {"float", "float", {}};
    if (cpp == "char" || cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return {"string", "string", {}};
    static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int", "long",
        "unsigned long", "long long", "unsigned long long", "std::size_t", "size_t", "std::int64_t", "std::uint64_t", "std::int32_t",
        "std::uint32_t", "std::int16_t", "std::uint16_t", "std::int8_t", "std::uint8_t"};
    if (integers.count(cpp)) return {"int", "int", {}};
    for (const auto& [qualified, plan] : classes_) if (normalize(qualified) == cpp) return class_type(qualified);
    for (const auto& [qualified, name] : enum_names_) if (normalize(qualified) == cpp) return {name + "|int", name + "|int", name + "::class"};
    if (starts(cpp, "std::vector<") || starts(cpp, "std::map<") || starts(cpp, "std::pair<") || starts(cpp, "std::tuple<")) return {"array", "array", {}};
    if (starts(cpp, "std::optional<")) return {"mixed", "mixed", {}};
    return {"mixed", "mixed", {}};
}

std::string Generator::php_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") {
        auto text = shortest_double(value.value);
        if (text.find_first_of(".eEn") == std::string::npos) text += ".0";
        return text;
    }
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "null";
    if (value.kind == "char") return value.value.size() == 1 ? php_string(value.value) : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos)
            return php_string(text.substr(1, text.size() - 2));
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        const auto found = enum_names_.find(qualified);
        if (found == enum_names_.end() || !enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return found->second + "::" + enum_cases_.at(qualified)[i];
    }
    return {};
}

std::string Generator::overload_signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto type = php_type(function.parameters[i], true);
        auto text = type.doc + " " + (writeback(function.parameters[i]) ? "&" : "") + "$" + names[i - first];
        if (i >= function.parameters.size() - function.default_arguments) {
            const auto shown = php_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            text += " = " + (shown.empty() ? std::string("<C++ default>") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + "(" + join(parameters, ", ") + ")";
    if (!function.is_constructor) result += ": " + php_type(function.result, false).doc;
    return result;
}

// The PHP signature of overloads called through one method: position i takes every overload's
// type there, and is optional from the fewest arguments any overload needs on.
Sig Generator::signature_of(const std::vector<Overload>& overloads, bool is_static, std::size_t skip) const {
    Sig sig;
    sig.is_static = is_static;
    std::size_t required = std::string::npos, positions = 0;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto count = function.parameters.size() - first - (skip < function.parameters.size() ? 1 : 0);
        required = std::min(required, count - std::min(count, function.default_arguments));
        positions = std::max(positions, count);
    }
    if (required == std::string::npos) required = 0;
    sig.params.resize(positions);
    std::vector<bool> all_byref(positions, true), all_callable(positions, true), any_bytes(positions, false);
    std::vector<std::set<std::string>> defaults(positions);
    std::vector<std::size_t> having(positions, 0);
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto names = parameter_names(overload);
        for (std::size_t i = first, p = 0; i < function.parameters.size(); ++i) {
            if (i == skip) continue;
            const auto n = i - first;
            auto& param = sig.params[p++];
            const auto type = php_type(function.parameters[i], true);
            for (const auto& member : members(type.hint)) param.types.push_back(member);
            param.docs.push_back(type.doc);
            const auto q = p - 1;
            if (param.name.empty()) param.name = names[n];
            all_byref[q] = all_byref[q] && writeback(function.parameters[i]);
            all_callable[q] = all_callable[q] && type.callable;
            any_bytes[q] = any_bytes[q] || type.bytes;
            ++having[q];
            if (i >= function.parameters.size() - function.default_arguments)
                defaults[q].insert(php_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]));
            else defaults[q].insert("");
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            if (param.description.empty())
                for (const auto& item : function.documentation.params)
                    if (item.name == cpp && !item.text.empty()) param.description = one_line(item.text);
        }
        const auto result = php_type(function.result, false);
        for (const auto& member : members(function.is_constructor ? std::string("void") : result.hint)) sig.results.push_back(member);
        sig.result_docs.push_back(function.is_constructor ? std::string("void") : result.doc);
        if (overloads.size() == 1 || sig.spec == result.spec) sig.spec = result.spec;
        else sig.spec.clear();
    }
    // Names must differ (overloads may name positions alike).
    std::set<std::string> taken;
    for (auto& param : sig.params) {
        while (!taken.insert(param.name).second) param.name += "_";
    }
    for (std::size_t p = 0; p < positions; ++p) {
        auto& param = sig.params[p];
        param.byref = all_byref[p] && having[p] == overloads.size();
        param.callable = all_callable[p];
        param.bytes = any_bytes[p];
        if (p < required) continue;
        param.optional = true;
        // A literal only when every overload agrees on it; else null (the argument is left out
        // anyway when omitted, and C++ applies its default).
        param.default_value = defaults[p].size() == 1 && having[p] == overloads.size() && !defaults[p].begin()->empty() ? *defaults[p].begin() : "null";
        if (param.default_value == "null") param.types.push_back("null");
    }
    // A mix of void and other results is mixed.
    const bool some_void = std::find(sig.results.begin(), sig.results.end(), "void") != sig.results.end();
    if (some_void && std::any_of(sig.results.begin(), sig.results.end(), [](const std::string& r) { return r != "void"; })) sig.results = {"mixed"};
    if (sig.results.empty()) sig.results = {"void"};
    return sig;
}

// Whether a result member is a subtype of the members `of` (for covariant overrides).
bool Generator::subtype(const std::string& type, const std::vector<std::string>& of) const {
    for (const auto& base : of) {
        if (base == "mixed" && type != "void") return true;
        if (base == type) return true;
        if (base == "NativeObject" && type != "void" && std::any_of(classes_.begin(), classes_.end(), [&](const auto& c) { return c.second.name == type; }))
            return true;
        for (const auto& [qualified, plan] : classes_) {
            if (plan.name != type) continue;
            for (const auto& [other, other_plan] : classes_)
                if (other_plan.name == base && primary_derives(qualified, other)) return true;
        }
    }
    return false;
}

// Makes `own` (a derived class's method) compatible with `base` (the method it overrides in PHP):
// parameters widen to accept the base's, the result must be a subtype. False when it cannot be.
bool Generator::merge_override(Sig& own, const Sig& base) const {
    if (own.is_static != base.is_static) return false;
    for (const auto& result : own.results)
        if (!subtype(result, base.results)) return false;
    const auto count = std::max(own.params.size(), base.params.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i >= own.params.size()) {
            auto param = base.params[i];
            param.optional = true;
            if (param.default_value.empty()) { param.default_value = "null"; param.types.push_back("null"); }
            own.params.push_back(param);
            continue;
        }
        if (i >= base.params.size()) {
            if (!own.params[i].optional) return false;
            continue;
        }
        auto& mine = own.params[i];
        const auto& theirs = base.params[i];
        if (mine.byref != theirs.byref) return false;
        for (const auto& type : theirs.types)
            if (std::find(mine.types.begin(), mine.types.end(), type) == mine.types.end()) mine.types.push_back(type);
        for (const auto& doc : theirs.docs)
            if (std::find(mine.docs.begin(), mine.docs.end(), doc) == mine.docs.end()) mine.docs.push_back(doc);
        if (theirs.optional && !mine.optional) {
            mine.optional = true;
            mine.default_value = theirs.default_value;
            if (mine.default_value == "null") mine.types.push_back("null");
        } else if (mine.optional && theirs.optional && mine.default_value != theirs.default_value && mine.default_value != "null") {
            mine.default_value = "null";
            mine.types.push_back("null");
        }
        mine.callable = mine.callable && theirs.callable;
        mine.bytes = mine.bytes || theirs.bytes;
    }
    return true;
}

// ---------------------------------------------------------------- naming
void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    // Classes some class derives from along a secondary base.
    for (const auto& derived : class_order_)
        for (const auto& base : class_order_)
            if (derived != base && derives(derived, base) && !primary_derives(derived, base)) loose_.insert(base);
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        auto& inherited = inherited_[owner];
        // Names a PHP parent declares stay taken (and are overridden only compatibly).
        if (!cls.bases.empty() && members_.count(cls.bases.front())) {
            members = members_.at(cls.bases.front());
            inherited = inherited_.at(cls.bases.front());
        }
        for (const char* reserved : {"close", "isClosed", "cast", "__construct", "__destruct", "__get", "__set", "__isset", "__unset", "__call",
                                     "__callStatic", "__toString", "__invoke", "__debugInfo", "__serialize", "__unserialize", "__sleep",
                                     "__wakeup", "__set_state", "__clone", "offsetExists", "offsetGet", "offsetSet", "offsetUnset"})
            members.reserve(reserved);
        for (const auto& other : class_order_)
            if (other != owner && derives(owner, other) && !primary_derives(owner, other)) members.reserve("as" + classes_.at(other).name);
        // Constructors: one __construct for every constructor C++ can run.
        const bool constructible = !cls.record->abstract;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) constructor_overloads_[owner].push_back(overload);
        if (constructor_overloads_.count(owner)) constructor_sigs_[owner] = signature_of(constructor_overloads_.at(owner), false);
        std::set<std::string> own;   // lowercase names this class declares
        const auto name_group = [&](Group& group, std::string wanted) {
            const auto& front = group.overloads.front();
            auto sig = signature_of(group.overloads, group.is_static, front.extension && !front.receives_self ? group.operand : std::string::npos);
            const auto key = lowered(wanted);
            std::string name;
            if (inherited.count(key) && !own.count(key) && !members.free(wanted) && merge_override(sig, inherited.at(key))) name = wanted;
            else name = members.claim(wanted);
            own.insert(lowered(name));
            inherited[lowered(name)] = sig;
            names_[&group] = name;
            sigs_[&group] = sig;
        };
        for (auto& group : cls.methods) name_group(group, camel(group.name));
        for (auto& group : cls.operators) name_group(group, group.name + (group.operand ? "R" : ""));
        for (const auto& field : cls.fields) property_names_[owner + "::" + field.field->name] = camel(field.name);
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(camel(constant.name));
    }
    for (auto& group : functions) {
        auto& scope = functions_[group.module];
        names_[&group] = scope.claim(snake(group.name));
        sigs_[&group] = signature_of(group.overloads, false);
    }
    for (auto& constant : constants) constant_names_[&constant] = functions_[""].claim(snake(constant.name));
    // PHPDoc references.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        links_[owner] = cls.name;
        links_[cls.record->name] = cls.name;
        for (const auto& group : cls.methods) {
            links_[owner + "::" + group.name] = cls.name + "::" + names_.at(&group) + "()";
            links_[cls.record->name + "::" + group.name] = cls.name + "::" + names_.at(&group) + "()";
        }
    }
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = names_.at(&group) + "()";
            links_.emplace(overload.function->name, names_.at(&group) + "()");
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = name;
        links_.emplace(enums_.at(qualified)->name, name);
    }
}

// ---------------------------------------------------------------- documentation
std::string Generator::link(const std::string& cpp) const {
    const auto found = links_.find(normalize(cpp));
    return found != links_.end() ? found->second : cpp;
}
void Generator::add_notes(Doc& comment, const Documentation& doc) const {
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { comment.tag("@deprecated " + one_line(item.text)); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        comment.paragraph("**" + label + ":** " + one_line(item.text));
    }
    for (const auto& see : doc.see) comment.tag("@see " + link(see));
}
Doc Generator::doc_of(const Documentation& doc, const std::string& lead) const {
    Doc comment;
    auto details = doc.details;
    auto brief = doc.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    comment.paragraph(brief.empty() ? lead : brief);
    if (!brief.empty() && !lead.empty()) comment.paragraph(lead);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") comment.code(block.text);
        else if (block.kind == "heading") comment.heading(block.text);
        else comment.paragraph(block.text);
    }
    return comment;
}

Doc Generator::group_doc(const std::vector<Overload>& overloads, const Sig& sig, const std::string& what, const std::string& name) const {
    // The first documented overload describes the method.
    const Function* documented = overloads.front().function;
    for (const auto& overload : overloads)
        if (!overload.function->documentation.brief.empty() || !overload.function->documentation.details.empty()) { documented = overload.function; break; }
    const auto& doc = documented->documentation;
    auto comment = doc_of(doc, "Calls " + what + ".");
    if (overloads.size() > 1) {
        std::vector<std::string> items;
        for (const auto& overload : overloads) items.push_back("`" + overload_signature(overload, name) + "`");
        comment.list("Overloads (C++ picks one by the arguments' types):", items);
    }
    // C++ default arguments, used when the call leaves the argument out.
    if (overloads.size() == 1) {
        const auto& function = *overloads.front().function;
        const std::size_t first = overloads.front().receives_self ? 1 : 0;
        const auto names = parameter_names(overloads.front());
        std::vector<std::string> defaults;
        for (std::size_t i = function.parameters.size() - function.default_arguments; i < function.parameters.size(); ++i) {
            if (i < first) continue;
            const auto shown = php_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            const auto cpp = i < function.default_values.size() ? function.default_values[i].value : std::string();
            defaults.push_back("`$" + names[i - first] + " = " + (shown.empty() ? cpp.empty() ? std::string("C++ default") : cpp : shown) + "`");
        }
        if (!defaults.empty())
            comment.paragraph("Arguments left out take the C++ defaults: " + join(defaults, ", ") + ".");
    }
    add_notes(comment, doc);
    for (const auto& param : sig.params)
        comment.tag("@param " + doc_union(param.docs) + (param.optional && param.default_value == "null" ? "|null" : "") + " " +
                    (param.byref ? "&" : "") + "$" + param.name + (param.description.empty() ? "" : " " + param.description));
    if (!(sig.results.size() == 1 && sig.results[0] == "void")) {
        auto text = one_line(doc.returns);
        comment.tag("@return " + doc_union(sig.result_docs) + (text.empty() ? "" : " " + text));
    }
    std::set<std::string> thrown;
    for (const auto& overload : overloads)
        for (const auto& item : overload.function->documentation.throws) {
            const auto cls = exception_class(item.name);
            if (!thrown.insert(cls + item.text).second) continue;
            comment.tag("@throws " + cls + (item.name.empty() ? "" : " C++ `" + item.name + "`") + (item.text.empty() ? "" : ": " + one_line(item.text)));
        }
    return comment;
}

// ---------------------------------------------------------------- emission
std::string Generator::parameter_list(const Sig& sig) const {
    std::vector<std::string> items;
    for (const auto& param : sig.params) {
        auto text = declaration(param.types) + " " + (param.byref ? "&" : "") + "$" + param.name;
        if (param.optional) text += " = " + param.default_value;
        items.push_back(text);
    }
    return join(items, ", ");
}

// The arguments of a call: the receiver, then the parameters (only those passed when some are
// optional), slots for by-reference parameters.
std::string Generator::call_arguments(const Sig& sig, const Overload* operand_overload, std::size_t operand, bool method) const {
    std::vector<std::string> items;
    std::size_t slot = 0;
    for (const auto& param : sig.params) {
        std::string value = "$" + param.name;
        if (param.callable) value = runtime_ + "::callable(" + value + ")";
        else if (param.bytes) value = runtime_ + "::bytes(" + value + ")";
        if (param.byref) value = "new \\" + namespace_ + "\\Rosetta\\Slot(" + std::to_string(slot++) + ", " + value + ")";
        items.push_back(value);
    }
    if (operand_overload) {
        // A free operator bound to a class: the receiver at the bound operand.
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(std::min(operand, items.size())), "$this");
        return "[" + join(items, ", ") + "]";
    }
    const bool optional = std::any_of(sig.params.begin(), sig.params.end(), [](const Param& p) { return p.optional; });
    std::string list = optional ? "...\\array_slice([" + join(items, ", ") + "], 0, \\func_num_args())" : join(items, ", ");
    if (method) list = list.empty() ? "$this" : "$this, " + list;
    return "[" + list + "]";
}

void Generator::emit_call(std::ostream& out, const Sig& sig, const std::string& call, const std::string& indent) const {
    const bool returns = !(sig.results.size() == 1 && sig.results[0] == "void");
    std::vector<std::string> byref;
    for (const auto& param : sig.params) if (param.byref) byref.push_back(param.name);
    auto value = call;
    if (!byref.empty()) {
        out << indent << "$rosettaWritebacks = [];\n" << indent << (returns ? "$rosettaResult = " : "") << value.substr(0, value.size() - 1)
            << ", $rosettaWritebacks);\n";
        for (std::size_t i = 0; i < byref.size(); ++i)
            out << indent << "if (\\array_key_exists(" << i << ", $rosettaWritebacks)) {\n" << indent << "    $" << byref[i] << " = $rosettaWritebacks[" << i
                << "];\n" << indent << "}\n";
        value = "$rosettaResult";
        if (!returns) return;
    }
    if (!returns) { out << indent << value << ";\n"; return; }
    if (!sig.spec.empty()) value = runtime_ + "::convert(" + value + ", " + sig.spec + ")";
    out << indent << "return " << value << ";\n";
}

void Generator::emit_group(std::ostream& out, const Group& group, const ClassPlan* plan, bool method, const std::string& indent) const {
    const auto& sig = sigs_.at(&group);
    const auto& name = names_.at(&group);
    const auto& function = *group.overloads.front().function;
    std::string what;
    if (group.overloads.front().extension) what = "C++ `" + function.qualified_name() + "`";
    else if (plan) what = "the C++ method `" + plan->record->qualified_name() + "::" + function.name + "`";
    else what = "C++ `" + function.qualified_name() + "`";
    const auto comment = group_doc(group.overloads, sig, what, name);
    std::string results = declaration(sig.results);
    out << "\n" << comment.render(indent) << indent << (plan ? "public " : "") << (group.is_static ? "static " : "") << "function " << name << "("
        << parameter_list(sig) << "): " << results << "\n" << indent << "{\n";
    const bool free_operator = method && group.overloads.front().extension && !group.overloads.front().receives_self;
    const auto arguments = call_arguments(sig, free_operator ? &group.overloads.front() : nullptr, group.operand, method && !free_operator);
    emit_call(out, sig, runtime_ + "::call(" + std::to_string(group.index) + ", " + arguments + ")", indent + "    ");
    out << indent << "}\n";
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto comment = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    comment.paragraph("C++ results that are not one of its enumerators are integers.");
    add_notes(comment, enumeration.documentation);
    const auto value_of = [](const std::string& value) {
        const bool negative = !value.empty() && value[0] == '-';
        const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
        if (big) return std::to_string(static_cast<long long>(std::stoull(value)));
        if (value == "-9223372036854775808") return std::string("\\PHP_INT_MIN");
        return value;
    };
    out << "\n" << comment.render() << "enum " << name << ": int\n{\n";
    std::map<std::string, std::string> seen;   // value -> case
    std::vector<std::string> aliases;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        const auto value = value_of(enumeration.enumerator_values[i]);
        const auto line = "    /** C++ `" + qualified + "::" + enumeration.enumerators[i] + "` (" + enumeration.enumerator_values[i] + "). */\n";
        if (seen.count(value)) { aliases.push_back(line + "    public const " + cases[i] + " = self::" + seen.at(value) + ";\n"); continue; }
        seen[value] = cases[i];
        out << line << "    case " << cases[i] << " = " << value << ";\n";
    }
    for (const auto& alias : aliases) out << alias;
    out << "}\n";
}

void Generator::emit_class(std::ostream& out, const std::string& owner) const {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& name = plan.name;
    auto comment = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    std::vector<std::string> secondary;
    for (const auto& other : class_order_)
        if (other != owner && derives(owner, other) && !primary_derives(owner, other)) secondary.push_back(other);
    if (!secondary.empty()) {
        std::vector<std::string> methods;
        for (const auto& other : secondary) methods.push_back("as" + classes_.at(other).name + "()");
        comment.paragraph("Its C++ base classes that are not PHP parent classes: " + join(methods, ", ") + ".");
    }
    if (plan.director)
        comment.paragraph(std::string(record.abstract ? "Abstract in C++: extend" : "Extend") + " it in PHP to implement its virtual methods: "
                          "constructing the PHP subclass creates a C++ subclass whose virtual methods call the PHP methods, and "
                          "`parent::method()` runs the C++ implementation.");
    else if (record.abstract) comment.paragraph("Abstract in C++: C++ functions return objects of derived classes.");
    // Fields are properties.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        comment.tag(std::string(writable ? "@property " : "@property-read ") + php_type(f.type, false).doc + " $" +
                    property_names_.at(owner + "::" + f.name) + (about.empty() ? "" : " " + about));
    }
    add_notes(comment, record.documentation);
    // operator[] is ArrayAccess, to_string() __toString.
    const Group* index = nullptr;
    const Group* to_string = nullptr;
    const Group* call = nullptr;
    for (const auto& group : plan.operators)
        if (group.overloads.front().function->name == "operator[]" && names_.at(&group) == "at") index = &group;
        else if (names_.at(&group) == "call") call = &group;
    for (const auto& group : plan.methods)
        if (!group.is_static && group.name == "to_string" && sigs_.at(&group).results == std::vector<std::string>{"string"} &&
            std::none_of(sigs_.at(&group).params.begin(), sigs_.at(&group).params.end(), [](const Param& p) { return !p.optional; }))
            to_string = &group;
    std::vector<std::string> interfaces;
    if (index) interfaces.push_back("\\ArrayAccess");
    if (to_string) interfaces.push_back("\\Stringable");
    out << "\n" << comment.render() << (record.is_final ? "final " : "") << "class " << name << " extends "
        << (plan.bases.empty() ? std::string("NativeObject") : classes_.at(plan.bases.front()).name)
        << (interfaces.empty() ? std::string() : " implements " + join(interfaces, ", ")) << "\n{";
    // The constructor.
    if (constructor_overloads_.count(owner)) {
        const auto& sig = constructor_sigs_.at(owner);
        const auto& overloads = constructor_overloads_.at(owner);
        auto about = group_doc(overloads, sig, "the C++ constructor `" + owner + "`", "new " + name);
        if (plan.director)
            about.paragraph("A PHP subclass's object is a C++ subclass whose virtual methods call the PHP methods: its constructor must call this one.");
        out << "\n" << about.render("    ") << "    public function __construct(" << parameter_list(sig) << ")\n    {\n"
            << "        " << runtime_ << "::construct($this, " << plan.constructor_index << ", "
            << (plan.director ? "static::class !== self::class" : "false") << ", " << call_arguments(sig, nullptr, 0, false) << ");\n    }\n";
    } else {
        out << "\n    /** C++ creates the objects of " << name << ": there is no public C++ constructor. */\n"
            << "    public function __construct()\n    {\n        throw new BindingException("
            << php_string(name + " has no public C++ constructor: C++ functions return its objects") << ");\n    }\n";
    }
    for (const auto& other : secondary) {
        const auto& base = classes_.at(other).name;
        out << "\n    /** The object as a " << base << " (a C++ base class). */\n    public function as" << base << "(): " << base << "\n    {\n"
            << "        return " << runtime_ << "::view($this, " << base << "::class);\n    }\n";
    }
    for (const auto& group : plan.methods) emit_group(out, group, &plan, !group.is_static, "    ");
    for (const auto& group : plan.operators) emit_group(out, group, &plan, true, "    ");
    if (index) {
        out << "\n    public function offsetExists(mixed $offset): bool\n    {\n        try {\n            $this->at($offset);\n"
            << "            return true;\n        } catch (\\OutOfRangeException | BindingException) {\n            return false;\n        }\n    }\n"
            << "\n    public function offsetGet(mixed $offset): mixed\n    {\n        return $this->at($offset);\n    }\n"
            << "\n    public function offsetSet(mixed $offset, mixed $value): void\n    {\n        throw new \\LogicException("
            << php_string("C++ operator[] of " + name + " is read-only in PHP") << ");\n    }\n"
            << "\n    public function offsetUnset(mixed $offset): void\n    {\n        throw new \\LogicException("
            << php_string("cannot unset an element of " + name) << ");\n    }\n";
    }
    if (to_string)
        out << "\n    /** The C++ `to_string()` text. */\n    public function __toString(): string\n    {\n        return $this->"
            << names_.at(to_string) << "();\n    }\n";
    if (call)
        out << "\n    /** Calls the C++ `operator()`. */\n    public function __invoke(mixed ...$arguments): mixed\n    {\n        return $this->"
            << names_.at(call) << "(...$arguments);\n    }\n";
    // Properties.
    if (!plan.fields.empty()) {
        std::ostringstream get, set, isset;
        for (const auto& field : plan.fields) {
            const auto& f = *field.field;
            const auto& property = property_names_.at(owner + "::" + f.name);
            const auto type = php_type(f.type, false);
            auto read = runtime_ + "::call(" + std::to_string(field.getter_index) + ", [$this])";
            if (!type.spec.empty()) read = runtime_ + "::convert(" + read + ", " + type.spec + ")";
            get << "            " << php_string(property) << " => " << read << ",\n";
            isset << "            " << php_string(property) << ",\n";
            const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
            if (writable) {
                const auto setter_type = php_type(f.type, true);
                const auto value = setter_type.callable ? runtime_ + "::callable($value)" : setter_type.bytes ? runtime_ + "::bytes($value)" : std::string("$value");
                set << "            case " << php_string(property) << ":\n                " << runtime_ << "::call(" << field.setter_index
                    << ", [$this, " << value << "]);\n                return;\n";
            } else
                set << "            case " << php_string(property) << ":\n                throw new \\Error("
                    << php_string("Cannot modify read-only property " + name + "::$" + property) << ");\n";
        }
        out << "\n    public function __get(string $name): mixed\n    {\n        return match ($name) {\n" << get.str()
            << "            default => parent::__get($name),\n        };\n    }\n"
            << "\n    public function __set(string $name, mixed $value): void\n    {\n        switch ($name) {\n" << set.str()
            << "        }\n        parent::__set($name, $value);\n    }\n"
            << "\n    public function __isset(string $name): bool\n    {\n        return \\in_array($name, [\n" << isset.str()
            << "        ], true) || parent::__isset($name);\n    }\n";
    }
    for (const auto& constant : plan.constants) {
        const auto type = php_type(*constant.type, false);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        about.tag("@return " + type.doc);
        auto read = runtime_ + "::call(" + std::to_string(constant.index) + ", [])";
        if (!type.spec.empty()) read = runtime_ + "::convert(" + read + ", " + type.spec + ")";
        out << "\n" << about.render("    ") << "    public static function " << constant_names_.at(&constant) << "(): " << type.hint
            << "\n    {\n        return " << read << ";\n    }\n";
    }
    out << "}\n";
}

std::string Generator::file_doc() const {
    Doc comment;
    comment.paragraph("The PHP interface to the C++ library " + name_ + " (`" + header_name() + "`), generated by Rosetta: namespace `" + namespace_ + "`.");
    comment.paragraph("It needs PHP 8.1 or later with the FFI extension (`ffi.enable` is `preload` by default, which allows FFI on the "
                      "command line; set it to `true` for other SAPIs). The native library `lib" + library_ + ".so` is found through the `" +
                      [&] { auto e = name_; for (auto& c : e) c = upper(c); return e; }() + "_PHP_LIBRARY` environment variable (a path), "
                      "beside this file, in `lib/` or `../lib`, else on the loader's search path.");
    return comment.render();
}

GeneratedPhpSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    namespace_ = options_.namespace_.empty() ? pascal(name_) : options_.namespace_;
    {
        std::size_t start = 0;
        while (true) {
            const auto slash = namespace_.find('\\', start);
            const auto part = namespace_.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!identifier(part) || php_reserved(part) || lowered(part) == "namespace")
                throw BindingError("PHP namespace must be identifiers joined by \\ and not reserved words: " + namespace_);
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
    }
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("PHP library name must be a plain file name: " + library_);
    runtime_ = "\\" + namespace_ + "\\Rosetta\\Runtime";
    GeneratedPhpSource result;
    prepare();
    // The namespace's class names: the runtime's first.
    for (const char* reserved : {"NativeObject", "NativeError", "NativeException", "BindingException", "InvalidArgumentException",
                                 "OutOfRangeException", "DomainException", "LengthException", "RangeException", "CallbackException",
                                 "Complex", "NativeFunction", "Rosetta"})
        types_.reserve(reserved);
    const auto type_name = [&](const std::string& wanted) {
        auto name = pascal(wanted);
        if (php_reserved(name)) name += "_";
        return types_.claim_type(name);
    };
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = type_name(plan.name);
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = type_name(name);
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        Names scope;
        scope.insensitive = false;
        scope.reserve("class");
        scope.reserve("CLASS");
        scope.reserve("Class");
        for (const auto& enumerator : enumeration.enumerators)
            enum_cases_[qualified].push_back(scope.claim(pascal(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Policy modules are namespaces under the main one (a.b nests).
    std::map<std::string, std::string> submodules;  // policy module -> PHP namespace
    for (const auto& group : functions) {
        if (group.module.empty() || submodules.count(group.module)) continue;
        std::string space = namespace_;
        std::size_t start = 0;
        while (true) {
            const auto dot = group.module.find('.', start);
            auto part = pascal(group.module.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (php_reserved(part) || part == "Rosetta") part += "_";
            space += "\\" + part;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        submodules[group.module] = space;
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "PHP namespace " + namespace_ + " (" + name_ + ").");
    // The PHP methods C++ calls for virtual methods (emit_cpp numbers them).
    for (const auto& owner : class_order_)
        for (const auto& group : classes_.at(owner).methods) {
            if (group.is_static) continue;
            for (const auto& overload : group.overloads) {
                if (overload.extension || !overload.function->is_virtual) continue;
                const auto found = generic_index_.find(overload.function->name);
                if (found != generic_index_.end()) generic_methods_.emplace(found->second, names_.at(&group));
            }
        }

    // ------------------------------------------------------------ the PHP file
    auto runtime = std::string(detail::php_full_runtime_parts[0]) + detail::php_full_runtime_parts[1];
    const auto replace = [&](const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
    };
    auto environment = name_;
    for (auto& c : environment) c = upper(c);
    replace("@NS@", namespace_);
    replace("@C@", "rosetta_" + name_);
    replace("@LIB@", library_);
    replace("@ENV@", environment + "_PHP_LIBRARY");
    std::ostringstream php;
    php << "<?php\n// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\n" << file_doc() << "\ndeclare(strict_types=1);\n\n"
        << runtime << "\nnamespace " << namespace_ << " {\n";
    for (const auto& [qualified, name] : enum_names_) emit_enum(php, qualified);
    for (const auto& owner : class_order_) emit_class(php, owner);
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        emit_group(php, group, nullptr, false, "");
    }
    for (const auto& constant : constants) {
        const auto type = php_type(*constant.type, false);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
        about.tag("@return " + type.doc);
        auto read = runtime_ + "::call(" + std::to_string(constant.index) + ", [])";
        if (!type.spec.empty()) read = runtime_ + "::convert(" + read + ", " + type.spec + ")";
        php << "\n" << about.render() << "function " << constant_names_.at(&constant) << "(): " << type.hint << "\n{\n    return " << read << ";\n}\n";
    }
    // The class table and the PHP methods C++ calls for virtual methods.
    php << "\n" << runtime_ << "::$classes = [\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        std::vector<std::string> bases;
        for (const auto& base : plan.bases) {
            std::size_t at = 0;
            while (class_order_[at] != base) ++at;
            bases.push_back(std::to_string(at));
        }
        php << "    [" << plan.name << "::class, [" << join(bases, ", ") << "]],\n";
    }
    php << "];\n" << runtime_ << "::$methods = [";
    for (const auto& [generic, method] : generic_methods_) php << generic << " => " << php_string(method) << ", ";
    php << "];\n\n} // namespace " << namespace_ << "\n";
    for (const auto& [module, space] : submodules) {
        std::string from;
        for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) from = ns;
        std::vector<std::string> imports = {"NativeObject", "Complex"};
        for (const auto& owner : class_order_) imports.push_back(classes_.at(owner).name);
        for (const auto& [qualified, name] : enum_names_) imports.push_back(name);
        php << "\n/** The functions of the C++ namespace `" << from << "` (policy module `" << module << "`). */\nnamespace " << space << " {\n\n"
            << "use " << namespace_ << "\\{" << join(imports, ", ") << "};\n";
        for (const auto& group : functions)
            if (group.module == module) emit_group(php, group, nullptr, false, "");
        php << "\n} // namespace " << space << "\n";
    }
    result.php = php.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedPhpSource generate_php(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                const PhpOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("PHP C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
