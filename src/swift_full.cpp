// Swift backend (--language swift): a Swift package over the shared C ABI library of the full C
// ABI backends (cabi_full.hpp, lisp_full_runtime.hpp).
//
// Classes are Swift classes over NativeObject; the first C++ base class is the superclass, and the
// members of further bases are declared again (C++ upcasts the object), with an asBase property
// for passing the object where a further base is expected. A Swift subclass overrides C++ virtual
// methods: constructing it creates a C++ subclass whose virtual methods call the Swift methods
// (super runs the C++ implementation). Overloads and default arguments become Swift overloads (one
// per overload and argument count, so C++ applies its default arguments), resolved again at run
// time in C++. Calls throw NativeError (noexcept functions, field accessors and constants do not
// throw); enums are RawRepresentable structs, containers arrays, sets, dictionaries and tuples,
// std::function closures, non-const reference parameters inout, operators Swift operators,
// operator[] a subscript and operator() callAsFunction. Doxygen becomes DocC comments.
//
// The output is a package: Package.swift, Sources/MODULE/MODULE.swift (the API),
// Sources/MODULE/Rosetta.swift (the runtime) and Sources/CMODULE (a C module declaring the
// native library's entry points).
#include "rosetta/swift.hpp"
#include "cabi_full.hpp"
#include "swift_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
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

// Swift keywords that need backticks as identifiers.
bool swift_keyword(const std::string& name) {
    static const std::set<std::string> words = {"associatedtype", "class", "deinit", "enum", "extension", "fileprivate", "func", "import",
        "init", "inout", "internal", "let", "operator", "private", "precedencegroup", "protocol", "public", "rethrows", "static", "struct",
        "subscript", "typealias", "var", "break", "case", "catch", "continue", "default", "defer", "do", "else", "fallthrough", "for",
        "guard", "if", "in", "repeat", "return", "throw", "switch", "where", "while", "as", "false", "is", "nil", "self", "Self", "super",
        "throws", "true", "try", "Any", "await", "_"};
    return words.count(name) != 0;
}
// Names that do not work as members even in backticks (x.self, X.Type, x.init).
bool swift_unusable(const std::string& name) {
    static const std::set<std::string> words = {"self", "Self", "Type", "Protocol", "init", "deinit", "subscript", "super", "_"};
    return words.count(name) != 0;
}
// Swift standard library functions: a module function named like one would take over calls
// meant for it (print(4)), so it takes a trailing underscore.
bool swift_function(const std::string& name) {
    static const std::set<std::string> words = {"print", "debugPrint", "dump", "min", "max", "abs", "zip", "swap", "stride", "sequence",
        "repeatElement", "type", "assert", "assertionFailure", "precondition", "preconditionFailure", "fatalError", "readLine",
        "numericCast", "withExtendedLifetime", "withUnsafePointer", "withUnsafeMutablePointer", "withUnsafeBytes",
        "withUnsafeMutableBytes", "isKnownUniquelyReferenced", "unsafeBitCast", "unsafeDowncast", "withoutActuallyEscaping",
        "rosettaDispatch", "rosettaClasses", "rosettaInvoke", "rosettaInvokeHook", "rosettaRetainHook"};
    return words.count(name) != 0;
}
// An identifier as Swift code spells it.
std::string ident(const std::string& name) { return swift_keyword(name) ? "`" + name + "`" : name; }

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
// lowerCamelCase (functions, methods, properties, parameters, enum cases, constants).
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
// UpperCamelCase (types).
std::string pascal(const std::string& name) {
    auto result = camel(name);
    result[0] = upper(result[0]);
    return result;
}
// A Swift string literal.
std::string swift_string(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') { result += "\\n"; continue; }
        result += c;
    }
    return result + "\"";
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

// Names in one Swift scope.
struct Names {
    std::set<std::string> taken;
    bool reserve(const std::string& name) { return taken.insert(name).second; }
    bool free(const std::string& name) const { return !taken.count(name); }
    // The wanted name (with an underscore if Swift does not allow it), else with a number (getName2).
    std::string claim(std::string wanted) {
        if (swift_unusable(wanted)) wanted += "_";
        if (reserve(wanted)) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
    }
    // A type name: the wanted name, else with a trailing underscore (Point_), else a number.
    std::string claim_type(std::string wanted) {
        if (swift_unusable(wanted) || swift_keyword(wanted)) wanted += "_";
        if (reserve(wanted)) return wanted;
        return claim(wanted + "_");
    }
};

// ---------------------------------------------------------------- doc comments
// Markdown lines of a DocC comment: "" is a blank line.
struct Doc {
    std::vector<std::string> lines;
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
        lines.push_back("## " + one_line(trim(text)));
        lines.push_back("");
    }
    void code(const std::string& text) {
        blank();
        lines.push_back("```");
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        lines.insert(lines.end(), body.begin(), body.end());
        lines.push_back("```");
        lines.push_back("");
    }
    // A DocC callout list item ("- Returns: ...").
    void callout(const std::string& text) {
        if (!lines.empty() && !lines.back().empty() && !starts(lines.back(), "- ") && !starts(lines.back(), "  ")) lines.push_back("");
        lines.push_back("- " + text);
    }
    void list(const std::string& title, const std::vector<std::string>& items) {
        if (items.empty()) return;
        blank();
        if (!title.empty()) lines.push_back(title);
        for (const auto& item : items) lines.push_back("- " + item);
        lines.push_back("");
    }
    std::string render(const std::string& indent = {}) const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        std::string out;
        bool previous_blank = true;
        bool fenced = false;
        for (const auto& line : copy) {
            if (line.empty() && previous_blank && !fenced) continue;
            if (starts(line, "```")) fenced = !fenced;
            previous_blank = line.empty();
            out += indent + "///" + (line.empty() ? "" : " " + line) + "\n";
        }
        return out;
    }
};

// C++ documentation text as Markdown DocC reads literally: brackets are no links, < no HTML tag,
// and a line starting with > or # no quote or heading; URLs become autolinks.
std::string md(const std::string& text) {
    std::string out;
    bool line_start = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (text.compare(i, 7, "http://") == 0 || text.compare(i, 8, "https://") == 0) {
            std::size_t end = i;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != ')' && text[end] != '>') ++end;
            while (end > i && (text[end - 1] == '.' || text[end - 1] == ',')) --end;
            out += "<" + text.substr(i, end - i) + ">";
            i = end - 1;
            line_start = false;
            continue;
        }
        if (c == '[' || c == ']' || c == '<' || ((c == '>' || c == '#') && line_start)) out += '\\';
        out += c;
        if (c == '\n') line_start = true;
        else if (c != ' ' && c != '\t') line_start = false;
    }
    return out;
}

std::string error_kind(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "invalidArgument";
    if (bare == "std::out_of_range") return "outOfRange";
    if (bare == "std::domain_error") return "domainError";
    if (bare == "std::length_error") return "lengthError";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "arithmeticError";
    if (bare == "std::bad_alloc") return "allocation";
    return "native";
}

