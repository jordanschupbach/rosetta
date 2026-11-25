// Rust backend (--language rust): a Cargo crate over the shared C ABI library of the full C ABI
// backends (cabi_full.hpp, lisp_full_runtime.hpp).
//
// Classes become structs holding a handle (cheap to clone; Clone shares the C++ object). A derived
// class holds its bases and derefs to its primary base, so base methods apply and &Derived passes
// where &Base is expected; as_base() reaches every ancestor. Overloads and default arguments become
// distinct names (snake_case), resolved again at run time in C++. Calls that can throw return
// Result<T> with a typed Error; noexcept functions return T. Enums are newtypes over i64 with an
// associated constant per enumerator, containers Vec/BTreeMap/HashMap/..., std::function closures,
// non-const reference parameters &mut T written back, and a Rust value implements C++ virtual
// methods through a trait (XImpl) and X::from_impl. Doxygen becomes rustdoc.
//
// The output is a crate: Cargo.toml, build.rs (the library search path), src/lib.rs (the API) and
// src/rosetta.rs (the runtime: the extern "C" declarations, value encoding, callbacks).
#include "rosetta/rust.hpp"
#include "cabi_full.hpp"
#include "rust_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

bool rust_keyword(const std::string& name) {
    static const std::set<std::string> words = {"as", "break", "const", "continue", "crate", "else", "enum", "extern", "false",
        "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self",
        "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", "async", "await", "dyn",
        "abstract", "become", "box", "do", "final", "macro", "override", "priv", "typeof", "unsized", "virtual", "yield", "try",
        "gen", "union"};
    return words.count(name) != 0;
}
char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// A type name: snake_case, camelCase or a__b qualified names in PascalCase.
std::string pascal(const std::string& name) {
    std::string result;
    bool start = true;
    for (const char c : name) {
        if (c == '_' || !is_alnum(c)) { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    if (result.empty()) return "X";
    if (is_digit(result[0])) result = "X" + result;
    if (result == "Self") result += "_";
    return result;
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
// A function, method or parameter name: snake_case, never a keyword.
std::string snake(const std::string& name) {
    auto result = snake_words(name);
    if (rust_keyword(result)) result += "_";
    return result;
}
std::string screaming(const std::string& name) {
    auto result = snake_words(name);
    for (auto& c : result) c = upper(c);
    return result;
}
std::string rust_string(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\t') result += "\\t";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + '"';
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
bool starts(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }
bool ends(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Names in one Rust scope (the types or the values of a module, the members of a type).
struct Names {
    std::set<std::string> taken;
    bool reserve(const std::string& name) { return taken.insert(name).second; }
    bool free(const std::string& name) const { return !taken.count(name); }
    // The wanted name, else with a number (get_name_2, ON_2; Pascal2).
    std::string claim(const std::string& wanted) {
        if (taken.insert(wanted).second) return wanted;
        const bool upper_case = wanted.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
        const bool lower_case = wanted.find_first_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos;
        const bool separated = !(upper_case && lower_case);
        for (int i = 2;; ++i) {
            const auto candidate = wanted + (separated ? "_" : "") + std::to_string(i);
            if (taken.insert(candidate).second) return candidate;
        }
    }
    // A type name: the wanted name, else with a trailing underscore (String_), else a number.
    std::string claim_type(const std::string& wanted) {
        if (taken.insert(wanted).second) return wanted;
        return claim(wanted + "_");
    }
};

// ---------------------------------------------------------------- doc comments
// Markdown lines of a rustdoc comment: "" is a blank line. Code blocks are fenced as text, so
// cargo test never compiles C++ examples as doctests.
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
        lines.push_back("# " + one_line(trim(text)));
        lines.push_back("");
    }
    void code(const std::string& text) {
        blank();
        lines.push_back("```text");
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
        if (!title.empty()) heading(title);
        else blank();
        for (const auto& item : items) lines.push_back("- " + item);
        lines.push_back("");
    }
    std::string render(const std::string& indent = {}, const std::string& marker = "///") const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        std::string out;
        bool previous_blank = true;
        bool fenced = false;
        for (const auto& line : copy) {
            if (line.empty() && previous_blank && !fenced) continue;
            if (starts(line, "```")) fenced = !fenced;
            previous_blank = line.empty();
            out += indent + marker + (line.empty() ? "" : " " + line) + "\n";
        }
        return out;
    }
};

// C++ documentation text as Markdown rustdoc reads literally: brackets are no intra-doc links,
// < no HTML tag, and a line starting with > or # no quote or heading; URLs become autolinks.
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

std::string kind_of_exception(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "InvalidArgument";
    if (bare == "std::out_of_range") return "OutOfRange";
    if (bare == "std::domain_error") return "DomainError";
    if (bare == "std::length_error") return "LengthError";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "ArithmeticError";
    if (bare == "std::bad_alloc") return "Allocation";
    return "Native";
}

// C++ operators and their Rust method names ("" = not bound).
std::string rust_operator(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"pos", "add"}}, {"operator-", {"neg", "sub"}}, {"operator*", {nullptr, "mul"}}, {"operator/", {nullptr, "div"}},
        {"operator%", {nullptr, "rem"}}, {"operator==", {nullptr, "equal"}}, {"operator!=", {nullptr, "not_equal"}},
        {"operator<", {nullptr, "less"}}, {"operator<=", {nullptr, "less_equal"}}, {"operator>", {nullptr, "greater"}},
        {"operator>=", {nullptr, "greater_equal"}}, {"operator&", {nullptr, "bit_and"}}, {"operator|", {nullptr, "bit_or"}},
        {"operator^", {nullptr, "bit_xor"}}, {"operator~", {"complement", nullptr}}, {"operator!", {"not", nullptr}},
        {"operator<<", {nullptr, "shl"}}, {"operator>>", {nullptr, "shr"}}, {"operator[]", {nullptr, "at"}},
        {"operator+=", {nullptr, "add_assign"}}, {"operator-=", {nullptr, "sub_assign"}}, {"operator*=", {nullptr, "mul_assign"}},
        {"operator/=", {nullptr, "div_assign"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// One Rust function or method: an overload called with its first `arity` C++ parameters.
struct Variant {
    const Overload* overload = nullptr;
    std::size_t arity = 0;
    std::string name;
    std::string link;   // doc link target ([`name`] or [`Type::name`])
};

enum class Mode { param, owned };

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const RustOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedRustSource run();

private:
    RustOptions options_;
    std::string crate_, library_;
    Names types_;                                                 // the crate root's type namespace
    Names values_;                                                // the crate root's value namespace
    std::map<std::string, Names> module_values_;                  // policy module -> its value namespace
    std::map<const Group*, std::vector<Variant>> variants_;
    std::map<std::string, std::vector<Variant>> constructors_;    // class -> variants
    std::map<std::string, std::vector<Variant>> directors_;       // class -> from_impl variants
    std::map<std::string, Names> members_;                        // class -> associated names
    std::map<std::string, std::string> traits_;                   // class -> XImpl
    struct FieldNames { std::string getter, setter; };
    std::map<const FieldPlan*, FieldNames> field_names_;
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::string> links_;                    // C++ name -> rustdoc link
    std::map<std::string, std::vector<std::string>> enum_constants_;
    std::map<std::string, std::vector<Overload>> public_constructors_; // class -> the constructors new calls

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Rust"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return rust_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken = {"self", "rc", "reply", "imp", "this", "handle", "args", "generic"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : snake(cpp);
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + signature(overload, snake(group.name), overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + signature(overload, plan.name + "::new", overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: create it with " + plan.name + "::from_impl and a Rust implementation of its virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: create it with " + plan.name + "::from_impl";
    }
    std::string symbol_prefix() const override { return "rosetta_rs_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_RUST"; }
    std::string module_prefix() const override { return "rosetta_rs_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    bool keyable(const Type& type) const;
    std::string rust_type(const Type& type, Mode mode) const;
    std::string rust_spelled(std::string cpp, Mode mode) const;
    std::string parameter_type(const Type& type) const { return writeback(type) ? "&mut " + rust_type(type, Mode::owned) : rust_type(type, Mode::param); }
    std::string result_type(const Function& function) const { return is_void(function.result) ? std::string("()") : rust_type(function.result, Mode::owned); }
    static std::string word(std::string type);
    std::string signature(const Overload& overload, const std::string& name, std::size_t arity) const;
    std::string rust_default(const DefaultValue& value, const Type& type) const;
    std::string argument(const Type& type, const std::string& name) const;     // rc.arg(...) for a parameter
    std::string borrowed(const Type& type, const std::string& owned) const;    // a parameter from an owned value

    // ------------------------------------------------------------ naming
    std::vector<Variant> plan_variants(const std::vector<Overload>& overloads, const std::string& base) const;
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);

    // ------------------------------------------------------------ emission
    std::string link(const std::string& cpp) const;
    Doc doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Doc& comment, const Documentation& doc) const;
    Doc variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const;
    void emit_variant(std::ostream& out, const Variant& variant, const std::vector<Variant>& siblings, const Group* group,
                      const ClassPlan* plan, bool method, const std::string& indent) const;
    void emit_class(std::ostream& out, const std::string& owner) const;
    void emit_director(std::ostream& out, const std::string& owner) const;
    void emit_enum(std::ostream& out, const std::string& qualified) const;
    std::string crate_doc() const;
};

// ---------------------------------------------------------------- types
bool Generator::keyable(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::enumeration) return enum_names_.count(target.declaration) != 0;
    if (target.kind == TypeKind::builtin) {
        const auto bare = normalize(strip_cv(target.canonical));
        return bare != "float" && bare != "double" && bare != "long double" && bare != "void";
    }
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) return strip_cv(target.arguments.front().canonical) == "char";
    const auto bare = normalize(strip_cv(target.canonical));
    if (bare == "std::filesystem::path") return true;
    if (target.kind != TypeKind::template_instance) return false;
    const auto declaration = normalize(target.declaration);
    if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return true;
    if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
        declaration == "std::optional" || declaration == "std::set")
        return !target.arguments.empty() && keyable(target.arguments.front());
    if (declaration == "std::pair" || declaration == "std::tuple" || declaration == "std::map") {
        for (const auto& argument : target.arguments) if (!keyable(argument)) return false;
        return true;
    }
    return false;
}

