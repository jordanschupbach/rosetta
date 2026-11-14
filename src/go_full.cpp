// Full Go backend (--go-api full): a cgo package over the shared C ABI library of the full C ABI
// backends (cabi_full.hpp, lisp_full_runtime.hpp).
//
// Classes become structs holding a handle, with methods; a derived class embeds its bases, and
// every class has an interface (ShapeLike: AsShape() *Shape) that its derived classes satisfy, so
// they pass where a base is expected. Overloads and default arguments become distinct Go names,
// resolved again at run time in C++. Calls that can throw return (T, error) with a typed *Error.
// Enums are typed integer constants, containers slices and maps, std::function Go funcs, and a Go
// value implements C++ virtual methods through NewXFrom(impl, ...). Doxygen becomes godoc.
//
// The output is a Go module: go.mod, the main package file, a subpackage per policy module, and
// internal/rosetta (the runtime: cgo declarations of the C ABI, value encoding, callbacks).
#include "rosetta/go.hpp"
#include "cabi_full.hpp"
#include "go_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

bool go_keyword(const std::string& name) {
    static const std::set<std::string> words = {"break", "case", "chan", "const", "continue", "default", "defer", "else",
        "fallthrough", "for", "func", "go", "goto", "if", "import", "interface", "map", "package", "range", "return", "select",
        "struct", "switch", "type", "var"};
    return words.count(name) != 0;
}
bool go_predeclared(const std::string& name) {
    static const std::set<std::string> words = {"any", "bool", "byte", "comparable", "complex64", "complex128", "error",
        "float32", "float64", "int", "int8", "int16", "int32", "int64", "rune", "string", "uint", "uint8", "uint16", "uint32",
        "uint64", "uintptr", "true", "false", "iota", "nil", "append", "cap", "clear", "close", "complex", "copy", "delete",
        "imag", "len", "make", "max", "min", "new", "panic", "print", "println", "real", "recover"};
    return words.count(name) != 0;
}
char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
// An exported Go name: snake_case, camelCase or a__b qualified names in PascalCase.
std::string pascal(const std::string& name) {
    std::string result;
    bool start = true;
    for (const char c : name) {
        if (c == '_') { start = true; continue; }
        if (!std::isalnum(static_cast<unsigned char>(c))) { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    if (result.empty()) return "X";
    if (std::isdigit(static_cast<unsigned char>(result[0]))) result = "X" + result;
    return result;
}
// A parameter name: lowerCamelCase, never a keyword or predeclared identifier.
std::string camel(const std::string& name) {
    auto result = pascal(name);
    std::size_t upper_run = 0;
    while (upper_run < result.size() && std::isupper(static_cast<unsigned char>(result[upper_run]))) ++upper_run;
    if (upper_run > 1 && upper_run < result.size()) --upper_run; // URLPath -> urlPath
    for (std::size_t i = 0; i < upper_run; ++i) result[i] = lower(result[i]);
    if (go_keyword(result) || go_predeclared(result) || result == "rosetta" || result == "impl") result += "_";
    return result;
}
std::string go_string(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    return result + '"';
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

// Names in one Go scope (a package, or the methods of a type).
struct Names {
    std::set<std::string> taken;
    bool reserve(const std::string& name) { return taken.insert(name).second; }
    bool free(const std::string& name) const { return !taken.count(name); }
    std::string claim(const std::string& wanted) {
        if (taken.insert(wanted).second) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + std::to_string(i);
            if (taken.insert(candidate).second) return candidate;
        }
    }
};

// ---------------------------------------------------------------- doc comments
// Lines of a Go doc comment, without the "// " prefix: "" is a blank line, a leading tab a code
// line, "  - " a list item and "    " its continuation.
struct Comment {
    std::vector<std::string> lines;
    void blank() { if (!lines.empty() && !lines.back().empty()) lines.push_back(""); }
    void paragraph(const std::string& text) {
        blank();
        bool list = false;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const auto stripped = trim(line);
            if (stripped.empty()) { blank(); list = false; continue; }
            const bool item = stripped.rfind("- ", 0) == 0 || stripped.rfind("* ", 0) == 0;
            const bool continued = list && !item && !line.empty() && (line[0] == ' ' || line[0] == '\t');
            if (item && !list && !lines.empty() && !lines.back().empty()) lines.push_back("");
            if (!item && !continued && list) lines.push_back("");
            if (item) lines.push_back("  - " + trim(stripped.substr(2)));
            else if (continued) lines.push_back("    " + stripped);
            else lines.push_back(stripped);
            list = item || continued;
        }
        if (list) lines.push_back("");
    }
    void heading(const std::string& text) {
        blank();
        lines.push_back("# " + one_line(trim(text)));
        lines.push_back("");
    }
    void code(const std::string& text) {
        blank();
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) lines.push_back(line.empty() ? "" : "\t" + line);
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
        lines.push_back("");
    }
    void list(const std::string& title, const std::vector<std::string>& items) {
        if (items.empty()) return;
        blank();
        lines.push_back(title);
        lines.push_back("");
        for (const auto& item : items) lines.push_back("  - " + item);
        lines.push_back("");
    }
    // A long paragraph line or list item as lines of at most about 100 columns. A line never
    // starts with something Go would read as a list marker or heading.
    static std::vector<std::string> wrap(const std::string& line) {
        const std::size_t width = 96;
        if (line.size() <= width || line.rfind("# ", 0) == 0) return {line};
        const bool item = line.rfind("  - ", 0) == 0 || line.rfind("    ", 0) == 0;
        const std::string prefix = item ? "    " : "";
        std::vector<std::string> result;
        std::string rest = line;
        const auto safe = [](const std::string& text, std::size_t at) {
            const char c = text[at];
            if (c == '-' || c == '+' || c == '*' || c == '#' || c == '>') return false;
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t end = at;
                while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
                if (end < text.size() && (text[end] == '.' || text[end] == ')')) return false;
            }
            return true;
        };
        while (rest.size() > width) {
            std::size_t cut = std::string::npos;
            for (std::size_t i = width; i > prefix.size() + 20 && i < rest.size(); --i)
                if (rest[i] == ' ' && i + 1 < rest.size() && safe(rest, i + 1)) { cut = i; break; }
            if (cut == std::string::npos) break;
            result.push_back(rest.substr(0, cut));
            rest = prefix + rest.substr(cut + 1);
        }
        result.push_back(rest);
        return result;
    }
    std::string render(const std::string& indent = {}) const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        std::string out;
        bool previous_blank = false;
        for (const auto& line : copy) {
            if (line.empty() && previous_blank) continue;
            previous_blank = line.empty();
            if (line.empty()) out += indent + "//\n";
            else if (line[0] == '\t') out += indent + "//" + line + "\n";
            else for (const auto& part : wrap(line)) out += indent + "// " + part + "\n";
        }
        return out;
    }
};
// Doxygen markup that Go doc comments show as is: drop ** and keep `code`.
std::string go_text(std::string text) {
    for (std::size_t i = 0; (i = text.find("**", i)) != std::string::npos;) text.erase(i, 2);
    return text;
}
// "Adds three integers" -> "adds three integers", for "Add adds three integers".
bool verb_first(const std::string& text) {
    std::size_t end = 0;
    while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) ++end;
    if (end < 3 || end == text.size() || text[end] != ' ' || !std::isupper(static_cast<unsigned char>(text[0]))) return false;
    for (std::size_t i = 1; i < end; ++i) if (!std::islower(static_cast<unsigned char>(text[i]))) return false;
    const auto word = text.substr(0, end);
    if (word.back() != 's' || word[end - 2] == 's' || word[end - 2] == 'u' || word[end - 2] == 'i') return false;
    static const std::set<std::string> nouns = {"Values", "Items", "Words", "Bytes", "Elements", "Pairs", "Rows", "Columns",
        "Options", "Settings", "Members", "Shapes", "Kinds", "Enumerators", "Units", "Names", "Results", "Statistics", "Points",
        "Parameters", "Arguments", "Defaults", "Errors", "Types", "Classes", "Methods", "Functions", "Numbers", "Strings"};
    return !nouns.count(word);
}