// C++ operators: their group names ("" = not bound) and Swift operators.
struct OperatorName { const char* unary; const char* binary; const char* symbol; };
const std::map<std::string, OperatorName>& operator_table() {
    static const std::map<std::string, OperatorName> table = {
        {"operator+", {"pos", "add", "+"}}, {"operator-", {"neg", "subtract", "-"}}, {"operator*", {nullptr, "multiply", "*"}},
        {"operator/", {nullptr, "divide", "/"}}, {"operator%", {nullptr, "modulo", "%"}}, {"operator==", {nullptr, "equals", "=="}},
        {"operator!=", {nullptr, "notEquals", "!="}}, {"operator<", {nullptr, "less", "<"}}, {"operator<=", {nullptr, "lessEqual", "<="}},
        {"operator>", {nullptr, "greater", ">"}}, {"operator>=", {nullptr, "greaterEqual", ">="}}, {"operator&", {nullptr, "bitAnd", "&"}},
        {"operator|", {nullptr, "bitOr", "|"}}, {"operator^", {nullptr, "bitXor", "^"}}, {"operator~", {"complement", nullptr, "~"}},
        {"operator!", {"not", nullptr, "!"}}, {"operator<<", {nullptr, "shiftLeft", "<<"}}, {"operator>>", {nullptr, "shiftRight", ">>"}},
        {"operator[]", {nullptr, "at", ""}}, {"operator+=", {nullptr, "addAssign", "+="}}, {"operator-=", {nullptr, "subtractAssign", "-="}},
        {"operator*=", {nullptr, "multiplyAssign", "*="}}, {"operator/=", {nullptr, "divideAssign", "/="}}};
    return table;
}
std::string swift_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "callAsFunction";
    const auto found = operator_table().find(name);
    if (found == operator_table().end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.unary : found->second.binary;
    return result ? result : "";
}

// A C++ type in Swift: its spelling, the runtime codec converting it, and whether it is Hashable.
struct ST {
    std::string type = "NativeValue";
    std::string codec = "Rosetta.value";
    bool hashable = false;
    bool function = false;   // a closure type (an @escaping parameter)
};

// One Swift function of a group: an overload called with its first `arity` C++ parameters.
struct Variant {
    const Overload* overload;
    std::size_t arity;
    std::vector<ST> params;      // the Swift parameters (receiver excluded)
    std::vector<std::string> names;
    std::vector<bool> inout;
    ST result;
    bool returns = false;
    std::string key;             // the Swift parameter types
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const SwiftOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedSwiftSource run();

private:
    SwiftOptions options_;
    std::string module_name_, library_, cmodule_;
    Names types_;                                           // the module's types
    std::map<std::string, Names> functions_;                // policy module ("" = main) -> function names
    std::map<std::string, Names> members_;                  // class -> member names (inherited ones included)
    std::map<std::string, std::map<std::string, std::string>> mapped_;  // class -> C++ member key -> Swift name
    std::map<const Group*, std::string> names_;             // group -> Swift name
    std::map<std::string, std::string> field_names_;        // class::field -> property
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::map<std::string, std::string> submodules_;         // policy module -> Swift enum
    std::map<std::string, std::string> links_;              // C++ name -> DocC reference
    std::map<std::string, std::string> secondary_names_;    // class::base -> asBase property
    std::map<std::string, std::vector<Overload>> constructor_overloads_;
    struct Emitted { std::string result; bool throws; };
    std::map<std::string, std::map<std::string, Emitted>> emitted_;   // class -> Swift member key -> what it is (inherited ones included)
    std::map<std::string, std::set<std::string>> init_keys_;          // class -> its initializers' parameter types

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Swift"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return swift_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken = {"self", "rc", "reply", "lhs", "rhs", "operand", "object", "arguments"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : camel(cpp);
            if (swift_unusable(name) || swift_keyword(name)) name += "_";
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
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, plan.name) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: subclass it in Swift and override its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " initializer is protected: subclass it in Swift";
    }
    std::string symbol_prefix() const override { return "rosetta_swift_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_SWIFT"; }
    std::string module_prefix() const override { return "rosetta_swift_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    ST swift_type(const Type& type) const;
    ST swift_spelled(std::string cpp) const;
    std::string swift_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    std::vector<Variant> plan_variants(const std::vector<Overload>& overloads) const;

    // ------------------------------------------------------------ naming and emission
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    bool conflicts(const std::string& owner, const Group& group) const;
    std::string link(const std::string& cpp) const;
    Doc doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Doc& comment, const Documentation& doc) const;
    Doc variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const;
    std::string parameter_list(const Variant& variant, const std::string& first = {}) const;
    void emit_body(std::ostream& out, const std::string& indent, const Variant& variant, std::size_t index, const std::string& receiver,
                   bool throws) const;
    void emit_function(std::ostream& out, const Group& group, const std::string& indent, const std::string& modifiers) const;
    void emit_members(std::ostream& out, const std::string& owner, const std::string& from, bool copied);
    void emit_class(std::ostream& out, const std::string& owner);
    void emit_enum(std::ostream& out, const std::string& qualified) const;
    std::string dispatch();
    std::string file_doc() const;
};