std::string Generator::rust_type(const Type& type, Mode mode) const {
    const bool param = mode == Mode::param;
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return param ? "&str" : "String";
        if (bound(pointee)) {
            const auto& name = classes_.at(pointee.declaration).name;
            return param ? "Option<&" + name + ">" : "Option<" + name + ">";
        }
        if (is_void(pointee)) return param ? "&Value" : "Value";
        return "Option<" + rust_type(pointee, Mode::owned) + ">";
    }
    if (is_void(target)) return "()";
    if (!conversion(target).empty()) return rust_spelled(policy_.conversions.at(conversion(target)), mode);
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        const auto spelled = normalize(strip_cv(target.spelling));
        if (spelled == "size_t" || spelled == "std::size_t") return "usize";
        if (spelled == "ssize_t" || spelled == "ptrdiff_t" || spelled == "std::ptrdiff_t") return "isize";
        static const std::map<std::string, std::string> builtins = {{"bool", "bool"}, {"char", "char"}, {"signed char", "i8"},
            {"unsigned char", "u8"}, {"short", "i16"}, {"unsigned short", "u16"}, {"int", "i32"}, {"unsigned int", "u32"},
            {"long", "i64"}, {"unsigned long", "u64"}, {"long long", "i64"}, {"unsigned long long", "u64"}, {"float", "f32"},
            {"double", "f64"}, {"long double", "f64"}, {"char16_t", "u16"}, {"char32_t", "u32"}, {"wchar_t", "i32"}};
        const auto found = builtins.find(bare);
        return found != builtins.end() ? found->second : param ? "&Value" : "Value";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? found->second : "i64";
    }
    if (bound(target)) {
        const auto& name = classes_.at(target.declaration).name;
        return param ? "&" + name : name;
    }
    if (bare == "std::filesystem::path") return param ? "&str" : "String";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "()";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto owned = [&](std::size_t i) { return i < target.arguments.size() ? rust_type(target.arguments[i], Mode::owned) : std::string("Value"); };
        const auto stringy = [&](std::size_t i) {
            if (i >= target.arguments.size()) return false;
            const auto& argument = referent(target.arguments[i]);
            const auto name = normalize(argument.declaration);
            return (argument.kind == TypeKind::template_instance && (name == "std::basic_string" || name == "std::basic_string_view")) ||
                   normalize(strip_cv(argument.canonical)) == "std::filesystem::path";
        };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return param ? "&str" : "String";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span") {
            if (!param) return "Vec<" + owned(0) + ">";
            return stringy(0) ? "&[impl AsRef<str>]" : "&[" + owned(0) + "]";
        }
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset" || declaration == "std::unordered_multiset") {
            const bool keyed = !target.arguments.empty() && keyable(target.arguments[0]) && declaration.find("multi") == std::string::npos;
            const auto container = !keyed ? "Vec<" + owned(0) + ">" : (declaration == "std::set" ? "BTreeSet<" : "HashSet<") + owned(0) + ">";
            if (!param) return container;
            return keyed ? "&" + container : "&[" + owned(0) + "]";
        }
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" || declaration == "std::unordered_multimap") {
            const bool keyed = !target.arguments.empty() && keyable(target.arguments[0]) && declaration.find("multi") == std::string::npos;
            const auto pair = "(" + owned(0) + ", " + owned(1) + ")";
            if (!keyed) return param ? "&[" + pair + "]" : "Vec<" + pair + ">";
            const auto container = (declaration == "std::map" ? "BTreeMap<" : "HashMap<") + owned(0) + ", " + owned(1) + ">";
            return param ? "&" + container : container;
        }
        if (declaration == "std::optional") {
            if (target.arguments.empty()) return "Option<Value>";
            if (!param) return "Option<" + owned(0) + ">";
            const auto inner = rust_type(target.arguments[0], Mode::param);
            return "Option<" + (inner.find("impl ") != std::string::npos ? owned(0) : inner) + ">";
        }
        if (declaration == "std::complex") return "Complex";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            if (target.arguments.empty()) return param ? "&Value" : "Value";
            return rust_type(target.arguments[0], mode);
        }
        if (declaration == "std::pair" && target.arguments.size() == 2) return "(" + owned(0) + ", " + owned(1) + ")";
        if (declaration == "std::tuple") {
            const auto count = target.arguments.size();
            if (count == 0) return "()";
            if (count > 8) return param ? "&Value" : "Value";
            std::string items;
            for (std::size_t i = 0; i < count; ++i) items += (i ? ", " : "") + owned(i);
            return "(" + items + (count == 1 ? ",)" : ")");
        }
        if (declaration == "std::variant") return param ? "&Value" : "Value";
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string items;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) items += (i > 1 ? ", " : "") + rust_type(function.arguments[i], Mode::owned);
            const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
            const auto signature = "Fn(" + items + ")" + (returns ? " -> " + rust_type(function.arguments[0], Mode::owned) : std::string());
            return param ? "impl " + signature + " + Send + Sync + 'static" : "Arc<dyn " + signature + " + Send + Sync>";
        }
        if (declaration == "std::chrono::duration") return "f64";
    }
    return param ? "&Value" : "Value";
}