std::string kind_of_exception(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "InvalidArgument";
    if (bare == "std::out_of_range") return "OutOfRange";
    if (bare == "std::domain_error") return "DomainError";
    if (bare == "std::length_error") return "LengthError";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "ArithmeticError";
    if (bare == "std::bad_alloc") return "AllocationError";
    return "NativeError";
}

// C++ operators and their Go method names ("" = not bound).
std::string go_operator(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"Pos", "Add"}}, {"operator-", {"Neg", "Sub"}}, {"operator*", {nullptr, "Mul"}}, {"operator/", {nullptr, "Div"}},
        {"operator%", {nullptr, "Mod"}}, {"operator==", {nullptr, "Equal"}}, {"operator!=", {nullptr, "NotEqual"}},
        {"operator<", {nullptr, "Less"}}, {"operator<=", {nullptr, "LessEqual"}}, {"operator>", {nullptr, "Greater"}},
        {"operator>=", {nullptr, "GreaterEqual"}}, {"operator&", {nullptr, "And"}}, {"operator|", {nullptr, "Or"}},
        {"operator^", {nullptr, "Xor"}}, {"operator~", {"Complement", nullptr}}, {"operator!", {"Not", nullptr}},
        {"operator<<", {nullptr, "Shl"}}, {"operator>>", {nullptr, "Shr"}}, {"operator[]", {nullptr, "At"}},
        {"operator+=", {nullptr, "AddAssign"}}, {"operator-=", {nullptr, "SubAssign"}}, {"operator*=", {nullptr, "MulAssign"}},
        {"operator/=", {nullptr, "DivAssign"}}};
    if (name == "operator()") return "Call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// One Go function or method: an overload called with its first `arity` C++ parameters.
struct Variant {
    const Overload* overload = nullptr;
    std::size_t arity = 0;
    std::string name;
    std::string link;   // doc link target ([Name] or [Type.Name])
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const GoFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedGoFullSource run();

private:
    GoFullOptions options_;
    std::string package_, module_path_, library_;
    std::string qualifier_;   // "pkg." inside a subpackage
    Names package_names_;
    std::map<std::string, std::string> interfaces_;               // class -> XLike
    std::map<std::string, std::set<std::string>> derived_;        // class -> classes deriving from it
    std::map<const Group*, std::vector<Variant>> variants_;
    std::map<std::string, std::vector<Variant>> constructors_;    // class -> variants
    std::map<std::string, std::vector<Variant>> directors_;       // class -> NewXFrom variants
    std::map<std::string, Names> members_;                        // class -> method names
    std::map<std::string, std::string> receivers_;                // class -> receiver name
    struct FieldNames { std::string getter, setter; };
    std::map<const FieldPlan*, FieldNames> field_names_;
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::string> links_;                    // C++ name -> Go doc link
    std::map<std::string, std::vector<std::string>> generic_names_; // C++ method name -> Go method names
    std::map<std::string, Names> subpackage_names_;               // policy module -> its names
    std::map<std::string, std::vector<Overload>> public_constructors_; // class -> the constructors NewX calls

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Go"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return go_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken;
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
        for (const auto& overload : group.overloads) text += "    " + signature(overload, pascal(group.name), overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + signature(overload, "New" + plan.name, overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: create it with New" + plan.name + "From and a Go implementation of its virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: create it with New" + plan.name + "From";
    }
    std::string symbol_prefix() const override { return "rosetta_go_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_GO"; }
    std::string module_prefix() const override { return "rosetta_go_module_"; }

    // ------------------------------------------------------------ types
    std::string q(const std::string& name) const { return qualifier_ + name; }
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    std::string class_parameter(const std::string& owner) const {
        const auto& plan = classes_.at(owner);
        return derived_.count(owner) || plan.director ? q(interfaces_.at(owner)) : "*" + q(plan.name);
    }
    std::string go_type(const Type& type, bool parameter) const;
    std::string go_spelled(std::string cpp, bool parameter) const;
    std::string parameter_type(const Type& type) const { return writeback(type) ? "*" + go_type(type, false) : go_type(type, true); }
    std::string result_type(const Function& function) const { return is_void(function.result) ? std::string() : go_type(function.result, false); }
    static std::string word(std::string type);
    std::string signature(const Overload& overload, const std::string& name, std::size_t arity) const;
    std::string go_default(const DefaultValue& value, const Type& type) const;

    // ------------------------------------------------------------ naming
    std::vector<Variant> plan_variants(const std::vector<Overload>& overloads, const std::string& base) const;
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);

    // ------------------------------------------------------------ emission
    Comment doc_of(const Documentation& doc, const std::string& name, const std::string& lead, bool verbs) const;
    void add_details(Comment& comment, const Documentation& doc) const;
    void add_notes(Comment& comment, const Documentation& doc) const;
    Comment variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const;
    void emit_variant(std::ostream& out, const Variant& variant, const std::vector<Variant>& siblings, const Group* group,
                      const ClassPlan* plan, bool method) const;
    void emit_class(std::ostream& out, const std::string& owner) const;
    std::string package_doc() const;
    std::string registration() const;
};

