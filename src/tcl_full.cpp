// The full Tcl backend (--language tcl --tcl-api full): a Tcl extension (C++ against the Tcl C
// API, built with stubs) whose classes are TclOO classes, plus a documented Tcl package file.
//
// Generated layout for module M (Tcl namespace ::NS, package M, shared library LIB):
//   OUTPUT (+ parts)  C++ sources of the extension; Prefix_Init (Prefix: M with its first letter
//                     upper case and the rest lower case) creates ::NS.
//   TCL_OUTPUT        M.tcl: loads LIB[info sharedlibextension] from its own directory and
//                     provides the package; then, inside `if {0} {...}` (never run, so the native
//                     commands stay in place), every command, class and method as a stub
//                     (`proc`, `oo::class create` with `constructor`/`method`, `oo::objdefine ...
//                     method` for static methods) right after a comment block in the conventional
//                     Tcl documentation style (`# name -- summary`, `args:`, `result:`, `errors:`).
//   PKGINDEX          pkgIndex.tcl: `package ifneeded M VERSION [list source $dir/M.tcl]`.
//
// Tcl conventions: names are the C++ names (namespaces flattened by the policy, or joined with
// __ when qualified); commands live in ::NS and policy modules are child namespaces. Classes are
// TclOO classes deriving from ::NS::NativeObject (C++ multiple inheritance maps to TclOO multiple
// superclasses): `set c [NS::Circle new 2]`, `$c area`, `$c destroy` (or `rename $c {}`). Fields
// are `$obj name ?value?` plus `cget -name`/`configure -name value`. Static methods are methods of
// the class object (`NS::Circle unit`). Tcl subclasses of polymorphic classes override virtual
// methods (directors), and `next` runs the C++ implementation. Enums are their enumerator names
// (arrays NS::Enum map names to values); constants are read-only namespace variables. Names that
// would replace a TclOO method get a trailing underscore.
#include "rosetta/tcl.hpp"
#include "tcl_full_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
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
// --- Names -------------------------------------------------------------------------------------
bool lower(char c) { return c >= 'a' && c <= 'z'; }
bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool word(char c) { return lower(c) || upper(c) || digit(c) || c == '_'; }
bool tcl_name(const std::string& text) {
    return !text.empty() && !digit(text[0]) && std::all_of(text.begin(), text.end(), word);
}
// Methods every TclOO object has (or that the runtime adds) and a native method must not replace.
const std::set<std::string>& reserved_methods() {
    static const std::set<std::string> names = {"destroy", "eval", "variable", "varname", "unknown", "cget", "configure",
        "alive", "owned", "<cloned>"};
    return names;
}
// Methods of class objects.
const std::set<std::string>& reserved_static() {
    static const std::set<std::string> names = {"new", "create", "createWithNamespace", "destroy", "eval", "variable",
        "varname", "unknown", "<cloned>"};
    return names;
}
std::string method_safe(const std::string& name) { return reserved_methods().count(name) ? name + "_" : name; }
std::string static_safe(const std::string& name) { return reserved_static().count(name) ? name + "_" : name; }
std::string class_safe(const std::string& name) { return name == "NativeObject" ? name + "_" : name; }
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
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
bool is_ostream(const Type& type) {
    const auto& target = referent(type);
    return type.kind == TypeKind::lvalue_reference && !target.is_const && normalize(target.declaration) == "std::basic_ostream";
}
// A mutable reference to a converted value: the Tcl argument is a variable name.
bool variable_parameter(const Type& type, bool bound) {
    return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound;
}

// Tcl method name for a C++ operator by arity (member operators: arity excludes this).
std::string operator_name(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"pos", "add"}}, {"operator-", {"neg", "sub"}}, {"operator*", {nullptr, "mul"}}, {"operator/", {nullptr, "div"}},
        {"operator%", {nullptr, "mod"}}, {"operator==", {nullptr, "eq"}}, {"operator!=", {nullptr, "ne"}}, {"operator<", {nullptr, "lt"}},
        {"operator<=", {nullptr, "le"}}, {"operator>", {nullptr, "gt"}}, {"operator>=", {nullptr, "ge"}}, {"operator&", {nullptr, "bitand"}},
        {"operator|", {nullptr, "bitor"}}, {"operator^", {nullptr, "bitxor"}}, {"operator<<", {nullptr, "lshift"}},
        {"operator>>", {nullptr, "rshift"}}, {"operator~", {"bitnot", nullptr}}, {"operator!", {"not", nullptr}},
        {"operator[]", {nullptr, "index"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// The -errorcode kind for a C++ exception type, for documentation.
std::string error_kind(std::string name) {
    name = normalize(name);
    if (name.rfind("std::", 0) == 0) name.erase(0, 5);
    static const std::set<std::string> known = {"invalid_argument", "domain_error", "length_error", "out_of_range", "logic_error",
        "overflow_error", "underflow_error", "range_error", "runtime_error", "bad_alloc", "bad_cast", "bad_optional_access",
        "bad_variant_access", "bad_function_call"};
    if (known.count(name)) return name;
    if (name == "filesystem::filesystem_error") return "filesystem_error";
    if (name == "ios_base::failure") return "io_error";
    return "exception";
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
std::string one_line(const std::string& text) {
    std::string out;
    bool space = false;
    for (const char c : trim(text)) {
        if (std::isspace(static_cast<unsigned char>(c))) { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
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
std::string section_kind(std::string title) {
    while (!title.empty() && (title.back() == ':' || title.back() == ' ')) title.pop_back();
    std::string lower_title;
    for (const char c : title) lower_title += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower_title == "example" || lower_title == "examples" || lower_title == "usage") return "example";
    if (lower_title == "note" || lower_title == "notes") return "note";
    if (lower_title == "see also") return "see";
    return {};
}
std::string block_text(const DocBlock& block) {
    if (block.kind == "code") return indent_lines(block.text, "    ");
    if (block.kind == "math") return indent_lines(block.text, "    ");
    return block.text;
}
// Braces in a comment still count when Tcl matches the braces of the `if {0} {...}` body, and a
// trailing backslash would continue the comment: a line with unbalanced braces gets each brace
// escaped, and a trailing backslash a space.
std::string safe_comment_line(std::string line) {
    int depth = 0;
    bool balanced = true;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') { ++i; continue; }
        if (line[i] == '{') ++depth;
        else if (line[i] == '}' && --depth < 0) balanced = false;
    }
    if (!balanced || depth != 0) {
        std::string out;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) { out += line[i]; out += line[++i]; continue; }
            if (line[i] == '{' || line[i] == '}') out += '\\';
            out += line[i];
        }
        line = out;
    }
    if (!line.empty() && line.back() == '\\') line += ' ';
    return line;
}
std::string comment(const std::string& text, const std::string& indent) {
    std::string out;
    for (const auto& line : lines_of(text)) {
        auto spelled = line;
        while (!spelled.empty() && (spelled.back() == ' ' || spelled.back() == '\t')) spelled.pop_back();
        out += indent + (spelled.empty() ? "#" : "# " + safe_comment_line(spelled)) + "\n";
    }
    return out;
}
// A Tcl word for any text: bare when safe, braced when possible, backslashed otherwise.
std::string tcl_word(const std::string& text) {
    if (text.empty()) return "{}";
    bool bare = true;
    for (const char c : text)
        if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '[' || c == ']' || c == '$' || c == '"' ||
            c == '\\' || c == ';' || c == '#') bare = false;
    if (bare) return text;
    int depth = 0;
    bool braceable = text.back() != '\\';
    for (std::size_t i = 0; i < text.size() && braceable; ++i) {
        if (text[i] == '\\') { ++i; continue; }
        if (text[i] == '{') ++depth;
        else if (text[i] == '}' && --depth < 0) braceable = false;
    }
    if (braceable && depth == 0) return "{" + text + "}";
    std::string out;
    for (const char c : text) {
        if (c == '\n') { out += "\\n"; continue; }
        if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '[' || c == ']' || c == '$' || c == '"' ||
            c == '\\' || c == ';' || c == '#') out += '\\';
        out += c;
    }
    return out;
}
// The text of a C++ string literal ("..." with escapes).
std::string unescape_literal(const std::string& literal) {
    std::string body = literal;
    if (body.size() >= 2 && body.front() == '"' && body.back() == '"') body = body.substr(1, body.size() - 2);
    std::string out;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\' || i + 1 >= body.size()) { out += body[i]; continue; }
        const char c = body[++i];
        if (c == 'n') out += '\n';
        else if (c == 't') out += '\t';
        else if (c == 'r') out += '\r';
        else if (c == '0') out += '\0';
        else out += c;
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
    std::string name;        // Tcl command or method name
    std::vector<Overload> overloads;
    bool is_static = false;
    std::string module;      // policy submodule of a function ("" = top level)
    std::string symbol;      // C++ wrapper
};
struct FieldPlan {
    const Field* field;
    std::string tcl;         // field name (cget/configure -tcl)
    std::string method;      // accessor method name ("" when it would clash)
    std::string symbol;
    bool writable = false;
};
struct ClassPlan {
    const Record* record;
    std::string tcl;         // class name
    std::string qualname;    // qualified Tcl command name (::ns::Outer::Name)
    std::string relative;    // below the module namespace (Outer::Name)
    std::string symbol;      // C++ identifier prefix
    std::vector<std::string> bases; // exported ancestor classes (qualified C++ names)
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<FieldPlan> fields;
    bool director = false;
    std::string parent;      // qualified outer class when nested and exported
    std::string stream;      // operator<< target for `str`
    std::string constructor_symbol;
};
struct EnumPlan {
    const Enumeration* enumeration;
    std::string tcl;         // array name
    std::string qualname;    // qualified variable name
    std::string relative;
    std::vector<std::string> members;
    bool flags = false;
};
struct ConstantPlan {
    std::string tcl, qualname, owner, expression;
    const Documentation* documentation;
    Type type;
};

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const TclFullOptions& options)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"),
          options_(options) { validate_modules(policy_, module_); }

    GeneratedTclFullSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    TclFullOptions options_;
    std::string ns_;            // ::features
    std::string code_;          // FEATURES
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
    std::vector<std::pair<std::string, std::string>> aliases_; // qualified alias, qualified target command
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
            if (!tcl_name(*name)) throw BindingError("Tcl rename target is not a simple name: " + *name);
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
    std::string class_tcl_name(const Record& record) const;
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
    std::string submodule_path(const std::string& module) const {
        std::string out = module;
        for (std::size_t i = 0; (i = out.find('.', i)) != std::string::npos;) out.replace(i, 1, "::");
        return out;
    }
    std::string command_path(const Group& group) const {
        return ns_ + (group.module.empty() ? "" : "::" + submodule_path(group.module)) + "::" + group.name;
    }
    void plan_classes();
    void plan_directors();
    void plan_members();
    void plan_functions();
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent, bool single);
    void emit_group(std::ostream& out, Group& group, const std::string& kind);
    std::pair<std::string, std::string> exception_handlers(const std::string& qualified) const;
    void emit_constructor(std::ostream& out, ClassPlan& plan);
    void emit_director(std::ostream& out, const ClassPlan& plan);
    std::string signature(const Overload& overload, const std::string& name) const;
    // Documentation
    std::string type_text(const Type& type) const;
    std::string tcl_default(const DefaultValue& value, const Type& type) const;
    std::string parameter_name(const Function& function, std::size_t index) const;
    std::string docs(const std::vector<Overload>& overloads, const std::string& name, const std::string& usage_name,
                     const Documentation* own, const std::string& fallback, const std::string& extra) const;
    std::string stub_parameters(const Overload& overload) const;
    std::string stub_parameters(const std::vector<Overload>& overloads) const;
    std::string package_file() const;
    std::string pkg_index() const;
    void doc_class(std::ostream& out, const ClassPlan& plan, const std::string& indent) const;
    void doc_enum(std::ostream& out, const EnumPlan& plan, const std::string& indent) const;
    void doc_group(std::ostream& out, const Group& group, const std::string& indent, const std::string& keyword,
                   const std::string& usage_prefix) const;
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
        return "unsupported Tcl value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to Tcl";
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
        return "unsupported Tcl template type " + type.canonical;
    }
    default:
        return "unsupported Tcl value type " + type.canonical;
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
        return "unsupported Tcl pointer type " + type.canonical;
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
        if (!why.empty()) return "unsupported Tcl result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported Tcl parameter type: " + why;
    }
    return {};
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_tcl::Return::copy" : "rosetta_tcl::Return::automatic";
    return "rosetta_tcl::Return::" + found->second;
}