// ---------------------------------------------------------------- types
ST Generator::swift_type(const Type& type) const {
    const auto& target = referent(type);
    const auto optional = [](const ST& inner) {
        ST result;
        result.type = (inner.function ? "(" + inner.type + ")" : inner.type) + "?";
        result.codec = "Rosetta.optional(" + inner.codec + ")";
        result.hashable = inner.hashable;
        return result;
    };
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return {"String", "Rosetta.string", true};
        if (is_void(pointee)) return {};
        return optional(swift_type(pointee));
    }
    if (is_void(target)) return {"Void", "Rosetta.void", false};
    if (!conversion(target).empty()) return swift_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        const auto spelled = normalize(strip_cv(target.spelling));
        if (spelled == "size_t" || spelled == "std::size_t" || spelled == "ssize_t" || spelled == "ptrdiff_t" || spelled == "std::ptrdiff_t")
            return {"Int", "Rosetta.int", true};
        static const std::map<std::string, std::string> fixed = {{"int8_t", "Int8"}, {"int16_t", "Int16"}, {"int32_t", "Int32"},
            {"int64_t", "Int64"}, {"uint8_t", "UInt8"}, {"uint16_t", "UInt16"}, {"uint32_t", "UInt32"}, {"uint64_t", "UInt64"}};
        const auto width = fixed.find(starts(spelled, "std::") ? spelled.substr(5) : spelled);
        if (width != fixed.end()) {
            auto codec = width->second;
            for (auto& c : codec) c = lower(c);
            return {width->second, "Rosetta." + codec, true};
        }
        static const std::map<std::string, std::pair<std::string, std::string>> builtins = {{"bool", {"Bool", "bool"}},
            {"char", {"Character", "character"}}, {"signed char", {"Int8", "int8"}}, {"unsigned char", {"UInt8", "uint8"}},
            {"short", {"Int16", "int16"}}, {"unsigned short", {"UInt16", "uint16"}}, {"int", {"Int32", "int32"}},
            {"unsigned int", {"UInt32", "uint32"}}, {"long", {"Int", "int"}}, {"unsigned long", {"UInt", "uint"}},
            {"long long", {"Int64", "int64"}}, {"unsigned long long", {"UInt64", "uint64"}}, {"float", {"Float", "float"}},
            {"double", {"Double", "double"}}, {"long double", {"Double", "double"}}, {"char16_t", {"UInt16", "uint16"}},
            {"char32_t", {"UInt32", "uint32"}}, {"wchar_t", {"Int32", "int32"}}};
        const auto found = builtins.find(bare);
        if (found == builtins.end()) return {};
        return {found->second.first, "Rosetta." + found->second.second, true};
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return {"Int64", "Rosetta.int64", true};
        return {found->second, "Rosetta.enumeration(" + found->second + ".self)", true};
    }
    if (bound(target)) {
        const auto& name = classes_.at(target.declaration).name;
        return {name, "Rosetta.object(" + name + ".self)", false};
    }
    if (bare == "std::filesystem::path") return {"String", "Rosetta.string", true};
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {"Void", "Rosetta.void", false};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? swift_type(target.arguments[i]) : ST{}; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return {"String", "Rosetta.string", true};
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span") {
            const auto item = argument(0);
            return {"[" + item.type + "]", "Rosetta.array(" + item.codec + ")", item.hashable};
        }
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset" || declaration == "std::unordered_multiset") {
            const auto item = argument(0);
            if (item.hashable && declaration.find("multi") == std::string::npos)
                return {"Set<" + item.type + ">", "Rosetta.set(" + item.codec + ")", true};
            return {"[" + item.type + "]", "Rosetta.array(" + item.codec + ")", item.hashable};
        }
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" || declaration == "std::unordered_multimap") {
            const auto key = argument(0), item = argument(1);
            if (key.hashable && declaration.find("multi") == std::string::npos)
                return {"[" + key.type + ": " + item.type + "]", "Rosetta.dictionary(" + key.codec + ", " + item.codec + ")", item.hashable};
            return {"[(" + key.type + ", " + item.type + ")]", "Rosetta.pairs(" + key.codec + ", " + item.codec + ")", false};
        }
        if (declaration == "std::optional") return target.arguments.empty() ? ST{} : optional(argument(0));
        if (declaration == "std::complex") return {"Complex", "Rosetta.complex", true};
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return target.arguments.empty() ? ST{} : argument(0);
        if ((declaration == "std::pair" || declaration == "std::tuple") && target.arguments.size() >= 2 && target.arguments.size() <= 6) {
            std::vector<std::string> types, codecs;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) {
                const auto item = argument(i);
                types.push_back(item.type);
                codecs.push_back(item.codec);
            }
            return {"(" + join(types, ", ") + ")", "Rosetta.tuple(" + join(codecs, ", ") + ")", false};
        }
        if (declaration == "std::tuple" && target.arguments.empty()) return {"Void", "Rosetta.void", false};
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            if (function.arguments.size() > 5) return {};
            std::vector<std::string> types, codecs;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) {
                const auto item = swift_type(function.arguments[i]);
                types.push_back(item.type);
                codecs.push_back(item.codec);
            }
            const auto result = function.arguments.empty() ? ST{"Void", "Rosetta.void"} : swift_type(function.arguments[0]);
            codecs.push_back(result.codec);
            ST closure{"(" + join(types, ", ") + ") throws -> " + result.type, "Rosetta.function(" + join(codecs, ", ") + ")", false};
            closure.function = true;
            return closure;
        }
        if (declaration == "std::chrono::duration") return {"Double", "Rosetta.double", true};
    }
    return {};
}

// A type a policy conversion spells in C++.
ST Generator::swift_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::map<std::string, std::pair<std::string, std::string>> scalars = {{"bool", {"Bool", "bool"}},
        {"char", {"Character", "character"}}, {"signed char", {"Int8", "int8"}}, {"unsigned char", {"UInt8", "uint8"}},
        {"short", {"Int16", "int16"}}, {"unsigned short", {"UInt16", "uint16"}}, {"int", {"Int32", "int32"}}, {"unsigned int", {"UInt32", "uint32"}},
        {"long", {"Int", "int"}}, {"unsigned long", {"UInt", "uint"}}, {"long long", {"Int64", "int64"}},
        {"unsigned long long", {"UInt64", "uint64"}}, {"float", {"Float", "float"}}, {"double", {"Double", "double"}},
        {"long double", {"Double", "double"}}, {"std::size_t", {"Int", "int"}}, {"size_t", {"Int", "int"}}, {"std::int64_t", {"Int64", "int64"}},
        {"std::uint64_t", {"UInt64", "uint64"}}, {"std::int32_t", {"Int32", "int32"}}, {"std::uint32_t", {"UInt32", "uint32"}},
        {"std::int16_t", {"Int16", "int16"}}, {"std::uint16_t", {"UInt16", "uint16"}}, {"std::int8_t", {"Int8", "int8"}},
        {"std::uint8_t", {"UInt8", "uint8"}}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return {found->second.first, "Rosetta." + found->second.second, true};
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return {"String", "Rosetta.string", true};
    for (const auto& [qualified, plan] : classes_)
        if (normalize(qualified) == cpp) return {plan.name, "Rosetta.object(" + plan.name + ".self)", false};
    for (const auto& [qualified, name] : enum_names_)
        if (normalize(qualified) == cpp) return {name, "Rosetta.enumeration(" + name + ".self)", true};
    return {};
}

std::string Generator::swift_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "nil";
    if (value.kind == "char") return value.value.size() == 1 ? swift_string(value.value) : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos) return text;
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        const auto found = enum_names_.find(qualified);
        if (found == enum_names_.end() || !enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return found->second + "." + enum_cases_.at(qualified)[i];
    }
    return {};
}

std::string Generator::overload_signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& type = function.parameters[i];
        auto text = "_ " + names[i - first] + ": " + std::string(writeback(type) ? "inout " : "") + swift_type(type).type;
        if (i >= function.parameters.size() - function.default_arguments) {
            const auto shown = swift_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, type);
            text += " = " + (shown.empty() ? std::string("<C++ default>") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + "(" + join(parameters, ", ") + ")";
    if (!function.is_constructor && !is_void(function.result)) result += " -> " + swift_type(function.result).type;
    return result;
}