// ---------------------------------------------------------------- types
std::string Generator::go_type(const Type& type, bool parameter) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "string";
        if (bound(pointee)) return parameter ? class_parameter(pointee.declaration) : "*" + q(classes_.at(pointee.declaration).name);
        const auto inner = go_type(pointee, parameter);
        return inner == "any" ? inner : "*" + inner;
    }
    if (is_void(target)) return "";
    if (!conversion(target).empty()) return go_spelled(policy_.conversions.at(conversion(target)), parameter);
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        auto spelled = normalize(strip_cv(target.spelling));
        static const std::set<std::string> sizes = {"size_t", "std::size_t", "ssize_t", "ptrdiff_t", "std::ptrdiff_t"};
        if (sizes.count(spelled)) return "int";
        static const std::map<std::string, std::string> builtins = {{"bool", "bool"}, {"char", "byte"}, {"signed char", "int8"},
            {"unsigned char", "uint8"}, {"short", "int16"}, {"unsigned short", "uint16"}, {"int", "int"}, {"unsigned int", "uint32"},
            {"long", "int64"}, {"unsigned long", "uint64"}, {"long long", "int64"}, {"unsigned long long", "uint64"},
            {"float", "float32"}, {"double", "float64"}, {"long double", "float64"}};
        const auto found = builtins.find(bare);
        return found != builtins.end() ? found->second : "any";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? q(found->second) : "int";
    }
    if (bound(target)) return parameter ? class_parameter(target.declaration) : "*" + q(classes_.at(target.declaration).name);
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "any";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i, bool as_parameter) { return i < target.arguments.size() ? go_type(target.arguments[i], as_parameter) : std::string("any"); };
        const auto comparable = [](const std::string& go) { return go.rfind("[]", 0) != 0 && go.rfind("map[", 0) != 0 && go.rfind("func", 0) != 0; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span")
            return "[]" + arg(0, parameter);
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset") {
            const auto element = arg(0, parameter);
            return comparable(element) && declaration != "std::multiset" ? "map[" + element + "]struct{}" : "[]" + element;
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") {
            const auto key = arg(0, false);
            return comparable(key) ? "map[" + key + "]" + arg(1, parameter) : "[]" + q("Pair") + "[" + key + ", " + arg(1, parameter) + "]";
        }
        if (declaration == "std::optional") {
            const auto inner = arg(0, parameter);
            if (inner == "any" || inner.rfind("[]", 0) == 0 || inner.rfind("map[", 0) == 0 || inner.rfind("func", 0) == 0 ||
                inner[0] == '*' || (inner.size() > 4 && inner.compare(inner.size() - 4, 4, "Like") == 0))
                return inner;
            return "*" + inner;
        }
        if (declaration == "std::complex") return arg(0, false) == "float32" ? "complex64" : "complex128";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0, parameter);
        if (declaration == "std::pair" && target.arguments.size() == 2) return q("Pair") + "[" + arg(0, parameter) + ", " + arg(1, parameter) + "]";
        if (declaration == "std::tuple") {
            const auto count = target.arguments.size();
            if (count == 0) return "struct{}";
            if (count > 8) return "[]any";
            std::string items;
            for (std::size_t i = 0; i < count; ++i) items += (i ? ", " : "") + arg(i, parameter);
            return q("Tuple" + std::to_string(count)) + "[" + items + "]";
        }
        if (declaration == "std::variant") return "any";
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string items;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) items += (i > 1 ? ", " : "") + go_type(function.arguments[i], false);
            const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
            return "func(" + items + ")" + (returns ? " " + go_type(function.arguments[0], true) : std::string());
        }
        if (declaration == "std::chrono::duration") return "float64";
    }
    return "any";
}

std::string Generator::go_spelled(std::string cpp, bool parameter) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (cpp.rfind("const ", 0) == 0) cpp = trim(cpp.substr(6));
    if (cpp.rfind("::", 0) == 0) cpp = cpp.substr(2);
    static const std::map<std::string, std::string> scalars = {{"bool", "bool"}, {"char", "byte"}, {"signed char", "int8"},
        {"unsigned char", "uint8"}, {"short", "int16"}, {"unsigned short", "uint16"}, {"int", "int"}, {"unsigned int", "uint32"},
        {"long", "int64"}, {"unsigned long", "uint64"}, {"long long", "int64"}, {"unsigned long long", "uint64"}, {"float", "float32"},
        {"double", "float64"}, {"long double", "float64"}, {"std::size_t", "int"}, {"size_t", "int"}, {"std::int64_t", "int64"},
        {"std::uint64_t", "uint64"}, {"std::int32_t", "int32"}, {"std::uint32_t", "uint32"}, {"std::string", "string"},
        {"std::string_view", "string"}, {"std::filesystem::path", "string"}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return found->second;
    for (const auto& [qualified, plan] : classes_) if (normalize(qualified) == cpp) return parameter ? class_parameter(qualified) : "*" + q(plan.name);
    for (const auto& [qualified, name] : enum_names_) if (normalize(qualified) == cpp) return q(name);
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') return "any";
    const auto name = cpp.substr(0, open);
    std::vector<std::string> arguments;
    int depth = 0;
    std::string current;
    for (std::size_t i = open + 1; i + 1 < cpp.size(); ++i) {
        const char c = cpp[i];
        if (c == '<' || c == '(') ++depth;
        if (c == '>' || c == ')') --depth;
        if (c == ',' && depth == 0) { arguments.push_back(trim(current)); current.clear(); continue; }
        current += c;
    }
    arguments.push_back(trim(current));
    const auto arg = [&](std::size_t i, bool as_parameter) { return i < arguments.size() ? go_spelled(arguments[i], as_parameter) : std::string("any"); };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::valarray") return "[]" + arg(0, parameter);
    if (name == "std::set" || name == "std::unordered_set") return "map[" + arg(0, parameter) + "]struct{}";
    if (name == "std::map" || name == "std::unordered_map") return "map[" + arg(0, false) + "]" + arg(1, parameter);
    if (name == "std::optional") {
        const auto inner = arg(0, parameter);
        return inner[0] == '*' || inner.rfind("[]", 0) == 0 || inner == "any" ? inner : "*" + inner;
    }
    if (name == "std::complex") return "complex128";
    if (name == "std::pair" && arguments.size() == 2) return q("Pair") + "[" + arg(0, parameter) + ", " + arg(1, parameter) + "]";
    if (name == "std::tuple" && !arguments.empty() && arguments.size() <= 8) {
        std::string items;
        for (std::size_t i = 0; i < arguments.size(); ++i) items += (i ? ", " : "") + arg(i, parameter);
        return q("Tuple" + std::to_string(arguments.size())) + "[" + items + "]";
    }
    return "any";
}

// A word naming a Go type in a generated name: []float64 -> Float64s, ShapeLike -> Shape.
std::string Generator::word(std::string type) {
    const auto dot = type.find('.');
    if (dot != std::string::npos && type.find('[') > dot) type = type.substr(dot + 1);
    if (type == "[]byte") return "Bytes";
    if (type.rfind("[]", 0) == 0) return word(type.substr(2)) + "s";
    if (type.rfind("map[", 0) == 0) return type.find("]struct{}") != std::string::npos ? "Set" : "Map";
    if (type.rfind("*", 0) == 0) return word(type.substr(1));
    if (type.rfind("func", 0) == 0) return "Func";
    const auto bracket = type.find('[');
    if (bracket != std::string::npos) type = type.substr(0, bracket);
    if (type.size() > 4 && type.compare(type.size() - 4, 4, "Like") == 0) type.resize(type.size() - 4);
    if (type == "struct{}") return "Empty";
    return pascal(type);
}

std::string Generator::signature(const Overload& overload, const std::string& name, std::size_t arity) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string result = name + "(";
    for (std::size_t i = first; i < arity && i < function.parameters.size(); ++i)
        result += (i > first ? ", " : "") + names[i - first] + " " + parameter_type(function.parameters[i]);
    result += ")";
    if (!function.is_constructor && !is_void(function.result)) result += " " + result_type(function);
    return result;
}

