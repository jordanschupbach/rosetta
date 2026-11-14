// The full Guile backend (--language guile --guile-api full): a libguile extension against Guile
// 3's C API plus the Scheme module that loads it, modeled on the full Ruby backend
// (src/ruby_full.cpp).
//
// Generated layout for module M (Guile module (M), extension EXT):
//   OUTPUT (+ parts)  C++ sources of the extension; init_M (run by load-extension) defines the
//                     native procedures (%f:..., %c:..., private to the module) and registers the
//                     classes.
//   GUILE_OUTPUT      M.scm: (define-module (M) ...), the Scheme runtime, the GOOPS classes,
//                     (load-extension EXT "init_M"), then every public procedure as a Scheme
//                     definition whose docstring documents it, so (procedure-documentation f),
//                     ,describe and editors reading the source all see the documentation.
//   M/SUB.scm         one file per policy module: (define-module (M SUB)) with its functions.
//
// Scheme conventions: procedures are kebab-case (is_x() and other bool results become x? / f?,
// set_x(v) becomes set-x!), classes <kebab-case> GOOPS classes deriving from <native-object>,
// methods and field accessors generic functions (a Scheme subclass extends them with
// define-method, and C++ calls its methods back through director classes), static methods
// <class>-<method>, enumerations symbols, constants variables. Names that would shadow Guile or
// GOOPS syntax, or a GOOPS procedure, get a trailing underscore; names of other core procedures
// (apply, length, ...) replace them (#:replace) and fall back to the core procedure for arguments
// no C++ overload accepts. C++ operators extend the core generics + - * / modulo equal? < <= > >=.
#include "rosetta/guile.hpp"
#include "guile_full_names.hpp"
#include "guile_full_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '?') result += "\\?"; // no trigraphs
        else result += c;
    }
    return result + '"';
}
// A Scheme string literal (newlines stay literal, which Guile reads as is).
std::string scheme_string(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        result += c;
    }
    return result + '"';
}
// --- Names -------------------------------------------------------------------------------------
bool lower(char c) { return c >= 'a' && c <= 'z'; }
bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool word(char c) { return lower(c) || upper(c) || digit(c) || c == '_'; }
// camelCase, PascalCase and snake_case to kebab-case; "a__b" (flattened namespaces) keeps "--".
std::string kebab(const std::string& name) {
    if (const auto at = name.find("__"); at != std::string::npos && at > 0 && at + 2 < name.size())
        return kebab(name.substr(0, at)) + "--" + kebab(name.substr(at + 2));
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (upper(c) && i > 0) {
            const char before = name[i - 1];
            const bool after_word = lower(before) || digit(before);
            const bool acronym_end = upper(before) && i + 1 < name.size() && lower(name[i + 1]);
            if ((after_word || acronym_end) && !out.empty() && out.back() != '-') out += '-';
        }
        if (c == '_') { if (!out.empty() && out.back() != '-') out += '-'; continue; }
        out += upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    return out.empty() ? "unnamed" : out;
}
std::set<std::string> name_set(const char* text) {
    std::set<std::string> names;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) if (!line.empty()) names.insert(line);
    return names;
}
// Syntax keywords of (guile) and (oop goops), and GOOPS procedures and classes: a generated
// definition would break code that uses them, so the generated name gets a trailing underscore.
const std::set<std::string>& renamed_names() {
    static const std::set<std::string> names = [] {
        auto result = name_set(detail::guile_core_syntax);
        for (const auto* list : {detail::guile_goops_syntax, detail::guile_goops_names})
            for (const auto& name : name_set(list)) result.insert(name);
        // The module's runtime.
        for (const char* name : {"<native-object>", "<documented-generic>", "close!", "closed?", "alive?", "owned?", "next-method",
                                 "define*", "lambda*", "else", "=>", "unquote", "unquote-splicing", "quasiquote", "_", "..."})
            result.insert(name);
        return result;
    }();
    return names;
}
// Other core procedures and variables: replaced, with the core binding as fallback.
const std::set<std::string>& core_names() {
    static const std::set<std::string> names = name_set(detail::guile_core_names);
    return names;
}
std::string safe(const std::string& name) {
    if (renamed_names().count(name) || name.rfind('%', 0) == 0) return name + "_";
    return name;
}
// A name the reader reads as one symbol, and not as a number.
bool scheme_symbol_name(const std::string& text) {
    if (text.empty() || text == "." || text[0] == '#' || text[0] == ',' || text[0] == '\'' || text[0] == '`') return false;
    for (char c : text)
        if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '[' || c == ']' || c == '"' || c == ';' ||
            c == '\'' || c == '`' || c == ',' || c == '|' || static_cast<unsigned char>(c) < 32)
            return false;
    if (digit(text[0])) return false;
    if ((text[0] == '+' || text[0] == '-' || text[0] == '.') && text.size() > 1 && (digit(text[1]) || text[1] == '.')) return false;
    return true;
}
std::string identifier(const std::string& text) {
    std::string out;
    for (char c : text) out += word(c) ? c : '_';
    if (out.empty() || digit(out[0])) out = "m_" + out;
    return out;
}
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 1) text.replace(i, 2, "_");
    return text;
}
// Standard library inline ABI namespaces do not change the type a binding sees.
std::string normalize(std::string text) {
    for (const char* inline_namespace : {"__cxx11::", "__1::", "__debug::", "__ndk1::", "__fs::"}) {
        for (std::size_t i = 0; (i = text.find(inline_namespace, i)) != std::string::npos;) text.erase(i, std::string(inline_namespace).size());
    }
    if (text.rfind("::", 0) == 0) text.erase(0, 2);
    return text;
}
std::string strip_cv(std::string text) {
    text = detail::trim_type(text);
    for (bool changed = true; changed;) {
        changed = false;
        for (const std::string prefix : {"const ", "volatile "})
            if (text.rfind(prefix, 0) == 0) { text = detail::trim_type(text.substr(prefix.size())); changed = true; }
    }
    return text;
}
const Type& referent(const Type& type) {
    return (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) && !type.arguments.empty()
        ? type.arguments.front() : type;
}
bool is_void(const Type& type) { return strip_cv(type.canonical) == "void"; }
bool is_bool(const Type& type) { return strip_cv(referent(type).canonical) == "bool"; }
bool is_floating(const Type& type) {
    const auto bare = strip_cv(referent(type).canonical);
    return referent(type).kind == TypeKind::builtin && (bare == "float" || bare == "double" || bare == "long double");
}
bool is_string(const Type& type) {
    const auto& target = referent(type);
    return target.kind == TypeKind::template_instance && normalize(target.declaration) == "std::basic_string";
}

// Core generic (or module procedure) for a C++ operator by arity (member operators: arity
// excludes this); "" when it is not exported.
std::string operator_name(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"+", "+"}}, {"operator-", {"-", "-"}}, {"operator*", {nullptr, "*"}}, {"operator/", {nullptr, "/"}},
        {"operator%", {nullptr, "modulo"}}, {"operator==", {nullptr, "equal?"}}, {"operator<", {nullptr, "<"}},
        {"operator<=", {nullptr, "<="}}, {"operator>", {nullptr, ">"}}, {"operator>=", {nullptr, ">="}},
        {"operator&", {nullptr, "bit-and"}}, {"operator|", {nullptr, "bit-or"}}, {"operator^", {nullptr, "bit-xor"}},
        {"operator<<", {nullptr, "shift-left"}}, {"operator>>", {nullptr, "shift-right"}}, {"operator~", {"bit-not", nullptr}},
        {"operator[]", {nullptr, "ref"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
// Operators that extend a core generic instead of defining a module procedure.
bool core_generic(const std::string& name) {
    static const std::set<std::string> names = {"+", "-", "*", "/", "modulo", "equal?", "<", "<=", ">", ">="};
    return names.count(name) != 0;
}

// Error key suffix (after PREFIX-) for a C++ exception type, for documentation.
std::string exception_suffix(std::string name) {
    name = normalize(name);
    if (name.rfind("std::", 0) == 0) name.erase(0, 5);
    if (name == "invalid_argument" || name == "domain_error" || name == "length_error") return "invalid-argument";
    if (name == "out_of_range") return "out-of-range";
    if (name == "overflow_error" || name == "underflow_error" || name == "range_error") return "range-error";
    if (name == "bad_cast" || name == "bad_optional_access" || name == "bad_variant_access") return "type-error";
    if (name == "filesystem::filesystem_error" || name == "ios_base::failure") return "io-error";
    if (name == "bad_alloc") return "memory-error";
    return "runtime-error";
}
// rosetta_gg::Kind for a policy exception name (Guile key suffix, or Ruby/Python spelling).
std::string error_kind(const std::string& name) {
    static const std::map<std::string, std::string> names = {{"invalid-argument", "invalid_argument"}, {"out-of-range", "index"},
        {"range-error", "range"}, {"type-error", "type"}, {"io-error", "io"}, {"not-implemented", "not_implemented"},
        {"runtime-error", "runtime"}, {"memory-error", "memory"}, {"key-error", "key"}, {"zero-division", "zero_division"},
        {"ArgumentError", "invalid_argument"}, {"ValueError", "invalid_argument"}, {"IndexError", "index"}, {"KeyError", "key"},
        {"LookupError", "index"}, {"RangeError", "range"}, {"OverflowError", "range"}, {"TypeError", "type"},
        {"NotImplementedError", "not_implemented"}, {"RuntimeError", "runtime"}, {"StandardError", "runtime"},
        {"Exception", "runtime"}, {"ZeroDivisionError", "zero_division"}, {"ArithmeticError", "zero_division"}, {"IOError", "io"},
        {"OSError", "io"}, {"FileNotFoundError", "io"}, {"PermissionError", "io"}, {"MemoryError", "memory"}};
    const auto found = names.find(name);
    return found == names.end() ? std::string() : found->second;
}

// --- Documentation ------------------------------------------------------------------------------
std::string trim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}
std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}
std::string indent_lines(const std::string& text, const std::string& indent) {
    std::string out;
    bool first = true;
    for (const auto& line : lines_of(text)) {
        out += (first ? "" : "\n") + (line.empty() ? std::string() : indent + line);
        first = false;
    }
    return out;
}
// Text that continues a "Label: " line: later lines indented under it.
std::string hanging(const std::string& text, const std::string& indent) {
    const auto lines = lines_of(trim(text));
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" + indent : "") + lines[i];
    return out;
}
std::string one_line(const std::string& text) {
    std::string out;
    for (char c : trim(text)) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && !out.empty() && out.back() == ' ') continue;
        out += c;
    }
    return out;
}
std::string sentence(std::string text) {
    text = trim(text);
    if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?' && text.back() != ':') text += '.';
    return text;
}
std::string capitalized(std::string text) {
    if (!text.empty() && lower(text[0])) text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}