std::string Generator::rust_spelled(std::string cpp, Mode mode) const {
    const bool param = mode == Mode::param;
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::map<std::string, std::string> scalars = {{"bool", "bool"}, {"char", "char"}, {"signed char", "i8"},
        {"unsigned char", "u8"}, {"short", "i16"}, {"unsigned short", "u16"}, {"int", "i32"}, {"unsigned int", "u32"},
        {"long", "i64"}, {"unsigned long", "u64"}, {"long long", "i64"}, {"unsigned long long", "u64"}, {"float", "f32"},
        {"double", "f64"}, {"long double", "f64"}, {"std::size_t", "usize"}, {"size_t", "usize"}, {"std::int64_t", "i64"},
        {"std::uint64_t", "u64"}, {"std::int32_t", "i32"}, {"std::uint32_t", "u32"}, {"std::int16_t", "i16"}, {"std::uint16_t", "u16"},
        {"std::int8_t", "i8"}, {"std::uint8_t", "u8"}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return found->second;
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return param ? "&str" : "String";
    for (const auto& [qualified, plan] : classes_) if (normalize(qualified) == cpp) return param ? "&" + plan.name : plan.name;
    for (const auto& [qualified, name] : enum_names_) if (normalize(qualified) == cpp) return name;
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') return param ? "&Value" : "Value";
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
    const auto owned = [&](std::size_t i) { return i < arguments.size() ? rust_spelled(arguments[i], Mode::owned) : std::string("Value"); };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::valarray")
        return param ? "&[" + owned(0) + "]" : "Vec<" + owned(0) + ">";
    if (name == "std::optional") return "Option<" + owned(0) + ">";
    if (name == "std::complex") return "Complex";
    if (name == "std::pair" && arguments.size() == 2) return "(" + owned(0) + ", " + owned(1) + ")";
    if (name == "std::tuple" && !arguments.empty() && arguments.size() <= 8) {
        std::string items;
        for (std::size_t i = 0; i < arguments.size(); ++i) items += (i ? ", " : "") + owned(i);
        return "(" + items + (arguments.size() == 1 ? ",)" : ")");
    }
    return param ? "&Value" : "Value";
}

// A word naming a Rust type in a generated name: &[f64] -> f64s, &Shape -> shape.
std::string Generator::word(std::string type) {
    if (starts(type, "&mut ")) type = type.substr(5);
    if (starts(type, "&")) type = type.substr(1);
    if (type == "str" || type == "String") return "str";
    if (type == "[impl AsRef<str>]") return "strs";
    if (type == "[u8]" || type == "Vec<u8>") return "bytes";
    if (starts(type, "[") && ends(type, "]")) return word(type.substr(1, type.size() - 2)) + "s";
    if (starts(type, "Vec<") && ends(type, ">")) return word(type.substr(4, type.size() - 5)) + "s";
    if (starts(type, "Option<") && ends(type, ">")) return word(type.substr(7, type.size() - 8));
    if (starts(type, "impl Fn") || starts(type, "Arc<dyn Fn")) return "fn";
    if (starts(type, "BTreeMap") || starts(type, "HashMap")) return "map";
    if (starts(type, "BTreeSet") || starts(type, "HashSet")) return "set";
    if (type == "()") return "unit";
    if (starts(type, "(")) return "tuple";
    return snake_words(type);
}

std::string Generator::signature(const Overload& overload, const std::string& name, std::size_t arity) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string result = name + "(";
    for (std::size_t i = first; i < arity && i < function.parameters.size(); ++i)
        result += (i > first ? ", " : "") + names[i - first] + ": " + parameter_type(function.parameters[i]);
    result += ")";
    if (!function.is_constructor && !is_void(function.result)) result += " -> " + result_type(function);
    return result;
}

std::string Generator::rust_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant" || value.kind == "string") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "None";
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "enum") {
        const auto found = enum_names_.find(referent(type).declaration);
        return found != enum_names_.end() ? found->second + "::" + screaming(value.value) : value.value;
    }
    if (value.kind == "default") return "the default value";
    return {};
}

// The statement passing parameter `name` of C++ type `type` to the call `rc`.
std::string Generator::argument(const Type& type, const std::string& name) const {
    const auto rust = rust_type(type, Mode::param);
    if (starts(rust, "impl Fn")) return "rc.arg(&(Arc::new(" + name + ") as " + rust_type(type, Mode::owned) + "));";
    if (rust == "&[impl AsRef<str>]") return "rc.arg(&rosetta::Strs(" + name + "));";
    return "rc.arg(&" + name + ");";
}

// The expression passing an owned value (what C++ gave a virtual method) as a parameter of `type`.
std::string Generator::borrowed(const Type& type, const std::string& owned) const {
    if (writeback(type)) return "&mut " + owned;
    const auto rust = rust_type(type, Mode::param);
    if (starts(rust, "&")) return "&" + owned;
    if (starts(rust, "Option<&str")) return owned + ".as_deref()";
    if (starts(rust, "Option<&")) return owned + ".as_ref()";
    if (starts(rust, "impl Fn")) {
        const auto& target = referent(type);
        const auto& function = target.arguments[0];
        std::string parameters, arguments;
        for (std::size_t i = 1; i < function.arguments.size(); ++i) {
            parameters += (i > 1 ? ", " : "") + std::string("x") + std::to_string(i) + ": " + rust_type(function.arguments[i], Mode::owned);
            arguments += (i > 1 ? ", " : "") + std::string("x") + std::to_string(i);
        }
        return "move |" + parameters + "| " + owned + "(" + arguments + ")";
    }
    return owned;
}