std::string Generator::go_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant" || value.kind == "string") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "nil";
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "enum") {
        const auto found = enum_names_.find(referent(type).declaration);
        return found != enum_names_.end() ? found->second + pascal(value.value) : value.value;
    }
    if (value.kind == "default") return "the zero value";
    return {};
}

// ---------------------------------------------------------------- naming
// The Go functions of a group of overloads: one per overload and arity (default arguments), with
// duplicates of an earlier Go signature dropped (C++ resolves the call at run time anyway). The
// overload taking the fewest arguments keeps `base`; the others add their parameter names
// (NewVec2XY), else their parameter types (PickFloat64), else a number. Longer arities of one
// overload add "With" and the last parameter's name (AddWithC).
std::vector<Variant> Generator::plan_variants(const std::vector<Overload>& overloads, const std::string& base) const {
    struct Candidate { const Overload* overload; std::vector<std::size_t> arities; std::string by_name, by_type; std::size_t minimum; };
    std::vector<Candidate> candidates;
    std::set<std::string> seen;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        Candidate candidate{&overload, {}, {}, {}, 0};
        for (std::size_t arity = std::max(required, first); arity <= function.parameters.size(); ++arity) {
            std::string key;
            for (std::size_t i = first; i < arity; ++i) key += parameter_type(function.parameters[i]) + ",";
            if (seen.insert(key).second) candidate.arities.push_back(arity);
        }
        if (candidate.arities.empty()) continue;
        candidate.minimum = candidate.arities.front() - first;
        bool named = true;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            if (cpp.empty() || !identifier(cpp)) named = false;
            else candidate.by_name += pascal(cpp);
            candidate.by_type += word(parameter_type(function.parameters[i]));
        }
        if (!named) candidate.by_name.clear();
        candidates.push_back(candidate);
    }
    std::size_t primary = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) if (candidates[i].minimum < candidates[primary].minimum) primary = i;
    std::vector<Variant> result;
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        const auto& candidate = candidates[c];
        std::string stem = base;
        if (c != primary) {
            const auto unique = [&](const std::string Candidate::*field) {
                const auto& value = candidate.*field;
                if (value.empty()) return false;
                for (std::size_t o = 0; o < candidates.size(); ++o)
                    if (o != c && candidates[o].*field == value) return false;
                return true;
            };
            if (unique(&Candidate::by_name)) stem += candidate.by_name;
            else if (unique(&Candidate::by_type)) stem += candidate.by_type;
            else stem += std::to_string(c + 1);
        }
        const auto& function = *candidate.overload->function;
        const std::size_t first = candidate.overload->receives_self ? 1 : 0;
        for (std::size_t a = 0; a < candidate.arities.size(); ++a) {
            const auto arity = candidate.arities[a];
            std::string name = stem;
            if (a > 0) {
                const auto cpp = arity - 1 < function.parameter_names.size() ? function.parameter_names[arity - 1] : std::string();
                name += "With" + (cpp.empty() || !identifier(cpp) ? "Arg" + std::to_string(arity - first) : pascal(cpp));
            }
            result.push_back({candidate.overload, arity, name, {}});
        }
    }
    return result;
}

void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    // Variants are planned with their wanted names, then claimed in two passes: every group's
    // plain name first, so that a variant never takes another group's plain name.
    struct Planned { std::vector<Variant>* variants; Names* scope; std::string base; std::string link; };
    std::vector<Planned> planned;
    const auto plan = [&](std::vector<Variant>& into, const std::vector<Overload>& overloads, const std::string& base, Names& scope,
                          const std::string& link) {
        into = plan_variants(overloads, base);
        planned.push_back({&into, &scope, base, link});
    };
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        for (const auto* reserved : {"Close", "IsClosed", "String"}) members.reserve(reserved);
        for (const auto& base : cls.bases) members.reserve(classes_.at(base).name);
        for (const auto& other : class_order_) if (derives(owner, other)) members.reserve("As" + classes_.at(other).name);
        const bool constructible = !cls.record->abstract;
        if (!cls.constructors.empty()) {
            auto& visible = public_constructors_[owner];
            for (const auto& overload : cls.constructors)
                if (constructible && overload.function->access == "public") visible.push_back(overload);
            if (!visible.empty()) plan(constructors_[owner], visible, "New" + cls.name, package_names_, "");
            if (cls.director) plan(directors_[owner], cls.constructors, "New" + cls.name + "From", package_names_, "");
        }
        for (auto& group : cls.methods) {
            if (group.is_static) plan(variants_[&group], group.overloads, cls.name + pascal(group.name), package_names_, "");
            else plan(variants_[&group], group.overloads, pascal(group.name), members, cls.name + ".");
        }
        for (auto& group : cls.operators) plan(variants_[&group], group.overloads, group.name + (group.operand ? "R" : ""), members, cls.name + ".");
    }
    for (auto& group : functions)
        plan(variants_[&group], group.overloads, pascal(group.name), group.module.empty() ? package_names_ : subpackage_names_[group.module], "");
    for (auto& item : planned)
        for (auto& variant : *item.variants)
            if (variant.name == item.base && item.scope->free(variant.name)) { item.scope->reserve(variant.name); variant.link = "!"; break; }
    for (auto& item : planned)
        for (auto& variant : *item.variants) {
            if (variant.link == "!") variant.link.clear();
            else variant.name = item.scope->claim(variant.name);
            variant.link = "[" + item.link + variant.name + "]";
        }
    // Fields (getter X, setter SetX) and constants, after the methods.
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        for (auto& field : cls.fields) {
            const auto& f = *field.field;
            FieldNames names;
            const auto base = pascal(field.name);
            names.getter = members.free(base) ? members.claim(base) : members.claim(base + "Field");
            const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
            if (writable) names.setter = members.free("Set" + base) ? members.claim("Set" + base) : members.claim("Set" + base + "Field");
            field_names_[&field] = names;
        }
        for (auto& constant : cls.constants) constant_names_[&constant] = package_names_.claim(cls.name + pascal(constant.name));
    }
    for (auto& constant : constants) constant_names_[&constant] = package_names_.claim(pascal(constant.name));
    // Doc links and the Go names of virtual methods.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        links_[owner] = "[" + cls.name + "]";
        links_[cls.record->name] = links_[owner];
        for (const auto& group : cls.methods) {
            for (const auto& variant : variants_.at(&group)) {
                auto& names = generic_names_[group.name];
                if (!group.is_static && std::find(names.begin(), names.end(), variant.name) == names.end()) names.push_back(variant.name);
            }
            const auto& first = variants_.at(&group);
            if (first.empty()) continue;
            const auto link = group.is_static ? "[" + first.front().name + "]" : "[" + cls.name + "." + first.front().name + "]";
            links_[owner + "::" + group.name] = link;
            links_[cls.record->name + "::" + group.name] = link;
        }
    }
    for (const auto& group : functions) {
        const auto& variants = variants_.at(&group);
        if (variants.empty() || !group.module.empty()) continue;
        const auto link = "[" + variants.front().name + "]";
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = link;
            links_.emplace(overload.function->name, link);
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = "[" + name + "]";
        links_.emplace(enums_.at(qualified)->name, "[" + name + "]");
    }
}