// The Swift functions of a group of overloads: one per overload and argument count (default
// arguments), except those whose Swift parameter types an earlier one has (C++ resolves the call
// again at run time anyway).
std::vector<Variant> Generator::plan_variants(const std::vector<Overload>& overloads) const {
    std::vector<Variant> result;
    std::set<std::string> keys;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto names = parameter_names(overload);
        const std::size_t total = function.parameters.size();
        const std::size_t required = total - std::min(function.default_arguments, total - first);
        for (std::size_t arity = required; arity <= total; ++arity) {
            Variant variant;
            variant.overload = &overload;
            variant.arity = arity;
            for (std::size_t i = first; i < arity; ++i) {
                const auto& type = function.parameters[i];
                variant.params.push_back(swift_type(type));
                variant.names.push_back(names[i - first]);
                variant.inout.push_back(writeback(type) && !function.is_constructor);
                variant.key += (i > first ? ", " : "") + std::string(variant.inout.back() ? "inout " : "") + variant.params.back().type;
            }
            variant.returns = !function.is_constructor && !is_void(function.result);
            variant.result = variant.returns ? swift_type(function.result) : ST{"Void", "Rosetta.void"};
            if (keys.insert(variant.key).second) result.push_back(std::move(variant));
        }
    }
    return result;
}

// ---------------------------------------------------------------- naming
// Whether a method of `owner` has the Swift parameters of a base class's method of the same C++
// name but another result: Swift could neither override nor overload it, so it takes its own name.
bool Generator::conflicts(const std::string& owner, const Group& group) const {
    std::map<std::string, std::string> mine;
    for (const auto& variant : plan_variants(group.overloads)) mine[variant.key] = variant.result.type;
    std::set<std::string> seen;
    std::function<bool(const std::string&)> walk = [&](const std::string& cls) {
        if (!seen.insert(cls).second) return false;
        for (const auto& base : classes_.at(cls).bases) {
            for (const auto& other : classes_.at(base).methods) {
                if (other.name != group.name || other.is_static != group.is_static) continue;
                for (const auto& variant : plan_variants(other.overloads)) {
                    const auto found = mine.find(variant.key);
                    if (found != mine.end() && found->second != variant.result.type) return true;
                }
            }
            if (walk(base)) return true;
        }
        return false;
    };
    return walk(owner);
}

void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        auto& mapped = mapped_[owner];
        // A C++ name a base class binds keeps its Swift name (an override, an overload, or a
        // further base's member declared again).
        for (const auto& base : cls.bases) {
            if (!members_.count(base)) continue;
            for (const auto& name : members_.at(base).taken) members.reserve(name);
            for (const auto& [key, name] : mapped_.at(base)) mapped.emplace(key, name);
        }
        for (const char* reserved : {"close", "isClosed", "cast", "description", "rosettaHandle", "init", "deinit", "self", "Type", "Protocol",
                                     "subscript", "super", "dynamicType"})
            members.reserve(reserved);
        const bool constructible = !cls.record->abstract;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) constructor_overloads_[owner].push_back(overload);
        const auto name_member = [&](const std::string& key, const std::string& wanted) {
            const auto found = mapped.find(key);
            if (found != mapped.end()) return found->second;
            const auto name = members.claim(wanted);
            mapped[key] = name;
            return name;
        };
        for (auto& group : cls.methods) {
            const auto key = "m:" + group.name;
            if (mapped.count(key) && conflicts(owner, group)) {
                names_[&group] = members.claim(camel(group.name));
                mapped[key] = names_[&group];
            } else {
                names_[&group] = name_member(key, camel(group.name));
            }
        }
        for (auto& group : cls.operators) {
            if (group.name == "at" || group.name == "callAsFunction") names_[&group] = name_member("o:" + group.name, group.name);
            else names_[&group] = group.name;
        }
        for (const auto& field : cls.fields) field_names_[owner + "::" + field.field->name] = name_member("f:" + field.field->name, camel(field.name));
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(camel(constant.name));
        // Further bases are reached through asBase.
        for (std::size_t i = 1; i < cls.bases.size(); ++i)
            secondary_names_[owner + "::" + cls.bases[i]] = name_member("a:" + cls.bases[i], "as" + classes_.at(cls.bases[i]).name);
    }
    for (auto& group : functions) {
        auto& scope = functions_[group.module];
        auto wanted = camel(group.name);
        if (group.module.empty() && swift_function(wanted)) wanted += "_";
        names_[&group] = scope.claim(wanted);
    }
    for (auto& constant : constants) constant_names_[&constant] = functions_[""].claim(camel(constant.name));
    // DocC references.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        links_[owner] = "``" + cls.name + "``";
        links_[cls.record->name] = links_[owner];
        for (const auto& group : cls.methods) {
            links_[owner + "::" + group.name] = "``" + cls.name + "/" + names_.at(&group) + "``";
            links_[cls.record->name + "::" + group.name] = links_[owner + "::" + group.name];
        }
    }
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = "``" + names_.at(&group) + "``";
            links_.emplace(overload.function->name, "``" + names_.at(&group) + "``");
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = "``" + name + "``";
        links_.emplace(enums_.at(qualified)->name, links_[qualified]);
    }
}

// ---------------------------------------------------------------- documentation
std::string Generator::link(const std::string& cpp) const {
    const auto found = links_.find(normalize(cpp));
    return found != links_.end() ? found->second : "`" + cpp + "`";
}
void Generator::add_notes(Doc& comment, const Documentation& doc) const {
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { comment.paragraph("**Deprecated:** " + md(one_line(item.text))); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        comment.paragraph("**" + label + ":** " + md(one_line(item.text)));
    }
    if (!doc.see.empty()) {
        std::string see = "See also ";
        for (std::size_t i = 0; i < doc.see.size(); ++i)
            see += (i == 0 ? "" : i + 1 == doc.see.size() ? " and " : ", ") + link(doc.see[i]);
        comment.paragraph(see + ".");
    }
}
// The brief (or `lead` without one), then the details.
Doc Generator::doc_of(const Documentation& doc, const std::string& lead) const {
    Doc comment;
    auto details = doc.details;
    auto brief = doc.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    comment.paragraph(brief.empty() ? lead : md(brief));
    if (!brief.empty() && !lead.empty()) comment.paragraph(lead);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") comment.code(block.text);
        else if (block.kind == "heading") comment.heading(md(block.text));
        else comment.paragraph(md(block.text));
    }
    return comment;
}

Doc Generator::variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const auto& doc = function.documentation;
    auto comment = doc_of(doc, "Calls " + what + ".");
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> omitted;
    for (std::size_t i = variant.arity; i < function.parameters.size(); ++i) {
        if (i < first) continue;
        const auto shown = swift_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        const auto cpp = i < function.default_values.size() ? function.default_values[i].value : std::string();
        omitted.push_back("`" + names[i - first] + " = " + (shown.empty() ? cpp.empty() ? std::string("C++ default") : cpp : shown) + "`");
    }
    if (!omitted.empty()) {
        std::size_t longer = 0;
        for (const auto& sibling : siblings) if (sibling.overload == variant.overload && sibling.arity > variant.arity) ++longer;
        comment.paragraph("Leaves out the C++ default arguments " + join(omitted, ", ") + (longer ? " (other overloads pass them)." : "."));
    }
    std::vector<std::string> parameters;
    for (std::size_t i = first; i < variant.arity; ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : doc.params) if (item.name == cpp && !item.text.empty()) text = md(one_line(item.text));
        if (variant.inout[i - first]) text += std::string(text.empty() ? "" : " ") + "(C++ writes it back)";
        parameters.push_back(names[i - first] + ": " + (text.empty() ? "`" + variant.params[i - first].type + "`" : text));
    }
    if (!parameters.empty()) {
        if (!comment.lines.empty() && !comment.lines.back().empty()) comment.lines.push_back("");
        comment.lines.push_back("- Parameters:");
        for (const auto& item : parameters) comment.lines.push_back("  - " + item);
    }
    if (variant.returns && !doc.returns.empty()) comment.callout("Returns: " + md(one_line(doc.returns)));
    std::vector<std::string> errors;
    for (const auto& item : doc.throws)
        errors.push_back("``NativeError`` of kind `." + error_kind(item.name) + "`" + (item.name.empty() ? std::string() : " (C++ `" + item.name + "`)") +
                         (item.text.empty() ? std::string() : ": " + md(one_line(item.text))));
    if (!errors.empty()) comment.callout("Throws: " + join(errors, "; "));
    add_notes(comment, doc);
    return comment;
}