std::string Generator::class_tcl_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, class_safe(instantiated->second));
    if (!record.owner.empty()) return renamed(qualified, class_safe(record.name));
    return renamed(qualified, class_safe(flat(record.namespaces) ? record.name : exported(qualified)));
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
            throw BindingError("unknown class in Tcl policy: " + selection.first);
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
        if (!record.complete) { if (listed) reject(owner, "Tcl requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed Tcl results are not yet supported"); continue;
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
    for (auto& [owner, plan] : classes_) {
        plan.tcl = class_tcl_name(*plan.record);
        plan.parent = plan.record->owner;
    }
    for (auto& [owner, plan] : classes_) {
        std::string relative = plan.tcl;
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            relative = classes_.at(parent).tcl + "::" + relative;
        plan.relative = relative;
        plan.qualname = ns_ + "::" + relative;
    }
    for (auto& [owner, plan] : classes_) {
        std::vector<std::string> found;
        const auto visit = [&](const auto& self, const Record& record, unsigned depth) -> void {
            if (depth > 64) return;
            for (const auto& base : record.bases) {
                if (base.access != "public") continue;
                const auto name = base.type.declaration.empty() ? base.type.canonical : base.type.declaration;
                if (classes_.count(name)) { if (std::find(found.begin(), found.end(), name) == found.end()) found.push_back(name); continue; }
                const auto record_found = records_.find(name);
                if (record_found != records_.end()) self(self, *record_found->second, depth + 1);
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
    const std::function<void(const std::string&, unsigned)> place = [&](const std::string& owner, unsigned depth) {
        if (placed.count(owner) || depth > 256) return;
        const auto& plan = classes_.at(owner);
        for (const auto& base : plan.bases) place(base, depth + 1);
        if (!plan.parent.empty()) place(plan.parent, depth + 1);
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

std::string Generator::parameter_name(const Function& function, std::size_t index) const {
    const auto written = index < function.parameter_names.size() ? function.parameter_names[index] : std::string();
    if (written.empty()) return "arg" + std::to_string(index);
    return written == "args" ? "args_" : written; // `args` is special in a proc parameter list
}

std::string Generator::signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    std::string result = name;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto item = parameter_name(function, i) + ":" + type_text(function.parameters[i]);
        result += " " + (i >= required ? "?" + item + "?" : item);
    }
    if (!function.is_constructor && !is_void(function.result)) result += " -> " + type_text(function.result);
    return result;
}

// A single overload checks only the arity: converting its arguments reports the precise error.
void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent,
                              bool single) {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - first;
    const std::size_t required = function.parameters.size() - function.default_arguments - first;
    out << indent << "if (n >= " << required << " && n <= " << count << " && call.match()";
    if (overload.receives_self && !single) out << " && rosetta_tcl::Param<" << function.parameters[0].canonical << ">::check(self_word, level)";
    for (std::size_t i = 0; i < count && !single; ++i)
        out << "\n" << indent << "    && (n <= " << i << " || rosetta_tcl::Param<" << function.parameters[i + first].canonical
            << ">::check(a[" << i << "], level))";
    out << ") {\n";
    const auto owner = overload.owner;
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "    if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) {
            out << indent << "        rosetta_tcl::Param<" << function.parameters[0].canonical << "> self_param(self_word);\n";
            arguments = "self_param.get()";
        }
        for (std::size_t i = 0; i < arity; ++i) {
            out << indent << "        rosetta_tcl::Param<" << function.parameters[i + first].canonical << "> p" << i << "(a[" << i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (kind == "method") out << indent << "        auto* native = rosetta_tcl::native<" << owner << ">(call.self);\n";
        std::string writebacks;
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& type = function.parameters[i + first];
            if (variable_parameter(type, bound(referent(type))))
                writebacks += indent + "        p" + std::to_string(i) + ".writeback(a[" + std::to_string(i) + "]);\n";
        }
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            out << indent << "        std::unique_ptr<" << owner << "> created;\n"
                << indent << "        rosetta_tcl::Director* director_part = nullptr;\n";
            if (plan.director)
                out << indent << "        if (director) { auto* made = new " << plan.symbol << "_director(0" << (arguments.empty() ? "" : ", ")
                    << arguments << "); director_part = made; created.reset(made); }\n"
                    << indent << "        else ";
            else out << indent << "        ";
            if (plan.record->abstract)
                out << "rosetta_tcl::fail(" << quote(plan.qualname + " is abstract: subclass it in Tcl and implement its pure virtual methods") << ");\n";
            else out << "created.reset(new " << owner << "(" << arguments << "));\n";
            out << writebacks;
            out << indent << "        rosetta_tcl::adopt<" << owner << ">(call, created.get(), director_part);\n"
                << indent << "        created.release();\n"
                << indent << "        return nullptr;\n";
        } else {
            std::string body;
            if (kind == "method") {
                const auto direct = "native->" + function.name + "(" + arguments + ")";
                if (function.is_virtual && policy_.directors != "none") {
                    if (function.is_pure)
                        body = "if (rosetta_tcl::is_director(call.self)) rosetta_tcl::pure_virtual(" + quote(owner + "::" + function.name) + "); return " + direct + ";";
                    else
                        body = "if (rosetta_tcl::is_director(call.self)) return native->" + owner + "::" + function.name + "(" + arguments + "); return " + direct + ";";
                } else body = "return " + direct + ";";
            } else {
                body = "return ::" + function.qualified_name() + "(" + arguments + ");";
            }
            out << indent << "        Tcl_Obj* result = rosetta_tcl::call([&]() -> decltype(auto) { " << body << " }, "
                << (parent ? "call.self" : "nullptr") << ", " << policy << ");\n";
            out << writebacks;
            out << indent << "        return result;\n";
        }
        out << indent << "    }\n";
    }
    out << indent << "}\n";
}