// ---------------------------------------------------------------- documentation
void Generator::add_details(Comment& comment, const Documentation& doc) const {
    for (const auto& block : doc.details) {
        if (block.kind == "code" || block.kind == "math") comment.code(block.text);
        else if (block.kind == "heading") comment.heading(block.text);
        else comment.paragraph(go_text(block.text));
    }
}
void Generator::add_notes(Comment& comment, const Documentation& doc) const {
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { comment.paragraph("Deprecated: " + go_text(one_line(item.text))); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        comment.paragraph(label + ": " + go_text(one_line(item.text)));
    }
    if (!doc.see.empty()) {
        std::string see = "See also ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) {
            const auto found = links_.find(normalize(doc.see[i]));
            see += (i == 0 ? "" : i + 1 == doc.see.size() ? " and " : ", ") + (found != links_.end() ? found->second : doc.see[i]);
        }
        comment.paragraph(see + ".");
    }
}
// A comment starting with `name`: "Name adds ..." when the brief starts with a verb, else
// `lead` (a sentence starting with the name) and the brief as the next paragraph.
Comment Generator::doc_of(const Documentation& doc, const std::string& name, const std::string& lead, bool verbs) const {
    Comment comment;
    auto brief = go_text(doc.brief);
    auto details = doc.details;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = go_text(details.front().text); details.erase(details.begin()); }
    if (verbs && verb_first(brief)) {
        brief[0] = lower(brief[0]);
        comment.paragraph(name + " " + brief);
    } else {
        comment.paragraph(lead);
        if (!brief.empty()) comment.paragraph(brief);
    }
    Documentation rest = doc;
    rest.details = details;
    add_details(comment, rest);
    return comment;
}

Comment Generator::variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const auto& doc = function.documentation;
    auto comment = doc_of(doc, variant.name, variant.name + " calls " + what + ".", !function.is_constructor);
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    std::string omitted;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (i < variant.arity) {
            for (const auto& item : doc.params)
                if (item.name == cpp && !item.text.empty()) parameters.push_back(names[i - first] + ": " + go_text(one_line(item.text)));
            continue;
        }
        const auto shown = go_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        omitted += (omitted.empty() ? "" : ", ") + names[i - first] + (shown.empty() ? std::string() : " = " + shown);
    }
    comment.list("Parameters:", parameters);
    if (!function.is_constructor && !is_void(function.result) && !doc.returns.empty()) {
        auto text = go_text(one_line(doc.returns));
        while (!text.empty() && text.back() == '.') text.pop_back();
        if (text.size() > 1 && std::isupper(static_cast<unsigned char>(text[0])) && std::islower(static_cast<unsigned char>(text[1]))) text[0] = lower(text[0]);
        comment.paragraph("It returns " + text + ".");
    }
    std::vector<std::string> errors;
    for (const auto& item : doc.throws) {
        const auto kind = kind_of_exception(item.name);
        errors.push_back("[" + kind + "]" + (item.name.empty() ? std::string() : " (C++ " + item.name + ")") +
                         (item.text.empty() ? std::string() : ": " + go_text(one_line(item.text))));
    }
    if (!errors.empty()) comment.list("The error is an [*Error] of kind:", errors);
    // The other arities of this overload.
    std::vector<std::string> longer;
    for (const auto& sibling : siblings)
        if (sibling.overload == variant.overload && sibling.arity > variant.arity) longer.push_back(sibling.link.empty() ? sibling.name : sibling.link);
    if (!omitted.empty()) {
        std::string text = "It uses the C++ defaults " + omitted;
        if (!longer.empty()) {
            text += "; ";
            for (std::size_t i = 0; i < longer.size(); ++i) text += (i == 0 ? "" : i + 1 == longer.size() ? " and " : ", ") + longer[i];
            text += longer.size() == 1 ? " passes more arguments" : " pass more arguments";
        }
        comment.paragraph(text + ".");
    }
    add_notes(comment, doc);
    return comment;
}

// ---------------------------------------------------------------- emission
void Generator::emit_variant(std::ostream& out, const Variant& variant, const std::vector<Variant>& siblings, const Group* group,
                             const ClassPlan* plan, bool method) const {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    auto names = parameter_names(overload);
    const auto receiver = plan && method ? receivers_.at(plan->record->qualified_name()) : std::string();
    for (auto& name : names) while (name == receiver) name += "_";
    std::string parameters, arguments;
    for (std::size_t i = first; i < variant.arity; ++i) {
        const auto& type = function.parameters[i];
        const auto& name = names[i - first];
        parameters += (parameters.empty() ? "" : ", ") + name + " " + parameter_type(type);
        arguments += ", " + (writeback(type) ? "rosetta.Ref(" + name + ")" : name);
    }
    const auto result = function.is_constructor ? "*" + q(plan->name) : result_type(function);
    const bool fallible = !function.is_noexcept;
    std::string what;
    if (function.is_constructor) what = "the C++ constructor " + plan->record->qualified_name();
    else if (overload.extension) what = "C++ " + function.qualified_name();
    else if (plan) what = "the C++ method " + plan->record->qualified_name() + "::" + function.name;
    else what = "C++ " + function.qualified_name();
    const std::size_t overloads = function.is_constructor ? plan->constructors.size() : group ? group->overloads.size() : 1;
    if (overloads > 1) {
        std::string spelled;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) spelled += (i ? ", " : "") + function.parameters[i].spelling;
        what += "(" + spelled + ")";
    }
    auto comment = variant_doc(variant, siblings, what);
    std::string head, call;
    if (function.is_constructor) {
        // Every NewXFrom variant (NewXFromWithB, NewXFromOptions, ...) takes impl, not only NewXFrom.
        const bool director = directors_.count(plan->record->qualified_name()) &&
                              std::any_of(directors_.at(plan->record->qualified_name()).begin(), directors_.at(plan->record->qualified_name()).end(),
                                          [&](const Variant& v) { return &v == &variant; });
        if (director) {
            comment.paragraph("The C++ object is a subclass whose virtual methods call impl's methods of the same names, when impl "
                              "has them, and the C++ implementation otherwise. If impl is a pointer to a struct with a nil field " +
                              plan->name + " of type *" + plan->name + " (usually embedded), that field is set to the new object, so impl's "
                              "methods reach the C++ implementation through it.");
            std::vector<std::string> overridable_list;
            for (const auto& item : overridable(*plan)) {
                const auto found = generic_names_.find(item.name);
                if (found == generic_names_.end() || found->second.empty()) continue;
                overridable_list.push_back(found->second.front() + (item.pure ? " (required: pure virtual in C++)" : ""));
            }
            comment.list("Virtual methods impl may implement:", overridable_list);
            parameters = "impl any" + (parameters.empty() ? "" : ", " + parameters);
        }
        out << "\n" << comment.render() << "func " << variant.name << "(" << parameters << ") (" << result << ", error) {\n"
            << "\treturn rosetta.Construct[" << result << "](" << plan->constructor_index << ", " << (director ? "impl" : "nil") << arguments << ")\n}\n";
        return;
    }
    // The arguments in C++ order: the receiver first, or at a free operator's bound operand.
    std::string all;
    if (method && overload.extension && !overload.receives_self) {
        parameters.clear();
        const std::size_t operand = group ? group->operand : 0;
        for (std::size_t i = 0; i < variant.arity; ++i) {
            const auto& type = function.parameters[i];
            if (i == operand) { all += ", " + receiver; continue; }
            parameters += (parameters.empty() ? "" : ", ") + names[i] + " " + parameter_type(type);
            all += ", " + (writeback(type) ? "rosetta.Ref(" + names[i] + ")" : names[i]);
        }
    } else if (method) all = ", " + receiver + arguments;
    else all = arguments;
    call = "rosetta.Call(" + std::to_string(group->index) + all + ")";
    std::string results, body;
    if (result.empty()) {
        results = fallible ? " error" : "";
        body = fallible ? "\treturn rosetta.Void(" + call + ")\n" : "\trosetta.MustVoid(" + call + ")\n";
    } else if (fallible) {
        results = " (" + result + ", error)";
        body = "\treturn rosetta.Result[" + result + "](" + call + ")\n";
    } else {
        results = " " + result;
        body = "\treturn rosetta.MustResult[" + result + "](" + call + ")\n";
    }
    out << "\n" << comment.render();
    if (method) out << "func (" << receiver << " *" << q(plan->name) << ") ";
    else out << "func ";
    out << variant.name << "(" << parameters << ")" << results << " {\n" << body << "}\n";
}