std::string section_kind(std::string title) {
    while (!title.empty() && (title.back() == ':' || title.back() == ' ')) title.pop_back();
    std::string lower_title;
    for (const char c : title) lower_title += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower_title == "example" || lower_title == "examples" || lower_title == "usage") return "example";
    if (lower_title == "note" || lower_title == "notes") return "note";
    if (lower_title == "see also") return "see";
    return {};
}
std::string comment(const std::string& text, const std::string& prefix = ";; ") {
    if (trim(text).empty()) return {};
    std::string out;
    for (const auto& line : lines_of(text)) {
        std::string trimmed = line;
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        out += trimmed.empty() ? std::string(";;") + "\n" : prefix + trimmed + "\n";
    }
    return out;
}

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;       // qualified class for constructors and methods
    bool extension = false;  // free function attached to a class
    bool receives_self = false;
};
struct Group {
    std::string name;        // Scheme name (a module procedure, or a generic)
    std::vector<Overload> overloads;
    bool is_static = false;  // a class procedure (<class>-<name>) rather than a method
    std::string module;      // policy submodule of a free function ("" = top level)
    std::string symbol;      // C++ wrapper
    std::string native;      // Scheme name of the native procedure
    bool setitem = false;    // ref-set!: the value is the last argument
    bool operator_method = false;
};
struct FieldPlan {
    const Field* field;
    std::string getter, setter; // Scheme names; setter empty when read-only
    std::string symbol;
    std::string native;
};
struct ClassPlan {
    const Record* record;
    std::string scheme;      // <class>
    std::string base;        // class without the brackets, the prefix of class procedures
    std::string symbol;      // C++ identifier prefix
    std::vector<std::string> bases; // exported ancestor classes (qualified C++ names)
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<FieldPlan> fields;
    bool director = false;
    std::string constructor_symbol;
    std::string maker;       // make-<class> convenience procedure, or ""
};
struct EnumPlan {
    const Enumeration* enumeration;
    std::string base;        // helpers <base>-values, <base>->integer, integer-><base>
    std::vector<std::string> symbols;
    bool flags = false;
};
struct ConstantPlan {
    std::string scheme, owner, expression, native;
    const Documentation* documentation;
    Type type;
};
// One public module-level name: a free (or class) procedure, methods and field accessors of
// several classes (a generic), or both.
struct Member {
    const ClassPlan* klass;
    const Group* group = nullptr;     // method
    const FieldPlan* field = nullptr; // accessor
    bool setter = false;
};
struct Entry {
    std::string name;
    const Group* function = nullptr;
    std::vector<Member> members;
    std::string core;        // "core" or "" when the name replaces a core binding
    std::string setter;      // public procedure (set! (name obj) value) calls
};

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const GuileFullOptions& options)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"),
          options_(options) { validate_modules(policy_, module_); }

    GeneratedGuileFullSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    GuileFullOptions options_;
    std::vector<std::string> module_path_; // (a b) of the top module
    std::string prefix_;                   // error key prefix
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_;
    std::map<std::string, std::string> function_template_names_;
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
    std::map<std::string, EnumPlan> enum_plans_;
    std::vector<Group> functions_;
    std::vector<ConstantPlan> constants_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> entry_order_;
    std::map<std::string, std::string> names_; // top-level Scheme name -> what
    std::size_t next_ = 0;

    void reject(const std::string& context, const std::string& reason) { diagnostics_.push_back(context + ": " + reason); }
    bool excluded(const std::string& qualified, const std::string& identity = {}) const {
        for (const auto& pattern : policy_.exclude)
            if (policy_match(pattern, qualified) || (!identity.empty() && policy_match(pattern, identity))) return true;
        return false;
    }
    bool readonly(const std::string& qualified, const std::string& owner) const {
        for (const auto& pattern : policy_.readonly)
            if (policy_match(pattern, qualified) || (!owner.empty() && policy_match(pattern, owner))) return true;
        return false;
    }
    const std::string* renamed_raw(const std::string& qualified) const {
        const auto found = policy_.rename.find(qualified);
        return found == policy_.rename.end() ? nullptr : &found->second;
    }
    std::string renamed(const std::string& qualified, const std::string& fallback) const {
        if (const auto* name = renamed_raw(qualified)) {
            if (!scheme_symbol_name(*name)) throw BindingError("Guile rename target is not a Scheme symbol: " + *name);
            return *name;
        }
        return fallback;
    }
    std::string symbol(const std::string& prefix) { return prefix + std::to_string(next_++); }
    bool bound(const Type& type) const { return type.kind == TypeKind::record && classes_.count(type.declaration); }
    std::string conversion(const Type& type) const {
        const auto spelling = normalize(strip_cv(type.canonical));
        for (const auto& item : policy_.conversions)
            if (normalize(item.first) == spelling) return item.first;
        if (type.kind == TypeKind::template_instance && spelling.size() > 1 && spelling.back() == '>') {
            const auto stem = spelling.substr(0, spelling.size() - 1);
            for (const auto& item : policy_.conversions) {
                const auto key = normalize(item.first);
                if (key.size() <= stem.size() || key.compare(0, stem.size(), stem) != 0) continue;
                const char next = key[stem.size()];
                if (stem.back() == '<' || next == ',') return item.first;
            }
        }
        return {};
    }
    std::map<std::string, bool> copyable_cache_;
    bool copyable_record(const std::string& name, unsigned depth = 0, bool as_base = false);
    bool copyable_type(const Type& type, unsigned depth);
    std::string value_problem(const Type& type, Use use) const;
    std::string type_problem(const Type& type, Use use) const;
    std::string function_problem(const Function& function, bool self_parameter) const;
    std::string return_policy(const Function& function) const;
    std::string class_scheme_name(const Record& record) const;
    std::string procedure_name(const Function& function, const std::string& base) const;
    std::string method_scheme_name(const Function& method) const { return procedure_name(method, method.name); }
    bool copy_type(const Type& type) const {
        if (type.kind == TypeKind::pointer || type.kind == TypeKind::rvalue_reference) return false;
        const auto& target = referent(type);
        return target.kind == TypeKind::record && policy_.copy_types.count(target.declaration) != 0;
    }
    bool flat(const std::vector<std::string>& namespaces) const {
        if (!flat_) return false;
        std::string scope;
        for (const auto& part : namespaces) scope += (scope.empty() ? "" : "::") + part;
        for (const auto& pattern : policy_.qualify) if (policy_match(pattern, scope)) return false;
        return true;
    }
    std::vector<std::string> submodule_path(const std::string& module) const {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (true) {
            const auto dot = module.find('.', start);
            out.push_back(kebab(module.substr(start, dot == std::string::npos ? std::string::npos : dot - start)));
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return out;
    }
    std::string module_spelling(const std::vector<std::string>& path) const {
        std::string out = "(";
        for (std::size_t i = 0; i < path.size(); ++i) out += (i ? " " : "") + path[i];
        return out + ")";
    }
    void plan_classes();
    void plan_directors();
    void plan_members();
    void plan_functions();
    void plan_entries();
    void reserve(const std::string& name, const std::string& what) {
        const auto inserted = names_.emplace(name, what);
        if (!inserted.second && inserted.first->second != what)
            throw BindingError("Guile export name collision: " + name + " (" + inserted.first->second + " and " + what + ")");
    }
    Entry& entry(const std::string& name) {
        auto found = entries_.find(name);
        if (found == entries_.end()) {
            found = entries_.emplace(name, Entry{}).first;
            found->second.name = name;
            entry_order_.push_back(name);
        }
        return found->second;
    }
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    bool fillable(const Function& function, std::size_t index) const;
    std::string fill_expression(const Function& function, std::size_t index) const;
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent);
    void emit_group(std::ostream& out, Group& group, const std::string& kind, const std::string& fallback);
    std::pair<std::string, std::string> exception_handlers(const std::string& qualified) const;
    void emit_constructor(std::ostream& out, ClassPlan& plan);
    void emit_director(std::ostream& out, const ClassPlan& plan);
    std::string signature(const Overload& overload, const std::string& name) const;
    // Scheme module
    std::string scheme_type(const Type& type) const;
    std::string scheme_default(const DefaultValue& value, const Type& type) const;
    std::string parameter_name(const Function& function, std::size_t index) const;
    std::string cpp_signature(const Overload& overload) const;
    std::vector<std::string> literal_defaults(const Overload& overload) const;
    std::string usage(const Overload& overload, const std::string& name, const std::string& self_name) const;
    std::string docstring(const std::vector<std::pair<const Overload*, std::string>>& items, const std::string& name,
                          const Documentation* own, const std::string& fallback_summary, const std::string& extra,
                          bool headers = false) const;
    std::string documentation_text(const Documentation& documentation) const;
    std::string class_doc(const ClassPlan& plan) const;
    std::string entry_doc(const Entry& entry) const;
    std::string scheme_file(const std::string& extension, const std::string& init);
    std::string submodule_file(const std::string& module, const std::vector<const Group*>& groups) const;
    std::string function_definition(const Group& group, const std::string& name, const std::string& native, const std::string& doc,
                                    bool rest_only) const;
};

bool Generator::copyable_type(const Type& type, unsigned depth) {
    if (depth > 32) return true;
    const auto bare = normalize(strip_cv(type.canonical));
    static const std::set<std::string> fixed = {"std::mutex", "std::recursive_mutex", "std::timed_mutex", "std::shared_mutex",
        "std::condition_variable", "std::condition_variable_any", "std::thread", "std::jthread", "std::stop_source"};
    if (fixed.count(bare)) return false;
    if (type.kind == TypeKind::rvalue_reference) return false;
    if (type.kind == TypeKind::record) return copyable_record(type.declaration, depth + 1);
    if (type.kind == TypeKind::template_instance) {
        const auto declaration = normalize(type.declaration);
        static const std::set<std::string> movable_only = {"std::unique_ptr", "std::atomic", "std::promise", "std::future",
            "std::packaged_task", "std::basic_ifstream", "std::basic_ofstream", "std::basic_fstream", "std::basic_stringstream",
            "std::basic_istringstream", "std::basic_ostringstream", "std::basic_filebuf", "std::unique_lock", "std::lock_guard",
            "std::scoped_lock", "std::move_only_function"};
        if (movable_only.count(declaration)) return false;
        if (declaration == "std::shared_ptr" || declaration == "std::weak_ptr" || declaration == "std::function") return true;
        for (const auto& argument : type.arguments) if (!copyable_type(argument, depth + 1)) return false;
    }
    return true;
}
bool Generator::copyable_record(const std::string& name, unsigned depth, bool as_base) {
    const auto found = records_.find(name);
    if (as_base && found != records_.end() && found->second->abstract) {
        const auto& record = *found->second;
        for (const auto& constructor : record.constructors)
            if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::lvalue_reference &&
                referent(constructor.parameters[0]).declaration == name && (constructor.is_deleted || constructor.access == "private"))
                return false;
        for (const auto& field : record.fields) if (!copyable_type(field.type, depth + 1)) return false;
        for (const auto& base : record.bases)
            if (!copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) return false;
        return true;
    }
    const auto cached = copyable_cache_.find(name);
    if (cached != copyable_cache_.end()) return cached->second;
    if (found == records_.end() || depth > 32) return true;
    copyable_cache_[name] = true; // provisional for recursive types
    const auto& record = *found->second;
    bool result = !record.abstract;
    bool copy_declared = false, move_declared = false;
    for (const auto& constructor : record.constructors) {
        if (constructor.parameters.size() != 1) continue;
        const auto& parameter = constructor.parameters[0];
        if (referent(parameter).declaration != name) continue;
        if (parameter.kind == TypeKind::lvalue_reference) {
            copy_declared = true;
            if (constructor.is_deleted || constructor.access != "public") result = false;
        } else if (parameter.kind == TypeKind::rvalue_reference) move_declared = true;
    }
    if (move_declared && !copy_declared) result = false;
    for (const auto& field : record.fields) if (result && !copyable_type(field.type, depth + 1)) result = false;
    for (const auto& base : record.bases)
        if (result && !copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) result = false;
    copyable_cache_[name] = result;
    return result;
}

std::string Generator::value_problem(const Type& type, Use) const {
    if (!conversion(type).empty()) return {};
    const auto bare = normalize(strip_cv(type.canonical));
    switch (type.kind) {
    case TypeKind::builtin: {
        static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
            "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double"};
        if (supported.count(bare)) return {};
        return "unsupported Guile value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to Guile";
    case TypeKind::template_instance: {
        const auto declaration = normalize(type.declaration);
        const auto all = [&](std::size_t count, Use element) -> std::string {
            if (type.arguments.size() < count) return "incomplete template arguments in " + type.canonical;
            for (std::size_t i = 0; i < count; ++i) {
                auto why = type_problem(type.arguments[i], element);
                if (!why.empty()) return why;
            }
            return {};
        };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view")
            return !type.arguments.empty() && strip_cv(type.arguments[0].canonical) == "char" ? "" : "only char strings are supported";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::set" ||
            declaration == "std::unordered_set" || declaration == "std::valarray" || declaration == "std::optional" ||
            declaration == "std::array")
            return all(1, Use::element);
        if (declaration == "std::map" || declaration == "std::unordered_map") return all(2, Use::element);
        if (declaration == "std::pair" || declaration == "std::tuple" || declaration == "std::variant")
            return all(type.arguments.size(), Use::element);
        if (declaration == "std::complex")
            return !type.arguments.empty() && type.arguments[0].kind == TypeKind::builtin ? "" : "unsupported complex type";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            if (type.arguments.empty() || !bound(type.arguments[0]))
                return "smart pointers require an exported class: " + type.canonical;
            if (declaration == "std::unique_ptr" && type.arguments.size() > 1 && normalize(type.arguments[1].declaration) != "std::default_delete")
                return "unique_ptr with a custom deleter is unsupported";
            return {};
        }
        if (declaration == "std::function") {
            if (type.arguments.empty() || type.arguments[0].kind != TypeKind::function) return "unsupported std::function signature";
            const auto& signature = type.arguments[0];
            auto why = type_problem(signature.arguments[0], Use::callback_result);
            if (!why.empty()) return "callback result: " + why;
            for (std::size_t i = 1; i < signature.arguments.size(); ++i) {
                why = type_problem(signature.arguments[i], Use::callback_argument);
                if (!why.empty()) return "callback argument: " + why;
            }
            return {};
        }
        if (declaration == "std::chrono::duration") return {};
        return "unsupported Guile template type " + type.canonical;
    }
    default:
        return "unsupported Guile value type " + type.canonical;
    }
}

std::string Generator::type_problem(const Type& type, Use use) const {
    if (use == Use::result && is_void(type)) return {};
    if (use == Use::callback_result && is_void(type)) return {};
    if (type.is_volatile) return "volatile types are unsupported";
    if (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) {
        const auto& target = referent(type);
        if (use == Use::element || use == Use::callback_result) return "references are unsupported here: " + type.canonical;
        if (type.kind == TypeKind::rvalue_reference && use != Use::parameter) return "rvalue reference results are unsupported";
        if (target.is_volatile) return "volatile types are unsupported";
        if (bound(target)) return {};
        if (target.kind == TypeKind::pointer) return "references to pointers are unsupported";
        return value_problem(target, use);
    }
    if (type.kind == TypeKind::pointer) {
        if (type.arguments.empty()) return "unsupported pointer";
        const auto& pointee = type.arguments.front();
        if (bound(pointee)) return {};
        if (pointee.kind == TypeKind::builtin && strip_cv(pointee.canonical) == "char" && (pointee.is_const || use == Use::result))
            return {};
        return "unsupported Guile pointer type " + type.canonical;
    }
    if (type.kind == TypeKind::function) return "function types are unsupported";
    return value_problem(type, use);
}