// ---------------------------------------------------------------- naming
// The Rust functions of a group of overloads: one per overload and arity (default arguments),
// with duplicates of an earlier Rust signature dropped (C++ resolves the call at run time anyway).
// The overload taking the fewest arguments keeps `base`; the others add the names of their
// required parameters (new_x_y), else of all their parameters, else their parameter types
// (pick_f64), else a number. Longer arities of one overload
// add "with" and the last parameter's name (add_with_c).
std::vector<Variant> Generator::plan_variants(const std::vector<Overload>& overloads, const std::string& base) const {
    struct Candidate { const Overload* overload; std::vector<std::size_t> arities; std::string by_required, by_name, by_type; std::size_t minimum; };
    std::vector<Candidate> candidates;
    std::set<std::string> seen;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        Candidate candidate{&overload, {}, {}, {}, {}, 0};
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
            else candidate.by_name += "_" + snake_words(cpp);
            candidate.by_type += "_" + word(parameter_type(function.parameters[i]));
        }
        if (!named) candidate.by_name.clear();
        // The names of the parameters its shortest function takes (new_x_y_knots, whose longer
        // arity adds with_degree), when they tell it apart.
        if (named)
            for (std::size_t i = first; i < candidate.arities.front(); ++i) candidate.by_required += "_" + snake_words(function.parameter_names[i]);
        candidates.push_back(candidate);
    }
    std::size_t primary = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) if (candidates[i].minimum < candidates[primary].minimum) primary = i;
    // A keyword's escape (box_) is dropped before a suffix: box_with_color, not box__with_color.
    const auto joinable = [](const std::string& name) {
        return name.size() > 1 && name.back() == '_' && rust_keyword(name.substr(0, name.size() - 1)) ? name.substr(0, name.size() - 1) : name;
    };
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
            if (unique(&Candidate::by_required)) stem = joinable(stem) + candidate.by_required;
            else if (unique(&Candidate::by_name)) stem = joinable(stem) + candidate.by_name;
            else if (unique(&Candidate::by_type)) stem = joinable(stem) + candidate.by_type;
            else stem += std::to_string(c + 1);
            if (rust_keyword(stem)) stem += "_";
        }
        const auto& function = *candidate.overload->function;
        const std::size_t first = candidate.overload->receives_self ? 1 : 0;
        for (std::size_t a = 0; a < candidate.arities.size(); ++a) {
            const auto arity = candidate.arities[a];
            std::string name = a > 0 ? joinable(stem) : stem;
            if (a > 0) {
                const auto cpp = arity - 1 < function.parameter_names.size() ? function.parameter_names[arity - 1] : std::string();
                name += "_with_" + (cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(arity - first) : snake_words(cpp));
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
        for (const auto* reserved : {"close", "is_closed", "rosetta_director"}) members.reserve(reserved);
        for (const auto& other : class_order_)
            if (other != owner && derives(owner, other)) members.reserve("as_" + snake_words(classes_.at(other).name));
        const bool constructible = !cls.record->abstract;
        if (!cls.constructors.empty()) {
            auto& visible = public_constructors_[owner];
            for (const auto& overload : cls.constructors)
                if (constructible && overload.function->access == "public") visible.push_back(overload);
            if (!visible.empty()) plan(constructors_[owner], visible, "new", members, cls.name + "::");
            if (cls.director) plan(directors_[owner], cls.constructors, "from_impl", members, cls.name + "::");
        }
        for (auto& group : cls.methods) plan(variants_[&group], group.overloads, snake(group.name), members, cls.name + "::");
        for (auto& group : cls.operators) plan(variants_[&group], group.overloads, group.name + (group.operand ? "_r" : ""), members, cls.name + "::");
    }
    for (auto& group : functions)
        plan(variants_[&group], group.overloads, snake(group.name), group.module.empty() ? values_ : module_values_[group.module], "");
    // Constructors from a C++ object keep their names first: new before any method named new.
    for (int pass = 0; pass < 2; ++pass)
        for (auto& item : planned)
            for (auto& variant : *item.variants)
                if ((pass == 1 || item.base == "new" || item.base == "from_impl") && variant.name == item.base && variant.link.empty() &&
                    item.scope->free(variant.name)) {
                    item.scope->reserve(variant.name);
                    variant.link = "!";
                    break;
                }
    for (auto& item : planned)
        for (auto& variant : *item.variants) {
            if (variant.link == "!") variant.link.clear();
            else variant.name = item.scope->claim(variant.name);
            variant.link = "[`" + item.link + variant.name + "`]";
        }
    // Fields (getter x, setter set_x) and constants, after the methods.
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        for (auto& field : cls.fields) {
            const auto& f = *field.field;
            FieldNames names;
            const auto base = snake(field.name);
            names.getter = members.free(base) ? members.claim(base) : members.claim(snake_words(field.name) + "_field");
            const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
            const auto setter = "set_" + snake_words(field.name);
            if (writable) names.setter = members.free(setter) ? members.claim(setter) : members.claim(setter + "_field");
            field_names_[&field] = names;
        }
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(snake(constant.name));
    }
    for (auto& constant : constants) constant_names_[&constant] = values_.claim(snake(constant.name));
    // Doc links.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        links_[owner] = "[`" + cls.name + "`]";
        links_[cls.record->name] = links_[owner];
        for (const auto& group : cls.methods) {
            const auto& first = variants_.at(&group);
            if (first.empty()) continue;
            links_[owner + "::" + group.name] = first.front().link;
            links_[cls.record->name + "::" + group.name] = first.front().link;
        }
    }
    for (const auto& group : functions) {
        const auto& variants = variants_.at(&group);
        if (variants.empty() || !group.module.empty()) continue;
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = variants.front().link;
            links_.emplace(overload.function->name, variants.front().link);
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = "[`" + name + "`]";
        links_.emplace(enums_.at(qualified)->name, "[`" + name + "`]");
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
    std::vector<std::string> parameters;
    std::string omitted;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (i < variant.arity) {
            for (const auto& item : doc.params)
                if (item.name == cpp && !item.text.empty()) parameters.push_back("`" + names[i - first] + "`: " + md(one_line(item.text)));
            continue;
        }
        const auto shown = rust_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        omitted += (omitted.empty() ? "`" : ", `") + names[i - first] + (shown.empty() ? std::string() : " = " + shown) + "`";
    }
    comment.list("Arguments", parameters);
    if (!function.is_constructor && !is_void(function.result) && !doc.returns.empty()) {
        auto text = md(one_line(doc.returns));
        while (!text.empty() && text.back() == '.') text.pop_back();
        if (text.size() > 1 && is_upper(text[0]) && is_lower(text[1])) text[0] = lower(text[0]);
        comment.paragraph("Returns " + text + ".");
    }
    std::vector<std::string> errors;
    for (const auto& item : doc.throws) {
        const auto kind = kind_of_exception(item.name);
        errors.push_back("[`ErrorKind::" + kind + "`]" + (item.name.empty() ? std::string() : " (C++ `" + item.name + "`)") +
                         (item.text.empty() ? std::string() : ": " + md(one_line(item.text))));
    }
    comment.list("Errors", errors);
    // The other arities of this overload.
    std::vector<std::string> longer;
    for (const auto& sibling : siblings)
        if (sibling.overload == variant.overload && sibling.arity > variant.arity) longer.push_back(sibling.link.empty() ? sibling.name : sibling.link);
    if (!omitted.empty()) {
        std::string text = "Uses the C++ defaults " + omitted;
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
                             const ClassPlan* plan, bool method, const std::string& indent) const {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string what;
    if (function.is_constructor) what = "the C++ constructor `" + plan->record->qualified_name() + "`";
    else if (overload.extension) what = "C++ `" + function.qualified_name() + "`";
    else if (plan) what = "the C++ method `" + plan->record->qualified_name() + "::" + function.name + "`";
    else what = "C++ `" + function.qualified_name() + "`";
    const std::size_t overloads = function.is_constructor ? plan->constructors.size() : group ? group->overloads.size() : 1;
    if (overloads > 1) {
        std::string spelled;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) spelled += (i ? ", " : "") + function.parameters[i].spelling;
        what = what.substr(0, what.size() - 1) + "(" + spelled + ")`";
    }
    auto comment = variant_doc(variant, siblings, what);
    // Parameters, and the statements passing them.
    std::string parameters;
    std::vector<std::string> statements;
    std::vector<std::pair<std::string, std::string>> slots;   // slot variable, parameter
    const auto add = [&](const Type& type, const std::string& name) {
        parameters += (parameters.empty() ? "" : ", ") + name + ": " + parameter_type(type);
        if (writeback(type)) {
            const auto slot = "s" + std::to_string(slots.size());
            statements.push_back("let " + slot + " = rc.slot(&*" + name + ");");
            slots.emplace_back(slot, name);
        } else statements.push_back(argument(type, name));
    };
    std::string head;
    const auto& owner_name = plan ? plan->name : std::string();
    if (function.is_constructor) {
        const auto owner = plan->record->qualified_name();
        const bool director = directors_.count(owner) &&
                              std::any_of(directors_.at(owner).begin(), directors_.at(owner).end(), [&](const Variant& v) { return &v == &variant; });
        if (director) {
            comment.paragraph("The C++ object is a subclass whose virtual methods call `imp`'s methods of the [`" + traits_.at(owner) +
                              "`] trait, which C++ calls back from any thread. [`implementation`] returns `imp` from the object.");
            parameters = "imp: impl " + traits_.at(owner);
        }
        for (std::size_t i = 0; i < variant.arity; ++i) add(function.parameters[i], names[i]);
        out << "\n" << comment.render(indent) << indent << "pub fn " << variant.name << "(" << parameters << ") -> Result<" << owner_name << "> {\n"
            << indent << "    let " << (statements.empty() ? "" : "mut ") << "rc = rosetta::Call::constructor(" << plan->constructor_index << ", "
            << (director ? "Some(Self::rosetta_director(imp))" : "None") << ");\n";
        for (const auto& statement : statements) out << indent << "    " << statement << "\n";
        out << indent << "    rc.construct().map(<" << owner_name << " as rosetta::Class>::from_handle)\n" << indent << "}\n";
        return;
    }
    // The arguments in C++ order: the receiver first, or at a free operator's bound operand.
    if (method && overload.extension && !overload.receives_self) {
        const std::size_t operand = group ? group->operand : 0;
        for (std::size_t i = 0; i < variant.arity; ++i) {
            if (i == operand) { statements.push_back("rc.arg(self);"); continue; }
            add(function.parameters[i], names[i]);
        }
    } else {
        if (method) statements.push_back("rc.arg(self);");
        for (std::size_t i = first; i < variant.arity; ++i) add(function.parameters[i], names[i - first]);
        // A virtual method on an object implemented in Rust calls the Rust implementation.
        const auto found = generic_index_.find(function.name);
        if (method && !overload.extension && function.is_virtual && found != generic_index_.end())
            statements.push_back("rc.virtual_method(" + std::to_string(found->second) + ", rosetta::Class::is_direct(self));");
    }
    const auto result = result_type(function);
    const bool fallible = !function.is_noexcept;
    std::string results = fallible ? " -> Result<" + result + ">" : result == "()" ? std::string() : " -> " + result;
    out << "\n" << comment.render(indent) << indent << "pub fn " << variant.name << "(" << (method ? std::string("&self") + (parameters.empty() ? "" : ", ") : "")
        << parameters << ")" << results << " {\n"
        << indent << "    let " << (statements.empty() ? "" : "mut ") << "rc = rosetta::Call::new(" << group->index << ");\n";
    for (const auto& statement : statements) out << indent << "    " << statement << "\n";
    if (slots.empty()) {
        if (fallible) out << indent << "    rc.run()?.value()\n";
        else out << indent << "    rosetta::must(rc.run().and_then(rosetta::Reply::value))\n";
    } else if (fallible) {
        out << indent << "    let mut reply = rc.run()?;\n";
        for (const auto& [slot, name] : slots) out << indent << "    reply.write_back(" << slot << ", " << name << ")?;\n";
        out << indent << "    reply.value()\n";
    } else {
        out << indent << "    let mut reply = rosetta::must(rc.run());\n";
        for (const auto& [slot, name] : slots) out << indent << "    rosetta::must(reply.write_back(" << slot << ", " << name << "));\n";
        out << indent << "    rosetta::must(reply.value())\n";
    }
    out << indent << "}\n";
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& constants = enum_constants_.at(qualified);
    auto comment = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    comment.paragraph("A newtype over the C++ value, with an associated constant per enumerator: other values pass through "
                      "unchanged. `Display` writes the C++ enumerator name (or the number).");
    add_notes(comment, enumeration.documentation);
    const auto value_of = [](const std::string& value) {
        const bool negative = !value.empty() && value[0] == '-';
        const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
        if (big) return std::to_string(static_cast<long long>(std::stoull(value)));
        if (value == "-9223372036854775808") return std::string("i64::MIN");
        return value;
    };
    out << "\n" << comment.render() << "#[derive(Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Default)]\npub struct " << name << "(pub i64);\n\n"
        << "impl " << name << " {\n";
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
        out << "    /// C++ `" << qualified << "::" << enumeration.enumerators[i] << "` (" << enumeration.enumerator_values[i] << ").\n"
            << "    pub const " << constants[i] << ": " << name << " = " << name << "(" << value_of(enumeration.enumerator_values[i]) << ");\n";
    out << "}\n\nimpl std::fmt::Display for " << name << " {\n    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
        << "        match self.0 {\n";
    std::set<std::string> values;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        const auto value = value_of(enumeration.enumerator_values[i]);
        if (!values.insert(value).second) continue;
        out << "            " << value << " => f.write_str(" << rust_string(renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i])) << "),\n";
    }
    out << "            n => write!(f, \"{}\", n),\n        }\n    }\n}\n\n"
        << "impl std::fmt::Debug for " << name << " {\n    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
        << "        match self.0 {\n";
    values.clear();
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        const auto value = value_of(enumeration.enumerator_values[i]);
        if (!values.insert(value).second) continue;
        out << "            " << value << " => f.write_str(\"" << name << "::" << constants[i] << "\"),\n";
    }
    out << "            n => write!(f, \"" << name << "({})\", n),\n        }\n    }\n}\n\n"
        << "impl rosetta::Encode for " << name << " {\n    fn encode(&self, w: &mut rosetta::Writer) -> Result<()> {\n"
        << "        rosetta::Encode::encode(&self.0, w)\n    }\n}\n\n"
        << "impl rosetta::FromValue for " << name << " {\n    fn from_value(value: Value) -> Result<Self> {\n"
        << "        <i64 as rosetta::FromValue>::from_value(value).map(" << name << ")\n    }\n}\n";
}