void Generator::emit_class(std::ostream& out, const std::string& owner) const {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& name = plan.name;
    const auto& receiver = receivers_.at(owner);
    const auto& interface = interfaces_.at(owner);
    // The interface.
    {
        Comment comment;
        comment.paragraph(interface + " is implemented by *" + name + ", by the classes deriving from " + name +
                          " and by Go types embedding one of them: they all pass where C++ expects a " + owner + ".");
        out << "\n" << comment.render() << "type " << interface << " interface {\n\tAs" << name << "() *" << name << "\n}\n";
    }
    // The struct.
    auto comment = doc_of(record.documentation, name, name + " wraps the C++ class " + owner + ".", false);
    if (!plan.bases.empty()) {
        std::string bases;
        for (std::size_t i = 0; i < plan.bases.size(); ++i)
            bases += (i == 0 ? "" : i + 1 == plan.bases.size() ? " and " : ", ") + std::string("[") + classes_.at(plan.bases[i]).name + "]";
        comment.paragraph("It embeds its C++ base " + std::string(plan.bases.size() > 1 ? "classes " : "class ") + bases +
                          ", so their methods apply to it, and it passes where they are expected.");
    }
    if (plan.director)
        comment.paragraph(std::string(record.abstract ? "Abstract in C++: create one" : "Create one") + " with a Go implementation of its virtual methods through [New" + name +
                          "From]; C++ calls the Go methods.");
    else if (record.abstract) comment.paragraph("Abstract in C++: C++ functions return objects of derived classes.");
    comment.paragraph("[" + name + ".Close] releases the C++ object; otherwise the garbage collector does.");
    add_notes(comment, record.documentation);
    out << "\n" << comment.render() << "type " << name << " struct {\n";
    if (plan.bases.empty()) out << "\tref *rosetta.Handle\n";
    for (const auto& base : plan.bases) out << "\t" << classes_.at(base).name << "\n";
    out << "}\n";
    // Construction from a handle, the handle, and the views as base classes.
    out << "\nfunc make" << name << "(h *rosetta.Handle) " << name << " {\n\treturn " << name << "{";
    if (plan.bases.empty()) out << "ref: h";
    for (std::size_t i = 0; i < plan.bases.size(); ++i) out << (i ? ", " : "") << classes_.at(plan.bases[i]).name << ": make" << classes_.at(plan.bases[i]).name << "(h)";
    out << "}\n}\n";
    out << "\nfunc (" << receiver << " *" << name << ") nativeRef() *rosetta.Handle {\n\tif " << receiver << " == nil {\n\t\treturn nil\n\t}\n\treturn "
        << receiver << (plan.bases.empty() ? ".ref" : "." + classes_.at(plan.bases.front()).name + ".nativeRef()") << "\n}\n";
    // Paths to every ancestor through the embedded fields.
    std::map<std::string, std::string> paths{{owner, ""}};
    std::vector<std::string> queue{owner};
    for (std::size_t i = 0; i < queue.size(); ++i)
        for (const auto& base : classes_.at(queue[i]).bases)
            if (!paths.count(base)) { paths[base] = paths[queue[i]] + "." + classes_.at(base).name; queue.push_back(base); }
    for (const auto& ancestor : queue) {
        const auto& target = classes_.at(ancestor).name;
        out << "\n// As" << target << " returns the object as a " << (ancestor == owner ? "[" + target + "] (itself)" : "[" + target + "] (a C++ base class)")
            << ", or nil for a nil receiver.\nfunc (" << receiver << " *" << name << ") As" << target << "() *" << target << " {\n";
        if (ancestor == owner) out << "\treturn " << receiver << "\n}\n";
        else out << "\tif " << receiver << " == nil {\n\t\treturn nil\n\t}\n\treturn &" << receiver << paths[ancestor] << "\n}\n";
    }
    if (plan.bases.size() != 1) {
        out << "\n// Close releases the C++ object now (it is idempotent); later calls fail with a [BindingError].\n"
            << "func (" << receiver << " *" << name << ") Close() { rosetta.Close(" << receiver << ".nativeRef()) }\n"
            << "\n// IsClosed reports whether the C++ object is released.\n"
            << "func (" << receiver << " *" << name << ") IsClosed() bool { return " << receiver << ".nativeRef().Closed() }\n";
    }
    // String: the C++ to_string(), else the class name.
    const Variant* to_string = nullptr;
    for (const auto& group : plan.methods) {
        if (group.is_static || group.name != "to_string") continue;
        for (const auto& variant : variants_.at(&group))
            if (variant.arity == (variant.overload->receives_self ? 1u : 0u) && result_type(*variant.overload->function) == "string" &&
                !variant.overload->function->is_noexcept)
                to_string = &variant;
    }
    if (to_string)
        out << "\n// String returns the C++ to_string() text (fmt.Stringer).\nfunc (" << receiver << " *" << name << ") String() string {\n\treturn rosetta.Stringify("
            << receiver << "." << to_string->name << "())\n}\n";
    else if (plan.bases.size() != 1)
        out << "\n// String names the object's class (fmt.Stringer).\nfunc (" << receiver << " *" << name << ") String() string { return rosetta.Describe("
            << receiver << ".nativeRef()) }\n";
    // Constructors.
    if (constructors_.count(owner)) for (const auto& variant : constructors_.at(owner)) emit_variant(out, variant, constructors_.at(owner), nullptr, &plan, false);
    if (directors_.count(owner)) for (const auto& variant : directors_.at(owner)) emit_variant(out, variant, directors_.at(owner), nullptr, &plan, false);
    for (const auto& group : plan.methods) {
        const auto& variants = variants_.at(&group);
        for (const auto& variant : variants) emit_variant(out, variant, variants, &group, &plan, !group.is_static);
    }
    for (const auto& group : plan.operators) {
        const auto& variants = variants_.at(&group);
        for (const auto& variant : variants) emit_variant(out, variant, variants, &group, &plan, true);
    }
    // Fields: getter and setter methods.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto& names = field_names_.at(&field);
        const auto type = go_type(f.type, false);
        const auto setter_type = go_type(f.type, true);
        auto about = doc_of(f.documentation, names.getter, names.getter + " returns the C++ field " + owner + "::" + f.name + ".", false);
        if (bound(referent(f.type))) about.paragraph("The result refers to the field inside this object, which it keeps alive.");
        out << "\n" << about.render() << "func (" << receiver << " *" << name << ") " << names.getter << "() " << type << " {\n"
            << "\treturn rosetta.MustResult[" << type << "](rosetta.Call(" << field.getter_index << ", " << receiver << "))\n}\n";
        if (!names.setter.empty()) {
            Comment setter;
            setter.paragraph(names.setter + " sets the C++ field " + owner + "::" + f.name + ".");
            out << "\n" << setter.render() << "func (" << receiver << " *" << name << ") " << names.setter << "(value " << setter_type << ") error {\n"
                << "\treturn rosetta.Void(rosetta.Call(" << field.setter_index << ", " << receiver << ", value))\n}\n";
        }
    }
    for (const auto& constant : plan.constants) {
        const auto type = go_type(*constant.type, false);
        const auto& go = constant_names_.at(&constant);
        auto about = doc_of(*constant.documentation, go, go + " returns the C++ constant " + constant.expression + ".", false);
        out << "\n" << about.render() << "func " << go << "() " << type << " {\n\treturn rosetta.MustResult[" << type << "](rosetta.Call("
            << constant.index << "))\n}\n";
    }
}