std::string Generator::function_problem(const Function& function, bool self_parameter) const {
    if (!function.unsupported_reason.empty() && function.unsupported_reason.rfind("default calls with ", 0) != 0)
        return function.unsupported_reason;
    if (function.is_deleted) return "deleted function";
    if (function.default_arguments > function.parameters.size()) return "invalid default argument metadata";
    if (!function.is_constructor) {
        auto why = type_problem(function.result, Use::result);
        if (!why.empty()) return "unsupported Guile result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported Guile parameter type: " + why;
    }
    return {};
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_gg::Return::copy" : "rosetta_gg::Return::automatic";
    return "rosetta_gg::Return::" + found->second;
}

std::string Generator::class_scheme_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    if (const auto* name = renamed_raw(qualified)) {
        auto text = name->size() > 2 && name->front() == '<' && name->back() == '>' ? *name : "<" + *name + ">";
        if (!scheme_symbol_name(text)) throw BindingError("Guile rename target is not a Scheme symbol: " + *name);
        return text;
    }
    std::string base;
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) base = kebab(instantiated->second);
    else if (!record.owner.empty()) {
        const auto outer = records_.find(record.owner);
        const auto outer_name = classes_.count(record.owner) ? classes_.at(record.owner).base
                                                             : kebab(outer == records_.end() ? record.owner : outer->second->name);
        base = outer_name + "-" + kebab(record.name);
    } else base = kebab(flat(record.namespaces) ? record.name : exported(qualified));
    // GOOPS classes (<string>, <class>, ...) and the runtime's classes keep their names.
    if (renamed_names().count("<" + base + ">")) base += "_";
    return "<" + base + ">";
}

// kebab-case; a bool result adds ? (is_x -> x?), set_x adds !; then the reserved-name rule.
std::string Generator::procedure_name(const Function& function, const std::string& base) const {
    if (const auto* name = renamed_raw(function.qualified_name())) {
        if (!scheme_symbol_name(*name)) throw BindingError("Guile rename target is not a Scheme symbol: " + *name);
        return *name;
    }
    auto name = kebab(base);
    if (!function.is_constructor && is_bool(function.result) && function.result.kind != TypeKind::lvalue_reference) {
        if (name.rfind("is-", 0) == 0 && name.size() > 3 && !digit(name[3])) name = name.substr(3);
        if (name.back() != '?') name += "?";
    } else if (name.rfind("set-", 0) == 0 && name.size() > 4 && !function.parameters.empty()) name += "!";
    return safe(name);
}

void Generator::plan_classes() {
    for (const auto& requested : policy_.templates) {
        const auto found = std::find_if(module_.records.begin(), module_.records.end(), [&](const Record& record) {
            return !record.template_name.empty() && normalize(record.qualified_name()) == normalize(requested.second);
        });
        if (found == module_.records.end()) {
            const auto function = std::find_if(module_.functions.begin(), module_.functions.end(), [&](const Function& f) {
                return !f.template_name.empty() && normalize(f.qualified_name()) == normalize(requested.second);
            });
            if (function == module_.functions.end()) { reject(requested.second, "template instantiation was not exported by the frontend"); continue; }
            function_template_names_[function->qualified_name()] = requested.first;
            continue;
        }
        template_names_[found->qualified_name()] = requested.first;
    }
    for (const auto& selection : policy_.classes)
        if (std::none_of(module_.records.begin(), module_.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in Guile policy: " + selection.first);
    for (const auto& record : module_.records) records_[record.qualified_name()] = &record;
    for (const auto& record : module_.records) {
        const auto owner = record.qualified_name();
        const bool listed = policy_.classes.count(owner) != 0;
        const bool instantiated = template_names_.count(owner) != 0;
        if (!record.template_name.empty() && !instantiated) continue;
        if (!listed && !instantiated && !policy_.select_all) {
            if (!policy_.explicit_class_selection && policy_.classes.empty()) reject(owner, "class requires an owned class policy");
            continue;
        }
        if (excluded(owner)) continue;
        if (!conversion(Type{owner, owner, TypeKind::record, owner}).empty()) continue; // converted classes are values
        if (!record.complete) { if (listed) reject(owner, "Guile requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed Guile results are not yet supported"); continue;
        }
        ClassPlan plan;
        plan.record = &record;
        plan.symbol = symbol("rosetta_class_");
        classes_.emplace(owner, std::move(plan));
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = classes_.begin(); it != classes_.end();) {
            const auto& outer = it->second.record->owner;
            if (!outer.empty() && !classes_.count(outer)) { it = classes_.erase(it); changed = true; }
            else ++it;
        }
    }
    // Outer classes first, so nested class names can use them.
    std::vector<std::string> by_depth;
    for (const auto& [owner, plan] : classes_) by_depth.push_back(owner);
    const auto depth = [&](const std::string& owner) {
        std::size_t result = 0;
        for (auto parent = classes_.at(owner).record->owner; !parent.empty(); parent = classes_.at(parent).record->owner) ++result;
        return result;
    };
    std::stable_sort(by_depth.begin(), by_depth.end(), [&](const std::string& a, const std::string& b) { return depth(a) < depth(b); });
    for (const auto& owner : by_depth) {
        auto& plan = classes_.at(owner);
        plan.scheme = class_scheme_name(*plan.record);
        plan.base = plan.scheme.substr(1, plan.scheme.size() - 2);
    }
    for (auto& [owner, plan] : classes_) {
        std::vector<std::string> found;
        const auto visit = [&](const auto& self, const Record& record, unsigned level) -> void {
            if (level > 64) return;
            for (const auto& base : record.bases) {
                if (base.access != "public") continue;
                const auto name = base.type.declaration.empty() ? base.type.canonical : base.type.declaration;
                if (classes_.count(name)) { if (std::find(found.begin(), found.end(), name) == found.end()) found.push_back(name); continue; }
                const auto record_found = records_.find(name);
                if (record_found != records_.end()) self(self, *record_found->second, level + 1);
            }
        };
        visit(visit, *plan.record, 0);
        plan.bases = found;
    }
    const std::function<bool(const std::string&, const std::string&)> derives = [&](const std::string& from, const std::string& to) {
        if (from == to) return true;
        for (const auto& base : classes_.at(from).bases) if (derives(base, to)) return true;
        return false;
    };
    for (auto& [owner, plan] : classes_) {
        std::vector<std::string> minimal;
        for (const auto& base : plan.bases) {
            bool redundant = false;
            for (const auto& other : plan.bases) if (other != base && derives(other, base)) redundant = true;
            if (!redundant) minimal.push_back(base);
        }
        plan.bases = minimal;
    }
    std::set<std::string> placed;
    const std::function<void(const std::string&, unsigned)> place = [&](const std::string& owner, unsigned level) {
        if (placed.count(owner) || level > 256) return;
        const auto& plan = classes_.at(owner);
        for (const auto& base : plan.bases) place(base, level + 1);
        if (placed.insert(owner).second) class_order_.push_back(owner);
    };
    for (const auto& record : module_.records) if (classes_.count(record.qualified_name())) place(record.qualified_name(), 0);
}

std::vector<const Function*> Generator::virtuals(const Record& record, std::map<std::string, bool>& pure) const {
    std::vector<const Function*> result;
    std::set<std::string> seen;
    const auto key = [](const Function& function) {
        std::string text = function.name + "(";
        for (const auto& parameter : function.parameters) text += parameter.canonical + ",";
        return text + (function.is_const ? ")const" : ")");
    };
    const std::function<bool(const Record&, unsigned)> visit = [&](const Record& current, unsigned depth) {
        if (depth > 64) return false;
        for (const auto& method : current.methods) {
            if (!method.is_virtual || method.is_static || method.name.rfind("~", 0) == 0) continue;
            if (seen.insert(key(method)).second) { result.push_back(&method); pure[key(method)] = method.is_pure; }
        }
        for (const auto& base : current.bases) {
            const auto found = records_.find(base.type.declaration.empty() ? base.type.canonical : base.type.declaration);
            if (found == records_.end()) return false;
            if (!visit(*found->second, depth + 1)) return false;
        }
        return true;
    };
    if (!visit(record, 0)) { pure.clear(); return {}; }
    return result;
}

void Generator::plan_directors() {
    if (policy_.directors == "none") return;
    for (auto& [owner, plan] : classes_) {
        const auto& record = *plan.record;
        if (!record.polymorphic || !record.virtual_destructor || record.is_final || !record.public_destructor) continue;
        std::map<std::string, bool> pure;
        const auto methods = virtuals(record, pure);
        if (methods.empty()) continue;
        bool possible = true;
        for (const auto* method : methods) {
            bool supported = method->unsupported_reason.empty() && type_problem(method->result, Use::callback_result).empty();
            for (const auto& parameter : method->parameters) supported = supported && type_problem(parameter, Use::callback_argument).empty();
            if (!supported && method->is_pure) {
                const auto found = std::find_if(pure.begin(), pure.end(), [&](const auto& item) { return item.first.rfind(method->name + "(", 0) == 0; });
                if (found != pure.end() && found->second) possible = false;
            }
        }
        const bool constructible = std::any_of(record.constructors.begin(), record.constructors.end(), [](const Function& constructor) {
            return !constructor.is_deleted && constructor.access != "private";
        });
        plan.director = possible && constructible;
    }
}

// A defaulted parameter a call may skip with #:default: its C++ default is a literal generated
// code can repeat.
bool Generator::fillable(const Function& function, std::size_t index) const {
    if (index >= function.default_values.size()) return false;
    const auto& value = function.default_values[index];
    const auto& type = function.parameters[index];
    const auto& target = referent(type);
    if (value.kind == "null") return type.kind == TypeKind::pointer;
    if (bound(target) || type.kind == TypeKind::pointer) return false;
    if (type.kind == TypeKind::rvalue_reference || (type.kind == TypeKind::lvalue_reference && !target.is_const)) return false;
    if (!conversion(target).empty()) return false;
    if (value.kind == "int" || value.kind == "float" || value.kind == "bool" || value.kind == "char")
        return target.kind == TypeKind::builtin || target.kind == TypeKind::enumeration;
    if (value.kind == "string") return is_string(target) || normalize(target.declaration) == "std::basic_string_view";
    if (value.kind == "enum") return target.kind == TypeKind::enumeration && enums_.count(target.declaration);
    if (value.kind == "default") return true;
    return false;
}
std::string Generator::fill_expression(const Function& function, std::size_t index) const {
    const auto& value = function.default_values[index];
    const auto& target = referent(function.parameters[index]);
    if (value.kind == "default") return "rosetta_gg::fill_default";
    std::string expression;
    if (value.kind == "null") expression = "nullptr";
    else if (value.kind == "char") {
        expression = "'";
        for (char c : value.value) {
            if (c == '\\' || c == '\'') expression += '\\';
            expression += c;
        }
        expression += "'";
    } else if (value.kind == "enum") expression = "::" + target.declaration + "::" + value.value;
    else expression = value.value;
    return "rosetta_gg::fill, " + expression;
}

std::string Generator::parameter_name(const Function& function, std::size_t index) const {
    const auto written = index < function.parameter_names.size() ? function.parameter_names[index] : std::string();
    if (written.empty()) return "arg" + std::to_string(index);
    auto name = kebab(written);
    // Formals must not shadow syntax or the names wrapper bodies use.
    if (renamed_names().count(name) || name == "args" || name == "self" || name == "core" || name == "goops") name += "_";
    return name;
}

std::string Generator::signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    std::string result = "(" + name;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    if (overload.receives_self || (!function.is_constructor && !function.is_static && !overload.owner.empty() && !overload.extension))
        result += " self";
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        result += " " + parameter_name(function, i) + (i >= required ? "?" : "") + ":" + scheme_type(function.parameters[i]);
    }
    result += ")";
    if (!function.is_constructor && !is_void(function.result)) result += " -> " + scheme_type(function.result);
    return result;
}