std::pair<std::string, std::string> Generator::exception_handlers(const std::string& qualified) const {
    for (const auto& [pattern, map] : policy_.exceptions) {
        if (!policy_match(pattern, qualified)) continue;
        std::string catches;
        for (const auto& [cpp, kind] : map)
            catches += "    } catch (const " + cpp + "& error) { rosetta_tcl::fail_as(" + quote(kind) +
                ", rosetta_tcl::demangle(typeid(error).name()), error.what());\n";
        return {"    try {\n", catches + "    }\n"};
    }
    return {};
}

void Generator::emit_group(std::ostream& out, Group& group, const std::string& kind) {
    group.symbol = symbol("rosetta_function_");
    const bool single = group.overloads.size() == 1;
    const auto handlers = exception_handlers(group.overloads.front().function->qualified_name());
    out << "Tcl_Obj* " << group.symbol << "(rosetta_tcl::Call& call) {\n"
        << "    const rosetta_tcl::Size n = call.n; Tcl_Obj* const* a = call.a; (void)n; (void)a;\n";
    if (std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) { return o.receives_self; }))
        out << "    Tcl_Obj* self_word = call.object ? Tcl_GetObjectName(rosetta_tcl::interp(), call.object) : rosetta_tcl::empty();\n";
    out << handlers.first;
    out << "    for (int level = " << (single ? 2 : 0) << "; level < 3; ++level) {\n";
    for (const auto& overload : group.overloads)
        emit_overload(out, overload, overload.extension ? "function" : kind, "        ", single && group.name != "eq");
    out << "    }\n" << handlers.second;
    if (group.name == "eq") out << "    if (call.arity_matched) return Tcl_NewBooleanObj(0);\n";
    std::string signatures;
    for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
    out << "    rosetta_tcl::no_overload(call, " << quote(signatures) << ");\n}\n";
}

void Generator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor_symbol = plan.symbol + "_construct";
    const auto owner = plan.record->qualified_name();
    const bool single = plan.constructors.size() == 1;
    const auto handlers = exception_handlers(owner + "::" + plan.record->name);
    out << "Tcl_Obj* " << plan.constructor_symbol << "(rosetta_tcl::Call& call) {\n"
        << "    const rosetta_tcl::Size n = call.n; Tcl_Obj* const* a = call.a; (void)n; (void)a;\n"
        << "    rosetta_tcl::check_unconstructed(call);\n";
    if (plan.director) out << "    const bool director = rosetta_tcl::constructing_subclass<" << owner << ">(call);\n";
    out << handlers.first;
    out << "    for (int level = " << (single ? 2 : 0) << "; level < 3; ++level) {\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", "        ", single);
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, plan.qualname + " new") + "\n";
    out << "    }\n" << handlers.second << "    rosetta_tcl::no_overload(call, " << quote(signatures) << ");\n}\n";
}

void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << plan.symbol << "_director final : " << owner << ", rosetta_tcl::Director {\n"
        << "    template<class... A> explicit " << plan.symbol << "_director(int, A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    // Tcl names of the bound methods (a renamed or reserved name differs from the C++ one).
    std::map<std::string, std::string> names;
    for (const auto& [qualified, class_plan] : classes_)
        for (const auto& group : class_plan.methods)
            for (const auto& overload : group.overloads)
                if (!overload.extension) names[overload.function->name] = group.name;
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
        const auto found = names.find(method->name);
        std::string tcl = found != names.end() ? found->second : method->name;
        if (method->name.rfind("operator", 0) == 0) {
            const auto spelled = operator_name(method->name, method->parameters.size());
            if (spelled.empty()) continue;
            tcl = spelled;
        }
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
        out << "    " << result << " " << method->name << "(" << parameters << ")" << (method->is_const ? " const" : "")
            << (method->is_noexcept ? " noexcept" : "") << " override {\n";
        if (method->is_noexcept) out << "        try {\n";
        out << "        if (rosetta_tcl::dispatchable(this, " << quote(tcl) << "))\n"
            << "            return rosetta_tcl::invoke_method<" << result << types << ">(this, " << quote(tcl) << arguments << ");\n";
        if (is_pure) out << "        rosetta_tcl::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (...) { rosetta_tcl::unraisable(" << quote(owner + "::" + method->name) << "); }\n";
        out << "    }\n";
    }
    out << "};\n";
}