std::string Generator::package_doc() const {
    Comment comment;
    comment.paragraph("Package " + package_ + " is the Go interface to the C++ library " + name_ + " (" + header_name() +
                      "), generated by Rosetta.");
    comment.heading("Building");
    comment.paragraph("The package calls a shared library through cgo (#cgo LDFLAGS: -l" + library_ + "). Point the linker and the "
                      "loader at the directory holding lib" + library_ + ".so, for example:");
    comment.code("export CGO_LDFLAGS=\"-L$DIR -Wl,-rpath,$DIR\"\ngo run .");
    comment.paragraph("or set LD_LIBRARY_PATH (DYLD_LIBRARY_PATH on macOS) when running. A program outside this module requires it "
                      "and replaces it with its directory in go.mod:");
    comment.code("require " + module_path_ + " v0.0.0\nreplace " + module_path_ + " => ../path/to/" + package_);
    comment.heading("Classes");
    comment.paragraph("A C++ class is a struct with methods. A derived class embeds its bases, and every class X has an interface "
                      "XLike, implemented by *X, its derived classes and Go types embedding them, which is the parameter type where "
                      "C++ takes an X that may be a derived object. Results have the declared class; [Cast] returns the most-derived "
                      "class (or the Go implementation behind a C++ object). Close releases the C++ object; otherwise a cleanup "
                      "(runtime.AddCleanup) does once the Go object is unreachable.");
    comment.paragraph("Constructors are NewX functions. Go types implement C++ virtual methods through NewXFrom(impl, ...): "
                      "C++ calls impl's methods of the same Go names.");
    comment.heading("Overloads and default arguments");
    comment.paragraph("Go has no overloading: each C++ overload is its own function. The one taking the fewest arguments keeps "
                      "the name; the others add their parameter names (NewVec2XY), else their parameter types (PickFloat64), else a "
                      "number. An overload with default arguments has one function per arity: the shortest keeps the name, the "
                      "longer ones add With and their last parameter (AddWithB, AddWithC). C++ resolves overloads again at run time.");
    comment.heading("Errors");
    comment.paragraph("Functions that can throw (all but noexcept ones) return an error last: an [*Error] whose [Error.Kind] says "
                      "what failed ([InvalidArgument], [OutOfRange], ... for C++ exceptions, [BindingError] for values that do not "
                      "convert or closed objects, [CallbackError] for a Go callback that panicked or returned an error, which "
                      "[errors.Unwrap] returns). [Must] turns an error into a panic. Field getters and constants panic on errors.");
    comment.heading("Types");
    comment.paragraph("C++ int is int (range-checked), std::size_t int, double float64, std::string string, std::vector []T, "
                      "std::map map[K]V, std::set map[T]struct{}, std::pair [Pair], std::tuple [Tuple2] and so on, "
                      "std::optional<T> *T (nil for nullopt), std::complex complex128, std::function a Go func (a panic in it "
                      "becomes the C++ call's error), and a non-const reference parameter a pointer the call writes back through. "
                      "Enumerations are integer types with a constant per enumerator.");
    return comment.render();
}

std::string Generator::registration() const {
    std::ostringstream out;
    out << "\nfunc init() {\n\trosetta.Register(rosetta.Registration{\n\t\tWrap: []func(*rosetta.Handle) any{\n";
    for (const auto& owner : class_order_) {
        const auto& name = classes_.at(owner).name;
        out << "\t\t\tfunc(h *rosetta.Handle) any { x := make" << name << "(h); return &x },\n";
    }
    out << "\t\t},\n\t\tHandleOf: func(v any) (*rosetta.Handle, bool) {\n\t\t\tif o, ok := v.(Object); ok {\n\t\t\t\treturn o.nativeRef(), true\n\t\t\t}\n"
        << "\t\t\treturn nil, false\n\t\t},\n"
        << "\t\tNewError: func(kind, message string, cause error) error {\n\t\t\treturn &Error{Kind: ErrorKind(kind), Message: message, Err: cause}\n\t\t},\n";
    // gofmt writes empty composite literals on one line: leave empty fields out.
    if (!generic_order_.empty()) out << "\t\tGenerics: [][]string{\n";
    for (const auto& name : generic_order_) {
        out << "\t\t\t{";
        const auto found = generic_names_.find(name);
        if (found != generic_names_.end())
            for (std::size_t i = 0; i < found->second.size(); ++i) out << (i ? ", " : "") << go_string(found->second[i]);
        out << "},\n";
    }
    if (!generic_order_.empty()) out << "\t\t},\n";
    if (!enum_names_.empty()) out << "\t\tEnums: []rosetta.Enum{\n";
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        out << "\t\t\t{Zero: " << name << "(0), Names: []string{";
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            out << (i ? ", " : "") << go_string(renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]));
        out << "}, Values: []int64{";
        for (std::size_t i = 0; i < enumeration.enumerator_values.size(); ++i) {
            const auto& value = enumeration.enumerator_values[i];
            const bool big = !value.empty() && value[0] != '-' && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            out << (i ? ", " : "") << (big ? "rosetta.Bits(" + value + ")" : value);
        }
        out << "}},\n";
    }
    if (!enum_names_.empty()) out << "\t\t},\n";
    out << "\t})\n}\n";
    return out.str();
}