void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent) {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - first;
    const std::size_t required = function.parameters.size() - function.default_arguments - first;
    out << indent << "{\n" << indent << "    static const bool fillable[] = {";
    for (std::size_t i = first; i < function.parameters.size(); ++i)
        out << (i > first ? ", " : "") << (i - first >= required && fillable(function, i) ? "true" : "false");
    if (count == 0) out << "false";
    out << "};\n";
    out << indent << "    SCM a[" << std::max<std::size_t>(count, 1) << "]; long n = 0;\n";
    out << indent << "    if (args.collect(" << count << ", " << required << ", fillable, a, &n)";
    if (overload.receives_self) out << " && rosetta_gg::Param<" << function.parameters[0].canonical << ">::check(self, false)";
    for (std::size_t i = 0; i < count; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || SCM_UNBNDP(a[" << i << "]) || rosetta_gg::Param<"
            << function.parameters[i + first].canonical << ">::check(a[" << i << "], strict))";
    out << ") {\n";
    const auto owner = overload.owner;
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& type = function.parameters[i + first].canonical;
            if (i >= required && fillable(function, i + first))
                out << indent << "            auto p" << i << " = SCM_UNBNDP(a[" << i << "]) ? rosetta_gg::Param<" << type << ">("
                    << fill_expression(function, i + first) << ") : rosetta_gg::Param<" << type << ">(a[" << i << "]);\n";
            else
                out << indent << "            rosetta_gg::Param<" << type << "> p" << i << "(a[" << i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (kind == "method" || kind == "setitem") out << indent << "            auto* native = rosetta_gg::native<" << owner << ">(self);\n";
        if (overload.receives_self) out << indent << "            rosetta_gg::Param<" << function.parameters[0].canonical << "> self_param(self);\n";
        std::string writebacks;
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& type = function.parameters[i + first];
            if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                writebacks += indent + "            if (!SCM_UNBNDP(a[" + std::to_string(i) + "])) p" + std::to_string(i) + ".writeback(a[" + std::to_string(i) + "]);\n";
        }
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            out << indent << "            " << owner << "* created = nullptr;\n";
            if (plan.director) {
                out << indent << "            if (director) created = new " << plan.symbol << "_director(" << arguments << ");\n";
                out << indent << "            else ";
            } else out << indent << "            ";
            if (plan.record->abstract)
                out << "rosetta_gg::fail(rosetta_gg::Kind::type, " << quote(plan.scheme + " is abstract; subclass it with define-class and implement its pure virtual methods") << ");\n";
            else out << "created = new " << owner << "(" << arguments << ");\n";
            out << writebacks;
            out << indent << "            object->ptr = created; object->info = &rosetta_gg::Info<" << owner << ">::value;\n"
                << indent << "            object->owned = true; object->director = " << (plan.director ? "director" : "false") << ";\n";
            if (plan.director) out << indent << "            if (director) static_cast<rosetta_gg::Director*>(static_cast<" << plan.symbol << "_director*>(created))->rosetta_self = self;\n";
            out << indent << "            return SCM_UNSPECIFIED;\n";
        } else if (kind == "setitem") {
            out << indent << "            rosetta_gg::Param<const std::remove_cv_t<std::remove_reference_t<decltype((*native)[" << arguments
                << "])>>&> item(value);\n"
                << indent << "            rosetta_gg::assign_field((*native)[" << arguments << "], item.get());\n"
                << indent << "            return SCM_UNSPECIFIED;\n";
        } else {
            std::string body;
            if (kind == "method") {
                const auto direct = "native->" + function.name + "(" + arguments + ")";
                if (function.is_virtual && policy_.directors != "none") {
                    if (function.is_pure)
                        body = "if (rosetta_gg::is_director(self)) rosetta_gg::pure_virtual(" + quote(owner + "::" + function.name) + "); return " + direct + ";";
                    else
                        body = "if (rosetta_gg::is_director(self)) return native->" + owner + "::" + function.name + "(" + arguments + "); return " + direct + ";";
                } else body = "return " + direct + ";";
            } else {
                body = "return ::" + function.qualified_name() + "(" + arguments + ");";
            }
            out << indent << "            SCM result = rosetta_gg::call([&]() -> decltype(auto) { " << body << " }, "
                << (parent ? "self" : "SCM_BOOL_F") << ", " << policy << ");\n";
            out << writebacks;
            out << indent << "            return result;\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
}

std::pair<std::string, std::string> Generator::exception_handlers(const std::string& qualified) const {
    for (const auto& [pattern, map] : policy_.exceptions) {
        if (!policy_match(pattern, qualified)) continue;
        std::string catches;
        for (const auto& [cpp, target] : map) {
            const auto kind = error_kind(target);
            if (kind.empty()) throw BindingError("policy exceptions: unsupported Guile exception " + target);
            catches += "        } catch (const " + cpp + "& error) { throw rosetta_gg::native_error{rosetta_gg::Kind::" + kind +
                ", error.what(), rosetta_gg::demangle(typeid(error).name())};\n";
        }
        return {"        try {\n", catches + "        }\n"};
    }
    return {};
}

// kind: "function" (SCM rest), "method" and "setitem" (SCM self, SCM rest).
void Generator::emit_group(std::ostream& out, Group& group, const std::string& kind, const std::string& fallback) {
    group.symbol = symbol("rosetta_function_");
    const bool single = group.overloads.size() == 1;
    const bool with_self = kind != "function";
    const auto handlers = exception_handlers(group.overloads.front().function->qualified_name());
    out << "SCM " << group.symbol << (with_self ? "(SCM self, SCM rest) {\n" : "(SCM rest) {\n")
        << "    return rosetta_gg::guard(" << quote(group.name) << ", [&]() -> SCM {\n";
    out << "        rosetta_gg::Args args(rest);\n";
    if (kind == "setitem")
        out << "        if (args.argv.empty()) rosetta_gg::fail(rosetta_gg::Kind::wrong_args, \"ref-set! needs a value\");\n"
            << "        const SCM value = args.argv.back();\n        args.argv.pop_back();\n";
    out << handlers.first;
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads) {
        const auto overload_kind = kind == "setitem" ? "setitem" : overload.extension || kind == "function" ? "function" : "method";
        emit_overload(out, overload, overload_kind, "            ");
    }
    out << "        }\n";
    if (group.name == "equal?") out << "        return SCM_BOOL_F;\n";
    else {
        if (!fallback.empty())
            out << "        static const SCM fallback = rosetta_gg::permanent(rosetta_gg::protect([]() { return scm_c_public_ref(\"guile\", "
                << quote(fallback) << "); }));\n"
                << "        return rosetta_gg::protect([&]() { return scm_apply_0(fallback, args.list); });\n";
        else {
            std::string signatures;
            for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
            out << "        rosetta_gg::no_overload(" << quote(group.name) << ", " << quote(signatures) << ", args, "
                << (with_self ? "&self" : "nullptr") << ");\n";
        }
    }
    out << handlers.second << "    });\n}\n";
}

void Generator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor_symbol = plan.symbol + "_construct";
    const auto owner = plan.record->qualified_name();
    const bool single = plan.constructors.size() == 1;
    const auto handlers = exception_handlers(owner + "::" + plan.record->name);
    // Runs inside the guard of %construct.
    out << "SCM " << plan.constructor_symbol << "(SCM self, SCM rest) {\n"
        << "        rosetta_gg::Args args(rest);\n" << handlers.first
        << "        auto* object = rosetta_gg::instance(self);\n"
        << "        if (!object) object = rosetta_gg::attach(self);\n"
        << "        if (object->ptr) rosetta_gg::fail(rosetta_gg::Kind::runtime, \"native object already initialized\");\n";
    if (plan.director)
        out << "        const bool director = !scm_is_eq(scm_class_of(self), rosetta_gg::Info<" << owner << ">::value.klass);\n";
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", "            ");
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, "make " + plan.scheme) + "\n";
    out << "        }\n        rosetta_gg::no_overload(" << quote("make " + plan.scheme) << ", " << quote(signatures) << ", args, nullptr);\n"
        << handlers.second << "}\n";
}

void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << plan.symbol << "_director final : " << owner << ", rosetta_gg::Director {\n"
        << "    template<class... A> explicit " << plan.symbol << "_director(A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    for (const auto* method : methods) {
        bool supported = method->unsupported_reason.empty() && type_problem(method->result, Use::callback_result).empty();
        for (const auto& parameter : method->parameters) supported = supported && type_problem(parameter, Use::callback_argument).empty();
        if (!supported) continue;
        const auto key = [&] {
            std::string text = method->name + "(";
            for (const auto& parameter : method->parameters) text += parameter.canonical + ",";
            return text + (method->is_const ? ")const" : ")");
        }();
        const bool is_pure = pure[key];
        if (method->access == "private" && !is_pure) continue;
        const auto scheme = method_scheme_name(*method);
        const auto found = entries_.find(scheme);
        const bool generic = found != entries_.end() && !found->second.members.empty();
        if (!generic && !is_pure) continue; // not reachable from Scheme: nothing to override
        std::string parameters, arguments, types, call_arguments;
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            parameters += (i ? ", " : "") + method->parameters[i].canonical + " a" + std::to_string(i);
            const auto& type = method->parameters[i];
            const bool forward = type.kind == TypeKind::rvalue_reference || (type.kind != TypeKind::lvalue_reference && type.kind != TypeKind::pointer);
            const auto passed = forward ? "std::move(a" + std::to_string(i) + ")" : "a" + std::to_string(i);
            arguments += ", " + passed;
            call_arguments += (i ? ", " : "") + passed;
            types += ", " + type.canonical;
        }
        const auto result = method->result.canonical;
        const auto base_call = is_pure ? "rosetta_gg::pure_virtual(" + quote(owner + "::" + method->name) + ");"
                                       : "return " + owner + "::" + method->name + "(" + call_arguments + ");";
        out << "    " << result << " " << method->name << "(" << parameters << ")" << (method->is_const ? " const" : "")
            << (method->is_noexcept ? " noexcept" : "") << " override {\n";
        if (method->is_noexcept) out << "        try {\n";
        if (generic) {
            out << "        if (!scm_is_false(rosetta_self)) {\n"
                << "            static SCM procedure = SCM_BOOL_F;\n"
                << "            return rosetta_gg::invoke_method<" << result << types << ">(&procedure, " << quote(scheme)
                << ", rosetta_self" << arguments << ");\n"
                << "        }\n";
        }
        out << "        " << base_call << "\n";
        if (method->is_noexcept)
            out << "        } catch (...) { rosetta_gg::unraisable(" << quote(owner + "::" + method->name) << "); }\n";
        out << "    }\n";
    }
    out << "};\n";
}

void Generator::plan_members() {
    // Free functions attached to classes as methods (policy extend).
    std::map<std::string, std::vector<Group>> extensions;
    const auto receives = [&](const Function& function, const std::string& owner) {
        if (function.parameters.empty()) return false;
        const auto& first = function.parameters[0];
        const auto& base = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
        if (base.kind != TypeKind::record) return false;
        if (base.declaration == owner) return true;
        std::function<bool(const std::string&)> derives = [&](const std::string& from) {
            if (from == base.declaration) return true;
            for (const auto& b : classes_.at(from).bases) if (derives(b)) return true;
            return false;
        };
        return derives(owner);
    };
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner) && excluded(owner)) continue;
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [name, target] : methods) {
            Group group;
            bool any = false, any_static = false;
            const Function* sample = nullptr;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const bool self = receives(function, owner);
                auto why = function_problem(function, self);
                if (!why.empty()) { reject(owner + "." + name, why); continue; }
                if (!self) any_static = true;
                sample = &function;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            if (const auto* rename = renamed_raw(owner + "::" + name)) group.name = renamed(owner + "::" + name, *rename);
            else if (sample) {
                Function copy = *sample;
                copy.namespaces.clear();
                copy.owner.clear();
                if (!any_static && !copy.parameters.empty()) copy.parameters.erase(copy.parameters.begin());
                group.name = procedure_name(copy, name);
            }
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    // The frontend rejects free operator functions; any it reports stay C++-only.
    for (const auto& function : module_.functions) {
        if (function.name.rfind("operator", 0) != 0 || (function.name.size() > 8 && word(function.name[8]))) continue;
        if (extension_functions_.count(function.qualified_name()) || excluded(function.qualified_name(), function.identity())) continue;
        extension_functions_.insert(function.qualified_name());
        reject(function.qualified_name(), "free operators are not exported to Guile");
    }
    plan_directors();

    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        const auto config = policy_.classes.find(owner);
        const bool constructors = config == policy_.classes.end() || config->second.constructors;
        if (config != policy_.classes.end()) {
            for (const auto& requested : config->second.methods) {
                const auto found = std::find_if(record.methods.begin(), record.methods.end(), [&](const Function& f) { return f.name == requested; });
                if (found == record.methods.end()) throw BindingError("unknown Guile policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Guile policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown Guile policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Guile policy field is not public: " + owner + "::" + requested);
            }
        }
        if (constructors && record.public_destructor) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || (constructor.access != "public" && !(plan.director && constructor.access == "protected"))) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                // Copy constructors duplicate shallow-clone.
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::lvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner && record.constructors.size() > 1) continue;
                auto why = function_problem(constructor, false);
                if (!why.empty()) { reject(owner + "::" + record.name, why); continue; }
                plan.constructors.push_back({&constructor, owner});
            }
        }
        std::map<std::string, std::size_t> group_index;
        const auto add = [&](const std::string& scheme, const Overload& overload, bool is_static, bool operator_method) {
            const auto key = (is_static ? "static " : "") + scheme;
            const auto found = group_index.find(key);
            if (found == group_index.end()) {
                group_index[key] = plan.methods.size();
                Group group;
                group.name = scheme;
                group.overloads = {overload};
                group.is_static = is_static;
                group.operator_method = operator_method;
                plan.methods.push_back(std::move(group));
            } else plan.methods[found->second].overloads.push_back(overload);
        };
        std::vector<Overload> setitems;
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            std::string scheme;
            bool operator_method = false;
            if (method.name.rfind("operator", 0) == 0 && (method.name.size() == 8 || !word(method.name[8]))) {
                scheme = operator_name(method.name, method.parameters.size());
                if (scheme.empty()) continue; // assignment, compound, conversion and other operators stay C++-only
                if (scheme == "ref" && method.parameters.size() != 1) continue;
                operator_method = core_generic(scheme);
                if (!operator_method) scheme = renamed(method.qualified_name(), scheme);
            } else if (method.name.rfind("operator", 0) == 0 && method.name.size() > 8 && method.name[8] == ' ') {
                continue; // conversion operators
            } else scheme = method_scheme_name(method);
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            if (method.is_static) scheme = renamed_raw(method.qualified_name()) ? scheme : safe(plan.base + "-" + scheme);
            add(scheme, {&method, owner}, method.is_static, operator_method);
            // (ref-set! obj i v) through a mutable reference result.
            if (scheme == "ref" && method.result.kind == TypeKind::lvalue_reference && !referent(method.result).is_const &&
                type_problem(referent(method.result), Use::parameter).empty() && !method.is_const)
                setitems.push_back({&method, owner});
        }
        for (auto& group : extensions[owner])
            for (const auto& overload : group.overloads)
                add(group.is_static ? (renamed_raw(owner + "::" + group.name) ? group.name : safe(plan.base + "-" + group.name)) : group.name,
                    overload, group.is_static, false);
        if (!setitems.empty()) {
            Group group;
            group.name = "ref-set!";
            group.overloads = setitems;
            group.setitem = true;
            plan.methods.push_back(std::move(group));
        }
        std::set<std::string> members;
        for (const auto& group : plan.methods) if (!group.is_static) members.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported Guile field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported Guile field type", 0) == 0 ? why : "unsupported Guile field type: " + why); continue; }
            const auto getter = renamed(qualified, safe(kebab(field.name)));
            if (!members.insert(getter).second) throw BindingError("duplicate Guile member: " + plan.scheme + " " + getter);
            FieldPlan field_plan{&field, getter, {}, symbol("rosetta_field_"), {}};
            const bool writable = !field.type.is_const && !readonly(qualified, owner) && field.type.kind != TypeKind::lvalue_reference;
            if (writable) {
                auto setter = safe("set-" + getter + "!");
                if (!getter.empty() && (getter.back() == '?' || getter.back() == '_')) setter = safe("set-" + getter.substr(0, getter.size() - 1) + "!");
                if (members.insert(setter).second) field_plan.setter = setter;
            }
            plan.fields.push_back(field_plan);
        }
    }
}