void Generator::plan_members() {
    // Free functions attached to classes as methods (policy extend), and free operators.
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
            group.name = renamed(owner + "::" + name, name);
            if (!tcl_name(group.name)) throw BindingError("policy extend name is not a Tcl name: " + name);
            bool any = false, any_static = false;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const bool self = receives(function, owner);
                auto why = function_problem(function, self);
                if (!why.empty()) { reject(owner + "." + name, why); continue; }
                if (!self) any_static = true;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            group.name = any_static ? static_safe(group.name) : method_safe(group.name);
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    // Free operators whose first parameter is an exported class become its methods; a stream
    // insertion operator<<(std::ostream&, const T&) becomes T's `str` method.
    for (const auto& function : module_.functions) {
        if (function.name.rfind("operator", 0) != 0 || (function.name.size() > 8 && word(function.name[8]))) continue;
        if (extension_functions_.count(function.qualified_name()) || excluded(function.qualified_name(), function.identity())) continue;
        extension_functions_.insert(function.qualified_name());
        if (function.name == "operator<<" && function.parameters.size() == 2 && is_ostream(function.parameters[0])) {
            const auto& target = referent(function.parameters[1]);
            if (bound(target) && classes_.at(target.declaration).stream.empty()) classes_.at(target.declaration).stream = target.declaration;
            continue;
        }
        const auto& first = function.parameters.empty() ? Type{} : referent(function.parameters[0]);
        const auto tcl = function.parameters.empty() ? std::string() : operator_name(function.name, function.parameters.size() - 1);
        if (!bound(first) || tcl.empty()) { reject(function.qualified_name(), "free operator without an exported class operand"); continue; }
        auto why = function_problem(function, true);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        auto& groups = extensions[first.declaration];
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.name == tcl; });
        if (found == groups.end()) { groups.push_back({tcl, {}, false, {}, {}}); found = groups.end() - 1; }
        found->overloads.push_back({&function, first.declaration, true, true});
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
                if (found == record.methods.end()) throw BindingError("unknown Tcl policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Tcl policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown Tcl policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Tcl policy field is not public: " + owner + "::" + requested);
            }
        }
        if (constructors && record.public_destructor) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || (constructor.access != "public" && !(plan.director && constructor.access == "protected"))) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                // Copy constructors duplicate oo::copy for Tcl callers.
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::lvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner && record.constructors.size() > 1) continue;
                auto why = function_problem(constructor, false);
                if (!why.empty()) { reject(owner + "::" + record.name, why); continue; }
                plan.constructors.push_back({&constructor, owner});
            }
        }
        std::map<std::string, std::size_t> group_index;
        const auto add = [&](const std::string& tcl, const Overload& overload, bool is_static) {
            const auto found = group_index.find(tcl);
            if (found == group_index.end()) {
                group_index[tcl] = plan.methods.size();
                plan.methods.push_back({tcl, {overload}, is_static, {}, {}});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a Tcl name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            std::string tcl;
            if (method.name.rfind("operator", 0) == 0 && (method.name.size() == 8 || !word(method.name[8]))) {
                tcl = operator_name(method.name, method.parameters.size());
                if (tcl.empty()) continue; // assignment, compound, conversion and other operators stay C++-only
                if (tcl == "index" && method.parameters.size() != 1) continue;
                tcl = renamed(method.qualified_name(), tcl);
            } else if (method.name.rfind("operator", 0) == 0 && method.name.size() > 8 && method.name[8] == ' ') {
                continue; // conversion operators
            } else {
                tcl = renamed(method.qualified_name(), method.name);
            }
            tcl = method.is_static ? static_safe(tcl) : method_safe(tcl);
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(tcl, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner])
            for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        std::set<std::string> members;
        for (const auto& group : plan.methods) if (!group.is_static) members.insert(group.name);
        if (!plan.stream.empty() && !members.count("str")) members.insert("str");
        else plan.stream.clear();
        std::set<std::string> fields;
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported Tcl field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported Tcl field type", 0) == 0 ? why : "unsupported Tcl field type: " + why); continue; }
            const auto tcl = renamed(qualified, field.name);
            if (!fields.insert(tcl).second) throw BindingError("duplicate Tcl field: " + plan.qualname + " -" + tcl);
            FieldPlan field_plan{&field, tcl, {}, symbol("rosetta_field_"), false};
            // The accessor method yields to a method of the same name and to TclOO's own methods.
            if (!members.count(tcl) && !reserved_methods().count(tcl)) { field_plan.method = tcl; members.insert(tcl); }
            field_plan.writable = !field.type.is_const && !readonly(qualified, owner) && field.type.kind != TypeKind::lvalue_reference;
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
        std::string tcl;
        if (specialization != function_template_names_.end()) tcl = renamed(function.qualified_name(), specialization->second);
        else tcl = renamed(function.qualified_name(), !submodule.empty() || flat(function.namespaces) ? function.name : exported(function.qualified_name()));
        if (tcl.find('#') != std::string::npos || tcl == "NativeObject") tcl += "_";
        const auto key = submodule + "." + tcl;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions_.size();
            functions_.push_back({tcl, {{&function, {}}}, false, submodule, {}});
        } else functions_[found->second].overloads.push_back({&function, {}});
    }
}

// --- Documentation ----------------------------------------------------------------------------
std::string Generator::type_text(const Type& type) const {
    const auto& target = referent(type);
    if (variable_parameter(type, bound(target))) return "varName of " + type_text(target);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "string";
        return type_text(pointee) + " or {}";
    }
    if (is_void(target)) return "nothing";
    if (!conversion(target).empty()) {
        const auto& converted = policy_.conversions.at(conversion(target));
        const auto open = converted.find('<');
        const auto element = [&](const std::string& text) {
            static const std::map<std::string, std::string> scalars = {{"double", "double"}, {"float", "double"}, {"int", "int"},
                {"long", "int"}, {"bool", "bool"}, {"std::string", "string"}, {"std::size_t", "int"}, {"size_t", "int"}};
            const auto found = scalars.find(trim(text));
            return found == scalars.end() ? std::string("value") : found->second;
        };
        if (open != std::string::npos) {
            const auto stem = normalize(converted.substr(0, open));
            const auto inner = trim(converted.substr(open + 1, converted.rfind('>') - open - 1));
            if (stem == "std::vector" || stem == "std::deque" || stem == "std::list" || stem == "std::set" || stem == "std::unordered_set")
                return "list of " + element(inner);
        }
        return element(normalize(converted));
    }
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "bool";
        if (bare == "char") return "char";
        if (bare == "float" || bare == "double" || bare == "long double") return "double";
        return bare.rfind("unsigned", 0) == 0 ? "unsigned int" : "int";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_plans_.find(target.declaration);
        if (found != enum_plans_.end()) return found->second.relative + (found->second.flags ? " flags" : "");
        return "int";
    }
    if (bound(target)) return classes_.at(target.declaration).relative;
    if (bare == "std::filesystem::path") return "path";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "{}";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? type_text(target.arguments[i]) : std::string("value"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set")
            return "list of " + arg(0);
        if (declaration == "std::map" || declaration == "std::unordered_map") return "dict of " + arg(0) + " -> " + arg(1);
        if (declaration == "std::optional") return arg(0) + " or {}";
        if (declaration == "std::complex") return "complex {re im}";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0) + " or {}";
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "list {";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " " : "") + type_text(target.arguments[i]);
            return result + "}";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " or " : "") + type_text(target.arguments[i]);
            return result;
        }
        if (declaration == "std::function") {
            std::string result = "command prefix";
            if (!target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
                const auto& signature = target.arguments[0];
                result += " (called with";
                if (signature.arguments.size() <= 1) result += " no arguments";
                for (std::size_t i = 1; i < signature.arguments.size(); ++i) result += " " + type_text(signature.arguments[i]);
                if (!signature.arguments.empty() && !is_void(signature.arguments[0])) result += ", returns " + type_text(signature.arguments[0]);
                result += ")";
            }
            return result;
        }
        if (declaration == "std::chrono::duration") return "seconds";
    }
    return "value";
}

// A default value as a Tcl word's text, or "" when Tcl cannot spell it.
std::string Generator::tcl_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") {
        std::string text = value.value;
        while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        return text;
    }
    if (value.kind == "float") {
        std::string text = value.value;
        if (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        if (!text.empty() && text.back() == '.') text += "0";
        if (!text.empty() && text[0] == '.') text = "0" + text;
        return text;
    }
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "{}";
    if (value.kind == "string") return tcl_word(unescape_literal(value.value));
    if (value.kind == "char") return tcl_word(value.value);
    if (value.kind == "enum") {
        const auto found = enum_plans_.find(referent(type).declaration);
        if (found != enum_plans_.end() && !found->second.flags) return value.value;
        for (const auto& enumeration : module_.enumerations) {
            if (enumeration.qualified_name() != referent(type).declaration) continue;
            for (std::size_t i = 0; i < enumeration.enumerators.size() && i < enumeration.enumerator_values.size(); ++i)
                if (enumeration.enumerators[i] == value.value) return enumeration.enumerator_values[i];
        }
    }
    return {};
}

std::string Generator::stub_parameters(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::string result;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto name = parameter_name(function, i);
        if (i >= required) {
            const auto value = tcl_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            result += (i > first ? " " : "") + std::string("{") + name + " " + (value.empty() ? std::string("{}") : value) + "}";
        } else result += (i > first ? " " : "") + name;
    }
    return "{" + result + "}";
}
std::string Generator::stub_parameters(const std::vector<Overload>& overloads) const {
    return overloads.size() == 1 ? stub_parameters(overloads.front()) : std::string("args");
}