GeneratedGoFullSource Generator::run() {
    package_ = options_.package.empty() ? name_ : options_.package;
    for (auto& c : package_) c = lower(c);
    if (!identifier(package_) || go_keyword(package_))
        throw BindingError("Go package name must be an identifier and not a keyword: " + package_);
    module_path_ = options_.module_path.empty() ? package_ : options_.module_path;
    library_ = options_.library.empty() ? name_ : options_.library;
    GeneratedGoFullSource result;
    prepare();
    // Package-level names: the runtime's first, then classes and their interfaces, enums.
    for (const char* reserved : {"Error", "ErrorKind", "BindingError", "InvalidArgument", "OutOfRange", "DomainError", "LengthError",
                                 "ArithmeticError", "AllocationError", "NativeError", "CallbackError", "Must", "Cast", "Object", "Pair",
                                 "Tuple1", "Tuple2", "Tuple3", "Tuple4", "Tuple5", "Tuple6", "Tuple7", "Tuple8"})
        package_names_.reserve(reserved);
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = package_names_.claim(pascal(plan.name));
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        interfaces_[owner] = package_names_.claim(plan.name + "Like");
        for (const auto& base : plan.bases)
            for (const auto& other : class_order_) if (derives(base, other)) derived_[other].insert(owner);
        receivers_[owner] = std::string(1, lower(plan.name[0]));
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = package_names_.claim(pascal(name));
    }
    std::map<std::string, std::vector<std::string>> enum_constants;
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        for (const auto& enumerator : enumeration.enumerators)
            enum_constants[qualified].push_back(package_names_.claim(name + pascal(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Subpackages (policy modules): a Go package each, its functions in its own scope.
    std::map<std::string, std::string> subpackages;  // policy module -> Go package name
    for (const auto& group : functions) {
        if (group.module.empty() || subpackages.count(group.module)) continue;
        const auto dot = group.module.rfind('.');
        const auto last = group.module.substr(dot == std::string::npos ? 0 : dot + 1);
        std::string go;
        for (const char c : last) if (std::isalnum(static_cast<unsigned char>(c))) go += lower(c);
        if (go.empty() || std::isdigit(static_cast<unsigned char>(go[0]))) go = "m" + go;
        if (go_keyword(go) || go == package_ || go == "internal") go += "pkg";
        subpackages[group.module] = go;
    }
    // variants_ is keyed by group address: the order is final before naming.
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Go package " + package_ + " (" + name_ + ", full API).");

    // ------------------------------------------------------------ the main package
    std::ostringstream go;
    go << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\n" << package_doc() << "package " << package_ << "\n\n"
       << "import rosetta \"" << module_path_ << "/internal/rosetta\"\n";
    go << detail::go_full_public;
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        bool big = false;
        for (const auto& value : enumeration.enumerator_values)
            if (!value.empty() && value[0] != '-' && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"))) big = true;
        auto comment = doc_of(enumeration.documentation, name, name + " is the C++ enumeration " + qualified + ".", false);
        comment.paragraph("Its String method returns the C++ enumerator name.");
        go << "\n" << comment.render() << "type " << name << " " << (big ? "uint64" : "int") << "\n";
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto& value = enumeration.enumerator_values[i];
            const auto& constant = enum_constants[qualified][i];
            go << "\n// " << constant << " is C++ " << qualified << "::" << enumeration.enumerators[i] << " (" << value << ").\n"
               << "const " << constant << " " << name << " = " << value << "\n";
        }
        go << "\n// String returns the C++ enumerator name, or the number for other values.\nfunc (k " << name << ") String() string {\n\tswitch k {\n";
        std::set<std::string> values;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            if (!values.insert(enumeration.enumerator_values[i]).second) continue;
            go << "\tcase " << enum_constants[qualified][i] << ":\n\t\treturn "
               << go_string(renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i])) << "\n";
        }
        go << "\t}\n\treturn " << (big ? "rosetta.Utoa(uint64(k))" : "rosetta.Itoa(int64(k))") << "\n}\n";
    }
    for (const auto& owner : class_order_) emit_class(go, owner);
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        const auto& variants = variants_.at(&group);
        for (const auto& variant : variants) emit_variant(go, variant, variants, &group, nullptr, false);
    }
    for (const auto& constant : constants) {
        const auto type = go_type(*constant.type, false);
        const auto& name = constant_names_.at(&constant);
        auto about = doc_of(*constant.documentation, name, name + " returns the C++ constant " + normalize(constant.expression) + ".", false);
        go << "\n" << about.render() << "func " << name << "() " << type << " {\n\treturn rosetta.MustResult[" << type << "](rosetta.Call("
           << constant.index << "))\n}\n";
    }
    go << registration();
    result.package = go.str();

    // ------------------------------------------------------------ subpackages
    for (const auto& [module, go_name] : subpackages) {
        std::ostringstream sub;
        auto alias = package_ == go_name ? std::string("root") : package_;
        qualifier_ = alias + ".";
        Comment comment;
        std::string space;
        for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) space = ns;
        comment.paragraph("Package " + go_name + " holds the functions of the C++ namespace " + space + " (policy module " + module + ") of [" +
                          module_path_ + "].");
        sub << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\n" << comment.render() << "package " << go_name << "\n\n"
            << "import (\n\t" << alias << " \"" << module_path_ << "\"\n\trosetta \"" << module_path_ << "/internal/rosetta\"\n)\n"
            << "\n// The parent package registers the classes and enums this package's results use.\nvar _ " << alias << ".Object\n";
        for (const auto& group : functions) {
            if (group.module != module) continue;
            const auto& variants = variants_.at(&group);
            for (const auto& variant : variants) emit_variant(sub, variant, variants, &group, nullptr, false);
        }
        qualifier_.clear();
        std::string path = module;
        for (auto& c : path) if (c == '.') c = '/';
        const auto slash = path.rfind('/');
        path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + go_name;
        result.files.emplace_back(path + "/" + go_name + ".go", sub.str());
    }

    // ------------------------------------------------------------ go.mod and the runtime
    result.files.emplace_back("go.mod", "module " + module_path_ + "\n\ngo 1.24\n");
    const auto runtime_text = [&](std::string text) {
        const auto replace = [&](const std::string& from, const std::string& to) {
            for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
        };
        replace("@C@", "rosetta_" + name_);
        replace("@LIB@", library_);
        replace("@MODULE@", module_path_);
        return text;
    };
    result.files.emplace_back("internal/rosetta/rosetta.go", "// Code generated by Rosetta; DO NOT EDIT.\n\n" + runtime_text(detail::go_full_runtime));
    result.files.emplace_back("internal/rosetta/exports.go", "// Code generated by Rosetta; DO NOT EDIT.\n\n" + runtime_text(detail::go_full_exports));
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedGoFullSource generate_go_full(const Module& module, const std::string& name, bool skip_unsupported,
                                       const BindingPolicy& policy, const GoFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Go source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