void Generator::plan_functions() {
    std::map<std::string, std::size_t> function_index;
    for (const auto& function : module_.functions) {
        if (extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        const auto specialization = function_template_names_.find(function.qualified_name());
        if (!function.template_name.empty() && specialization == function_template_names_.end()) continue;
        const auto submodule = policy_module(policy_, function.namespaces);
        std::string scheme;
        if (specialization != function_template_names_.end()) scheme = renamed(function.qualified_name(), safe(kebab(specialization->second)));
        else {
            const auto base = !submodule.empty() || flat(function.namespaces) ? function.name : exported(function.qualified_name());
            scheme = procedure_name(function, base);
        }
        const auto key = submodule + "." + scheme;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions_.size();
            Group group;
            group.name = scheme;
            group.overloads = {{&function, {}}};
            group.module = submodule;
            functions_.push_back(std::move(group));
        } else functions_[found->second].overloads.push_back({&function, {}});
    }
}

// Public names: free functions, class procedures, methods and accessors share one namespace.
void Generator::plan_entries() {
    for (auto& group : functions_) {
        if (!group.module.empty()) continue;
        auto& item = entry(group.name);
        if (item.function) throw BindingError("Guile export name collision: " + group.name);
        item.function = &group;
        group.native = "%f:" + group.name;
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        for (auto& group : plan.methods) {
            if (group.operator_method) { group.native = "%c:" + plan.base + ":" + group.name; continue; }
            if (group.is_static) {
                auto& item = entry(group.name);
                if (item.function) throw BindingError("Guile export name collision: " + group.name + " (class procedure of " + owner + ")");
                item.function = &group;
                group.native = "%f:" + group.name;
                continue;
            }
            group.native = "%c:" + plan.base + ":" + group.name;
            entry(group.name).members.push_back({&plan, &group, nullptr, false});
        }
        for (auto& field : plan.fields) {
            field.native = "%c:" + plan.base + ":" + field.getter;
            entry(field.getter).members.push_back({&plan, nullptr, &field, false});
            if (!field.setter.empty()) {
                entry(field.setter).members.push_back({&plan, nullptr, &field, true});
                auto& getter = entries_.at(field.getter);
                if (getter.setter.empty()) getter.setter = field.setter;
            }
        }
    }
    // (set! (x obj) v) also through set-x! methods (Vec2::set_x).
    for (auto& [name, item] : entries_) {
        if (!item.setter.empty() || item.members.empty()) continue;
        const auto setter = "set-" + name + "!";
        const auto found = entries_.find(setter);
        if (found != entries_.end()) item.setter = setter;
    }
    for (auto& [name, item] : entries_) {
        reserve(name, "procedure " + name);
        if (core_names().count(name)) item.core = "core";
    }
}

// --- Scheme module ------------------------------------------------------------------------------
std::string Generator::scheme_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "string or #f";
        return scheme_type(pointee) + " or #f";
    }
    if (is_void(target)) return "unspecified";
    if (!conversion(target).empty()) {
        Type converted;
        converted.canonical = policy_.conversions.at(conversion(target));
        const auto bare = normalize(strip_cv(converted.canonical));
        if (bare == "double" || bare == "float") return "real";
        if (bare == "int" || bare == "long" || bare == "std::size_t" || bare == "size_t") return "exact integer";
        if (bare == "bool") return "boolean";
        if (bare == "std::string") return "string";
        if (bare.rfind("std::vector<", 0) == 0) return "list";
        return "value";
    }
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "boolean";
        if (bare == "char") return "character";
        if (bare == "float" || bare == "double" || bare == "long double") return "real";
        return "exact integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_plans_.find(target.declaration);
        std::string values;
        if (found != enum_plans_.end())
            for (const auto& item : found->second.symbols) values += (values.empty() ? "" : ", ") + item;
        if (found != enum_plans_.end() && found->second.flags) return "exact integer or list of symbols (" + values + ")";
        return values.empty() ? std::string("symbol") : "symbol (one of " + values + ")";
    }
    if (bound(target)) return classes_.at(target.declaration).scheme;
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "#f";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? scheme_type(target.arguments[i]) : std::string("value"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set") {
            if (declaration == "std::vector" && !target.arguments.empty() && strip_cv(target.arguments[0].canonical) == "unsigned char")
                return "bytevector";
            return "list of " + arg(0);
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") return "alist of (" + arg(0) + " . " + arg(1) + ")";
        if (declaration == "std::optional") return arg(0) + " or #f";
        if (declaration == "std::complex") return "complex number";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0) + " or #f";
        if (declaration == "std::pair") return "pair (" + arg(0) + " . " + arg(1) + ")";
        if (declaration == "std::tuple") {
            std::string result = "list (";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " " : "") + scheme_type(target.arguments[i]);
            return result + ")";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " or " : "") + scheme_type(target.arguments[i]);
            return result;
        }
        if (declaration == "std::function") {
            if (target.arguments.empty() || target.arguments[0].kind != TypeKind::function) return "procedure";
            const auto& signature = target.arguments[0];
            std::string result = "procedure (";
            for (std::size_t i = 1; i < signature.arguments.size(); ++i) result += (i > 1 ? " " : "") + scheme_type(signature.arguments[i]);
            return result + " -> " + scheme_type(signature.arguments[0]) + ")";
        }
        if (declaration == "std::chrono::duration") return "real (seconds)";
    }
    return "value";
}

// A default value as a Scheme literal, or "" when Scheme cannot spell it.
std::string Generator::scheme_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") {
        std::string text;
        for (char c : value.value) if (c != '\'') text += c;
        while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        bool negative = !text.empty() && text[0] == '-';
        std::string digits = negative ? text.substr(1) : text;
        std::string radix;
        if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) { radix = "#x"; digits = digits.substr(2); }
        else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B')) { radix = "#b"; digits = digits.substr(2); }
        else if (digits.size() > 1 && digits[0] == '0') { radix = "#o"; digits = digits.substr(1); }
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            return {};
        if (is_floating(type) && radix.empty()) return text + ".0";
        if (referent(type).kind == TypeKind::builtin && strip_cv(referent(type).canonical) == "bool") return text == "0" ? "#f" : "#t";
        if (referent(type).kind == TypeKind::enumeration) return {};
        return radix + (negative ? "-" : "") + digits;
    }
    if (value.kind == "float") {
        std::string text = value.value;
        if (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        if (!text.empty() && text.back() == '.') text += "0";
        if (!text.empty() && text[0] == '.') text = "0" + text;
        if (text.size() > 1 && text[0] == '-' && text[1] == '.') text = "-0" + text.substr(1);
        if (text.find_first_not_of("0123456789.eE+-") != std::string::npos) return {};
        if (text.find('.') == std::string::npos && text.find_first_of("eE") == std::string::npos) text += ".0";
        return text;
    }
    if (value.kind == "bool") return value.value == "true" ? "#t" : "#f";
    if (value.kind == "null") return "#f";
    if (value.kind == "string") {
        // Only literals whose escapes Scheme shares.
        const auto& text = value.value;
        if (text.size() < 2 || text.front() != '"' || text.back() != '"') return {};
        for (std::size_t i = 1; i + 1 < text.size(); ++i)
            if (text[i] == '\\') {
                if (i + 2 >= text.size() || std::string("\\\"nt").find(text[i + 1]) == std::string::npos) return {};
                ++i;
            }
        return text;
    }
    if (value.kind == "char") {
        if (value.value.size() != 1) return {};
        const char c = value.value[0];
        if (c == ' ') return "#\\space";
        if (std::isgraph(static_cast<unsigned char>(c))) return std::string("#\\") + c;
        return {};
    }
    if (value.kind == "enum") {
        const auto found = enum_plans_.find(referent(type).declaration);
        if (found == enum_plans_.end()) return {};
        const auto& enumeration = *found->second.enumeration;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            if (enumeration.enumerators[i] != value.value) continue;
            if (found->second.flags) return i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string();
            return "'" + found->second.symbols[i];
        }
    }
    return {};
}

// Scheme defaults of the optional parameters: literals while every earlier one is a literal too,
// #:default (the C++ default) from the first one Scheme cannot spell.
std::vector<std::string> Generator::literal_defaults(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::vector<std::string> result;
    bool literal = true;
    for (std::size_t i = std::max<std::size_t>(required, overload.receives_self ? 1 : 0); i < function.parameters.size(); ++i) {
        std::string text = literal && i < function.default_values.size() ? scheme_default(function.default_values[i], function.parameters[i]) : "";
        if (text.empty()) { literal = false; text = "#:default"; }
        result.push_back(text);
    }
    return result;
}

std::string Generator::usage(const Overload& overload, const std::string& name, const std::string& self_name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::string out = "(" + name;
    if (!self_name.empty()) out += " " + self_name;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        if (i == std::max(required, first) && i >= required) out += " #:optional";
        std::string parameter = parameter_name(function, i);
        for (auto& c : parameter) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        out += " " + parameter;
    }
    return out + ")";
}

std::string Generator::cpp_signature(const Overload& overload) const {
    const auto& function = *overload.function;
    std::string out;
    if (function.is_constructor) out = overload.owner + "::" + function.name + "(";
    else {
        out = (function.result.spelling.empty() ? function.result.canonical : function.result.spelling) + " " +
              (overload.owner.empty() || overload.extension ? function.qualified_name() : overload.owner + "::" + function.name) + "(";
    }
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        const auto& type = function.parameters[i];
        out += (i ? ", " : "") + (type.spelling.empty() ? type.canonical : type.spelling);
        const auto written = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (!written.empty()) out += " " + written;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        if (i >= required && i < function.default_values.size() && !function.default_values[i].value.empty())
            out += " = " + (function.default_values[i].kind == "char" ? "'" + function.default_values[i].value + "'" : function.default_values[i].value);
    }
    out += ")";
    if (function.is_const) out += " const";
    return out;
}

std::string Generator::documentation_text(const Documentation& documentation) const {
    std::string text = trim(documentation.brief);
    for (const auto& block : documentation.details) {
        if (block.kind == "heading") continue;
        const auto body = block.kind == "code" ? indent_lines(block.text, "    ") : trim(block.text);
        text += (text.empty() ? "" : "\n\n") + body;
    }
    for (const auto& note : documentation.notes) text += (text.empty() ? "" : "\n\n") + trim(note.text);
    return text;
}