// The comment block of a command or method: `name -- summary`, the description, then args,
// result, errors, notes and examples.
std::string Generator::docs(const std::vector<Overload>& overloads, const std::string& name, const std::string& usage_name,
                            const Documentation* own, const std::string& fallback, const std::string& extra) const {
    std::vector<std::string> paragraphs;
    std::vector<std::string> notes, examples, sees;
    std::string summary;
    const auto paragraph = [&](const std::string& text) { if (!trim(text).empty()) paragraphs.push_back(trim(text)); };
    std::string override_text;
    for (const auto& overload : overloads) {
        const auto found = policy_.docs.find(overload.function->qualified_name());
        if (found != policy_.docs.end()) { override_text = found->second; break; }
    }
    std::vector<const Documentation*> documents;
    std::set<std::string> seen;
    if (own && !own->empty()) documents.push_back(own);
    if (override_text.empty())
        for (const auto& overload : overloads) {
            const auto& documentation = overload.function->documentation;
            if (!documentation.empty() && seen.insert(plain_documentation(documentation)).second) documents.push_back(&documentation);
        }
    else summary = one_line(override_text);
    for (const auto* documentation : documents) {
        if (summary.empty()) summary = one_line(documentation->brief);
        else paragraph(documentation->brief);
        std::string section, section_title;
        for (const auto& block : documentation->details) {
            if (block.kind == "heading") { section = section_kind(block.text); section_title = trim(block.text); continue; }
            if (section == "example") {
                if (block.kind == "code") examples.push_back(block.text);
                else paragraph(block_text(block));
            } else if (section == "note") notes.push_back("note: " + one_line(block_text(block)));
            else if (section == "see") sees.push_back(one_line(block.text));
            else if (!section_title.empty()) paragraph(section_title + ":\n" + block_text(block));
            else paragraph(block_text(block));
            if (section.empty()) section_title.clear();
        }
        for (const auto& note : documentation->notes) {
            std::string label = note.name.empty() || note.name == "remark" ? std::string("note") : note.name;
            notes.push_back(label + ": " + one_line(note.text));
        }
        for (const auto& target : documentation->see) sees.push_back(target);
    }
    if (summary.empty()) summary = fallback;
    if (!extra.empty()) paragraph(extra);
    std::string out = name + " -- " + summary + "\n";
    for (const auto& text : paragraphs) out += "\n" + indent_lines(text, "  ") + "\n";
    std::string tags;
    if (overloads.size() == 1) {
        const auto& overload = overloads.front();
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        tags += "  usage: " + usage_name;
        for (std::size_t i = first; i < function.parameters.size(); ++i)
            tags += " " + (i >= required ? "?" + parameter_name(function, i) + "?" : parameter_name(function, i));
        tags += "\n";
        if (function.parameters.size() > first) {
            tags += "  args:\n";
            for (std::size_t i = first; i < function.parameters.size(); ++i) {
                const auto cpp_name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
                std::string text;
                for (const auto& item : function.documentation.params) if (item.name == cpp_name) text = one_line(item.text);
                std::string kind = type_text(function.parameters[i]);
                if (i >= required) {
                    const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
                    const auto shown = tcl_default(value, function.parameters[i]);
                    kind += shown.empty() ? ", optional (C++ default " + (value.value.empty() ? std::string("value") : value.value) + ")"
                                          : ", default " + shown;
                }
                const auto& target = referent(function.parameters[i]);
                if (target.kind == TypeKind::enumeration) {
                    const auto found = enum_plans_.find(target.declaration);
                    if (found != enum_plans_.end() && !found->second.flags) {
                        std::string values;
                        for (const auto& member : found->second.members) values += (values.empty() ? "" : ", ") + member;
                        kind += "; one of " + values;
                    }
                }
                tags += "    " + parameter_name(function, i) + " - " + kind + (text.empty() ? "" : ": " + text) + "\n";
            }
        }
        if (!function.is_constructor) {
            std::string text = one_line(function.documentation.returns);
            for (const auto& value : function.documentation.retvals)
                text += (text.empty() ? "" : "; ") + value.name + (value.text.empty() ? "" : ": " + one_line(value.text));
            if (!is_void(function.result) || !text.empty())
                tags += "  result: " + type_text(function.result) + (text.empty() ? "" : " - " + text) + "\n";
        }
    } else {
        tags += "  usage:\n";
        for (const auto& overload : overloads) {
            const auto line = signature(overload, usage_name);
            tags += "    " + line + "\n";
        }
    }
    std::set<std::string> raised;
    std::string errors;
    for (const auto& overload : overloads)
        for (const auto& item : overload.function->documentation.throws) {
            const auto line = "{" + code_ + " " + error_kind(item.name) + "}" + (item.text.empty() ? "" : " " + one_line(item.text));
            if (raised.insert(line).second) errors += "    " + line + "\n";
        }
    if (!errors.empty()) tags += "  errors:\n" + errors;
    for (const auto& note : notes) tags += "  " + note + "\n";
    if (!sees.empty()) {
        std::string list;
        for (const auto& item : sees) list += (list.empty() ? "" : ", ") + item;
        tags += "  see: " + list + "\n";
    }
    for (const auto& example : examples) tags += "  example:\n" + indent_lines(example, "    ") + "\n";
    out += "\n" + tags;
    return out;
}

void Generator::doc_group(std::ostream& out, const Group& group, const std::string& indent, const std::string& keyword,
                          const std::string& usage_prefix) const {
    const auto& function = *group.overloads.front().function;
    const auto fallback = std::string(function.is_constructor ? "Creates a native object" : "Calls") + " C++ " +
        (function.is_constructor ? function.qualified_name() : function.qualified_name()) + ".";
    out << comment(docs(group.overloads, group.name, usage_prefix + group.name, nullptr, fallback, ""), indent);
    out << indent << keyword << " " << tcl_word(group.name) << " " << stub_parameters(group.overloads) << " {}\n";
}

void Generator::doc_enum(std::ostream& out, const EnumPlan& plan, const std::string& indent) const {
    const auto& enumeration = *plan.enumeration;
    std::string summary = one_line(enumeration.documentation.brief);
    if (summary.empty()) summary = "The C++ enumeration " + enumeration.qualified_name() + ".";
    std::string values, pairs;
    for (std::size_t i = 0; i < plan.members.size(); ++i) {
        const auto value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string("0");
        values += (values.empty() ? "" : ", ") + plan.members[i] + " (" + value + ")";
        pairs += (pairs.empty() ? "" : " ") + tcl_word(plan.members[i]) + " " + value;
    }
    std::string text = plan.tcl + " -- " + summary + "\n\n";
    text += plan.flags ? "  Flags: values are integers (combine with |); arguments also take a list of names.\n"
                       : "  Values are the enumerator names; arguments also take the integer value.\n";
    text += "  Enumerators: " + values + ".\n  The array " + plan.qualname.substr(2) + " maps each name to its value.\n";
    out << comment(text, indent) << indent << "variable " << plan.tcl << "\n"
        << indent << "array set " << plan.tcl << " {" << pairs << "}\n";
}