// ---------------------------------------------------------------- emission
std::string Generator::parameter_list(const Variant& variant, const std::string& first) const {
    std::vector<std::string> items;
    if (!first.empty()) items.push_back(first);
    for (std::size_t i = 0; i < variant.params.size(); ++i)
        items.push_back("_ " + variant.names[i] + ": " + (variant.inout[i] ? "inout " : variant.params[i].function ? "@escaping " : "") +
                        variant.params[i].type);
    return join(items, ", ");
}

// The body of a Swift function calling native function `index` (with `receiver` first, unless empty).
void Generator::emit_body(std::ostream& out, const std::string& indent, const Variant& variant, std::size_t index, const std::string& receiver,
                          bool throws) const {
    std::ostringstream body;
    const std::string in = throws ? indent : indent + "    ";
    const bool bare = receiver.empty() && variant.params.empty();
    body << in << (bare ? "let" : "var") << " rc = Rosetta.Call(" << index << ")\n";
    if (!receiver.empty()) body << in << "rc.object(" << receiver << ")\n";
    std::vector<std::string> writebacks;
    for (std::size_t i = 0; i < variant.params.size(); ++i) {
        const auto& name = variant.names[i];
        const auto& codec = variant.params[i].codec;
        if (variant.inout[i]) {
            body << in << "let slot" << i << " = try rc.slot(" << name << ", " << codec << ")\n";
            writebacks.push_back(in + "try reply.writeBack(slot" + std::to_string(i) + ", &" + name + ", " + codec + ")\n");
        } else {
            body << in << "try rc.arg(" << name << ", " << codec << ")\n";
        }
    }
    if (writebacks.empty()) {
        if (variant.returns) body << in << "return try rc.run().value(" << variant.result.codec << ")\n";
        else body << in << "_ = try rc.run()\n";
    } else {
        body << in << "let reply = try rc.run()\n";
        for (const auto& line : writebacks) body << line;
        if (variant.returns) body << in << "return try reply.value(" << variant.result.codec << ")\n";
    }
    if (throws) out << body.str();
    else out << indent << (variant.returns ? "return " : "") << "Rosetta.must {\n" << body.str() << indent << "}\n";
}

// A module function (or a function of a policy module).
void Generator::emit_function(std::ostream& out, const Group& group, const std::string& indent, const std::string& modifiers) const {
    const auto variants = plan_variants(group.overloads);
    const auto& name = names_.at(&group);
    for (const auto& variant : variants) {
        const auto& function = *variant.overload->function;
        const bool throws = !function.is_noexcept;
        out << "\n" << variant_doc(variant, variants, "C++ `" + function.qualified_name() + "`").render(indent)
            << indent << modifiers << "func " << ident(name) << "(" << parameter_list(variant) << ")" << (throws ? " throws" : "")
            << (variant.returns ? " -> " + variant.result.type : std::string()) << " {\n";
        emit_body(out, indent + "    ", variant, group.index, "", throws);
        out << indent << "}\n";
    }
}