// The docstring of a procedure: a one-line summary, the C++ description, then per signature its
// arguments, result, exceptions and C++ declaration.
std::string Generator::docstring(const std::vector<std::pair<const Overload*, std::string>>& items, const std::string& name,
                                 const Documentation* own, const std::string& fallback_summary, const std::string& extra,
                                 bool headers) const {
    (void)name;
    std::vector<std::string> paragraphs, trailer;
    std::string override_text;
    for (const auto& [overload, usage_text] : items) {
        const auto found = policy_.docs.find(overload->function->qualified_name());
        if (found != policy_.docs.end()) { override_text = trim(found->second); break; }
    }
    std::vector<const Documentation*> docs;
    std::set<std::string> seen;
    if (own && !own->empty()) docs.push_back(own);
    if (override_text.empty())
        for (const auto& [overload, usage_text] : items) {
            const auto& documentation = overload->function->documentation;
            if (!documentation.empty() && seen.insert(plain_documentation(documentation)).second) docs.push_back(&documentation);
        }
    std::string summary;
    if (!override_text.empty()) {
        const auto lines = lines_of(override_text);
        summary = one_line(lines.front());
        std::string rest;
        for (std::size_t i = 1; i < lines.size(); ++i) rest += (rest.empty() ? "" : "\n") + lines[i];
        if (!trim(rest).empty()) paragraphs.push_back(trim(rest));
    }
    for (const auto* documentation : docs) {
        const auto brief = one_line(documentation->brief);
        if (summary.empty()) summary = brief;
        else if (!brief.empty()) paragraphs.push_back(sentence(brief));
        std::string section, section_title;
        for (const auto& block : documentation->details) {
            if (block.kind == "heading") { section = section_kind(block.text); section_title = trim(block.text); continue; }
            const auto body = block.kind == "code" || block.kind == "math" ? indent_lines(block.text, "    ") : trim(block.text);
            if (section == "example") paragraphs.push_back((block.kind == "code" ? "Example:\n" : "") + body);
            else if (section == "note") trailer.push_back("Note: " + hanging(body, "  "));
            else if (section == "see") trailer.push_back("See also: " + hanging(body, "  "));
            else if (!section_title.empty()) paragraphs.push_back(section_title + (section_title.back() == ':' ? "\n" : ":\n") + body);
            else paragraphs.push_back(body);
            if (section.empty()) section_title.clear();
        }
        for (const auto& note : documentation->notes) {
            std::string label = note.name.empty() || note.name == "remark" ? std::string("Note") : capitalized(note.name);
            if (note.name == "pre") label = "Precondition";
            if (note.name == "post") label = "Postcondition";
            trailer.push_back(label + ": " + hanging(note.text, "  "));
        }
        for (const auto& target : documentation->see) trailer.push_back("See also: " + one_line(target));
    }
    if (summary.empty()) summary = fallback_summary;
    if (!extra.empty()) trailer.push_back(extra);
    // Per signature: arguments, result, exceptions, C++ declaration.
    const auto describe = [&](const Overload& overload, const std::string& indent) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        const auto defaults = literal_defaults(overload);
        std::string out;
        if (function.parameters.size() > first) out += indent + "Arguments:\n";
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp_name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            std::string text;
            for (const auto& item : function.documentation.params) if (item.name == cpp_name) text = sentence(one_line(item.text));
            std::string type_text = capitalized(scheme_type(function.parameters[i]));
            if (i >= required) {
                const auto shown = defaults[i - std::max(required, first)];
                const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
                if (shown != "#:default") type_text += "; default " + shown;
                else type_text += "; default: the C++ default" + (value.value.empty() ? std::string() : " " + value.value) + " (#:default)";
            }
            std::string parameter = parameter_name(function, i);
            for (auto& c : parameter) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            out += indent + "  " + parameter + ": " + (text.empty() ? "" : text + " ") + sentence(type_text) + "\n";
        }
        if (!function.is_constructor) {
            std::string text = sentence(one_line(function.documentation.returns));
            for (const auto& value : function.documentation.retvals)
                text += (text.empty() ? "" : " ") + value.name + (value.text.empty() ? "" : ": " + sentence(one_line(value.text)));
            if (!is_void(function.result) || !text.empty())
                out += indent + "Returns: " + (text.empty() ? "" : text + " ") + sentence(capitalized(scheme_type(function.result))) + "\n";
        }
        for (const auto& item : function.documentation.throws) {
            const auto key = prefix_ + "-" + exception_suffix(item.name);
            out += indent + "Raises: " + key + (item.name.empty() ? "" : " (" + item.name + ")") +
                   (item.text.empty() ? "" : " " + sentence(one_line(item.text))) + "\n";
        }
        out += indent + "C++: " + cpp_signature(overload) + "\n";
        return out;
    };
    std::string signatures;
    if (items.size() == 1 && !headers) signatures = describe(*items.front().first, "");
    else
        for (const auto& [overload, usage_text] : items) signatures += usage_text + "\n" + describe(*overload, "  ");
    std::string out = summary;
    for (const auto& text : paragraphs) out += "\n\n" + text;
    if (!signatures.empty()) {
        while (!signatures.empty() && signatures.back() == '\n') signatures.pop_back();
        out += "\n\n" + signatures;
    }
    if (!trailer.empty()) {
        out += "\n";
        for (const auto& text : trailer) out += "\n" + text;
    }
    return out;
}

std::string Generator::class_doc(const ClassPlan& plan) const {
    const auto& record = *plan.record;
    const auto found = policy_.docs.find(record.qualified_name());
    std::string text = found != policy_.docs.end() ? trim(found->second) : documentation_text(record.documentation);
    if (text.empty()) text = "The C++ class " + record.qualified_name() + ".";
    std::string constructors;
    for (const auto& overload : plan.constructors) constructors += "\n  " + usage(overload, "make " + plan.scheme, "");
    if (!constructors.empty()) text += "\n\nConstruct with:" + constructors;
    else text += "\n\nInstances come from C++ functions (no public constructor).";
    if (!plan.bases.empty()) {
        std::string bases;
        for (const auto& base : plan.bases) bases += (bases.empty() ? "" : " ") + classes_.at(base).scheme;
        text += "\nBase classes: " + bases + ".";
    }
    std::string methods, operators, statics, fields;
    for (const auto& group : plan.methods) {
        if (group.operator_method) operators += (operators.empty() ? "" : " ") + group.name;
        else if (group.is_static) statics += (statics.empty() ? "" : " ") + group.name;
        else methods += (methods.empty() ? "" : " ") + group.name;
    }
    for (const auto& field : plan.fields)
        fields += (fields.empty() ? "" : " ") + field.getter + (field.setter.empty() ? "" : " (" + field.setter + ")");
    if (!methods.empty()) text += "\nMethods: " + methods + ".";
    if (!fields.empty()) text += "\nFields: " + fields + ".";
    if (!operators.empty()) text += "\nOperators (core generics): " + operators + ".";
    if (!statics.empty()) text += "\nClass procedures: " + statics + ".";
    if (plan.director) {
        std::string virtual_names;
        std::map<std::string, bool> pure;
        for (const auto* method : virtuals(record, pure)) {
            const auto name = method_scheme_name(*method);
            if ((" " + virtual_names + " ").find(" " + name + " ") == std::string::npos)
                virtual_names += (virtual_names.empty() ? "" : " ") + name;
        }
        text += "\n\nSubclass it with define-class and specialize " + virtual_names +
                " with define-method: C++ calls the Scheme methods, and (next-method) runs the C++ implementation.";
    } else if (record.abstract) text += "\n\nAbstract: instances come from C++.";
    text += "\nC++: " + record.qualified_name();
    return text;
}

// Defines a module procedure calling a native procedure. A single overload without a core
// fallback gets its parameters as formals (defaults as #:optional), so editors show them.
std::string Generator::function_definition(const Group& group, const std::string& name, const std::string& native, const std::string& doc,
                                           bool rest_only) const {
    std::ostringstream out;
    if (group.overloads.size() == 1 && !rest_only) {
        const auto& overload = group.overloads.front();
        const auto& function = *overload.function;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        const auto defaults = literal_defaults(overload);
        std::string formals, call;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const auto parameter = parameter_name(function, i);
            if (i == required) formals += " #:optional";
            formals += i >= required ? " (" + parameter + " " + defaults[i - required] + ")" : " " + parameter;
            call += " " + parameter;
        }
        out << "(define" << (function.default_arguments ? "* (" : " (") << name << formals << ")\n  " << scheme_string(doc) << "\n  ("
            << native << call << "))\n";
    } else out << "(define (" << name << " . args)\n  " << scheme_string(doc) << "\n  (core:apply " << native << " args))\n";
    return out.str();
}

std::string Generator::entry_doc(const Entry& entry) const {
    std::vector<std::pair<const Overload*, std::string>> items;
    std::set<std::string> seen;
    std::string fallback;
    if (entry.function) {
        for (const auto& overload : entry.function->overloads) items.push_back({&overload, usage(overload, entry.name, "")});
        fallback = "Call the C++ function " + entry.function->overloads.front().function->qualified_name() + ".";
    }
    std::string field_text;
    for (const auto& member : entry.members) {
        const auto self_name = [&] {
            auto text = member.klass->base;
            for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return text;
        }();
        if (member.group) {
            // Overrides of a virtual method repeat their base's signature: listed once.
            for (const auto& overload : member.group->overloads)
                if (seen.insert(signature(overload, entry.name)).second) items.push_back({&overload, usage(overload, entry.name, self_name)});
            if (fallback.empty()) fallback = "Call the C++ method " + member.group->overloads.front().function->qualified_name() + ".";
        } else if (member.field) {
            const auto& field = *member.field->field;
            const auto description = one_line(field.documentation.brief);
            const auto type = scheme_type(field.type);
            if (member.setter) {
                field_text += "\n(" + entry.name + " " + self_name + " VALUE)\n  Set the field " + member.klass->record->qualified_name() +
                              "::" + field.name + (description.empty() ? "" : " (" + sentence(description) + ")") + "\n  VALUE: " +
                              sentence(capitalized(type));
                if (fallback.empty()) fallback = "Set the field " + field.name + " of a " + member.klass->scheme + ".";
            } else {
                field_text += "\n(" + entry.name + " " + self_name + ")\n  The field " + member.klass->record->qualified_name() + "::" +
                              field.name + (description.empty() ? "." : ": " + sentence(description)) + "\n  Returns: " + sentence(capitalized(type));
                const auto& setter = member.field->setter.empty() ? entry.setter : member.field->setter;
                if (!setter.empty())
                    field_text += "\n  Set with (" + setter + " " + self_name + " VALUE) or (set! (" + entry.name + " " + self_name + ") VALUE).";
                if (fallback.empty())
                    fallback = description.empty() ? "The field " + field.name + " of a " + member.klass->scheme + "." : sentence(description);
            }
        }
    }
    std::string extra;
    if (!entry.members.empty()) {
        std::string classes;
        for (const auto& member : entry.members)
            if ((" " + classes + " ").find(" " + member.klass->scheme + " ") == std::string::npos)
                classes += (classes.empty() ? "" : " ") + member.klass->scheme;
        extra = "Generic function with methods for " + classes + "; extend it with define-method.";
        if (entry.function) extra += " Other arguments call the module procedure above.";
    }
    if (!entry.core.empty()) extra += (extra.empty() ? "" : " ") + std::string("Arguments no C++ overload accepts call Guile's ") + entry.name + ".";
    std::string doc;
    if (!items.empty()) doc = docstring(items, entry.name, nullptr, fallback, "", !entry.members.empty());
    else doc = fallback;
    if (!field_text.empty()) doc += "\n\n" + trim(field_text);
    if (!extra.empty()) doc += "\n\n" + extra;
    return doc;
}

std::string Generator::submodule_file(const std::string& module, const std::vector<const Group*>& groups) const {
    auto path = module_path_;
    for (const auto& part : submodule_path(module)) path.push_back(part);
    std::ostringstream out, body;
    std::string exports, replaces;
    for (const auto* group : groups) {
        const bool core = core_names().count(group->name) != 0;
        (core ? replaces : exports) += " " + group->name;
        std::vector<std::pair<const Overload*, std::string>> items;
        for (const auto& overload : group->overloads) items.push_back({&overload, usage(overload, group->name, "")});
        auto doc = docstring(items, group->name, nullptr, "Call the C++ function " + group->overloads.front().function->qualified_name() + ".",
                             core ? "Arguments no C++ overload accepts call Guile's " + group->name + "." : "");
        body << "\n" << function_definition(*group, group->name, "(@@ " + module_spelling(module_path_) + " " + group->native + ")", doc, core);
    }
    out << ";;; Generated by Rosetta (--guile-api full): functions of the C++ namespace mapped to the\n"
        << ";;; " << module << " module. Procedures and docstrings as in " << module_spelling(module_path_) << ".\n"
        << "(define-module " << module_spelling(path) << "\n"
        << "  #:use-module ((guile) #:prefix core:)\n"
        << "  #:use-module (" << module_spelling(module_path_) << " #:select ())\n";
    if (!exports.empty()) out << "  #:export (" << trim(exports) << ")\n";
    if (!replaces.empty()) out << "  #:replace (" << trim(replaces) << ")\n";
    out.seekp(-1, std::ios_base::cur);
    out << ")\n" << body.str();
    return out.str();
}