void Generator::emit_class(std::ostream& out, const std::string& owner) const {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& name = plan.name;
    // Paths to every ancestor through the base fields.
    std::map<std::string, std::string> paths{{owner, ""}};
    std::vector<std::string> queue{owner};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const auto& bases = classes_.at(queue[i]).bases;
        for (std::size_t b = 0; b < bases.size(); ++b)
            if (!paths.count(bases[b])) { paths[bases[b]] = paths[queue[i]] + (b == 0 ? ".base" : ".base" + std::to_string(b)); queue.push_back(bases[b]); }
    }
    // The struct.
    auto comment = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    if (!plan.bases.empty()) {
        comment.paragraph("It derefs to its C++ base class [`" + classes_.at(plan.bases.front()).name + "`], so the base's methods apply "
                          "to it and `&" + name + "` passes where `&" + classes_.at(plan.bases.front()).name + "` is expected.");
        if (plan.bases.size() > 1) {
            std::string others;
            for (std::size_t i = 1; i < plan.bases.size(); ++i)
                others += (i > 1 ? ", " : "") + std::string("[`") + name + "::as_" + snake_words(classes_.at(plan.bases[i]).name) + "`]";
            comment.paragraph("Its other C++ base classes: " + others + ".");
        }
    }
    if (plan.director)
        comment.paragraph(std::string(record.abstract ? "Abstract in C++: create one" : "Create one") + " with a Rust implementation of its "
                          "virtual methods (the [`" + traits_.at(owner) + "`] trait) through [`" + name + "::from_impl`]; C++ calls the Rust methods.");
    else if (record.abstract) comment.paragraph("Abstract in C++: C++ functions return objects of derived classes.");
    comment.paragraph("Cloning shares the C++ object. Dropping the last clone releases it (or `close` does at once); [`cast`] "
                      "converts to a derived class.");
    add_notes(comment, record.documentation);
    out << "\n" << comment.render() << "#[derive(Clone)]\npub struct " << name << " {\n";
    if (plan.bases.empty()) out << "    h: Arc<rosetta::Handle>,\n    direct: bool,\n";
    for (std::size_t i = 0; i < plan.bases.size(); ++i) out << "    base" << (i ? std::to_string(i) : "") << ": " << classes_.at(plan.bases[i]).name << ",\n";
    out << "}\n\n";
    // The runtime's traits.
    std::size_t index = 0;
    while (class_order_[index] != owner) ++index;
    out << "impl rosetta::Class for " << name << " {\n    const CLASS: u32 = " << index << ";\n"
        << "    fn from_handle(handle: Arc<rosetta::Handle>) -> Self {\n        " << name << " {";
    if (plan.bases.empty()) out << " h: handle, direct: false ";
    for (std::size_t i = 0; i < plan.bases.size(); ++i)
        out << (i ? ", " : " ") << "base" << (i ? std::to_string(i) : "") << ": <" << classes_.at(plan.bases[i]).name
            << " as rosetta::Class>::from_handle(" << (i + 1 < plan.bases.size() ? "handle.clone()" : "handle") << ")";
    out << (plan.bases.empty() ? "" : " ") << "}\n    }\n    fn handle(&self) -> &Arc<rosetta::Handle> {\n        "
        << (plan.bases.empty() ? std::string("&self.h") : "rosetta::Class::handle(&self.base)") << "\n    }\n"
        << "    fn is_direct(&self) -> bool {\n        " << (plan.bases.empty() ? std::string("self.direct") : "rosetta::Class::is_direct(&self.base)") << "\n    }\n"
        << "    fn set_direct(&mut self) {\n";
    if (plan.bases.empty()) out << "        self.direct = true;\n";
    for (std::size_t i = 0; i < plan.bases.size(); ++i) out << "        rosetta::Class::set_direct(&mut self.base" << (i ? std::to_string(i) : "") << ");\n";
    out << "    }\n}\n\n"
        << "impl rosetta::Encode for " << name << " {\n    fn encode(&self, w: &mut rosetta::Writer) -> Result<()> {\n"
        << "        rosetta::encode_object(w, rosetta::Class::handle(self))\n    }\n"
        << "    fn object_handle(&self) -> Option<&Arc<rosetta::Handle>> {\n        Some(rosetta::Class::handle(self))\n    }\n}\n\n"
        << "impl rosetta::FromValue for " << name << " {\n    fn from_value(value: Value) -> Result<Self> {\n"
        << "        rosetta::object_from(value, " << rust_string(name) << ")\n    }\n}\n\n"
        << "impl std::fmt::Debug for " << name << " {\n    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
        << "        rosetta::describe(f, " << rust_string(name) << ", rosetta::Class::handle(self))\n    }\n}\n";
    if (!plan.bases.empty())
        out << "\nimpl std::ops::Deref for " << name << " {\n    type Target = " << classes_.at(plan.bases.front()).name << ";\n"
            << "    fn deref(&self) -> &" << classes_.at(plan.bases.front()).name << " {\n        &self.base\n    }\n}\n";
    for (std::size_t i = 1; i < queue.size(); ++i) {
        const auto& ancestor = classes_.at(queue[i]).name;
        out << "\nimpl From<" << name << "> for " << ancestor << " {\n    fn from(x: " << name << ") -> " << ancestor << " {\n        x"
            << paths[queue[i]] << "\n    }\n}\n"
            << "\nimpl From<&" << name << "> for " << ancestor << " {\n    fn from(x: &" << name << ") -> " << ancestor << " {\n        Clone::clone(&x"
            << paths[queue[i]] << ")\n    }\n}\n";
    }
    // To string: the C++ to_string(), as Display.
    const Variant* to_string = nullptr;
    for (const auto& group : plan.methods) {
        if (group.is_static || group.name != "to_string") continue;
        for (const auto& variant : variants_.at(&group))
            if (variant.arity == (variant.overload->receives_self ? 1u : 0u) && result_type(*variant.overload->function) == "String") to_string = &variant;
    }
    if (to_string) {
        out << "\n/// Writes the C++ `to_string()` text.\nimpl std::fmt::Display for " << name << " {\n"
            << "    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n";
        if (to_string->overload->function->is_noexcept) out << "        f.write_str(&self." << to_string->name << "())\n";
        else out << "        match self." << to_string->name << "() {\n            Ok(text) => f.write_str(&text),\n"
                 << "            Err(error) => write!(f, \"<{}>\", error),\n        }\n";
        out << "    }\n}\n";
    }
    // Associated functions and methods.
    out << "\nimpl " << name << " {";
    if (plan.bases.empty())
        out << "\n    /// Releases the C++ object now (it is idempotent); later calls fail with an [`ErrorKind::Binding`] error.\n"
            << "    pub fn close(&self) {\n        rosetta::close(&self.h)\n    }\n"
            << "\n    /// Whether the C++ object is released.\n    pub fn is_closed(&self) -> bool {\n        rosetta::is_closed(&self.h)\n    }\n";
    for (std::size_t i = 1; i < queue.size(); ++i) {
        const auto& ancestor = classes_.at(queue[i]).name;
        out << "\n    /// The object as a [`" << ancestor << "`] (a C++ base class).\n    pub fn as_" << snake_words(ancestor) << "(&self) -> &" << ancestor
            << " {\n        &self" << paths[queue[i]] << "\n    }\n";
    }
    if (constructors_.count(owner)) for (const auto& variant : constructors_.at(owner)) emit_variant(out, variant, constructors_.at(owner), nullptr, &plan, false, "    ");
    if (directors_.count(owner)) for (const auto& variant : directors_.at(owner)) emit_variant(out, variant, directors_.at(owner), nullptr, &plan, false, "    ");
    for (const auto& group : plan.methods) {
        const auto& variants = variants_.at(&group);
        for (const auto& variant : variants) emit_variant(out, variant, variants, &group, &plan, !group.is_static, "    ");
    }
    for (const auto& group : plan.operators) {
        const auto& variants = variants_.at(&group);
        for (const auto& variant : variants) emit_variant(out, variant, variants, &group, &plan, true, "    ");
    }
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto& names = field_names_.at(&field);
        auto about = doc_of(f.documentation, "The C++ field `" + owner + "::" + f.name + "`.");
        if (bound(referent(f.type))) about.paragraph("The result refers to the field inside this object, which it keeps alive.");
        out << "\n" << about.render("    ") << "    pub fn " << names.getter << "(&self) -> " << rust_type(f.type, Mode::owned) << " {\n"
            << "        let mut rc = rosetta::Call::new(" << field.getter_index << ");\n        rc.arg(self);\n"
            << "        rosetta::must(rc.run().and_then(rosetta::Reply::value))\n    }\n";
        if (!names.setter.empty()) {
            out << "\n    /// Sets the C++ field `" << owner << "::" << f.name << "`.\n    pub fn " << names.setter << "(&self, value: "
                << rust_type(f.type, Mode::param) << ") -> Result<()> {\n        let mut rc = rosetta::Call::new(" << field.setter_index << ");\n"
                << "        rc.arg(self);\n        " << argument(f.type, "value") << "\n        rc.run()?.value()\n    }\n";
        }
    }
    for (const auto& constant : plan.constants) {
        const auto& rust = constant_names_.at(&constant);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        out << "\n" << about.render("    ") << "    pub fn " << rust << "() -> " << rust_type(*constant.type, Mode::owned) << " {\n"
            << "        rosetta::must(rosetta::Call::new(" << constant.index << ").run().and_then(rosetta::Reply::value))\n    }\n";
    }
    if (plan.director) emit_director(out, owner);
    out << "}\n";
    if (plan.director) {
        // The trait a Rust implementation of the virtual methods implements.
        const auto& trait = traits_.at(owner);
        Doc about;
        about.paragraph("The virtual methods of [`" + name + "`] (C++ `" + owner + "`) a Rust value implements: pass it to [`" + name +
                        "::from_impl`]. C++ calls the methods, with `this` the C++ object, from any thread.");
        about.paragraph("`this` is the C++ object: its virtual methods run the C++ implementation, as `" + owner + "::method()` "
                        "would in C++, so an override reaches the implementation it replaces through it. Each method's default does "
                        "just that (for a method that is pure virtual in C++, it fails), so an implementation overrides only what it "
                        "needs. Calling a virtual method on the object [`" + name + "::from_impl`] returns runs the Rust implementation.");
        about.paragraph("An error or a panic in a method becomes the C++ call's [`ErrorKind::Callback`] error, whose `source()` is the "
                        "original error.");
        out << "\n" << about.render() << "pub trait " << trait << ": std::any::Any + Send + Sync {";
        Names methods;
        methods.reserve("type_id");
        for (const auto& item : overridable(plan)) {
            const auto* method = item.method;
            const auto rust = methods.claim(snake(item.name));
            std::vector<std::string> names;
            std::set<std::string> taken = {"self", "this"};
            std::string parameters;
            for (std::size_t i = 0; i < method->parameters.size(); ++i) {
                const auto cpp = i < method->parameter_names.size() ? method->parameter_names[i] : std::string();
                auto parameter = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i + 1) : snake(cpp);
                while (!taken.insert(parameter).second) parameter += "_";
                names.push_back(parameter);
            }
            // The wrapper method running the C++ implementation, on this class or an ancestor.
            const Variant* wrapper = nullptr;
            std::string via;
            for (const auto& ancestor : queue) {
                for (const auto& group : classes_.at(ancestor).methods) {
                    if (group.is_static) continue;
                    for (const auto& variant : variants_.at(&group)) {
                        const auto& function = *variant.overload->function;
                        if (!variant.overload->extension && function.name == method->name && virtual_key(function) == virtual_key(*method) &&
                            variant.arity == function.parameters.size())
                            wrapper = &variant;
                    }
                }
                if (wrapper) { via = ancestor == owner ? "" : ".as_" + snake_words(classes_.at(ancestor).name) + "()"; break; }
            }
            std::string arguments;
            bool mutated = false;
            for (std::size_t i = 0; i < method->parameters.size(); ++i) {
                const auto& type = method->parameters[i];
                const bool changed = writeback(type) && wrapper;
                mutated = mutated || changed;
                parameters += ", " + std::string(changed ? "mut " : "") + names[i] + ": " + rust_type(type, Mode::owned);
                arguments += (i ? ", " : "") + borrowed(type, names[i]);
            }
            const auto result = is_void(method->result) ? std::string("()") : rust_type(method->result, Mode::owned);
            out << "\n    /// C++ `" << owner << "::" << method->name << "`" << (item.pure ? " (pure virtual: the default fails)" : "")
                << ". The default runs the C++ implementation.\n"
                << "    fn " << rust << "(&self, this: &" << name << parameters << ") -> Result<" << result << "> {\n";
            if (!wrapper) {
                std::string unused;
                for (const auto& parameter : names) unused += parameter + ", ";
                if (!unused.empty()) out << "        let _ = (" << unused.substr(0, unused.size() - 2) << (names.size() == 1 ? ",);\n" : ");\n");
                out << "        let _ = this;\n        Err(Error::binding(" << rust_string("no C++ implementation of " + owner + "::" + method->name + " is bound") << "))\n";
            } else if (wrapper->overload->function->is_noexcept) {
                out << "        Ok(this" << via << "." << wrapper->name << "(" << arguments << "))\n";
            } else out << "        this" << via << "." << wrapper->name << "(" << arguments << ")\n";
            (void)mutated;
            out << "    }\n";
        }
        out << "}\n";
    }
}