void Generator::doc_class(std::ostream& out, const ClassPlan& plan, const std::string& indent) const {
    const auto& record = *plan.record;
    const auto found = policy_.docs.find(record.qualified_name());
    std::string summary, body;
    if (found != policy_.docs.end()) summary = one_line(found->second);
    else {
        summary = one_line(record.documentation.brief);
        for (const auto& block : record.documentation.details)
            if (block.kind != "heading") body += (body.empty() ? "" : "\n\n") + block_text(block);
        for (const auto& note : record.documentation.notes) body += (body.empty() ? "" : "\n\n") + note.name + ": " + one_line(note.text);
    }
    if (summary.empty()) summary = "The C++ class " + record.qualified_name() + ".";
    std::string text = plan.tcl + " -- " + summary + "\n";
    if (!body.empty()) text += "\n" + indent_lines(body, "  ") + "\n";
    text += "\n";
    if (!plan.constructors.empty() && record.abstract)
        text += "  Abstract: `" + plan.qualname.substr(2) + " new` works only through a Tcl subclass that implements\n"
                "  the pure virtual methods.\n";
    else if (!plan.constructors.empty())
        text += "  Create with `set obj [" + plan.qualname.substr(2) + " new ...]`; `$obj destroy` (or `rename $obj {}`)\n"
                "  destroys the C++ object.\n";
    else text += "  Instances come from C++ (" + plan.qualname.substr(2) + " cannot be created from Tcl).\n";
    if (plan.director) {
        std::string virtual_names;
        std::map<std::string, bool> pure;
        for (const auto* method : virtuals(record, pure))
            if (virtual_names.find(" " + method->name + ",") == std::string::npos && (" " + virtual_names).find(" " + method->name + ",") == std::string::npos)
                virtual_names += (virtual_names.empty() ? "" : " ") + method->name + ",";
        if (!virtual_names.empty()) virtual_names.pop_back();
        text += "  Subclass it in Tcl (oo::class create X {superclass " + plan.qualname + "; ...}) and override\n  " +
            virtual_names + ": C++ calls the Tcl methods, and `next` runs the C++ implementation.\n"
            "  A subclass constructor must call `next` (with the constructor's arguments).\n";
    } else if (record.abstract) text += "  Abstract: instances come from C++.\n";
    if (!plan.fields.empty()) {
        text += "  fields (`$obj cget -name`, `$obj configure -name value`):\n";
        for (const auto& field : plan.fields) {
            auto doc = one_line(field.field->documentation.brief);
            text += "    -" + field.tcl + " - " + type_text(field.field->type) + (field.writable ? "" : ", read-only") + (doc.empty() ? "" : ": " + doc) + "\n";
        }
    }
    out << comment(text, indent) << indent << "oo::class create " << tcl_word(plan.relative) << " {\n";
    const auto inner = indent + "    ";
    std::string superclasses;
    for (const auto& base : plan.bases) superclasses += " " + classes_.at(base).qualname;
    out << inner << "superclass" << (superclasses.empty() ? " " + ns_ + "::NativeObject" : superclasses) << "\n";
    if (!plan.constructors.empty()) {
        const auto& function = *plan.constructors.front().function;
        Group group{"constructor", plan.constructors, false, {}, {}};
        const auto fallback = "Creates the C++ object (" + function.qualified_name() + ").";
        out << comment(docs(plan.constructors, "constructor", plan.qualname.substr(2) + " new", nullptr, fallback, ""), inner);
        out << inner << "constructor " << stub_parameters(plan.constructors) << " {}\n";
    }
    for (const auto& field : plan.fields) {
        if (field.method.empty()) continue;
        std::string doc = one_line(field.field->documentation.brief);
        std::string text2 = field.method + " -- " + (doc.empty() ? "The field " + field.field->name + "." : doc) + "\n\n" +
            "  usage: $obj " + field.method + (field.writable ? " ?value?" : "") + "\n" +
            "  result: " + type_text(field.field->type) + (field.writable ? " (nothing when setting)" : "") + "\n" +
            (field.writable ? "" : "  note: read-only\n");
        out << comment(text2, inner) << inner << "method " << tcl_word(field.method) << (field.writable ? " {{value {}}} {}\n" : " {} {}\n");
    }
    for (const auto& group : plan.methods) {
        if (group.is_static) continue;
        doc_group(out, group, inner, "method", "$obj ");
    }
    if (!plan.stream.empty())
        out << comment("str -- The C++ stream output (operator<<).\n\n  result: string\n", inner) << inner << "method str {} {}\n";
    out << indent << "}\n";
    for (const auto& group : plan.methods) {
        if (!group.is_static) continue;
        std::ostringstream method;
        doc_group(method, group, indent, "oo::objdefine " + tcl_word(plan.relative) + " method", plan.qualname.substr(2) + " ");
        out << method.str();
    }
    for (const auto& constant : constants_) {
        if (constant.owner != record.qualified_name()) continue;
        std::string summary2 = one_line(constant.documentation->brief);
        out << comment(constant.tcl + " -- " + (summary2.empty() ? "A constant." : summary2) + "\n\n  value: " + type_text(constant.type) +
                       " (read-only variable " + constant.qualname.substr(2) + ")\n", indent)
            << indent << "variable " << tcl_word(constant.qualname.substr(ns_.size() + 2)) << "\n";
    }
    for (const auto& [qualified, enum_plan] : enum_plans_)
        if (enum_plan.enumeration->owner == record.qualified_name()) {
            out << indent << "namespace eval " << tcl_word(plan.relative) << " {\n";
            doc_enum(out, enum_plan, indent + "    ");
            out << indent << "}\n";
        }
}

std::string Generator::package_file() const {
    std::ostringstream out;
    const auto header = std::filesystem::path(module_.header).filename().string();
    const auto library = options_.library.empty() ? name_ : options_.library;
    std::string prefix = name_;
    for (auto& c : prefix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    prefix[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(prefix[0])));
    out << "# " << name_ << ".tcl -- the " << name_ << " Tcl package, generated by Rosetta from " << header << ".\n"
        << "#\n"
        << "# Sourcing this file (`package require " << name_ << "` does, through pkgIndex.tcl) loads the native\n"
        << "# extension " << library << "[info sharedlibextension] from this directory and provides the package.\n"
        << "# The rest of the file documents every command, class and method in the conventional Tcl\n"
        << "# style: a comment block (`# name -- summary`, `usage:`, `args:`, `result:`, `errors:`) right\n"
        << "# before a stub `proc`, `constructor` or `method` with the real parameter list. The stubs sit\n"
        << "# inside `if {0} {...}`, so Tcl never runs them and the native commands stay in place.\n"
        << "#\n"
        << "# Conventions: objects are TclOO objects (`set obj [" << ns_.substr(2) << "::Class new ...]`, `$obj method ...`,\n"
        << "# `$obj destroy`); every object a command returns is a new Tcl command that lives until it is\n"
        << "# destroyed (a borrowed one only drops the Tcl side). Lists are Tcl lists, maps dicts, pairs and\n"
        << "# tuples lists, optional values and null pointers the empty string, enumerations their names,\n"
        << "# callbacks command prefixes (called with the arguments appended). A mutable reference parameter\n"
        << "# takes a variable name. C++ exceptions raise Tcl errors with -errorcode {" << code_ << " kind cpp_type\n"
        << "# message} (kind: invalid_argument, out_of_range, runtime_error, ...); binding errors use\n"
        << "# {" << code_ << " binding message}.\n"
        << "package require Tcl 8.6-\n"
        << "load [file join [file dirname [file normalize [info script]]] " << tcl_word(library) << "[info sharedlibextension]] " << prefix << "\n"
        << "package provide " << name_ << " " << options_.version << "\n\n"
        << "if {0} {\n"
        << "namespace eval " << ns_ << " {\n";
    const std::string indent = "    ";
    // Runtime.
    out << comment("NativeObject -- Base class of every native class.\n\n"
                   "  Every object a command returns is a TclOO object; `$obj destroy` (or `rename $obj {}`)\n"
                   "  destroys the C++ object unless C++ owns or shares it.\n", indent)
        << indent << "oo::class create NativeObject {\n";
    out << comment("cget -- The value of a field.\n\n  usage: $obj cget -name\n  result: the field's value\n", indent + "    ")
        << indent << "    method cget {option} {}\n";
    out << comment("configure -- Reads or writes fields.\n\n  usage: $obj configure ?-name? ?-name value ...?\n"
                   "  result: with no arguments a list of -name value pairs; with one option its value\n", indent + "    ")
        << indent << "    method configure {args} {}\n";
    out << comment("alive -- Whether the C++ object is still usable (not destroyed or moved to C++).\n\n  result: bool\n", indent + "    ")
        << indent << "    method alive {} {}\n";
    out << comment("owned -- Whether Tcl owns or shares the C++ object (destroy releases it).\n\n  result: bool\n", indent + "    ")
        << indent << "    method owned {} {}\n";
    out << indent << "}\n\n";
    for (const auto& constant : constants_) {
        if (!constant.owner.empty()) continue;
        std::string summary = one_line(constant.documentation->brief);
        out << comment(constant.tcl + " -- " + (summary.empty() ? "A constant." : summary) + "\n\n  value: " + type_text(constant.type) +
                       " (read-only variable " + constant.qualname.substr(2) + ")\n", indent)
            << indent << "variable " << tcl_word(constant.tcl) << "\n";
    }
    for (const auto& [qualified, plan] : enum_plans_) if (plan.enumeration->owner.empty()) { doc_enum(out, plan, indent); out << "\n"; }
    for (const auto& group : functions_) {
        if (!group.module.empty()) continue;
        doc_group(out, group, indent, "proc", ns_.substr(2) + "::");
        out << "\n";
    }
    for (const auto& owner : class_order_) { doc_class(out, classes_.at(owner), indent); out << "\n"; }
    std::map<std::string, std::vector<const Group*>> submodules;
    for (const auto& group : functions_) if (!group.module.empty()) submodules[group.module].push_back(&group);
    for (const auto& [module, groups] : submodules) {
        const auto path = submodule_path(module);
        out << comment(path + " -- Commands of the C++ namespace mapped to the " + module + " module.\n", indent)
            << indent << "namespace eval " << tcl_word(path) << " {\n";
        for (const auto* group : groups) { doc_group(out, *group, indent + "    ", "proc", ns_.substr(2) + "::" + path + "::"); out << "\n"; }
        out << indent << "}\n";
    }
    out << "}\n}\n";
    return out.str();
}