std::string Generator::scheme_file(const std::string& extension, const std::string& init) {
    std::ostringstream out, body;
    std::vector<std::string> exports, replaces;
    const auto export_name = [&](const std::string& name, bool replace) { (replace ? replaces : exports).push_back(name); };
    for (const char* name : {"<native-object>", "close!", "closed?", "alive?", "owned?"}) export_name(name, false);
    for (const auto* suffix : {"-error?", "-error-kind", "-error-message", "-error-cpp-class"}) export_name(prefix_ + suffix, false);
    // Classes.
    body << "\n;;; ------------------------------------------------------------------ classes\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        std::string supers;
        for (const auto& base : plan.bases) supers += (supers.empty() ? "" : " ") + classes_.at(base).scheme;
        if (supers.empty()) supers = "<native-object>";
        const auto doc = class_doc(plan);
        body << "\n" << comment(doc) << "(goops:define-class " << plan.scheme << " (" << supers << "))\n"
             << "(core:set-object-property! " << plan.scheme << " 'documentation\n  " << scheme_string(doc) << ")\n";
        export_name(plan.scheme, false);
    }
    // Using-declarations re-export bound classes under another name.
    for (const auto& reexport : module_.reexports) {
        if (reexport.kind != "record" && reexport.kind != "type") continue;
        const auto c = classes_.find(reexport.target);
        if (c == classes_.end()) continue;
        std::string scope;
        for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
        const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
        const auto alias = "<" + kebab(flat(reexport.namespaces) ? reexport.name : exported(qualified)) + ">";
        if (alias == c->second.scheme || names_.count(alias) || renamed_names().count(alias)) continue;
        reserve(alias, "alias of " + reexport.target);
        body << "\n;; Alias of " << c->second.scheme << " (using " << reexport.target << ").\n(define " << alias << " " << c->second.scheme << ")\n";
        export_name(alias, false);
    }
    // Loading the extension: beside this file, else through Guile's extension path.
    std::string relative;
    for (const auto& part : module_path_) relative += (relative.empty() ? "" : "/") + part;
    body << "\n;;; ------------------------------------------------------------------ native procedures\n"
         << ";; Defined by the extension (declared for the compiler; load-extension replaces them).\n";
    {
        std::vector<std::string> natives;
        for (const auto& group : functions_) natives.push_back(group.native);
        for (const auto& owner : class_order_) {
            const auto& plan = classes_.at(owner);
            for (const auto& group : plan.methods) natives.push_back(group.native);
            for (const auto& field : plan.fields) {
                natives.push_back(field.native);
                if (!field.setter.empty()) natives.push_back(field.native + "-set!");
            }
        }
        for (const auto& constant : constants_) natives.push_back(constant.native);
        std::size_t column = 0;
        for (const auto& native : natives) {
            const auto text = "(define " + native + " #f)";
            if (column && column + text.size() > 100) { body << "\n"; column = 0; }
            body << (column ? " " : "") << text;
            column += text.size() + 1;
        }
        if (column) body << "\n";
    }
    body << "(let* ((source (core:search-path core:%load-path " << scheme_string(relative + ".scm") << "))\n"
         << "       (candidates (core:if source\n"
         << "                        (core:map (lambda (suffix) (core:string-append (core:dirname source) \"/\" "
         << scheme_string(extension) << " suffix))\n"
         << "                                  '(\".so\" \".dylib\" \".dll\"))\n"
         << "                        '()))\n"
         << "       (found (core:let loop ((files candidates))\n"
         << "                (core:cond ((core:null? files) " << scheme_string(extension) << ")\n"
         << "                           ((core:file-exists? (core:car files)) (core:car files))\n"
         << "                           (core:else (loop (core:cdr files)))))))\n"
         << "  (core:load-extension found " << scheme_string(init) << "))\n";
    // Enumerations.
    if (!enum_plans_.empty()) body << "\n;;; ------------------------------------------------------------------ enumerations\n";
    for (const auto& [qualified, plan] : enum_plans_) {
        const auto& enumeration = *plan.enumeration;
        std::string table, values;
        for (std::size_t i = 0; i < plan.symbols.size(); ++i) {
            const auto& value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string("0");
            table += (i ? " " : "") + std::string("(") + plan.symbols[i] + " . " + value + ")";
            values += (i ? " " : "") + plan.symbols[i];
        }
        const auto description = documentation_text(enumeration.documentation);
        const auto table_name = "%e:" + plan.base;
        body << "\n" << comment((description.empty() ? "The C++ enumeration " + qualified + "." : description) +
                                (plan.flags ? "\nFlags: values are exact integers; arguments also take a list of symbols."
                                            : "\nValues are the symbols " + values + "; arguments also take the integer value."))
             << "(define " << table_name << " '(" << table << "))\n";
        const auto doc_head = description.empty() ? "the C++ enumeration " + qualified : qualified;
        body << "(define (" << plan.base << "-values)\n  "
             << scheme_string("Return the enumerators of " + doc_head + " as a list of symbols: (" + values + ").\n\nC++: enum " + qualified)
             << "\n  (core:map core:car " << table_name << "))\n";
        body << "(define (" << plan.base << "->integer value)\n  "
             << scheme_string("Return the integer value of an enumerator of " + qualified + ".\n\nArguments:\n  VALUE: Symbol (one of " + values +
                              ") or exact integer.\nReturns: Exact integer.\nRaises: " + prefix_ + "-invalid-argument for an unknown symbol.")
             << "\n  (%enum->integer " << table_name << " '" << plan.base << " value))\n";
        body << "(define (integer->" << plan.base << " value)\n  "
             << scheme_string("Return the enumerator of " + qualified + " with integer VALUE, or #f.\n\nArguments:\n  VALUE: Exact integer.\nReturns: Symbol or #f.")
             << "\n  (%integer->enum " << table_name << " value))\n";
        for (const auto& name : {plan.base + "-values", plan.base + "->integer", "integer->" + plan.base}) export_name(name, false);
    }
    // Procedures, generics and accessors.
    body << "\n;;; ------------------------------------------------------------------ procedures\n";
    for (const auto& name : entry_order_) {
        const auto& item = entries_.at(name);
        const auto doc = entry_doc(item);
        const bool replace = !item.core.empty();
        export_name(name, replace);
        body << "\n";
        if (item.members.empty()) {
            body << function_definition(*item.function, name, item.function->native, doc, replace);
            continue;
        }
        const auto generic = "%g:" + name;
        const bool open = item.function || replace; // the default method takes any arguments
        body << "(define " << generic << " (%generic '" << name << "))\n";
        for (const auto& member : item.members) {
            const auto& klass = member.klass->scheme;
            if (member.group)
                body << "(goops:add-method! " << generic << " (goops:method ((self " << klass << ") . args) (core:apply "
                     << member.group->native << " self args)))\n";
            else if (member.setter)
                body << "(goops:add-method! " << generic << " (goops:method ((self " << klass << ") value) (" << member.field->native
                     << "-set! self value)))\n";
            else
                body << "(goops:add-method! " << generic << " (goops:method ((self " << klass << ")) (" << member.field->native << " self)))\n";
        }
        if (item.function)
            body << "(goops:add-method! " << generic << " (goops:method args (core:apply " << item.function->native << " args)))\n";
        else if (replace)
            body << "(goops:add-method! " << generic << " (goops:method args (core:apply core:" << name << " args)))\n";
        body << "(define (" << name << (open ? " . args)" : " self . args)") << "\n  " << scheme_string(doc) << "\n  (core:apply " << generic
             << (open ? " args))\n" : " self args))\n");
        // set!, not a second define: one definition per name (no shadowed-toplevel warning), and
        // editors still see the documented define above.
        body << "(set! " << name << " (%documented " << name << " " << generic << " "
             << (item.setter.empty() ? "#f" : "(lambda (self value) (" + item.setter + " self value))") << "))\n";
    }
    // Operators extend the core generics.
    bool any_operator = false;
    for (const auto& owner : class_order_)
        for (const auto& group : classes_.at(owner).methods) {
            if (!group.operator_method) continue;
            if (!any_operator) body << "\n;;; ------------------------------------------------------------------ operators\n";
            any_operator = true;
            body << "(goops:add-method! core:" << group.name << " (goops:method ((self " << classes_.at(owner).scheme
                 << ") . args) (core:apply " << group.native << " self args)))\n";
            // The compiler turns (- x) into (- 0 x).
            const bool unary = std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                return o.function->parameters.empty();
            });
            if (group.name == "-" && unary)
                body << "(goops:add-method! core:- (goops:method ((zero goops:<number>) (self " << classes_.at(owner).scheme
                     << ")) (core:if (core:eqv? zero 0) (" << group.native << " self) (next-method))))\n";
        }
    // make-<class>.
    bool any_maker = false;
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        if (plan.constructors.empty()) continue;
        const auto maker = "make-" + plan.base;
        if (names_.count(maker) || renamed_names().count(maker) || core_names().count(maker)) continue;
        reserve(maker, "constructor of " + owner);
        plan.maker = maker;
        if (!any_maker) body << "\n;;; ------------------------------------------------------------------ constructors\n";
        any_maker = true;
        std::vector<std::pair<const Overload*, std::string>> items;
        for (const auto& overload : plan.constructors) items.push_back({&overload, usage(overload, maker, "")});
        const auto doc = docstring(items, maker, nullptr, "Create a " + plan.scheme + ".", "Same as (make " + plan.scheme + " ARG ...).");
        body << "\n(define (" << maker << " . args)\n  " << scheme_string(doc) << "\n  (core:apply goops:make " << plan.scheme << " args))\n";
        export_name(maker, false);
    }
    // Constants.
    if (!constants_.empty()) body << "\n;;; ------------------------------------------------------------------ constants\n";
    for (const auto& constant : constants_) {
        const auto description = documentation_text(*constant.documentation);
        body << "\n" << comment((description.empty() ? "The C++ constant " + constant.expression.substr(constant.expression.rfind("::") == 0 ? 2 : 0) + "."
                                                     : description) + "\nType: " + scheme_type(constant.type) + ".")
             << "(define " << constant.scheme << " " << constant.native << ")\n";
        export_name(constant.scheme, false);
    }
    // Function aliases (using-declarations).
    for (const auto& reexport : module_.reexports) {
        if (reexport.kind != "function") continue;
        std::string scope;
        for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
        const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
        for (const auto& group : functions_) {
            if (!group.module.empty() || std::none_of(group.overloads.begin(), group.overloads.end(), [&](const Overload& o) {
                    return o.function->qualified_name() == reexport.target; })) continue;
            Function copy = *group.overloads.front().function;
            copy.namespaces.clear();
            const auto alias = renamed(qualified, procedure_name(copy, flat(reexport.namespaces) ? reexport.name : exported(qualified)));
            if (alias == group.name || names_.count(alias) || core_names().count(alias)) break;
            reserve(alias, "alias of " + reexport.target);
            body << "\n;; Alias of " << group.name << " (using " << reexport.target << ").\n(define " << alias << " " << group.name << ")\n";
            export_name(alias, false);
            break;
        }
    }

    out << ";;; Generated by Rosetta: Guile module " << module_spelling(module_path_) << " (--guile-api full) for " << module_.header << ".\n"
        << ";;;\n"
        << ";;; Classes are GOOPS classes deriving from <native-object>; (make <class> arg ...) calls the C++\n"
        << ";;; constructor. Methods and field accessors are generic functions: a Scheme subclass specializes\n"
        << ";;; them with define-method, and C++ calls its virtual methods back. Optional parameters take\n"
        << ";;; #:default for the C++ default. C++ exceptions raise " << prefix_ << "-invalid-argument, "
        << prefix_ << "-out-of-range,\n;;; ... (see " << prefix_ << "-error?). Every procedure has a docstring (procedure-documentation).\n"
        << "(define-module " << module_spelling(module_path_) << "\n"
        << "  #:use-module ((guile) #:prefix core:)\n"
        << "  #:use-module ((oop goops) #:prefix goops:)\n"
        << "  #:use-module ((ice-9 exceptions) #:prefix exn:)\n"
        << "  #:declarative? #f\n";
    const auto list = [&](const char* keyword, const std::vector<std::string>& names) {
        if (names.empty()) return;
        out << "  " << keyword << " (";
        std::size_t column = 4 + std::string(keyword).size();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i && column + names[i].size() > 96) { out << "\n           "; column = 11; }
            else if (i) { out << " "; ++column; }
            out << names[i];
            column += names[i].size();
        }
        out << ")\n";
    };
    list("#:export", exports);
    list("#:replace", replaces);
    out.seekp(-1, std::ios_base::cur);
    out << ")\n";
    auto runtime = std::string(detail::guile_full_runtime_scheme);
    for (std::size_t i = 0; (i = runtime.find("@PREFIX@", i)) != std::string::npos;) runtime.replace(i, 8, prefix_);
    out << runtime << body.str();
    return out.str();
}