// X::rosetta_director: the runtime's Director calling a Rust implementation's trait methods by C++
// virtual method number.
void Generator::emit_director(std::ostream& out, const std::string& owner) const {
    const auto& plan = classes_.at(owner);
    const auto& trait = traits_.at(owner);
    Names methods;
    methods.reserve("type_id");
    std::set<std::pair<std::size_t, std::size_t>> seen;
    std::ostringstream arms;
    std::string implemented;
    for (const auto& item : overridable(plan)) {
        const auto rust = methods.claim(snake(item.name));
        const auto found = generic_index_.find(item.name);
        if (found == generic_index_.end()) continue;
        const auto count = item.method->parameters.size();
        if (!seen.insert({found->second, count}).second) continue;
        implemented += (implemented.empty() ? "(" : ", (") + std::to_string(found->second) + ", " + std::to_string(count) + ")";
        auto& out = arms;
        out << "                (" << found->second << ", " << count << ") => ";
        if (count == 0) out << "rosetta::reply(imp." << rust << "(&this)),\n";
        else {
            out << "{\n";
            std::string arguments;
            for (std::size_t i = 0; i < count; ++i) {
                out << "                    let a" << i << " = args.next()?;\n";
                arguments += ", a" + std::to_string(i);
            }
            out << "                    rosetta::reply(imp." << rust << "(&this" << arguments << "))\n                }\n";
        }
    }
    out << "\n    fn rosetta_director<I: " << trait << ">(imp: I) -> rosetta::Director {\n        let imp = Arc::new(imp);\n"
        << "        rosetta::Director::new(imp.clone(), &[" << implemented << "], move |generic, handle, args| {\n"
        << "            // The C++ object, whose virtual methods run the C++ implementation.\n"
        << "            let mut this = <" << plan.name << " as rosetta::Class>::from_handle(handle);\n"
        << "            rosetta::Class::set_direct(&mut this);\n"
        << "            #[allow(unused_mut)]\n            let mut args = rosetta::Args::new(args);\n"
        << "            match (generic, args.len()) {\n" << arms.str()
        << "                _ => Err(Error::binding(\"no Rust method for this virtual method\")),\n            }\n        })\n    }\n";
}