std::string Generator::pkg_index() const {
    const auto file = options_.tcl_file.empty() ? name_ + ".tcl" : options_.tcl_file;
    return "# Generated by Rosetta: `package require " + name_ + "` finds this directory through auto_path\n"
           "# (or TCLLIBPATH) and sources " + file + ", which loads the native extension.\n"
           "package ifneeded " + name_ + " " + options_.version + " [list source [file join $dir " + tcl_word(file) + "]]\n";
}

GeneratedTclFullSource Generator::run() {
    std::string space = options_.tcl_namespace.empty() ? name_ : options_.tcl_namespace;
    if (space.rfind("::", 0) == 0) space.erase(0, 2);
    for (std::size_t start = 0;;) {
        const auto colon = space.find("::", start);
        if (!tcl_name(space.substr(start, colon == std::string::npos ? std::string::npos : colon - start)))
            throw BindingError("Tcl namespace must be a namespace path of simple names (a or a::b): " + space);
        if (colon == std::string::npos) break;
        start = colon + 2;
    }
    if (space == "tcl" || space == "oo" || space.rfind("tcl::", 0) == 0 || space.rfind("oo::", 0) == 0)
        throw BindingError("Tcl namespace is reserved: " + space);
    ns_ = "::" + space;
    if (!tcl_name(name_)) throw BindingError("Tcl package (module) name must be a simple identifier: " + name_);
    code_.clear();
    for (const char c : name_) code_ += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& rule : policy_.enum_flags)
        if (std::none_of(module_.enumerations.begin(), module_.enumerations.end(), [&](const Enumeration& e) { return e.qualified_name() == rule.first; }))
            throw BindingError("enum flag policy refers to an unknown enum: " + rule.first);
    GeneratedTclFullSource result;
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
        plan.tcl = renamed(qualified, nested || flat(enumeration->namespaces) ? enumeration->name : exported(qualified));
        plan.relative = nested ? classes_.at(enumeration->owner).relative + "::" + plan.tcl : plan.tcl;
        plan.qualname = ns_ + "::" + plan.relative;
        plan.flags = policy_.enum_flags.count(qualified) != 0;
        std::set<std::string> names;
        for (const auto& enumerator : enumeration->enumerators) {
            const auto member = renamed(qualified + "::" + enumerator, enumerator);
            if (!names.insert(member).second) reject(qualified + "::" + enumerator, "enumerator name " + member + " is not unique");
            plan.members.push_back(member);
        }
        enum_plans_[qualified] = plan;
    }
    plan_members();
    plan_functions();

    // Command names (functions and classes) and variable names (enums and constants) must be unique.
    std::map<std::string, std::string> commands, vars;
    const auto reserve = [](std::map<std::string, std::string>& names, const std::string& tcl, const std::string& what) {
        const auto inserted = names.emplace(tcl, what);
        if (!inserted.second) throw BindingError("Tcl export name collision: " + tcl + " (" + inserted.first->second + " and " + what + ")");
    };
    reserve(commands, ns_ + "::NativeObject", "the Tcl runtime");
    for (const auto& group : functions_) reserve(commands, command_path(group), "function " + group.overloads.front().function->qualified_name());
    for (const auto& owner : class_order_) reserve(commands, classes_.at(owner).qualname, "class " + owner);
    for (const auto& [qualified, plan] : enum_plans_) {
        if (vars.count(plan.qualname)) { reject(qualified, "enum name collides with another export: " + plan.tcl); continue; }
        reserve(vars, plan.qualname, "enum " + qualified);
    }
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to Tcl"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto tcl = renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
        const auto path = ns_ + "::" + tcl;
        if (vars.count(path)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(vars, path, "constant " + qualified);
        constants_.push_back({tcl, path, {}, "::" + qualified, &variable.documentation, variable.type});
    }
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& variable : plan.record->static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to Tcl"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            const auto tcl = renamed(qualified, variable.name);
            const auto path = plan.qualname + "::" + tcl;
            if (vars.count(path)) { reject(qualified, "constant name collides with another export"); continue; }
            reserve(vars, path, "constant " + qualified);
            constants_.push_back({tcl, path, owner, qualified, &variable.documentation, variable.type});
        }
    }
    // Using-declarations re-export bound functions and classes under another name.
    for (const auto& reexport : module_.reexports) {
        std::string scope;
        for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
        const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
        const auto tcl = renamed(qualified, flat(reexport.namespaces) ? reexport.name : exported(qualified));
        const auto path = ns_ + "::" + tcl;
        std::string target;
        if (reexport.kind == "function") {
            for (const auto& group : functions_)
                if (group.module.empty() && std::any_of(group.overloads.begin(), group.overloads.end(), [&](const Overload& o) {
                        return o.function->qualified_name() == reexport.target; })) { target = command_path(group); break; }
        } else if (reexport.kind == "record" || reexport.kind == "type") {
            const auto c = classes_.find(reexport.target);
            if (c != classes_.end()) target = c->second.qualname;
        }
        if (target.empty() || target == path || commands.count(path)) continue;
        reserve(commands, path, "alias of " + reexport.target);
        aliases_.emplace_back(path, target);
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Tcl";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Wrapper code is collected in units (a class, or a function) that a split build spreads over
    // several sources; `out` is the prelude every source repeats.
    struct Unit { std::string code, declaration; };
    std::vector<Unit> units;
    std::ostringstream out, init;
    const std::string module_namespace = "rosetta_tcl_module_" + name_;
    out << "// Generated by Rosetta: Tcl extension (--tcl-api full).\n#include <tcl.h>\n#include <tclOO.h>\n"
        << "#if TCL_MAJOR_VERSION < 8 || (TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 6)\n#error Rosetta requires Tcl 8.6 or newer\n#endif\n";
    for (const auto* header : {"array", "cctype", "cerrno", "chrono", "climits", "cmath", "complex", "cstdio", "cstdlib", "cstring", "deque", "filesystem",
                               "functional", "future", "ios", "iostream", "limits", "list", "map", "memory", "new", "optional", "set", "sstream",
                               "stdexcept", "string", "string_view", "tuple", "typeindex", "typeinfo", "type_traits", "unordered_map",
                               "unordered_set", "utility", "valarray", "variant", "vector", "cxxabi.h"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n';
    out << "#define ROSETTA_TCL_MODULE " << module_namespace << '\n';
    for (const auto* part : detail::tcl_full_runtime_parts) out << part;
    out << "namespace ROSETTA_TCL_MODULE { namespace rosetta_tcl {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(Tcl_Obj* obj, int level) { return Value<" << to << ">::check(obj, level); }\n"
            << "    static " << from << " load(Tcl_Obj* obj) { return " << from << "(Value<" << to << ">::load(obj)); }\n"
            << "    static Tcl_Obj* cast(const " << from << "& value) { return Value<" << to << ">::cast(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\nnamespace ROSETTA_TCL_MODULE {\n";
    for (const auto& owner : class_order_) if (classes_.at(owner).director) emit_director(out, classes_.at(owner));
    out << "}\n";

    for (auto& group : functions_) {
        std::ostringstream unit;
        emit_group(unit, group, "function");
        units.push_back({unit.str(), "Tcl_Obj* " + group.symbol + "(rosetta_tcl::Call&);\n"});
    }
    init << "        rosetta_tcl::initialize_runtime(s);\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        init << "        {\n            auto& info = rosetta_tcl::Info<" << owner << ">::value;\n"
             << "            rosetta_tcl::register_type<" << owner << ">(info, " << quote(plan.qualname) << ");\n"
             << "            info.bases.clear();\n            info.fields.clear();\n";
        for (const auto& base : plan.bases) init << "            rosetta_tcl::add_base<" << owner << ", " << base << ">(info);\n";
        init << "        }\n";
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::ostringstream unit, registration;
        std::string declarations;
        for (auto& group : plan.methods) {
            emit_group(unit, group, group.is_static ? "function" : "method");
            declarations += "Tcl_Obj* " + group.symbol + "(rosetta_tcl::Call&);\n";
            registration << "            rosetta_tcl::" << (group.is_static ? "add_static" : "add_method") << "(s, cls, " << quote(group.name)
                         << ", " << group.symbol << ");\n";
        }
        for (auto& field : plan.fields) {
            const auto& name = field.field->name;
            const auto member_type = "std::remove_cv_t<std::remove_reference_t<decltype(std::declval<" + owner + "&>()." + name + ")>>";
            unit << "Tcl_Obj* " << field.symbol << "_get(rosetta_tcl::Instance* self) {\n"
                 << "    auto* native = rosetta_tcl::native<" << owner << ">(self);\n"
                 << "    return rosetta_tcl::result<decltype((native->" << name << "))>(native->" << name << ", self, rosetta_tcl::Return::"
                 << (copy_type(field.field->type) ? "copy" : "automatic") << ");\n}\n";
            declarations += "Tcl_Obj* " + field.symbol + "_get(rosetta_tcl::Instance*);\n";
            std::string setter = "nullptr";
            if (field.writable) {
                unit << "void " << field.symbol << "_set(rosetta_tcl::Instance* self, Tcl_Obj* value) {\n"
                     << "    rosetta_tcl::Param<const " << member_type << "&> converted(value);\n"
                     << "    auto* native = rosetta_tcl::native<" << owner << ">(self);\n"
                     << "    rosetta_tcl::assign_field(native->" << name << ", converted.get());\n}\n";
                declarations += "void " + field.symbol + "_set(rosetta_tcl::Instance*, Tcl_Obj*);\n";
                setter = "(std::is_copy_assignable_v<" + member_type + "> ? &" + field.symbol + "_set : nullptr)";
            }
            unit << "Tcl_Obj* " << field.symbol << "_method(rosetta_tcl::Call& call) {\n"
                 << "    return rosetta_tcl::field_access(call, " << quote(field.tcl) << ", " << field.symbol << "_get, " << setter << ");\n}\n";
            declarations += "Tcl_Obj* " + field.symbol + "_method(rosetta_tcl::Call&);\n";
            registration << "            rosetta_tcl::add_field<" << owner << ">(info, " << quote(field.tcl) << ", " << field.symbol << "_get, "
                         << setter << ");\n";
            if (!field.method.empty())
                registration << "            rosetta_tcl::add_method(s, cls, " << quote(field.method) << ", " << field.symbol << "_method);\n";
        }
        if (!plan.constructors.empty()) {
            emit_constructor(unit, plan);
            declarations += "Tcl_Obj* " + plan.constructor_symbol + "(rosetta_tcl::Call&);\n";
            registration << "            rosetta_tcl::set_constructor(s, cls, " << plan.constructor_symbol << ");\n";
        } else {
            unit << "Tcl_Obj* " << plan.symbol << "_construct(rosetta_tcl::Call&) {\n    rosetta_tcl::fail("
                 << quote(plan.qualname + " cannot be created from Tcl") << ");\n}\n";
            declarations += "Tcl_Obj* " + plan.symbol + "_construct(rosetta_tcl::Call&);\n";
            registration << "            rosetta_tcl::set_constructor(s, cls, " << plan.symbol << "_construct);\n";
        }
        if (!plan.stream.empty()) registration << "            rosetta_tcl::add_method(s, cls, \"str\", rosetta_tcl::stream_method<" << owner << ">);\n";
        for (const auto& constant : constants_) {
            if (constant.owner != owner) continue;
            registration << "            rosetta_tcl::define_constant(s, " << quote(constant.qualname) << ", rosetta_tcl::Value<std::remove_cv_t<decltype("
                         << constant.expression << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>("
                         << constant.expression << ")));\n";
        }
        units.push_back({unit.str(), declarations});
        std::string superclasses;
        for (const auto& base : plan.bases) superclasses += (superclasses.empty() ? "" : ", ") + quote(classes_.at(base).qualname);
        if (superclasses.empty()) superclasses = quote(ns_ + "::NativeObject");
        init << "        {\n            auto& info = rosetta_tcl::Info<" << owner << ">::value;\n"
             << "            Tcl_Class cls = rosetta_tcl::define_class(s, info, " << quote(plan.qualname) << ", {" << superclasses << "});\n"
             << registration.str() << "        }\n";
    }
    // Enumerations.
    for (const auto& [qualified, plan] : enum_plans_) {
        const auto& enumeration = *plan.enumeration;
        if (vars.count(plan.qualname) && vars.at(plan.qualname) != "enum " + qualified) continue;
        init << "        rosetta_tcl::define_enum<" << qualified << ">(s, " << quote(plan.qualname) << ", " << (plan.flags ? "true" : "false") << ", {";
        for (std::size_t i = 0; i < plan.members.size(); ++i) {
            const auto& value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string("0");
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            const auto number = big ? "static_cast<long long>(" + value + "ULL)" : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL";
            init << (i ? ", " : "") << "{" << quote(plan.members[i]) << ", " << number << "}";
        }
        init << "});\n";
    }
    // Functions, constants and aliases.
    for (const auto& group : functions_)
        init << "        rosetta_tcl::add_command(s, " << quote(command_path(group)) << ", " << group.symbol << ");\n";
    for (const auto& constant : constants_) {
        if (!constant.owner.empty()) continue;
        init << "        rosetta_tcl::define_constant(s, " << quote(constant.qualname) << ", rosetta_tcl::Value<std::remove_cv_t<decltype("
             << constant.expression << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>("
             << constant.expression << ")));\n";
    }
    for (const auto& [alias, target] : aliases_)
        init << "        rosetta_tcl::eval_words(s, {\"::interp\", \"alias\", \"\", " << quote(alias) << ", \"\", " << quote(target) << "});\n";
    for (const auto& group : functions_) init << "        rosetta_tcl::export_command(s, " << quote(command_path(group)) << ");\n";
    for (const auto& owner : class_order_)
        if (classes_.at(owner).parent.empty()) init << "        rosetta_tcl::export_command(s, " << quote(classes_.at(owner).qualname) << ");\n";
    for (const auto& alias : aliases_) init << "        rosetta_tcl::export_command(s, " << quote(alias.first) << ");\n";
    init << "        if (Tcl_PkgProvideEx(s.interp, " << quote(name_) << ", " << quote(options_.version) << ", nullptr) != TCL_OK) rosetta_tcl::raise_pending(s.interp);\n";
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Tcl";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Units go to parts - 1 extra sources by size (largest first, into the smallest source),
    // keeping declaration order within each; Init stays in the main source.
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
        for (auto& part : extra) part << prelude << "namespace ROSETTA_TCL_MODULE {\n";
        out << "namespace ROSETTA_TCL_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) {
            extra[bin[i]] << units[i].code;
            out << units[i].declaration;
        }
        for (auto& part : extra) part << "} // namespace ROSETTA_TCL_MODULE\n";
    } else {
        out << "namespace ROSETTA_TCL_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    std::string prefix = name_;
    for (auto& c : prefix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    prefix[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(prefix[0])));
    out << "static int rosetta_init(Tcl_Interp* interp) {\n"
        << "    auto* state = rosetta_tcl::create_state(interp, " << quote(code_) << ", " << quote(ns_) << ", " << quote("rosetta_tcl::" + name_) << ");\n"
        << "    return rosetta_tcl::guard(state, [&]() -> Tcl_Obj* {\n        auto& s = *state;\n" << init.str()
        << "        return nullptr;\n    });\n}\n} // namespace ROSETTA_TCL_MODULE\n"
        << "extern \"C\" DLLEXPORT int " << prefix << "_Init(Tcl_Interp* interp) {\n"
        << "#ifdef USE_TCL_STUBS\n"
        << "    if (!Tcl_InitStubs(interp, TCL_VERSION, 0)) return TCL_ERROR;\n"
        << "#endif\n"
        << "    if (!Tcl_OOInitStubs(interp)) return TCL_ERROR;\n"
        << "    return ROSETTA_TCL_MODULE::rosetta_init(interp);\n}\n";
    result.source = out.str();
    for (const auto& part : extra) result.parts.push_back(part.str());
    result.tcl = package_file();
    result.pkg_index = pkg_index();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedTclFullSource generate_tcl_full(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy,
                                         const TclFullOptions& options) {
    if (options.parts == 0) throw BindingError("Tcl source parts must be positive");
    return Generator(module, name, skip, policy, options).run();
}
} // namespace rosetta