GeneratedGuileFullSource Generator::run() {
    // Module path: "(a b)", "a b" or "a.b"; default the module name.
    {
        std::string text = options_.module.empty() ? name_ : options_.module;
        for (auto& c : text) if (c == '(' || c == ')' || c == '.' || c == '/') c = ' ';
        std::istringstream stream(text);
        for (std::string part; stream >> part;) {
            if (!scheme_symbol_name(part)) throw BindingError("Guile module name part is not a symbol: " + part);
            module_path_.push_back(part);
        }
        if (module_path_.empty()) throw BindingError("Guile module name is empty");
    }
    for (std::size_t i = 0; i < module_path_.size(); ++i) prefix_ += (i ? "-" : "") + module_path_[i];
    const auto extension = options_.extension.empty() ? "lib" + identifier(name_) + "_guile" : options_.extension;
    std::string init_name = "init";
    for (const auto& part : module_path_) init_name += "_" + identifier(part);
    for (const auto& rule : policy_.enum_flags)
        if (std::none_of(module_.enumerations.begin(), module_.enumerations.end(), [&](const Enumeration& e) { return e.qualified_name() == rule.first; }))
            throw BindingError("enum flag policy refers to an unknown enum: " + rule.first);
    GeneratedGuileFullSource result;
    std::set<std::string> variables;
    for (const auto& variable : module_.variables) variables.insert(variable.qualified_name() + ": unsupported declaration VarDecl");
    for (const auto& record : module_.records)
        if (!record.template_name.empty()) variables.insert(record.template_name + ": unsupported declaration ClassTemplateDecl");
    for (const auto& diagnostic : module_.diagnostics) {
        if (variables.count(diagnostic)) continue;
        const auto colon = diagnostic.find(": ");
        if (colon != std::string::npos && excluded(diagnostic.substr(0, colon))) continue;
        diagnostics_.push_back(diagnostic);
    }

    for (const auto& enumeration : module_.enumerations)
        if (enumeration.selected && !enumeration.enumerators.empty() && !excluded(enumeration.qualified_name()))
            enums_[enumeration.qualified_name()] = &enumeration;
    plan_classes();
    for (auto it = enums_.begin(); it != enums_.end();) {
        if (!it->second->owner.empty() && !classes_.count(it->second->owner)) it = enums_.erase(it);
        else ++it;
    }
    for (const auto& [qualified, enumeration] : enums_) {
        EnumPlan plan;
        plan.enumeration = enumeration;
        const bool nested = !enumeration->owner.empty();
        const auto base = nested ? classes_.at(enumeration->owner).base + "-" + kebab(enumeration->name)
                                 : kebab(flat(enumeration->namespaces) ? enumeration->name : exported(qualified));
        plan.base = renamed(qualified, base); // only the helpers <base>-values, ... are bound
        plan.flags = policy_.enum_flags.count(qualified) != 0;
        // Enumerators whose kebab-case names collide (None and none) keep their C++ spelling.
        std::map<std::string, int> uses;
        for (const auto& enumerator : enumeration->enumerators) ++uses[kebab(enumerator)];
        std::set<std::string> symbols;
        for (const auto& enumerator : enumeration->enumerators) {
            auto name = renamed(qualified + "::" + enumerator, uses[kebab(enumerator)] > 1 ? enumerator : kebab(enumerator));
            while (!symbols.insert(name).second) name += "_";
            plan.symbols.push_back(name);
        }
        enum_plans_[qualified] = plan;
    }
    plan_members();
    plan_functions();
    for (auto& group : functions_)
        if (!group.module.empty()) {
            std::string native = "%f:";
            for (const auto& part : submodule_path(group.module)) native += part + ":";
            group.native = native + group.name;
        }
    plan_entries();
    for (const auto& name : {std::string("<native-object>"), std::string("close!"), std::string("closed?"), std::string("alive?"),
                             std::string("owned?"), prefix_ + "-error?", prefix_ + "-error-kind", prefix_ + "-error-message",
                             prefix_ + "-error-cpp-class"})
        reserve(name, "the Guile runtime");
    for (const auto& owner : class_order_) reserve(classes_.at(owner).scheme, "class " + owner);
    for (const auto& [qualified, plan] : enum_plans_)
        for (const auto& name : {plan.base + "-values", plan.base + "->integer", "integer->" + plan.base}) reserve(name, "enum " + qualified);
    // Constants: namespace-scope variables and static members.
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to Guile"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto scheme = renamed(qualified, safe(kebab(flat(variable.namespaces) ? variable.name : exported(qualified))));
        if (names_.count(scheme) || core_names().count(scheme)) { reject(qualified, "constant name collides with another binding: " + scheme); continue; }
        reserve(scheme, "constant " + qualified);
        constants_.push_back({scheme, {}, "::" + qualified, "%k:" + scheme, &variable.documentation, variable.type});
    }
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& variable : plan.record->static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to Guile"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            const auto scheme = renamed(qualified, safe(plan.base + "-" + kebab(variable.name)));
            if (names_.count(scheme) || core_names().count(scheme)) { reject(qualified, "constant name collides with another binding: " + scheme); continue; }
            reserve(scheme, "constant " + qualified);
            constants_.push_back({scheme, owner, qualified, "%k:" + scheme, &variable.documentation, variable.type});
        }
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Guile";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Wrapper code is collected in units (a class, or a function) that a split build spreads over
    // several sources; `out` is the prelude every source repeats.
    struct Unit { std::string code, declaration; };
    std::vector<Unit> units;
    std::ostringstream out, init;
    std::string module_namespace = "rosetta_gg_module_" + identifier(prefix_);
    out << "// Generated by Rosetta: Guile extension (--guile-api full) for the module " << module_spelling(module_path_) << ".\n";
    for (const auto* header : {"array", "chrono", "climits", "cmath", "complex", "cstdio", "cstdlib", "cstring", "deque", "exception",
                               "filesystem", "functional", "ios", "iostream", "limits", "list", "map", "memory", "mutex", "new", "optional",
                               "set", "sstream", "stdexcept", "string", "string_view", "tuple", "typeindex", "typeinfo", "type_traits",
                               "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector", "cxxabi.h"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n';
    out << "#include <libguile.h>\n";
    out << "#define ROSETTA_GG_MODULE " << module_namespace << '\n';
    for (const auto* part : detail::guile_full_runtime_parts) out << part;
    out << "namespace ROSETTA_GG_MODULE { namespace rosetta_gg {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(SCM value, bool strict) { return Value<" << to << ">::check(value, strict); }\n"
            << "    static " << from << " load(SCM value) { return " << from << "(Value<" << to << ">::load(value)); }\n"
            << "    static SCM cast(const " << from << "& value) { return Value<" << to << ">::cast(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\nnamespace ROSETTA_GG_MODULE {\n";
    for (const auto& owner : class_order_) if (classes_.at(owner).director) emit_director(out, classes_.at(owner));
    out << "}\n";

    const auto subr = [](const std::string& symbol) { return "reinterpret_cast<scm_t_subr>(&" + symbol + ")"; };
    init << "        rosetta_gg::initialize_runtime(" << quote(prefix_) << ");\n";
    // Free functions (module-level and submodules).
    for (auto& group : functions_) {
        std::ostringstream unit;
        // Arguments no overload accepts go to the core procedure of the same name.
        const bool fallback = core_names().count(group.name) != 0;
        emit_group(unit, group, "function", fallback ? group.name : std::string());
        units.push_back({unit.str(), "SCM " + group.symbol + "(SCM);\n"});
        init << "        rosetta_gg::define(" << quote(group.native) << ", 0, 1, " << subr(group.symbol) << ");\n";
    }
    // Classes: registration, then their natives.
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        init << "        {\n            auto& info = rosetta_gg::Info<" << owner << ">::value;\n"
             << "            rosetta_gg::register_type<" << owner << ">(info, " << quote(plan.scheme) << ", rosetta_gg::lookup(" << quote(plan.scheme) << "));\n"
             << "            info.bases.clear();\n";
        for (const auto& base : plan.bases) init << "            rosetta_gg::add_base<" << owner << ", " << base << ">(info);\n";
        init << "        }\n";
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::ostringstream unit, registration;
        std::string declarations;
        for (auto& group : plan.methods) {
            const bool fallback = group.is_static && core_names().count(group.name) != 0;
            const auto kind = group.setitem ? "setitem" : group.is_static ? "function" : "method";
            emit_group(unit, group, kind, fallback ? group.name : std::string());
            if (group.is_static) {
                declarations += "SCM " + group.symbol + "(SCM);\n";
                registration << "            rosetta_gg::define(" << quote(group.native) << ", 0, 1, " << subr(group.symbol) << ");\n";
            } else {
                declarations += "SCM " + group.symbol + "(SCM, SCM);\n";
                registration << "            rosetta_gg::define(" << quote(group.native) << ", 1, 1, " << subr(group.symbol) << ");\n";
            }
        }
        for (auto& field : plan.fields) {
            const auto& name = field.field->name;
            unit << "SCM " << field.symbol << "_get(SCM self) {\n    return rosetta_gg::guard(" << quote(field.getter) << ", [&]() -> SCM {\n"
                 << "        auto* native = rosetta_gg::native<" << owner << ">(self);\n"
                 << "        return rosetta_gg::result<decltype((native->" << name << "))>(native->" << name << ", self, rosetta_gg::Return::"
                 << (copy_type(field.field->type) ? "copy" : "automatic") << ");\n    });\n}\n";
            declarations += "SCM " + field.symbol + "_get(SCM);\n";
            registration << "            scm_c_define_gsubr(" << quote(field.native) << ", 1, 0, 0, " << subr(field.symbol + "_get") << ");\n";
            if (!field.setter.empty()) {
                unit << "SCM " << field.symbol << "_set(SCM self, SCM value) {\n    return rosetta_gg::guard(" << quote(field.setter) << ", [&]() -> SCM {\n"
                     << "        auto* native = rosetta_gg::native<" << owner << ">(self);\n"
                     << "        rosetta_gg::Param<const std::remove_cv_t<std::remove_reference_t<decltype(std::declval<" << owner << "&>()." << name
                     << ")>>&> converted(value);\n"
                     << "        rosetta_gg::assign_field(native->" << name << ", converted.get());\n        return SCM_UNSPECIFIED;\n    });\n}\n";
                declarations += "SCM " + field.symbol + "_set(SCM, SCM);\n";
                registration << "            scm_c_define_gsubr(" << quote(field.native + "-set!") << ", 2, 0, 0, " << subr(field.symbol + "_set") << ");\n";
            }
        }
        if (!plan.constructors.empty()) {
            emit_constructor(unit, plan);
            declarations += "SCM " + plan.constructor_symbol + "(SCM, SCM);\n";
            registration << "            rosetta_gg::Info<" << owner << ">::value.construct = &" << plan.constructor_symbol << ";\n";
        }
        for (const auto& constant : constants_) {
            if (constant.owner != owner) continue;
            registration << "            scm_c_define(" << quote(constant.native) << ", rosetta_gg::Value<std::remove_cv_t<decltype("
                         << constant.expression << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>("
                         << constant.expression << ")));\n";
        }
        units.push_back({unit.str(), declarations});
        init << "        {\n" << registration.str() << "        }\n";
    }
    // Enumerations.
    for (const auto& [qualified, plan] : enum_plans_) {
        const auto& enumeration = *plan.enumeration;
        init << "        rosetta_gg::define_enum<" << qualified << ">(" << (plan.flags ? "true" : "false") << ", {";
        for (std::size_t i = 0; i < plan.symbols.size(); ++i) {
            const auto& value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string("0");
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            const auto number = big ? "static_cast<long long>(" + value + "ULL)" : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL";
            init << (i ? ", " : "") << "{" << quote(plan.symbols[i]) << ", " << number << "}";
        }
        init << "});\n";
    }
    for (const auto& constant : constants_) {
        if (!constant.owner.empty()) continue;
        init << "        scm_c_define(" << quote(constant.native) << ", rosetta_gg::Value<std::remove_cv_t<decltype(" << constant.expression
             << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>(" << constant.expression << ")));\n";
    }

    // Units go to parts - 1 extra sources by size (largest first, into the smallest source),
    // keeping declaration order within each; the init function stays in the main source.
    const std::size_t parts = options_.parts;
    std::vector<std::ostringstream> extra(parts > 1 ? parts - 1 : 0);
    if (!extra.empty()) {
        std::vector<std::size_t> order(units.size()), sizes(extra.size(), 0), bin(units.size(), 0);
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return units[a].code.size() > units[b].code.size(); });
        for (const auto index : order) {
            const auto smallest = static_cast<std::size_t>(std::min_element(sizes.begin(), sizes.end()) - sizes.begin());
            bin[index] = smallest;
            sizes[smallest] += units[index].code.size();
        }
        const auto prelude = out.str();
        for (auto& part : extra) part << prelude << "namespace ROSETTA_GG_MODULE {\n";
        out << "namespace ROSETTA_GG_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) {
            extra[bin[i]] << units[i].code;
            out << units[i].declaration;
        }
        for (auto& part : extra) part << "} // namespace ROSETTA_GG_MODULE\n";
    } else {
        out << "namespace ROSETTA_GG_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "static void rosetta_init() {\n    rosetta_gg::guard(\"load-extension\", [&]() -> SCM {\n" << init.str()
        << "        return SCM_UNSPECIFIED;\n    });\n}\n} // namespace ROSETTA_GG_MODULE\n"
        << "extern \"C\" __attribute__((visibility(\"default\"))) void " << init_name << "(void) {\n"
        << "    ROSETTA_GG_MODULE::rosetta_init();\n}\n";
    result.source = out.str();
    for (const auto& part : extra) result.parts.push_back(part.str());
    result.scheme = scheme_file(extension, init_name);
    std::map<std::string, std::vector<const Group*>> submodules;
    for (const auto& group : functions_) if (!group.module.empty()) submodules[group.module].push_back(&group);
    for (const auto& [module, groups] : submodules) {
        std::string relative = module_path_.back();
        for (const auto& part : submodule_path(module)) relative += "/" + part;
        result.submodules.emplace_back(relative + ".scm", submodule_file(module, groups));
    }
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedGuileFullSource generate_guile_full(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy,
                                             const GuileFullOptions& options) {
    if (options.parts == 0) throw BindingError("Guile source parts must be positive");
    return Generator(module, name, skip, policy, options).run();
}
} // namespace rosetta