// The methods, operators and fields of class `from`, in class `owner`: its own members, or
// (`copied`) those of a further base class declared again.
void Generator::emit_members(std::ostream& out, const std::string& owner, const std::string& from, bool copied) {
    const auto& plan = classes_.at(owner);
    const auto& source = classes_.at(from);
    const bool open = plan.director && !plan.record->is_final;
    auto& emitted = emitted_[owner];
    const auto inherited = [&](const std::string& key) -> const Emitted* {
        const auto found = emitted.find(key);
        return found == emitted.end() ? nullptr : &found->second;
    };
    const auto self_name = camel(plan.name);
    for (const auto& group : source.methods) {
        if (copied && group.is_static) continue;
        const auto& name = names_.at(&group);
        const auto variants = plan_variants(group.overloads);
        for (const auto& variant : variants) {
            const auto& function = *variant.overload->function;
            const auto key = std::string(group.is_static ? "static " : "") + name + "(" + variant.key + ")";
            const auto* previous = inherited(key);
            if (previous && (copied || previous->result != variant.result.type)) continue;
            bool throws = !function.is_noexcept;
            if (previous && !previous->throws) throws = false;
            const bool virtual_method = !group.is_static && function.is_virtual && !variant.overload->extension;
            std::string what;
            if (variant.overload->extension) what = "C++ `" + function.qualified_name() + "`";
            else what = "the C++ method `" + source.record->qualified_name() + "::" + function.name + "`";
            auto comment = variant_doc(variant, variants, what);
            if (virtual_method && open && !copied)
                comment.paragraph(std::string(function.is_pure ? "Pure virtual in C++: a" : "A") + " Swift subclass overrides it, and C++ calls the override.");
            std::string modifiers = virtual_method && open ? "open " : "public ";
            if (previous) modifiers += "override ";
            if (group.is_static) modifiers += plan.record->is_final ? "static " : "class ";
            out << "\n" << comment.render("    ") << "    " << modifiers << "func " << ident(name) << "(" << parameter_list(variant) << ")"
                << (throws ? " throws" : "") << (variant.returns ? " -> " + variant.result.type : std::string()) << " {\n";
            emit_body(out, "        ", variant, group.index, group.is_static ? "" : "self", throws);
            out << "    }\n";
            emitted[key] = {variant.result.type, throws};
        }
        // to_string() is the description.
        if (!copied && !group.is_static && group.name == "to_string")
            for (const auto& variant : variants)
                if (variant.params.empty() && variant.result.type == "String") {
                    out << "\n    /// The C++ `to_string()`.\n    open override var description: String { (try? " << ident(name) << "()) ?? super.description }\n";
                    break;
                }
    }
    for (const auto& group : source.operators) {
        if (copied) continue;
        const auto variants = plan_variants(group.overloads);
        const auto& cpp = group.overloads.front().function->name;
        for (const auto& variant : variants) {
            const auto& function = *variant.overload->function;
            const bool throws = !function.is_noexcept;
            const auto what = "the C++ `" + source.record->qualified_name() + "::" + cpp + "`";
            if (group.name == "at" || group.name == "callAsFunction") {
                const auto& name = names_.at(&group);
                const auto key = name + "(" + variant.key + ")";
                if (inherited(key)) continue;
                out << "\n" << variant_doc(variant, variants, what).render("    ") << "    public func " << name << "(" << parameter_list(variant) << ")"
                    << (throws ? " throws" : "") << (variant.returns ? " -> " + variant.result.type : std::string()) << " {\n";
                emit_body(out, "        ", variant, group.index, "self", throws);
                out << "    }\n";
                emitted[key] = {variant.result.type, throws};
                if (group.name == "at" && variant.returns && variant.params.size() == 1 && !variant.inout[0]) {
                    out << "\n    /// " << "`" << ident(name) << "(_:)` as a subscript.\n    public subscript(_ " << variant.names[0] << ": "
                        << variant.params[0].type << ") -> " << variant.result.type << " {\n        get" << (throws ? " throws" : "") << " { "
                        << (throws ? "try " : "") << name << "(" << variant.names[0] << ") }\n    }\n";
                }
                continue;
            }
            const auto found = operator_table().find(cpp);
            if (found == operator_table().end() || group.operand != 0) continue;
            const std::string symbol = found->second.symbol;
            const bool unary = variant.params.empty();
            if (variant.params.size() > 1 || (variant.params.size() == 1 && variant.inout[0])) continue;
            const bool compound = group.name == "addAssign" || group.name == "subtractAssign" || group.name == "multiplyAssign" || group.name == "divideAssign";
            const auto key = std::string("operator ") + symbol + "(" + variant.key + ")";
            if (inherited(key)) continue;
            auto comment = variant_doc(variant, variants, what);
            out << "\n" << comment.render("    ");
            if (compound && variant.returns) out << "    @discardableResult\n";
            Variant renamed = variant;
            if (!unary) renamed.names[0] = "rhs";
            out << "    public static " << (unary ? "prefix " : "") << "func " << symbol << " ("
                << (unary ? "operand: " + plan.name : "lhs: " + plan.name + ", rhs: " + variant.params[0].type) << ")" << (throws ? " throws" : "")
                << (variant.returns ? " -> " + variant.result.type : std::string()) << " {\n";
            emit_body(out, "        ", renamed, group.index, unary ? "operand" : "lhs", throws);
            out << "    }\n";
            emitted[key] = {variant.result.type, throws};
        }
    }
    // Fields are properties.
    for (const auto& field : source.fields) {
        const auto& f = *field.field;
        const auto& name = field_names_.at(from + "::" + f.name);
        const auto key = "var " + name;
        if (inherited(key)) continue;
        const bool writable = !f.type.is_const && !readonly(from + "::" + f.name, from) && f.type.kind != TypeKind::lvalue_reference;
        const auto type = swift_type(f.type);
        const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        out << "\n    /// " << (about.empty() ? "The C++ field `" + from + "::" + f.name + "`" : md(about)) << (writable ? "" : " (read-only)") << "\n"
            << "    public var " << ident(name) << ": " << type.type << " {\n"
            << "        get { Rosetta.must { var rc = Rosetta.Call(" << field.getter_index << "); rc.object(self); return try rc.run().value("
            << type.codec << ") } }\n";
        if (writable)
            out << "        set { Rosetta.must { var rc = Rosetta.Call(" << field.setter_index << "); rc.object(self); try rc.arg(newValue, " << type.codec
                << "); _ = try rc.run() } }\n";
        out << "    }\n";
        emitted[key] = {type.type, false};
    }
    (void)self_name;
}

void Generator::emit_class(std::ostream& out, const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto primary = plan.bases.empty() ? std::string() : plan.bases.front();
    const auto superclass = primary.empty() ? std::string("NativeObject") : classes_.at(primary).name;
    if (!primary.empty()) emitted_[owner] = emitted_[primary];
    auto comment = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    if (plan.bases.size() > 1) {
        std::vector<std::string> further;
        for (std::size_t i = 1; i < plan.bases.size(); ++i)
            further.push_back("``" + classes_.at(plan.bases[i]).name + "`` (``" + secondary_names_.at(owner + "::" + plan.bases[i]) + "``)");
        comment.paragraph("Its further C++ base classes' members are declared again here; pass it where one of them is expected through "
                          "its `as` property: " + join(further, ", ") + ".");
    }
    if (plan.director && !record.is_final)
        comment.paragraph(std::string(record.abstract ? "Abstract in C++: subclass" : "Subclass") +
                          " it in Swift to override its virtual methods: the object of a Swift subclass is a C++ subclass whose virtual methods "
                          "call the Swift overrides, and `super` runs the C++ implementation.");
    else if (record.abstract) comment.paragraph("Abstract in C++: C++ functions return objects of derived classes.");
    add_notes(comment, record.documentation);
    const std::string access = record.is_final ? "public final" : "open";
    out << "\n" << comment.render() << access << " class " << plan.name << ": " << superclass << " {\n"
        << "    override init(rosettaHandle: Rosetta.Handle) { super.init(rosettaHandle: rosettaHandle) }\n";
    // Initializers.
    const auto& base_inits = primary.empty() ? std::set<std::string>() : init_keys_[primary];
    if (constructor_overloads_.count(owner)) {
        const auto& overloads = constructor_overloads_.at(owner);
        const auto variants = plan_variants(overloads);
        for (const auto& variant : variants) {
            auto about = variant_doc(variant, variants, "the C++ constructor of `" + owner + "`");
            if (plan.director && !record.is_final)
                about.paragraph("The object of a Swift subclass is a C++ subclass whose virtual methods call the Swift overrides.");
            out << "\n" << about.render("    ") << "    " << (base_inits.count(variant.key) ? "override " : "") << "public init("
                << parameter_list(variant) << ") throws {\n        super.init(rosettaHandle: Rosetta.Handle())\n"
                << "        try Rosetta.construct(self, " << plan.constructor_index << ", director: "
                << (plan.director && !record.is_final ? "type(of: self) != " + plan.name + ".self" : std::string("false")) << ")";
            if (variant.params.empty()) out << " { _ in }\n    }\n";
            else {
                out << " { rc in\n";
                for (std::size_t i = 0; i < variant.params.size(); ++i)
                    out << "            try rc.arg(" << variant.names[i] << ", " << variant.params[i].codec << ")\n";
                out << "        }\n    }\n";
            }
            init_keys_[owner].insert(variant.key);
        }
    }
    emit_members(out, owner, owner, false);
    // Further bases: their members again, and asBase.
    std::set<std::string> primary_chain;
    std::function<void(const std::string&)> collect = [&](const std::string& cls) {
        if (!primary_chain.insert(cls).second) return;
        for (const auto& base : classes_.at(cls).bases) collect(base);
    };
    if (!primary.empty()) collect(primary);
    std::set<std::string> done = primary_chain;
    std::function<void(const std::string&)> copy = [&](const std::string& cls) {
        if (!done.insert(cls).second) return;
        emit_members(out, owner, cls, true);
        for (const auto& base : classes_.at(cls).bases) copy(base);
    };
    for (std::size_t i = 1; i < plan.bases.size(); ++i) {
        const auto& base = plan.bases[i];
        const auto& name = classes_.at(base).name;
        out << "\n    /// This object as its further C++ base class ``" << name << "``.\n    public var "
            << secondary_names_.at(owner + "::" + base) << ": " << name << " { cast(" << name << ".self)! }\n";
        copy(base);
    }
    for (const auto& constant : plan.constants) {
        const auto type = swift_type(*constant.type);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        out << "\n" << about.render("    ") << "    public static var " << ident(constant_names_.at(&constant)) << ": " << type.type
            << " { Rosetta.must { try Rosetta.Call(" << constant.index << ").run().value(" << type.codec << ") } }\n";
    }
    out << "}\n";
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto comment = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    comment.paragraph("Its enumerators are static properties (`" + name + "." + (cases.empty() ? std::string("x") : cases.front()) +
                      "`); other values of the C++ type are `" + name + "(rawValue:)`.");
    add_notes(comment, enumeration.documentation);
    out << "\n" << comment.render() << "public struct " << name << ": RawRepresentable, Hashable, Sendable, CustomStringConvertible {\n"
        << "    public let rawValue: Int64\n    public init(rawValue: Int64) { self.rawValue = rawValue }\n";
    std::vector<std::string> values;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        auto value = enumeration.enumerator_values[i];
        if (!value.empty() && value[0] != '-' && value.size() >= 19) {
            const auto number = std::stoull(value);
            if (number > 9223372036854775807ull) value = "Int64(bitPattern: " + value + ")";
        }
        if (value == "-9223372036854775808") value = "Int64.min";
        values.push_back(value);
        out << "    /// C++ `" << qualified << "::" << enumeration.enumerators[i] << "`.\n    public static let " << ident(cases[i]) << " = " << name
            << "(rawValue: " << value << ")\n";
    }
    out << "    /// The enumerator's name, else the value.\n    public var description: String {\n        switch self {\n";
    std::set<std::string> seen;
    for (std::size_t i = 0; i < cases.size(); ++i)
        if (seen.insert(values[i]).second) out << "        case ." << ident(cases[i]) << ": return " << swift_string(cases[i]) << "\n";
    out << "        default: return String(rawValue)\n        }\n    }\n}\n";
}