std::string Generator::crate_doc() const {
    Doc comment;
    comment.paragraph("The Rust interface to the C++ library " + name_ + " (`" + header_name() + "`), generated by Rosetta.");
    comment.heading("Building");
    comment.paragraph("The crate links the shared library `lib" + library_ + ".so` (`" + library_ + ".dll` on Windows). Point the build at the "
                      "directory holding it with `" + [&] { auto upper_name = name_; for (auto& c : upper_name) c = upper(c); return upper_name; }() +
                      "_RUST_LIB_DIR` (build.rs adds it to the link search path), and the loader with `LD_LIBRARY_PATH` "
                      "(`DYLD_LIBRARY_PATH` on macOS) or an rpath:");
    comment.code("export " + [&] { auto upper_name = name_; for (auto& c : upper_name) c = upper(c); return upper_name; }() +
                 "_RUST_LIB_DIR=$DIR\nRUSTFLAGS=\"-C link-arg=-Wl,-rpath,$DIR\" cargo run");
    comment.heading("Classes");
    comment.paragraph("A C++ class is a struct holding a handle to the C++ object; cloning it shares the object, and dropping the last "
                      "clone releases it (`close` does at once). A derived class derefs to its primary base, so base methods apply "
                      "and `&Derived` passes where `&Base` is expected; `as_base()` reaches every ancestor. Results have the declared "
                      "class; [`cast`] returns the most-derived class. Constructors are associated functions `new`, `new_x_y`, ...");
    comment.paragraph("Rust types implement C++ virtual methods through a trait per class (`ShapeImpl` for `Shape`) and "
                      "`Shape::from_impl(imp, ...)`: C++ calls the trait's methods; [`implementation`] returns the Rust value.");
    comment.heading("Overloads and default arguments");
    comment.paragraph("Rust has no overloading: each C++ overload is its own function. The one taking the fewest arguments keeps the "
                      "name; the others add the names of their required parameters (`new_x_y`), else of all their parameters, else their parameter types (`pick_f64`), else a number. "
                      "An overload with default arguments has one function per arity: the shortest keeps the name, the longer ones add "
                      "`with` and their last parameter (`add_with_b`, `add_with_c`). C++ resolves overloads again at run time.");
    comment.heading("Errors");
    comment.paragraph("Functions that can throw (all but noexcept ones) return [`Result`]: an [`Error`] whose [`Error::kind`] says what "
                      "failed ([`ErrorKind::InvalidArgument`], ... for C++ exceptions, [`ErrorKind::Binding`] for values that do not "
                      "convert or closed objects, [`ErrorKind::Callback`] for a Rust callback that failed or panicked, whose original "
                      "error `source()` returns). Field getters, constants and noexcept functions panic on errors.");
    comment.heading("Types");
    comment.paragraph("C++ `int` is `i32`, `std::size_t` `usize`, `double` `f64`, `char` `char` (one byte), `std::string` `String` "
                      "(`&str` as a parameter), `std::vector` `Vec` (a slice as a parameter), `std::map` `BTreeMap`, "
                      "`std::unordered_map` `HashMap`, `std::set` `BTreeSet`, `std::pair` and `std::tuple` tuples, `std::optional` "
                      "`Option`, `std::complex` [`Complex`], `std::function` a closure (`Arc<dyn Fn>` as a result), a non-const "
                      "reference parameter `&mut T` written back, and other types a [`Value`]. Enumerations are newtypes with an "
                      "associated constant per enumerator.");
    return comment.render("", "//!");
}

GeneratedRustSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    crate_ = options_.crate.empty() ? name_ : options_.crate;
    if (!identifier(crate_) || rust_keyword(crate_) || crate_ == "std" || crate_ == "core" || crate_ == "alloc" || crate_ == "test" || crate_ == "proc_macro")
        throw BindingError("Rust crate name must be an identifier and not a keyword: " + crate_);
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Rust library name must be a plain file name: " + library_);
    GeneratedRustSource result;
    prepare();
    // The crate root's names: the runtime's, the prelude's and the imports' first, then classes,
    // their traits and enums.
    for (const char* reserved : {"Error", "ErrorKind", "Result", "Value", "Complex", "Class", "Encode", "FromValue", "BTreeMap", "BTreeSet",
                                 "HashMap", "HashSet", "Arc", "Option", "Some", "None", "Ok", "Err", "Vec", "String", "Box", "ToString",
                                 "ToOwned", "Clone", "Copy", "Send", "Sync", "Sized", "Unpin", "Drop", "Fn", "FnMut", "FnOnce", "Iterator",
                                 "IntoIterator", "DoubleEndedIterator", "ExactSizeIterator", "Extend", "Default", "Eq", "PartialEq", "Ord",
                                 "PartialOrd", "AsRef", "AsMut", "Into", "From", "TryFrom", "TryInto", "FromIterator"})
        types_.reserve(reserved);
    for (const char* reserved : {"cast", "implementation", "same", "std", "core", "alloc", "rosetta", "ROSETTA_CLASSES"}) {
        types_.reserve(reserved);
        values_.reserve(reserved);
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = types_.claim_type(pascal(plan.name));
    }
    for (const auto& owner : class_order_)
        if (classes_.at(owner).director) traits_[owner] = types_.claim_type(classes_.at(owner).name + "Impl");
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = types_.claim_type(pascal(name));
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        Names scope;
        for (const auto& enumerator : enumeration.enumerators)
            enum_constants_[qualified].push_back(scope.claim(screaming(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Submodules (policy modules): a Rust module each (a.b nests b in a), its functions in its own scope.
    std::map<std::string, std::vector<std::string>> submodules;  // policy module -> Rust module path
    for (const auto& group : functions) {
        if (group.module.empty() || submodules.count(group.module)) continue;
        std::vector<std::string> path;
        std::size_t start = 0;
        while (true) {
            const auto dot = group.module.find('.', start);
            auto part = snake(group.module.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (path.empty() && !types_.free(part)) part += "_";
            path.push_back(part);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        submodules[group.module] = path;
    }
    for (const auto& [module, path] : submodules) types_.reserve(path.front());
    // variants_ is keyed by group address: the order is final before naming.
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Rust crate " + crate_ + " (" + name_ + ").");

    // ------------------------------------------------------------ src/lib.rs
    std::ostringstream rs;
    rs << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\n" << crate_doc()
       << "#![allow(non_camel_case_types, clippy::too_many_arguments, clippy::new_ret_no_self, clippy::should_implement_trait,\n"
       << "    clippy::wrong_self_convention, clippy::type_complexity, clippy::len_without_is_empty, clippy::module_inception)]\n\n"
       << "#[doc(hidden)]\npub mod rosetta;\n\n"
       << "pub use rosetta::{cast, implementation, same, Class, Complex, Error, ErrorKind, Value};\n"
       << "#[allow(unused_imports)]\nuse std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};\n"
       << "#[allow(unused_imports)]\nuse std::sync::Arc;\n\n"
       << "/// The result of a call into the C++ library.\npub type Result<T> = std::result::Result<T, Error>;\n\n"
       << "#[doc(hidden)]\npub static ROSETTA_CLASSES: &[rosetta::ClassInfo] = &[\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        std::string bases;
        for (const auto& base : plan.bases) {
            std::size_t index = 0;
            while (class_order_[index] != base) ++index;
            bases += (bases.empty() ? "" : ", ") + std::to_string(index);
        }
        rs << "    rosetta::ClassInfo { name: " << rust_string(plan.name) << ", bases: &[" << bases << "] },\n";
    }
    rs << "];\n";
    for (const auto& [qualified, name] : enum_names_) emit_enum(rs, qualified);
    for (const auto& owner : class_order_) emit_class(rs, owner);
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        const auto& variants = variants_.at(&group);
        for (const auto& variant : variants) emit_variant(rs, variant, variants, &group, nullptr, false, "");
    }
    for (const auto& constant : constants) {
        const auto& rust = constant_names_.at(&constant);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
        rs << "\n" << about.render() << "pub fn " << rust << "() -> " << rust_type(*constant.type, Mode::owned) << " {\n"
           << "    rosetta::must(rosetta::Call::new(" << constant.index << ").run().and_then(rosetta::Reply::value))\n}\n";
    }
    // Submodules, nested by their paths.
    std::vector<std::string> open;
    for (const auto& [module, path] : submodules) {
        std::size_t common = 0;
        while (common < open.size() && common < path.size() && open[common] == path[common]) ++common;
        while (open.size() > common) { open.pop_back(); rs << std::string(open.size() * 4, ' ') << "}\n"; }
        while (open.size() < path.size()) {
            const std::string indent(open.size() * 4, ' ');
            Doc about;
            std::string space;
            for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) space = ns;
            if (open.size() + 1 == path.size())
                about.paragraph("The functions of the C++ namespace `" + space + "` (policy module `" + module + "`).");
            else about.paragraph("Policy modules under `" + path[open.size()] + "`.");
            rs << "\n" << about.render(indent) << indent << "pub mod " << path[open.size()] << " {\n" << indent << "    #[allow(unused_imports)]\n"
               << indent << "    use crate::*;\n";
            open.push_back(path[open.size()]);
        }
        const std::string indent(open.size() * 4, ' ');
        for (const auto& group : functions) {
            if (group.module != module) continue;
            const auto& variants = variants_.at(&group);
            for (const auto& variant : variants) emit_variant(rs, variant, variants, &group, nullptr, false, indent);
        }
    }
    while (!open.empty()) { open.pop_back(); rs << std::string(open.size() * 4, ' ') << "}\n"; }
    result.lib = rs.str();

    // ------------------------------------------------------------ Cargo.toml, build.rs and the runtime
    auto environment = name_;
    for (auto& c : environment) c = upper(c);
    environment += "_RUST_LIB_DIR";
    result.files.emplace_back("Cargo.toml", "# Generated by Rosetta from " + header_name() + "; DO NOT EDIT.\n[package]\nname = \"" + crate_ +
                                                "\"\nversion = \"0.1.0\"\nedition = \"2021\"\nbuild = \"build.rs\"\npublish = false\n\n[lib]\npath = \"src/lib.rs\"\n");
    result.files.emplace_back("build.rs", "// Generated by Rosetta; DO NOT EDIT.\n// The directory holding the native library " + library_ +
                                              " (" + environment + ") joins the link search path.\nfn main() {\n"
                                              "    println!(\"cargo:rerun-if-env-changed=" + environment + "\");\n"
                                              "    if let Some(dir) = std::env::var_os(\"" + environment + "\") {\n"
                                              "        println!(\"cargo:rustc-link-search=native={}\", std::path::Path::new(&dir).display());\n    }\n}\n");
    auto runtime = std::string(detail::rust_full_runtime_parts[0]) + detail::rust_full_runtime_parts[1] + detail::rust_full_runtime_parts[2];
    const auto replace = [&](const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
    };
    replace("@C@", "rosetta_" + name_);
    replace("@LIB@", library_);
    result.files.emplace_back("src/rosetta.rs", "// Generated by Rosetta; DO NOT EDIT.\n" + runtime);
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedRustSource generate_rust(const Module& module, const std::string& name, bool skip_unsupported,
                                  const BindingPolicy& policy, const RustOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Rust source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