// The function C++ calls a director's virtual methods through: the Swift method of the object.
std::string Generator::dispatch() {
    std::ostringstream out;
    out << "\n// The Swift methods C++ calls for the virtual methods of Swift subclasses.\n"
        << "func rosettaDispatch(_ object: NativeObject, _ generic: UInt32, _ arguments: [NativeValue]) throws -> NativeValue {\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director || plan.record->is_final) continue;
        for (const auto& item : overridable(plan)) {
            const auto generic = generic_index_.find(item.name);
            const auto name = mapped_.at(owner).find("m:" + item.name);
            if (generic == generic_index_.end() || name == mapped_.at(owner).end()) continue;
            const auto& method = *item.method;
            std::vector<std::string> types, arguments;
            std::ostringstream conversions;
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                const auto& type = method.parameters[i];
                const auto st = swift_type(type);
                const bool inout = writeback(type);
                types.push_back(std::string(inout ? "inout " : "") + st.type);
                conversions << "        " << (inout ? "var" : "let") << " a" << i << " = try " << st.codec << ".from(arguments[" << i << "])\n";
                arguments.push_back((inout ? "&a" : "a") + std::to_string(i));
            }
            const auto key = name->second + "(" + join(types, ", ") + ")";
            if (!emitted_[owner].count(key)) continue;
            const bool returns = !is_void(method.result);
            out << "    if generic == " << generic->second << ", arguments.count == " << method.parameters.size() << ", let object = object as? " << plan.name
                << " {\n" << conversions.str();
            if (returns) out << "        return try " << swift_type(method.result).codec << ".to(try object." << ident(name->second) << "(" << join(arguments, ", ") << "))\n";
            else out << "        try object." << ident(name->second) << "(" << join(arguments, ", ") << ")\n        return .null\n";
            out << "    }\n";
        }
    }
    out << "    throw NativeError(.binding, \"no Swift method for C++ virtual method \\(generic) of \\(object)\")\n}\n";
    return out.str();
}

std::string Generator::file_doc() const {
    std::ostringstream doc;
    doc << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n//\n"
        << "// The Swift interface to the C++ library " << name_ << ". Each C++ class is a class over NativeObject\n"
        << "// (Circle(2)), each module function a function, each C++ constant a property. Overloaded C++\n"
        << "// functions are Swift overloads, one per argument count for default arguments, which C++ resolves\n"
        << "// again at run time. Calls throw NativeError for C++ exceptions and values C++ does not accept.\n";
    return doc.str();
}

GeneratedSwiftSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    module_name_ = options_.module.empty() ? swift_module_name(name_) : options_.module;
    if (!identifier(module_name_) || swift_keyword(module_name_) || swift_unusable(module_name_) || module_name_ == "Swift")
        throw BindingError("Swift module name must be an identifier and not a Swift keyword: " + module_name_);
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Swift library name must be a plain file name: " + library_);
    cmodule_ = "C" + module_name_;
    GeneratedSwiftSource result;
    prepare();
    // The module's types: the runtime's and the standard library's names first.
    for (const char* reserved : {"NativeObject", "NativeError", "NativeValue", "NativeFunction", "Complex", "Rosetta", "Swift", "Any", "AnyObject",
                                 "Array", "Bool", "Character", "Dictionary", "Double", "Error", "Float", "Float16", "Float80", "Int", "Int8", "Int16",
                                 "Int32", "Int64", "UInt", "UInt8", "UInt16", "UInt32", "UInt64", "Never", "Optional", "Result", "Set", "String",
                                 "Substring", "Void", "Sequence", "Collection", "Hashable", "Equatable", "Comparable", "Codable", "Encodable",
                                 "Decodable", "Identifiable", "CustomStringConvertible", "RawRepresentable", "Sendable", "Unicode", "Range",
                                 "ClosedRange", "ObjectIdentifier", "Type", "Protocol", "Self", "UnsafeRawPointer", "UnsafeMutablePointer",
                                 "UnsafePointer", "AnyClass", "Glibc", "Darwin", "Musl", "Foundation"})
        types_.reserve(reserved);
    types_.reserve(module_name_);
    types_.reserve(cmodule_);
    const auto type_name = [&](const std::string& wanted) { return types_.claim_type(pascal(wanted)); };
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
        for (const char* reserved : {"rawValue", "description", "hashValue", "hash", "init", "self", "Type", "Protocol", "allCases"}) scope.reserve(reserved);
        for (const auto& enumerator : enumeration.enumerators)
            enum_cases_[qualified].push_back(scope.claim(camel(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Policy modules are caseless enums (a.b nests).
    for (const auto& group : functions) {
        if (group.module.empty() || submodules_.count(group.module)) continue;
        const auto dot = group.module.find('.');
        const auto top = pascal(group.module.substr(0, dot));
        std::string name;
        for (const auto& [module, enumeration] : submodules_)
            if (module.substr(0, module.find('.')) == group.module.substr(0, dot)) name = enumeration.substr(0, enumeration.find('.'));
        if (name.empty()) name = types_.claim_type(top);
        std::size_t start = dot;
        while (start != std::string::npos) {
            const auto next = group.module.find('.', start + 1);
            name += "." + pascal(group.module.substr(start + 1, next == std::string::npos ? std::string::npos : next - start - 1));
            start = next;
        }
        submodules_[group.module] = name;
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    for (const auto& name : types_.taken) functions_[""].reserve(name);
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Swift module " + module_name_ + " (" + name_ + ").");

    // ------------------------------------------------------------ the API
    std::ostringstream swift;
    swift << file_doc();
    for (const auto& [qualified, name] : enum_names_) emit_enum(swift, qualified);
    for (const auto& owner : class_order_) emit_class(swift, owner);
    for (const auto& group : functions)
        if (group.module.empty()) emit_function(swift, group, "", "public ");
    for (const auto& constant : constants) {
        const auto type = swift_type(*constant.type);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
        swift << "\n" << about.render() << "public var " << ident(constant_names_.at(&constant)) << ": " << type.type
              << " { Rosetta.must { try Rosetta.Call(" << constant.index << ").run().value(" << type.codec << ") } }\n";
    }
    // Policy modules: nested caseless enums.
    std::map<std::string, std::vector<std::string>> children;   // enum path -> nested names
    std::set<std::string> paths;
    for (const auto& [module, path] : submodules_) {
        std::string current;
        std::size_t start = 0;
        while (true) {
            const auto dot = path.find('.', start);
            const auto part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            const auto next = current.empty() ? part : current + "." + part;
            if (paths.insert(next).second) children[current].push_back(next);
            current = next;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }
    std::function<void(const std::string&, const std::string&)> emit_namespace = [&](const std::string& path, const std::string& indent) {
        std::string module, from;
        for (const auto& [m, p] : submodules_) if (p == path) module = m;
        for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) from = ns;
        const auto name = path.substr(path.rfind('.') == std::string::npos ? 0 : path.rfind('.') + 1);
        swift << "\n" << indent << "/// " << (module.empty() ? "Policy modules." : "The functions of the C++ namespace `" + from + "` (policy module `" + module + "`).")
              << "\n" << indent << "public enum " << name << " {";
        for (const auto& group : functions)
            if (!module.empty() && group.module == module) emit_function(swift, group, indent + "    ", "public static ");
        for (const auto& child : children[path]) emit_namespace(child, indent + "    ");
        swift << indent << "}\n";
    };
    for (const auto& top : children[""]) emit_namespace(top, "");
    // The class table.
    swift << "\n// The C++ classes: name, base classes (indexes), and how to wrap an object.\nlet rosettaClasses: [Rosetta.ClassInfo] = [\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        std::vector<std::string> bases;
        for (const auto& base : plan.bases) {
            std::size_t at = 0;
            while (class_order_[at] != base) ++at;
            bases.push_back(std::to_string(at));
        }
        swift << "    Rosetta.ClassInfo(name: " << swift_string(plan.name) << ", bases: [" << join(bases, ", ") << "], type: " << plan.name
              << ".self, make: { " << plan.name << "(rosettaHandle: $0) }),\n";
    }
    swift << "]\n" << dispatch();
    result.swift = swift.str();

    // ------------------------------------------------------------ the package
    std::string runtime;
    for (const auto* part : detail::swift_full_runtime_parts) runtime += part;
    const auto replace = [&](std::string& text, const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    };
    replace(runtime, "@CMOD@", cmodule_);
    replace(runtime, "@C@", "rosetta_" + name_);
    replace(runtime, "@LIB@", library_);
    const auto prefix = "rosetta_" + name_;
    auto guard = prefix;
    for (auto& c : guard) c = upper(c);
    std::ostringstream header;
    header << "// Code generated by Rosetta; DO NOT EDIT.\n// The C entry points of the native library " << library_ << " (the C++ library of the Swift module "
           << module_name_ << ").\n#ifndef " << guard << "_H\n#define " << guard << "_H\n#include <stddef.h>\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
           << "typedef int (*" << prefix << "_invoke_fn)(int kind, unsigned long long id, unsigned generic, const unsigned char* data, size_t size,\n"
           << "    unsigned char** result, size_t* result_size);\n"
           << "typedef void (*" << prefix << "_retain_fn)(unsigned long long id, int delta);\n"
           << "int " << prefix << "_call(unsigned index, const unsigned char* data, size_t size, unsigned char** out, size_t* out_size);\n"
           << "void " << prefix << "_free(void* pointer);\n"
           << "void* " << prefix << "_alloc(size_t size);\n"
           << "void " << prefix << "_release(void* holder, int now);\n"
           << "int " << prefix << "_init(int version, " << prefix << "_invoke_fn invoke, " << prefix << "_retain_fn retain);\n"
           << "#ifdef __cplusplus\n}\n#endif\n#endif\n";
    std::ostringstream modulemap;
    modulemap << "module " << cmodule_ << " [system] {\n    header \"" << name_ << ".h\"\n    link \"" << library_ << "\"\n    export *\n}\n";
    std::ostringstream package;
    package << "// swift-tools-version:5.9\n// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n"
            << "// The Swift module " << module_name_ << " over the native library lib" << library_ << ", which the linker finds through\n"
            << "// -Xlinker -L<dir> (swift build) or LIBRARY_PATH, and the program at run time through its rpath or\n"
            << "// LD_LIBRARY_PATH.\nimport PackageDescription\n\nlet package = Package(\n    name: " << swift_string(module_name_) << ",\n"
            << "    products: [.library(name: " << swift_string(module_name_) << ", targets: [" << swift_string(module_name_) << "])],\n"
            << "    targets: [\n        .systemLibrary(name: " << swift_string(cmodule_) << ", path: \"Sources/" << cmodule_ << "\"),\n"
            << "        .target(name: " << swift_string(module_name_) << ", dependencies: [" << swift_string(cmodule_) << "], path: \"Sources/"
            << module_name_ << "\"),\n    ],\n    swiftLanguageVersions: [.v5]\n)\n";
    result.files.emplace_back("Package.swift", package.str());
    result.files.emplace_back("Sources/" + module_name_ + "/Rosetta.swift", runtime);
    result.files.emplace_back("Sources/" + cmodule_ + "/module.modulemap", modulemap.str());
    result.files.emplace_back("Sources/" + cmodule_ + "/" + name_ + ".h", header.str());
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

std::string swift_module_name(const std::string& name) { return pascal(name); }

GeneratedSwiftSource generate_swift(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                    const SwiftOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Swift C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
