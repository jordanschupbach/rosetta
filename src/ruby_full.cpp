// The full Ruby backend (--language ruby --ruby-api full): an MRI native extension against the
// Ruby C API, modeled on the Python backend (src/python.cpp), plus a YARD documentation file.
//
// Generated layout for module M (Ruby module MOD, native extension path NATIVE):
//   OUTPUT (+ parts)  C++ sources of the extension; Init_<basename of NATIVE> defines MOD.
//   RUBY_OUTPUT       M.rb: require_relative NATIVE, then every module, class and method as a
//                     YARD-documented stub inside `if nil ... end`, which Ruby never runs (so the
//                     native methods stay in place) while YARD (which reads both branches of a
//                     condition it cannot evaluate) and editors read it.
//
// Ruby conventions: classes are CamelCase, methods snake_case (is_x()/has_x() returning bool become
// x?/has_x?, set_x(v) also gets x=), constants UPPER_CASE, enumerations modules of Symbol constants
// (values are Symbols; flag enumerations are Integers), fields attr accessors, static methods class
// methods, module functions module_function. Names that would replace a Ruby keyword or a core
// Object method get a trailing underscore. Arguments may be passed by keyword, and keywords may skip
// defaulted parameters whose C++ default is a literal. A block fills a std::function parameter.
#include "rosetta/ruby.hpp"
#include "ruby_full_runtime.hpp"
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
// --- Names -------------------------------------------------------------------------------------
bool lower(char c) { return c >= 'a' && c <= 'z'; }
bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool word(char c) { return lower(c) || upper(c) || digit(c) || c == '_'; }
// camelCase and PascalCase to snake_case; names that are already snake stay as they are.
std::string snake(const std::string& name) {
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (upper(c)) {
            const bool after_word = i > 0 && (lower(name[i - 1]) || digit(name[i - 1]));
            const bool acronym_end = i > 0 && upper(name[i - 1]) && i + 1 < name.size() && lower(name[i + 1]);
            if ((after_word || acronym_end) && !out.empty() && out.back() != '_') out += '_';
            out += static_cast<char>(c - 'A' + 'a');
        } else out += c;
    }
    return out;
}
// Class and module names: CamelCase.
std::string camel(const std::string& name) {
    std::string out;
    bool start = true;
    for (char c : name) {
        if (c == '_') { start = true; continue; }
        out += start && lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
        start = false;
    }
    if (out.empty() || !upper(out[0])) out = "C" + out;
    return out;
}
// Constants: UPPER_SNAKE_CASE.
std::string constant_name(const std::string& name) {
    std::string out;
    for (char c : snake(name)) out += lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
    while (!out.empty() && out[0] == '_') out.erase(0, 1);
    if (out.empty() || !upper(out[0])) out = "C_" + out;
    return out;
}
// Ruby keywords and core Object/Kernel methods a native method must not replace.
const std::set<std::string>& reserved_methods() {
    static const std::set<std::string> names = {"__ENCODING__", "__LINE__", "__FILE__", "BEGIN", "END", "alias", "and", "begin",
        "break", "case", "class", "def", "defined?", "do", "else", "elsif", "end", "ensure", "false", "for", "if", "in", "module",
        "next", "nil", "not", "or", "redo", "rescue", "retry", "return", "self", "super", "then", "true", "undef", "unless",
        "until", "when", "while", "yield", "object_id", "__id__", "__send__", "send", "public_send", "singleton_class",
        "initialize", "initialize_copy", "instance_of?", "is_a?", "kind_of?", "respond_to?", "respond_to_missing?", "method",
        "methods", "equal?", "nil?", "freeze", "frozen?", "dup", "clone", "instance_variable_get", "instance_variable_set",
        "instance_variables", "instance_eval", "instance_exec", "define_singleton_method", "extend", "method_missing", "tap",
        "yield_self", "itself", "raise", "fail", "require", "require_relative", "load", "lambda", "proc", "binding",
        "block_given?", "catch", "throw", "loop", "puts", "p", "pp", "allocate", "new", "superclass", "close", "closed?",
        "alive?", "owned?"};
    return names;
}
// Class names the generated module's runtime defines itself.
const std::set<std::string>& reserved_constants() {
    static const std::set<std::string> names = {"NativeObject", "NativeCallable", "Error", "ArgumentError", "IndexError",
        "KeyError", "RangeError", "TypeError", "NotImplementedError", "RuntimeError", "ZeroDivisionError", "IOError"};
    return names;
}
std::string method_safe(const std::string& name) {
    if (!reserved_methods().count(name)) return name;
    const char last = name.back();
    if (last == '?' || last == '!' || last == '=') return name.substr(0, name.size() - 1) + "_" + last;
    return name + "_";
}
std::string constant_safe(const std::string& name) { return reserved_constants().count(name) ? name + "_" : name; }
bool ruby_method_name(const std::string& text) {
    if (text.empty()) return false;
    static const std::set<std::string> operators = {"+", "-", "*", "/", "%", "**", "==", "!=", "<", "<=", ">", ">=", "<=>", "===",
        "=~", "[]", "[]=", "+@", "-@", "~", "!", "&", "|", "^", "<<", ">>", "call"};
    if (operators.count(text)) return true;
    std::size_t end = text.size();
    if (text.back() == '?' || text.back() == '!' || text.back() == '=') --end;
    if (end == 0 || digit(text[0]) || upper(text[0])) return false;
    for (std::size_t i = 0; i < end; ++i) if (!word(text[i])) return false;
    return true;
}
bool ruby_constant_name(const std::string& text) {
    return !text.empty() && upper(text[0]) && std::all_of(text.begin(), text.end(), word);
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
bool is_integral(const Type& type) {
    static const std::set<std::string> names = {"short", "unsigned short", "int", "unsigned int", "long", "unsigned long",
        "long long", "unsigned long long"};
    return referent(type).kind == TypeKind::builtin && names.count(strip_cv(referent(type).canonical));
}
bool is_string(const Type& type) {
    const auto& target = referent(type);
    return target.kind == TypeKind::template_instance && normalize(target.declaration) == "std::basic_string";
}
bool is_ostream(const Type& type) {
    const auto& target = referent(type);
    return type.kind == TypeKind::lvalue_reference && !target.is_const && normalize(target.declaration) == "std::basic_ostream";
}

// Ruby operator method for a C++ operator by arity (member operators: arity excludes this).
std::string operator_name(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"+@", "+"}}, {"operator-", {"-@", "-"}}, {"operator*", {nullptr, "*"}}, {"operator/", {nullptr, "/"}},
        {"operator%", {nullptr, "%"}}, {"operator==", {nullptr, "=="}}, {"operator<", {nullptr, "<"}}, {"operator<=", {nullptr, "<="}},
        {"operator>", {nullptr, ">"}}, {"operator>=", {nullptr, ">="}}, {"operator&", {nullptr, "&"}}, {"operator|", {nullptr, "|"}},
        {"operator^", {nullptr, "^"}}, {"operator<<", {nullptr, "<<"}}, {"operator>>", {nullptr, ">>"}}, {"operator~", {"~", nullptr}},
        {"operator[]", {nullptr, "[]"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
bool is_operator_name(const std::string& ruby) { return !ruby.empty() && !word(ruby[0]); }

// Ruby exception class for a C++ exception type, for @raise.
std::string ruby_exception(std::string name) {
    name = normalize(name);
    if (name.rfind("std::", 0) == 0) name.erase(0, 5);
    if (name == "invalid_argument" || name == "domain_error" || name == "length_error") return "ArgumentError";
    if (name == "out_of_range") return "IndexError";
    if (name == "overflow_error" || name == "underflow_error" || name == "range_error") return "RangeError";
    if (name == "bad_cast" || name == "bad_optional_access" || name == "bad_variant_access") return "TypeError";
    if (name == "filesystem::filesystem_error" || name == "ios_base::failure") return "IOError";
    return "RuntimeError";
}
// rosetta_rb::errors() member for a policy exception name (Ruby or Python spelling).
std::string error_member(const std::string& name) {
    static const std::map<std::string, std::string> names = {{"ArgumentError", "argument"}, {"ValueError", "argument"},
        {"IndexError", "index"}, {"KeyError", "key"}, {"LookupError", "index"}, {"RangeError", "range"}, {"OverflowError", "range"},
        {"FloatDomainError", "range"}, {"TypeError", "type"}, {"NotImplementedError", "not_implemented"},
        {"RuntimeError", "runtime"}, {"StandardError", "runtime"}, {"ZeroDivisionError", "zero_division"},
        {"ArithmeticError", "zero_division"}, {"IOError", "io"}, {"OSError", "io"}, {"FileNotFoundError", "io"},
        {"FileExistsError", "io"}, {"PermissionError", "io"}, {"EOFError", "io"}};
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
// A YARD tag's text: continuation lines are indented under the tag.
std::string tag_text(const std::string& text) {
    const auto lines = lines_of(trim(text));
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n  " : "") + lines[i];
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
std::string code_block(const DocBlock& block) {
    std::string language = block.language;
    if (language.empty() || language == "c++" || language == "cxx" || language == "cc") language = "cpp";
    if (language == "rb") language = "ruby";
    return "```" + language + "\n" + block.text + "\n```";
}
// C++ documentation text as YARD prose: outside `code` spans, `{` would start a YARD link and a
// line starting with `@` a tag, so both become HTML entities (Markdown renders them as is).
// Wrapping such a span in backticks keeps it literal for YARD and Markdown viewers alike.
std::string prose(const std::string& text) {
    std::string out;
    bool code = false, line_start = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '`') code = !code;
        std::size_t end = std::string::npos;
        if (!code && c == '{') {
            end = text.find_first_of("}`\n", i + 1);
            if (end != std::string::npos && text[end] != '}') end = std::string::npos;
        } else if (!code && line_start && c == '@') {
            end = text.find_first_of(" \t\n`", i + 1);
            end = (end == std::string::npos ? text.size() : end) - 1;
        }
        if (end != std::string::npos) {
            out += "`" + text.substr(i, end - i + 1) + "`";
            i = end;
            line_start = false;
            continue;
        }
        out += c;
        if (c == '\n') line_start = true;
        else if (c != ' ' && c != '\t') line_start = false;
    }
    return out;
}
std::string block_text(const DocBlock& block) {
    if (block.kind == "code") return code_block(block);
    if (block.kind == "math") return "```math\n" + block.text + "\n```";
    return prose(block.text);
}

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;       // qualified class for constructors and methods
    bool extension = false;  // free function attached to a class
    bool receives_self = false;
};
struct Group {
    std::string name;        // Ruby method name
    std::vector<Overload> overloads;
    bool is_static = false;
    std::string module;      // policy submodule of a module function ("" = top level)
    std::string symbol;      // C++ wrapper
    std::vector<std::string> aliases;
};
struct FieldPlan {
    const Field* field;
    std::string ruby;
    std::string symbol;
    bool writable = false;
};
struct ClassPlan {
    const Record* record;
    std::string ruby;        // constant name
    std::string qualname;    // Ruby path below the module (A::B)
    std::string symbol;      // C++ identifier prefix
    std::vector<std::string> bases; // exported ancestor classes (qualified C++ names)
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<FieldPlan> fields;
    bool director = false;
    std::string parent;      // qualified outer class when nested and exported
    std::string stream;      // operator<< target for to_s
    std::string each_size, each_item; // Ruby names for each/Enumerable
    bool comparable = false;
    std::string constructor_symbol;
};
struct EnumPlan {
    const Enumeration* enumeration;
    std::string ruby;        // module name
    std::string qualname;
    std::vector<std::pair<std::string, std::string>> members; // symbol name, constant name
    bool flags = false;
};
struct ConstantPlan {
    std::string ruby, owner, expression;
    const Documentation* documentation;
    Type type;
};

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const RubyFullOptions& options)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"),
          options_(options) { validate_modules(policy_, module_); }

    GeneratedRubyFullSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    RubyFullOptions options_;
    std::string ruby_module_;   // Ruby path of the top module (A or A::B)
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
    std::string renamed_method(const std::string& qualified, const std::string& fallback) const {
        if (const auto* name = renamed_raw(qualified)) {
            if (!ruby_method_name(*name)) throw BindingError("Ruby rename target is not a method name: " + *name);
            return *name;
        }
        return fallback;
    }
    std::string renamed_constant(const std::string& qualified, const std::string& fallback) const {
        if (const auto* name = renamed_raw(qualified)) {
            if (!ruby_constant_name(*name)) throw BindingError("Ruby rename target is not a constant name: " + *name);
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
    std::string class_ruby_name(const Record& record) const;
    std::string method_ruby_name(const Function& method) const;
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
        std::string out;
        std::size_t start = 0;
        while (true) {
            const auto dot = module.find('.', start);
            out += (out.empty() ? "" : "::") + camel(module.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return out;
    }
    void plan_classes();
    void plan_directors();
    void plan_members();
    void plan_functions();
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    bool fillable(const Function& function, std::size_t index) const;
    std::string fill_expression(const Function& function, std::size_t index) const;
    long callback_index(const Overload& overload) const;
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent);
    void emit_group(std::ostream& out, Group& group, const std::string& kind);
    std::pair<std::string, std::string> exception_handlers(const std::string& qualified) const;
    void emit_constructor(std::ostream& out, ClassPlan& plan);
    void emit_director(std::ostream& out, const ClassPlan& plan);
    std::string signature(const Overload& overload, const std::string& name) const;
    // YARD
    std::string yard_type(const Type& type) const;
    std::string ruby_default(const DefaultValue& value, const Type& type) const;
    std::string parameter_name(const Function& function, std::size_t index) const;
    std::string yard_docs(const std::vector<Overload>& overloads, const std::string& name, const Documentation* own,
                          const std::string& extra_note) const;
    std::string stub_parameters(const Overload& overload) const;
    std::string yard_file() const;
    void yard_class(std::ostream& out, const ClassPlan& plan, const std::string& indent) const;
    void yard_enum(std::ostream& out, const EnumPlan& plan, const std::string& indent) const;
    void yard_group(std::ostream& out, const Group& group, const std::string& indent, bool singleton, const Documentation* own = nullptr) const;
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
        return "unsupported Ruby value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to Ruby";
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
        return "unsupported Ruby template type " + type.canonical;
    }
    default:
        return "unsupported Ruby value type " + type.canonical;
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
        return "unsupported Ruby pointer type " + type.canonical;
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
        if (!why.empty()) return "unsupported Ruby result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported Ruby parameter type: " + why;
    }
    return {};
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_rb::Return::copy" : "rosetta_rb::Return::automatic";
    return "rosetta_rb::Return::" + found->second;
}

std::string Generator::class_ruby_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed_constant(qualified, constant_safe(camel(instantiated->second)));
    if (!record.owner.empty()) return renamed_constant(qualified, constant_safe(camel(record.name)));
    return renamed_constant(qualified, constant_safe(camel(flat(record.namespaces) ? record.name : exported(qualified))));
}

// snake_case, predicates (is_x/has_x returning bool with no arguments) as x?/has_x?, then
// a trailing underscore for names Ruby reserves.
std::string Generator::method_ruby_name(const Function& method) const {
    if (const auto* name = renamed_raw(method.qualified_name())) {
        if (!ruby_method_name(*name)) throw BindingError("Ruby rename target is not a method name: " + *name);
        return *name;
    }
    auto name = snake(method.name);
    if (!method.is_static && method.parameters.empty() && is_bool(method.result) && !method.is_constructor) {
        if (name.rfind("is_", 0) == 0 && name.size() > 3 && !digit(name[3])) name = name.substr(3) + "?";
        else if (name.rfind("has_", 0) == 0 || name.rfind("can_", 0) == 0 || name == "empty") name += "?";
    }
    return method_safe(name);
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
            throw BindingError("unknown class in Ruby policy: " + selection.first);
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
        if (!record.complete) { if (listed) reject(owner, "Ruby requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed Ruby results are not yet supported"); continue;
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
        plan.ruby = class_ruby_name(*plan.record);
        plan.parent = plan.record->owner;
    }
    for (auto& [owner, plan] : classes_) {
        std::string qualname = plan.ruby;
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            qualname = classes_.at(parent).ruby + "::" + qualname;
        plan.qualname = qualname;
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

// A defaulted parameter a keyword call may skip: its C++ default is a literal generated code can repeat.
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
    if (value.kind == "default") return "rosetta_rb::fill_default";
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
    return "rosetta_rb::fill, " + expression;
}
long Generator::callback_index(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& target = referent(function.parameters[i]);
        if (target.kind == TypeKind::template_instance && normalize(target.declaration) == "std::function")
            return static_cast<long>(i - first);
    }
    return -1;
}

std::string Generator::parameter_name(const Function& function, std::size_t index) const {
    const auto written = index < function.parameter_names.size() ? function.parameter_names[index] : std::string();
    if (written.empty()) return "arg" + std::to_string(index);
    // Parameters are locals: only keywords (and names starting with a capital) are a problem.
    static const std::set<std::string> keywords = {"__ENCODING__", "__LINE__", "__FILE__", "BEGIN", "END", "alias", "and", "begin",
        "break", "case", "class", "def", "defined?", "do", "else", "elsif", "end", "ensure", "false", "for", "if", "in", "module",
        "next", "nil", "not", "or", "redo", "rescue", "retry", "return", "self", "super", "then", "true", "undef", "unless",
        "until", "when", "while", "yield", "options"};
    auto name = snake(written);
    if (upper(name[0])) name = "a_" + name;
    return keywords.count(name) ? name + "_" : name;
}

std::string Generator::signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    std::string result = name + "(";
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        if (i > first) result += ", ";
        result += parameter_name(function, i) + " [" + yard_type(function.parameters[i]) + "]";
        if (i >= required) result += " = ...";
    }
    result += ")";
    if (!function.is_constructor && !is_void(function.result)) result += " -> " + yard_type(function.result);
    return result;
}

void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent) {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - first;
    const std::size_t required = function.parameters.size() - function.default_arguments - first;
    out << indent << "{\n" << indent << "    static const char* const names[] = {";
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& written = i < function.parameter_names.size() ? function.parameter_names[i] : std::string{};
        out << (i > first ? ", " : "") << quote(written.empty() ? std::string() : parameter_name(function, i));
    }
    if (count == 0) out << "nullptr";
    out << "};\n" << indent << "    static const bool fillable[] = {";
    for (std::size_t i = first; i < function.parameters.size(); ++i)
        out << (i > first ? ", " : "") << (i - first >= required && fillable(function, i) ? "true" : "false");
    if (count == 0) out << "false";
    out << "};\n";
    out << indent << "    VALUE a[" << std::max<std::size_t>(count, 1) << "]; long n = 0;\n";
    out << indent << "    if (args.collect(names, fillable, " << count << ", " << required << ", " << callback_index(overload) << ", a, &n)";
    if (overload.receives_self) out << " && rosetta_rb::Param<" << function.parameters[0].canonical << ">::check(self, false)";
    for (std::size_t i = 0; i < count; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || a[" << i << "] == Qundef || rosetta_rb::Param<"
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
                out << indent << "            auto p" << i << " = a[" << i << "] == Qundef ? rosetta_rb::Param<" << type << ">("
                    << fill_expression(function, i + first) << ") : rosetta_rb::Param<" << type << ">(a[" << i << "]);\n";
            else
                out << indent << "            rosetta_rb::Param<" << type << "> p" << i << "(a[" << i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (kind == "method" || kind == "setitem") {
            if (!function.is_const) out << indent << "            rosetta_rb::writable(self);\n";
            out << indent << "            auto* native = rosetta_rb::native<" << owner << ">(self);\n";
        }
        if (overload.receives_self) out << indent << "            rosetta_rb::Param<" << function.parameters[0].canonical << "> self_param(self);\n";
        std::string writebacks;
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& type = function.parameters[i + first];
            if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                writebacks += indent + "            if (a[" + std::to_string(i) + "] != Qundef) p" + std::to_string(i) + ".writeback(a[" + std::to_string(i) + "]);\n";
        }
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            out << indent << "            " << owner << "* created = nullptr;\n";
            if (plan.director) {
                out << indent << "            if (director) created = new " << plan.symbol << "_director(self" << (arguments.empty() ? "" : ", ") << arguments << ");\n";
                out << indent << "            else ";
            } else out << indent << "            ";
            if (plan.record->abstract) out << "rosetta_rb::fail(rb_eTypeError, " << quote(plan.qualname + " is abstract; subclass it in Ruby and implement its pure virtual methods") << ");\n";
            else out << "created = new " << owner << "(" << arguments << ");\n";
            out << writebacks;
            out << indent << "            object->ptr = created; object->info = &rosetta_rb::Info<" << owner << ">::value;\n"
                << indent << "            object->owned = 1; object->director = " << (plan.director ? "director" : "false") << ";\n";
            if (plan.director) out << indent << "            if (director) static_cast<rosetta_rb::Director*>(static_cast<" << plan.symbol << "_director*>(created))->rosetta_self = self;\n";
            out << indent << "            return Qnil;\n";
        } else if (kind == "setitem") {
            out << indent << "            rosetta_rb::Param<const std::remove_cv_t<std::remove_reference_t<decltype((*native)[" << arguments
                << "])>>&> item(value);\n"
                << indent << "            rosetta_rb::assign_field((*native)[" << arguments << "], item.get());\n"
                << indent << "            return value;\n";
        } else {
            std::string body;
            if (kind == "method") {
                const auto direct = "native->" + function.name + "(" + arguments + ")";
                if (function.is_virtual && policy_.directors != "none") {
                    if (function.is_pure)
                        body = "if (rosetta_rb::is_director(self)) rosetta_rb::pure_virtual(" + quote(owner + "::" + function.name) + "); return " + direct + ";";
                    else
                        body = "if (rosetta_rb::is_director(self)) return native->" + owner + "::" + function.name + "(" + arguments + "); return " + direct + ";";
                } else body = "return " + direct + ";";
            } else {
                body = "return ::" + function.qualified_name() + "(" + arguments + ");";
            }
            out << indent << "            VALUE result = rosetta_rb::call([&]() -> decltype(auto) { " << body << " }, "
                << (parent ? "self" : "Qnil") << ", " << policy << ");\n";
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
        for (const auto& [cpp, ruby] : map) {
            const auto member = error_member(ruby);
            if (member.empty()) throw BindingError("policy exceptions: unsupported Ruby exception " + ruby);
            catches += "        } catch (const " + cpp + "& error) { rosetta_rb::fail(rosetta_rb::or_default(rosetta_rb::errors()." + member +
                ", rb_eRuntimeError), error.what());\n";
        }
        return {"        try {\n", catches + "        }\n"};
    }
    return {};
}

void Generator::emit_group(std::ostream& out, Group& group, const std::string& kind) {
    group.symbol = symbol("rosetta_function_");
    const bool single = group.overloads.size() == 1;
    const bool setitem = kind == "setitem";
    const auto handlers = exception_handlers(group.overloads.front().function->qualified_name());
    out << "VALUE " << group.symbol << "(int argc, VALUE* argv, VALUE self) {\n"
        << "    return rosetta_rb::guard([&]() -> VALUE {\n        (void)self;\n";
    if (setitem) {
        // obj[index...] = value: the value is the last positional argument.
        out << "        if (argc < 1) rosetta_rb::fail(rb_eArgError, \"[]= needs a value\");\n"
            << "        const VALUE value = argv[argc - 1];\n"
            << "        rosetta_rb::Args args(argc - 1, argv);\n";
    } else out << "        rosetta_rb::Args args(argc, argv);\n";
    out << handlers.first;
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads) {
        if (setitem) emit_overload(out, overload, "setitem", "            ");
        else emit_overload(out, overload, overload.extension && !overload.receives_self ? "function" : overload.extension ? "function" : kind, "            ");
    }
    out << "        }\n";
    if (group.name == "==") out << "        return Qfalse;\n";
    else {
        std::string signatures;
        for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
        out << "        rosetta_rb::no_overload(" << quote(group.name) << ", " << quote(signatures) << ", args);\n";
    }
    out << handlers.second << "    });\n}\n";
}

void Generator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor_symbol = plan.symbol + "_initialize";
    const auto owner = plan.record->qualified_name();
    const bool single = plan.constructors.size() == 1;
    const auto handlers = exception_handlers(owner + "::" + plan.record->name);
    out << "VALUE " << plan.constructor_symbol << "(int argc, VALUE* argv, VALUE self) {\n"
        << "    return rosetta_rb::guard([&]() -> VALUE {\n"
        << "        rosetta_rb::Args args(argc, argv);\n" << handlers.first
        << "        if (!rosetta_rb::is_instance_object(self)) rosetta_rb::fail(rb_eTypeError, \"not a native object\");\n"
        << "        auto* object = rosetta_rb::instance(self);\n"
        << "        if (object->ptr) rosetta_rb::fail(rb_eRuntimeError, \"native object already initialized\");\n";
    if (plan.director)
        out << "        const bool director = rb_obj_class(self) != rosetta_rb::Info<" << owner << ">::value.klass;\n";
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", "            ");
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, plan.qualname + ".new") + "\n";
    out << "        }\n        rosetta_rb::no_overload(" << quote(plan.qualname + ".new") << ", " << quote(signatures) << ", args);\n"
        << handlers.second << "    });\n}\n";
}

void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << plan.symbol << "_director final : " << owner << ", rosetta_rb::Director {\n"
        << "    template<class... A> explicit " << plan.symbol << "_director(VALUE, A&&... arguments) : " << owner
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
        const auto ruby = method_ruby_name(*method);
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
        out << "        if (rosetta_rb::overridden(rosetta_self, " << quote(ruby) << ")) {\n"
            << "            static const ID name = rb_intern(" << quote(ruby) << ");\n"
            << "            return rosetta_rb::invoke_ruby<" << result << types << ">(rosetta_self, name" << arguments << ");\n"
            << "        }\n";
        if (is_pure) out << "        rosetta_rb::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (...) { rosetta_rb::unraisable(" << quote(owner + "::" + method->name) << "); }\n";
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
        // Any exported ancestor of owner.
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
            group.name = renamed_method(owner + "::" + name, method_safe(snake(name)));
            if (!ruby_method_name(group.name)) throw BindingError("policy extend name is not a Ruby method name: " + name);
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
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    // (The frontend currently rejects free operator functions, so this is dormant.)
    // Free operators whose first parameter is an exported class become its methods; a stream
    // insertion operator<<(std::ostream&, const T&) becomes T#to_s.
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
        const auto ruby = function.parameters.empty() ? std::string() : operator_name(function.name, function.parameters.size() - 1);
        if (!bound(first) || ruby.empty()) { reject(function.qualified_name(), "free operator without an exported class operand"); continue; }
        auto why = function_problem(function, true);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        auto& groups = extensions[first.declaration];
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.name == ruby; });
        if (found == groups.end()) { groups.push_back({ruby, {}, false, {}, {}, {}}); found = groups.end() - 1; }
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
                if (found == record.methods.end()) throw BindingError("unknown Ruby policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Ruby policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown Ruby policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Ruby policy field is not public: " + owner + "::" + requested);
            }
        }
        if (constructors && record.public_destructor) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || (constructor.access != "public" && !(plan.director && constructor.access == "protected"))) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                // Copy constructors duplicate dup/clone for Ruby callers.
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::lvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner && record.constructors.size() > 1) continue;
                auto why = function_problem(constructor, false);
                if (!why.empty()) { reject(owner + "::" + record.name, why); continue; }
                plan.constructors.push_back({&constructor, owner});
            }
        }
        std::map<std::string, std::size_t> group_index;
        const auto add = [&](const std::string& ruby, const Overload& overload, bool is_static) {
            const auto found = group_index.find(ruby);
            if (found == group_index.end()) {
                group_index[ruby] = plan.methods.size();
                plan.methods.push_back({ruby, {overload}, is_static, {}, {}, {}});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a Ruby name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        std::vector<Overload> setitems;
        // A predicate name (is_empty -> empty?) yields to a method already spelled that way (empty).
        std::map<std::string, std::set<std::string>> spellings;
        for (const auto& method : record.methods)
            if (method.access == "public" && method.name.rfind("operator", 0) != 0) spellings[method_ruby_name(method)].insert(method.name);
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            std::string ruby;
            if (method.name.rfind("operator", 0) == 0 && (method.name.size() == 8 || !word(method.name[8]))) {
                ruby = operator_name(method.name, method.parameters.size());
                if (ruby.empty()) continue; // assignment, compound, conversion and other operators stay C++-only
                if (ruby == "[]" && method.parameters.size() != 1) continue;
                ruby = renamed_method(method.qualified_name(), ruby);
            } else if (method.name.rfind("operator", 0) == 0 && method.name.size() > 8 && method.name[8] == ' ') {
                continue; // conversion operators
            } else {
                ruby = method_ruby_name(method);
                if (spellings[ruby].size() > 1 && ruby.back() == '?' && !renamed_raw(method.qualified_name()) &&
                    snake(method.name).rfind("is_", 0) == 0)
                    ruby = method_safe(snake(method.name));
            }
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(ruby, {&method, owner}, method.is_static);
            // obj[i] = v through a mutable reference result.
            if (ruby == "[]" && method.result.kind == TypeKind::lvalue_reference && !referent(method.result).is_const &&
                type_problem(referent(method.result), Use::parameter).empty() && !method.is_const)
                setitems.push_back({&method, owner});
        }
        for (auto& group : extensions[owner])
            for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        if (!setitems.empty()) plan.methods.push_back({"[]=", setitems, false, {}, {}, {}});
        std::set<std::string> members;
        for (const auto& group : plan.methods) members.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported Ruby field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported Ruby field type", 0) == 0 ? why : "unsupported Ruby field type: " + why); continue; }
            const auto ruby = renamed_method(qualified, method_safe(snake(field.name)));
            if (!members.insert(ruby).second) throw BindingError("reserved or duplicate Ruby member: " + plan.qualname + "#" + ruby);
            FieldPlan field_plan{&field, ruby, symbol("rosetta_field_"), false};
            field_plan.writable = !field.type.is_const && !readonly(qualified, owner) && field.type.kind != TypeKind::lvalue_reference;
            if (field_plan.writable) members.insert(ruby + "=");
            plan.fields.push_back(field_plan);
        }
        // Ruby aliases: set_x(v) also as x=, to_string as to_s.
        for (auto& group : plan.methods) {
            if (group.is_static || is_operator_name(group.name)) continue;
            const auto& function = *group.overloads.front().function;
            if (renamed_raw(function.qualified_name())) continue;
            const bool one_argument = std::all_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                return !o.receives_self && o.function->parameters.size() == 1;
            });
            if (group.name.rfind("set_", 0) == 0 && group.name.size() > 4 && one_argument) {
                const auto setter = group.name.substr(4) + "=";
                if (ruby_method_name(setter) && !members.count(setter)) { group.aliases.push_back(setter); members.insert(setter); }
            }
            if (group.name.rfind("get_", 0) == 0 && group.name.size() > 4 && group.overloads.size() == 1 && function.parameters.empty() &&
                !group.overloads.front().receives_self) {
                const auto getter = method_safe(group.name.substr(4));
                if (ruby_method_name(getter) && !members.count(getter)) { group.aliases.push_back(getter); members.insert(getter); }
            }
            // to_string() also when its parameters all have defaults (to_string(max_rows = 10)).
            const bool nullary_string = std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                return !o.receives_self && o.function->parameters.size() == o.function->default_arguments && is_string(o.function->result);
            });
            if (group.name == "to_string" && nullary_string && !members.count("to_s")) {
                group.aliases.push_back("to_s");
                members.insert("to_s");
            }
        }
        // Comparable through < and ==; each/Enumerable through size and [] (or at).
        if (members.count("<") && members.count("==") && !members.count("<=>")) plan.comparable = true;
        if (!members.count("each") && members.count("size")) {
            const auto size_group = std::find_if(plan.methods.begin(), plan.methods.end(), [](const Group& g) { return g.name == "size"; });
            const bool size_ok = size_group != plan.methods.end() && !size_group->is_static && size_group->overloads.size() == 1 &&
                size_group->overloads[0].function->parameters.empty() && is_integral(size_group->overloads[0].function->result);
            for (const char* item : {"[]"}) { // at() often reaches only part of size() (see Canvas)
                const auto item_group = std::find_if(plan.methods.begin(), plan.methods.end(), [&](const Group& g) { return g.name == item; });
                if (!size_ok || item_group == plan.methods.end() || item_group->is_static) continue;
                const bool integral = std::any_of(item_group->overloads.begin(), item_group->overloads.end(), [](const Overload& o) {
                    return !o.receives_self && o.function->parameters.size() == 1 && is_integral(o.function->parameters[0]);
                });
                if (integral) { plan.each_size = "size"; plan.each_item = item; break; }
            }
        }
        if (!plan.stream.empty() && members.count("to_s")) plan.stream.clear();
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
        std::string ruby;
        if (specialization != function_template_names_.end()) ruby = renamed_method(function.qualified_name(), method_safe(snake(specialization->second)));
        else if (const auto* name = renamed_raw(function.qualified_name())) ruby = renamed_method(function.qualified_name(), *name);
        else {
            const auto base = !submodule.empty() || flat(function.namespaces) ? function.name : exported(function.qualified_name());
            Function copy = function;
            copy.name = base;
            copy.namespaces.clear();
            ruby = method_ruby_name(copy);
            if (ruby.size() > 1 && ruby.back() == '?' && !function.parameters.empty()) ruby = method_safe(snake(base));
        }
        const auto key = submodule + "." + ruby;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions_.size();
            functions_.push_back({ruby, {{&function, {}}}, false, submodule, {}, {}});
        } else functions_[found->second].overloads.push_back({&function, {}});
    }
}

// --- YARD -----------------------------------------------------------------------------------------
std::string Generator::yard_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "String, nil";
        return yard_type(pointee) + ", nil";
    }
    if (is_void(target)) return "void";
    if (!conversion(target).empty()) {
        const auto& converted = policy_.conversions.at(conversion(target));
        Type spelled;
        spelled.canonical = converted;
        const auto open = converted.find('<');
        if (open != std::string::npos) {
            const auto stem = normalize(converted.substr(0, open));
            const auto inner = trim(converted.substr(open + 1, converted.rfind('>') - open - 1));
            const auto element = [&](const std::string& text) {
                static const std::map<std::string, std::string> scalars = {{"double", "Float"}, {"float", "Float"}, {"int", "Integer"},
                    {"long", "Integer"}, {"bool", "Boolean"}, {"std::string", "String"}, {"std::size_t", "Integer"}, {"size_t", "Integer"}};
                const auto found = scalars.find(trim(text));
                return found == scalars.end() ? std::string("Object") : found->second;
            };
            if (stem == "std::vector" || stem == "std::deque" || stem == "std::list") return "Array<" + element(inner) + ">";
            if (stem == "std::set" || stem == "std::unordered_set") return "Set<" + element(inner) + ">";
        }
        static const std::map<std::string, std::string> scalars = {{"double", "Float"}, {"float", "Float"}, {"int", "Integer"},
            {"long", "Integer"}, {"bool", "Boolean"}, {"std::string", "String"}};
        const auto found = scalars.find(normalize(converted));
        return found == scalars.end() ? "Object" : found->second;
    }
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "Boolean";
        if (bare == "char") return "String";
        if (bare == "float" || bare == "double" || bare == "long double") return "Float";
        return "Integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_plans_.find(target.declaration);
        if (found != enum_plans_.end() && found->second.flags) return "Integer, Array<Symbol>";
        return "Symbol";
    }
    if (bound(target)) return classes_.at(target.declaration).qualname;
    if (bare == "std::filesystem::path") return "String, Pathname";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "nil";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? yard_type(target.arguments[i]) : std::string("Object"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "String";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray") {
            if (declaration == "std::vector" && !target.arguments.empty() && strip_cv(target.arguments[0].canonical) == "unsigned char")
                return "Array<Integer>, String";
            return "Array<" + arg(0) + ">";
        }
        if (declaration == "std::set" || declaration == "std::unordered_set") return "Set<" + arg(0) + ">";
        if (declaration == "std::map" || declaration == "std::unordered_map") return "Hash{" + arg(0) + " => " + arg(1) + "}";
        if (declaration == "std::optional") return arg(0) + ", nil";
        if (declaration == "std::complex") return "Complex";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0) + ", nil";
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "Array(";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + yard_type(target.arguments[i]);
            return result + ")";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + yard_type(target.arguments[i]);
            return result;
        }
        if (declaration == "std::function") return "Proc";
        if (declaration == "std::chrono::duration") return "Float";
    }
    return "Object";
}

// A default value as a Ruby expression, or "" when Ruby cannot spell it.
std::string Generator::ruby_default(const DefaultValue& value, const Type& type) const {
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
    if (value.kind == "null") return "nil";
    if (value.kind == "string") {
        // C++ and Ruby double-quoted literals share their common escapes; #{ must not interpolate.
        std::string text = value.value;
        for (std::size_t i = 0; (i = text.find("#{", i)) != std::string::npos; i += 3) text.replace(i, 2, "\\#{");
        return text;
    }
    if (value.kind == "char") {
        std::string text = "\"";
        for (char c : value.value) {
            if (c == '"' || c == '\\' || c == '#') text += '\\';
            text += c;
        }
        return text + "\"";
    }
    if (value.kind == "enum") {
        const auto found = enum_plans_.find(referent(type).declaration);
        if (found != enum_plans_.end() && !found->second.flags) return ":" + value.value;
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
        result += (i > first ? ", " : "") + parameter_name(function, i);
        if (i >= required) {
            const auto value = ruby_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            result += " = " + (value.empty() ? std::string("nil") : value);
        }
    }
    return result;
}

std::string Generator::yard_docs(const std::vector<Overload>& overloads, const std::string& name, const Documentation* own,
                                 const std::string& extra_note) const {
    std::vector<std::string> paragraphs, tags;
    const auto paragraph = [&](const std::string& text) { if (!trim(text).empty()) paragraphs.push_back(trim(text)); };
    std::string override_text;
    for (const auto& overload : overloads) {
        const auto found = policy_.docs.find(overload.function->qualified_name());
        if (found != policy_.docs.end()) { override_text = prose(found->second); break; }
    }
    std::vector<const Documentation*> docs;
    std::set<std::string> seen;
    if (own && !own->empty()) docs.push_back(own);
    if (override_text.empty())
        for (const auto& overload : overloads) {
            const auto& documentation = overload.function->documentation;
            if (!documentation.empty() && seen.insert(plain_documentation(documentation)).second) docs.push_back(&documentation);
        }
    else paragraph(override_text);
    for (const auto* documentation : docs) {
        paragraph(prose(documentation->brief));
        std::string section, section_title;
        for (const auto& block : documentation->details) {
            if (block.kind == "heading") { section = section_kind(block.text); section_title = trim(block.text); continue; }
            if (section == "example") {
                if (block.kind == "code") tags.push_back("@example" + (section_title.empty() || section_title == "Example" ? std::string() : " " + section_title) +
                                                        "\n" + indent_lines(block.text, "  "));
                else paragraph(block_text(block));
            } else if (section == "note") tags.push_back("@note " + tag_text(block_text(block)));
            else if (section == "see") tags.push_back("@see " + tag_text(block.text));
            else if (!section.empty() || !section_title.empty()) paragraph("**" + section_title + "**\n\n" + block_text(block));
            else paragraph(block_text(block));
            if (section.empty()) section_title.clear();
        }
        for (const auto& note : documentation->notes) {
            if (note.name == "deprecated") { tags.push_back("@deprecated " + tag_text(prose(note.text))); continue; }
            if (note.name == "since") { tags.push_back("@since " + tag_text(prose(note.text))); continue; }
            if (note.name == "todo") { tags.push_back("@todo " + tag_text(prose(note.text))); continue; }
            std::string label;
            if (note.name == "warning") label = "Warning: ";
            else if (note.name == "pre") label = "Precondition: ";
            else if (note.name == "post") label = "Postcondition: ";
            else if (note.name == "invariant") label = "Invariant: ";
            else if (note.name != "note" && note.name != "remark" && !note.name.empty()) {
                label = note.name;
                label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                label += ": ";
            }
            tags.push_back("@note " + tag_text(label + prose(note.text)));
        }
        for (const auto& target : documentation->see) tags.push_back("@see " + tag_text(target));
    }
    if (!extra_note.empty()) tags.push_back("@note " + tag_text(extra_note));
    // Parameters, returns and raises: per overload when there are several.
    const auto describe = [&](const Overload& overload, std::vector<std::string>& out, bool with_raise) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp_name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            std::string text;
            for (const auto& item : function.documentation.params) if (item.name == cpp_name) text = prose(item.text);
            if (i >= required) {
                const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
                const auto shown = ruby_default(value, function.parameters[i]);
                std::string note;
                if (!shown.empty()) note = "(default: `" + shown + "`)";
                else note = "(default: the C++ default" + (value.value.empty() ? std::string() : " `" + value.value + "`") + "; pass nothing to use it)";
                text = trim(text + " " + note);
            }
            const auto& target = referent(function.parameters[i]);
            if (target.kind == TypeKind::enumeration) {
                const auto found = enum_plans_.find(target.declaration);
                if (found != enum_plans_.end() && !found->second.flags) {
                    std::string values;
                    for (const auto& member : found->second.members) values += (values.empty() ? ":" : ", :") + member.first;
                    text = trim(text + " (one of " + values + ")");
                }
            }
            if (target.kind == TypeKind::template_instance && normalize(target.declaration) == "std::function")
                text = trim(text + " (or a block)");
            out.push_back("@param " + parameter_name(function, i) + " [" + yard_type(function.parameters[i]) + "]" +
                          (text.empty() ? "" : " " + tag_text(text)));
        }
        if (!function.is_constructor) {
            std::string text = prose(function.documentation.returns);
            for (const auto& value : function.documentation.retvals)
                text += (text.empty() ? "" : "\n") + std::string("- `") + value.name + "`" + (value.text.empty() ? "" : ": " + prose(value.text));
            if (!is_void(function.result) || !text.empty())
                out.push_back("@return [" + yard_type(function.result) + "]" + (text.empty() ? "" : " " + tag_text(text)));
        }
        if (with_raise)
            for (const auto& item : function.documentation.throws) {
                auto text = prose(item.text);
                const auto exception = ruby_exception(item.name);
                if (exception == "RuntimeError" && !item.name.empty()) text = "`" + item.name + "`" + (text.empty() ? "" : ": " + text);
                out.push_back("@raise [" + exception + "]" + (text.empty() ? "" : " " + tag_text(text)));
            }
    };
    std::vector<std::string> signature_tags;
    if (overloads.size() == 1) describe(overloads.front(), signature_tags, true);
    else {
        std::set<std::string> raised;
        for (const auto& overload : overloads) {
            std::vector<std::string> inner;
            describe(overload, inner, false);
            std::string block = "@overload " + name + "(" + stub_parameters(overload) + ")";
            for (const auto& tag : inner) block += "\n" + indent_lines(tag, "  ");
            signature_tags.push_back(block);
            for (const auto& item : overload.function->documentation.throws) {
                auto text = prose(item.text);
                const auto exception = ruby_exception(item.name);
                if (exception == "RuntimeError" && !item.name.empty()) text = "`" + item.name + "`" + (text.empty() ? "" : ": " + text);
                const auto tag = "@raise [" + exception + "]" + (text.empty() ? "" : " " + tag_text(text));
                if (raised.insert(tag).second) tags.push_back(tag);
            }
        }
    }
    // Keyword calls: with two or more defaulted parameters a keyword may skip the ones before it.
    if (overloads.size() == 1) {
        const auto& overload = overloads.front();
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        std::size_t skippable = 0;
        for (std::size_t i = std::max(required, first); i < function.parameters.size(); ++i) if (fillable(function, i)) ++skippable;
        if (function.default_arguments >= 2 && skippable == function.default_arguments) {
            std::string head;
            for (std::size_t i = first; i < required; ++i) head += parameter_name(function, i) + ", ";
            std::string block = "@overload " + name + "(" + head + "**options)\n  Defaulted parameters by keyword, in any order.";
            for (std::size_t i = std::max(required, first); i < function.parameters.size(); ++i) {
                if (i < required) continue;
                const auto cpp_name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
                std::string text;
                for (const auto& item : function.documentation.params) if (item.name == cpp_name) text = prose(item.text);
                const auto shown = ruby_default(function.default_values[i], function.parameters[i]);
                block += "\n  @option options [" + yard_type(function.parameters[i]) + "] :" + parameter_name(function, i) +
                    (shown.empty() ? "" : " (" + shown + ")") + (text.empty() ? "" : " " + tag_text(text));
            }
            for (const auto& tag : signature_tags)
                if (tag.rfind("@return", 0) == 0) block += "\n  " + tag;
            // Editors list only @overload signatures once there is one, so the positional
            // signature becomes an @overload too; @raise stays shared.
            std::string positional = "@overload " + name + "(" + stub_parameters(overload) + ")";
            std::vector<std::string> shared;
            for (const auto& tag : signature_tags) {
                if (tag.rfind("@raise", 0) == 0) shared.push_back(tag);
                else positional += "\n" + indent_lines(tag, "  ");
            }
            signature_tags = {positional, block};
            tags.insert(tags.begin(), shared.begin(), shared.end());
        }
    }
    std::string out;
    for (const auto& text : paragraphs) out += (out.empty() ? "" : "\n\n") + text;
    std::string tag_block;
    for (const auto& tag : signature_tags) tag_block += (tag_block.empty() ? "" : "\n") + tag;
    for (const auto& tag : tags) tag_block += (tag_block.empty() ? "" : "\n") + tag;
    if (!tag_block.empty()) out += (out.empty() ? "" : "\n\n") + tag_block;
    return out;
}

std::string comment(const std::string& text, const std::string& indent) {
    if (trim(text).empty()) return {};
    std::string out;
    for (const auto& line : lines_of(text)) out += indent + (line.empty() ? "#" : "# " + line) + "\n";
    return out;
}
std::string documentation_text(const Documentation& documentation) {
    std::string text = prose(documentation.brief);
    for (const auto& block : documentation.details)
        if (block.kind != "heading") text += (text.empty() ? "" : "\n\n") + block_text(block);
    for (const auto& note : documentation.notes) text += (text.empty() ? "" : "\n\n") + prose(note.text);
    return text;
}

void Generator::yard_group(std::ostream& out, const Group& group, const std::string& indent, bool singleton, const Documentation* own) const {
    std::string note;
    const bool block = std::any_of(group.overloads.begin(), group.overloads.end(), [&](const Overload& o) { return callback_index(o) >= 0; });
    if (block) note = "A std::function parameter also takes a block.";
    const auto docs = yard_docs(group.overloads, group.name, own, note);
    out << comment(docs, indent);
    const auto prefix = singleton ? "self." : "";
    if (group.name == "[]=" ) out << indent << "def []=(*args); end\n";
    else if (group.overloads.size() == 1) {
        const auto parameters = stub_parameters(group.overloads.front());
        out << indent << "def " << prefix << group.name << (parameters.empty() ? "" : "(" + parameters + ")") << "; end\n";
    } else out << indent << "def " << prefix << group.name << "(*args); end\n";
    for (const auto& alias : group.aliases) {
        out << indent << "# Alias of #" << group.name << ".\n";
        if (alias.back() == '=') out << indent << "# @param value [Object]\n";
        out << indent << "alias_method :" << alias << ", :" << group.name << "\n";
    }
}

void Generator::yard_enum(std::ostream& out, const EnumPlan& plan, const std::string& indent) const {
    const auto& enumeration = *plan.enumeration;
    std::string text = documentation_text(enumeration.documentation);
    std::string values;
    for (const auto& member : plan.members) values += (values.empty() ? ":" : ", :") + member.first;
    text += (text.empty() ? "" : "\n\n") + std::string(plan.flags
        ? "Flags: values are Integers (combine with |); arguments also take a Symbol or an Array of Symbols (" + values + ")."
        : "Values are the Symbols " + values + "; arguments also take the enumerator's Integer value.");
    out << comment(text, indent) << indent << "module " << plan.ruby << "\n";
    for (std::size_t i = 0; i < plan.members.size(); ++i) {
        const auto& [name, constant] = plan.members[i];
        if (constant.empty()) continue;
        const auto value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string("0");
        out << indent << "  # The enumerator `" << name << "` (" << value << ").\n";
        out << indent << "  " << constant << " = " << (plan.flags ? value : ":" + name) << "\n";
    }
    out << indent << "  # The enumerators and their Integer values.\n" << indent << "  # @return [Hash{Symbol => Integer}]\n"
        << indent << "  def self.to_h; end\n"
        << indent << "  # The enumerator names.\n" << indent << "  # @return [Array<Symbol>]\n" << indent << "  def self.values; end\n"
        << indent << "  # The Integer value of an enumerator.\n" << indent << "  # @param name [Symbol]\n" << indent << "  # @return [Integer]\n"
        << indent << "  def self.to_i(name); end\n"
        << indent << "  # The enumerator with an Integer value.\n" << indent << "  # @param value [Integer]\n" << indent << "  # @return [Symbol, nil]\n"
        << indent << "  def self.from_i(value); end\n";
    out << indent << "end\n";
}

void Generator::yard_class(std::ostream& out, const ClassPlan& plan, const std::string& indent) const {
    const auto& record = *plan.record;
    const auto found = policy_.docs.find(record.qualified_name());
    std::string text = found != policy_.docs.end() ? prose(found->second) : documentation_text(record.documentation);
    if (plan.director) {
        std::string virtual_names;
        std::map<std::string, bool> pure;
        for (const auto* method : virtuals(record, pure)) {
            const auto name = method_ruby_name(*method);
            if (virtual_names.find("`" + name + "`") == std::string::npos) virtual_names += (virtual_names.empty() ? "`" : ", `") + name + "`";
        }
        text += (text.empty() ? "" : "\n\n") + std::string("Subclass it in Ruby and override ") + virtual_names +
            ": C++ calls the Ruby methods (`super` runs the C++ implementation). A subclass `initialize` must call `super`.";
    } else if (record.abstract) text += (text.empty() ? "" : "\n\n") + std::string("Abstract: instances come from C++.");
    if (plan.bases.size() > 1) {
        std::string others;
        for (std::size_t i = 1; i < plan.bases.size(); ++i) others += (i > 1 ? ", " : "") + classes_.at(plan.bases[i]).qualname;
        text += (text.empty() ? "" : "\n\n") + std::string("Also has the methods of ") + others + " (C++ multiple inheritance).";
    }
    std::string superclass = plan.bases.empty() ? std::string("NativeObject") : classes_.at(plan.bases.front()).qualname;
    out << comment(text, indent) << indent << "class " << plan.ruby << " < " << superclass << "\n";
    const auto inner = indent + "  ";
    if (!plan.each_item.empty()) out << inner << "include ::Enumerable\n";
    if (plan.comparable) out << inner << "include ::Comparable\n";
    // Constants.
    for (const auto& constant : constants_) {
        if (constant.owner != record.qualified_name()) continue;
        out << comment(documentation_text(*constant.documentation), inner) << inner << "# @return [" << yard_type(constant.type) << "]\n"
            << inner << constant.ruby << " = nil\n";
    }
    // Fields.
    for (const auto& field : plan.fields) {
        std::string doc = documentation_text(field.field->documentation);
        doc += (doc.empty() ? "" : "\n") + std::string("@return [") + yard_type(field.field->type) + "]";
        out << comment(doc, inner) << inner << (field.writable ? "attr_accessor :" : "attr_reader :") << field.ruby << "\n";
    }
    // Constructors.
    if (!plan.constructors.empty()) {
        Group group{"initialize", plan.constructors, false, {}, {}, {}};
        yard_group(out, group, inner, false);
    }
    for (const auto& group : plan.methods) yard_group(out, group, inner, group.is_static);
    if (!plan.stream.empty()) out << inner << "# The C++ stream output (operator<<).\n" << inner << "# @return [String]\n" << inner << "def to_s; end\n";
    if (!plan.each_item.empty())
        out << inner << "# Yields each element (`" << plan.each_item << "` for every index below `size`).\n" << inner
            << "# @yieldparam item [Object]\n" << inner << "# @return [self, Enumerator]\n" << inner << "def each; end\n";
    if (plan.comparable)
        out << inner << "# Compares through `<` and `==`.\n" << inner << "# @param other [" << plan.qualname << "]\n" << inner
            << "# @return [Integer, nil]\n" << inner << "def <=>(other); end\n";
    for (const auto& [qualified, enum_plan] : enum_plans_)
        if (enum_plan.enumeration->owner == record.qualified_name()) yard_enum(out, enum_plan, inner);
    for (const auto& owner : class_order_)
        if (classes_.at(owner).parent == record.qualified_name()) yard_class(out, classes_.at(owner), inner);
    out << indent << "end\n";
}

std::string Generator::yard_file() const {
    std::ostringstream out;
    out << "# frozen_string_literal: true\n# Generated by Rosetta from " << std::filesystem::path(module_.header).filename().string()
        << ": the " << ruby_module_ << " Ruby API.\n#\n"
        << "# Loading this file loads the native extension. The rest of the file documents the extension's\n"
        << "# modules, classes and methods for YARD and editors; it sits under `if nil`, so Ruby never runs\n"
        << "# it and the native methods stay in place (YARD reads both branches of a condition it cannot\n"
        << "# evaluate). Text is Markdown: run `yard doc --markup markdown`.\n"
        << "require_relative " << quote(options_.native) << "\n\n"
        << "if nil # documentation only\n";
    // Open each level of a nested module path.
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto colon = ruby_module_.find("::", start);
        parts.push_back(ruby_module_.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos) break;
        start = colon + 2;
    }
    std::string indent;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i + 1 == parts.size())
            out << indent << "# The " << ruby_module_ << " bindings of the C++ library (" << std::filesystem::path(module_.header).filename().string()
                << "), generated by Rosetta.\n" << indent << "#\n"
                << indent << "# Methods take their arguments by position or by keyword (keywords may skip a defaulted\n"
                << indent << "# parameter); a block fills a callback parameter. C++ exceptions raise classes that include\n"
                << indent << "# {" << ruby_module_ << "::Error}.\n";
        out << indent << "module " << parts[i] << "\n";
        indent += "  ";
    }
    // Runtime classes.
    out << indent << "# Base class of every wrapped C++ object.\n" << indent << "class NativeObject\n"
        << indent << "  # Destroys the C++ object now (when Ruby owns it); later calls raise RuntimeError.\n"
        << indent << "  # @return [nil]\n" << indent << "  def close; end\n"
        << indent << "  # Whether the C++ object was closed, moved to C++ or never initialized.\n" << indent << "  # @return [Boolean]\n"
        << indent << "  def closed?; end\n"
        << indent << "  # Whether the C++ object is still usable.\n" << indent << "  # @return [Boolean]\n" << indent << "  def alive?; end\n"
        << indent << "  # Whether Ruby owns (and finally destroys) the C++ object.\n" << indent << "  # @return [Boolean]\n"
        << indent << "  def owned?; end\n" << indent << "end\n\n"
        << indent << "# A C++ std::function returned to Ruby.\n" << indent << "class NativeCallable\n"
        << indent << "  # Calls the C++ function.\n" << indent << "  # @return [Object]\n" << indent << "  def call(*args); end\n"
        << indent << "  # @return [Proc]\n" << indent << "  def to_proc; end\n"
        << indent << "  # @return [Integer]\n" << indent << "  def arity; end\n" << indent << "end\n\n"
        << indent << "# Included by every exception raised for a C++ exception.\n" << indent << "module Error\n"
        << indent << "  # The C++ exception type, such as \"std::invalid_argument\".\n" << indent << "  # @return [String, nil]\n"
        << indent << "  def cpp_class; end\n" << indent << "end\n";
    for (const auto& [name, base] : std::vector<std::pair<std::string, std::string>>{{"ArgumentError", "std::invalid_argument, std::domain_error, std::length_error"},
             {"IndexError", "std::out_of_range"}, {"KeyError", "policy mappings"}, {"RangeError", "std::overflow_error, std::range_error"},
             {"TypeError", "std::bad_cast, std::bad_variant_access"}, {"NotImplementedError", "pure virtual methods a Ruby subclass did not implement"},
             {"RuntimeError", "other std::exception types"}, {"ZeroDivisionError", "policy mappings"}, {"IOError", "std::filesystem::filesystem_error, std::ios_base::failure"}})
        out << indent << "# Raised for " << base << ".\n" << indent << "class " << name << " < ::" << name << "\n" << indent << "  include Error\n"
            << indent << "end\n";
    out << "\n";
    for (const auto& constant : constants_) {
        if (!constant.owner.empty()) continue;
        out << comment(documentation_text(*constant.documentation), indent) << indent << "# @return [" << yard_type(constant.type) << "]\n"
            << indent << constant.ruby << " = nil\n";
    }
    for (const auto& group : functions_) {
        if (!group.module.empty()) continue;
        yard_group(out, group, indent, true);
    }
    for (const auto& [qualified, plan] : enum_plans_) if (plan.enumeration->owner.empty()) yard_enum(out, plan, indent);
    for (const auto& owner : class_order_) if (classes_.at(owner).parent.empty()) { out << "\n"; yard_class(out, classes_.at(owner), indent); }
    // Policy submodules.
    std::map<std::string, std::vector<const Group*>> submodules;
    for (const auto& group : functions_) if (!group.module.empty()) submodules[group.module].push_back(&group);
    for (const auto& [module, groups] : submodules) {
        const auto path = submodule_path(module);
        out << "\n" << indent << "# Functions of the C++ namespace mapped to the " << module << " module.\n"
            << indent << "module " << path << "\n";
        for (const auto* group : groups) yard_group(out, *group, indent + "  ", true);
        out << indent << "end\n";
    }
    for (std::size_t i = parts.size(); i-- > 0;) {
        indent.resize(indent.size() - 2);
        out << indent << "end\n";
    }
    out << "end\n";
    return out.str();
}

GeneratedRubyFullSource Generator::run() {
    ruby_module_ = options_.ruby_module.empty() ? camel(name_) : options_.ruby_module;
    for (std::size_t start = 0;;) {
        const auto colon = ruby_module_.find("::", start);
        if (!ruby_constant_name(ruby_module_.substr(start, colon == std::string::npos ? std::string::npos : colon - start)))
            throw BindingError("Ruby module name must be a constant path (Name or Outer::Name): " + ruby_module_);
        if (colon == std::string::npos) break;
        start = colon + 2;
    }
    if (options_.native.empty()) options_.native = name_ + "/" + name_;
    auto init_name = options_.native.substr(options_.native.rfind('/') == std::string::npos ? 0 : options_.native.rfind('/') + 1);
    if (init_name.empty() || !std::all_of(init_name.begin(), init_name.end(), word) || digit(init_name[0]))
        throw BindingError("Ruby native extension name must be an identifier: " + init_name);
    for (const auto& rule : policy_.enum_flags)
        if (std::none_of(module_.enumerations.begin(), module_.enumerations.end(), [&](const Enumeration& e) { return e.qualified_name() == rule.first; }))
            throw BindingError("enum flag policy refers to an unknown enum: " + rule.first);
    GeneratedRubyFullSource result;
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
    // Enumerations: modules of Symbol (or, for flags, Integer) constants.
    for (const auto& [qualified, enumeration] : enums_) {
        EnumPlan plan;
        plan.enumeration = enumeration;
        const bool nested = !enumeration->owner.empty();
        plan.ruby = renamed_constant(qualified, constant_safe(camel(nested || flat(enumeration->namespaces) ? enumeration->name : exported(qualified))));
        plan.qualname = nested ? classes_.at(enumeration->owner).qualname + "::" + plan.ruby : plan.ruby;
        plan.flags = policy_.enum_flags.count(qualified) != 0;
        std::set<std::string> constants;
        for (const auto& enumerator : enumeration->enumerators) {
            auto constant = renamed_constant(qualified + "::" + enumerator, constant_name(enumerator));
            if (!constants.insert(constant).second) { reject(qualified + "::" + enumerator, "enumerator constant " + constant + " is not unique"); constant.clear(); }
            plan.members.emplace_back(enumerator, constant);
        }
        enum_plans_[qualified] = plan;
    }
    plan_members();
    plan_functions();

    // Module-level names must be unique.
    std::map<std::string, std::string> names;
    const auto reserve = [&](const std::string& ruby, const std::string& what) {
        const auto inserted = names.emplace(ruby, what);
        if (!inserted.second) throw BindingError("Ruby export name collision: " + ruby + " (" + inserted.first->second + " and " + what + ")");
    };
    std::map<std::string, std::set<std::string>> submodule_names;
    for (const auto& group : functions_) {
        if (group.module.empty()) { reserve(group.name, "function " + group.overloads.front().function->qualified_name()); continue; }
        if (!submodule_names.count(group.module)) reserve(submodule_path(group.module), "submodule " + group.module);
        submodule_names[group.module].insert(group.name);
    }
    for (const auto& name : reserved_constants()) reserve(name, "the Ruby runtime");
    for (const auto& owner : class_order_) if (classes_.at(owner).parent.empty()) reserve(classes_.at(owner).ruby, "class " + owner);
    for (const auto& [qualified, plan] : enum_plans_) {
        if (!plan.enumeration->owner.empty()) continue;
        if (names.count(plan.ruby)) { reject(qualified, "enum name collides with another export: " + plan.ruby); continue; }
        reserve(plan.ruby, "enum " + qualified);
    }
    // Constants: namespace-scope variables and static members.
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to Ruby"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto ruby = renamed_constant(qualified, constant_name(flat(variable.namespaces) ? variable.name : exported(qualified)));
        if (names.count(ruby)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(ruby, "constant " + qualified);
        constants_.push_back({ruby, {}, "::" + qualified, &variable.documentation, variable.type});
    }
    for (const auto& owner : class_order_) {
        const auto& record = *classes_.at(owner).record;
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to Ruby"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            constants_.push_back({renamed_constant(qualified, constant_name(variable.name)), owner, qualified, &variable.documentation, variable.type});
        }
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Ruby";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Wrapper code is collected in units (a class, or a module function) that a split build
    // spreads over several sources; `out` is the prelude every source repeats.
    struct Unit { std::string code, declaration; };
    std::vector<Unit> units;
    std::ostringstream out, init;
    std::string module_namespace = "rosetta_rb_module_" + init_name;
    // The bound header comes before ruby.h, whose function-like macros (StringValue, Check_Type,
    // ...) would otherwise rewrite same-named C++ declarations in it.
    out << "// Generated by Rosetta: MRI Ruby native extension (--ruby-api full).\n";
    for (const auto* header : {"array", "chrono", "climits", "cmath", "complex", "cstdio", "cstdlib", "cstring", "deque", "filesystem",
                               "functional", "ios", "iostream", "limits", "list", "map", "memory", "mutex", "new", "optional", "set", "sstream",
                               "stdexcept", "string", "string_view", "tuple", "typeindex", "typeinfo", "type_traits", "unordered_map",
                               "unordered_set", "utility", "valarray", "variant", "vector", "cxxabi.h"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n';
    out << "#include <ruby.h>\n#include <ruby/encoding.h>\n"
        << "#include <ruby/version.h>\n#if RUBY_API_VERSION_CODE < 30100\n#error Rosetta requires Ruby 3.1 or newer\n#endif\n";
    out << "#define ROSETTA_RB_MODULE " << module_namespace << '\n';
    for (const auto* part : detail::ruby_full_runtime_parts) out << part;
    out << "namespace ROSETTA_RB_MODULE { namespace rosetta_rb {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(VALUE value, bool strict) { return Value<" << to << ">::check(value, strict); }\n"
            << "    static " << from << " load(VALUE value) { return " << from << "(Value<" << to << ">::load(value)); }\n"
            << "    static VALUE cast(const " << from << "& value) { return Value<" << to << ">::cast(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\nnamespace ROSETTA_RB_MODULE {\n";
    for (const auto& owner : class_order_) if (classes_.at(owner).director) emit_director(out, classes_.at(owner));
    out << "}\n";

    const auto method_cast = [](const std::string& symbol) { return "RUBY_METHOD_FUNC(" + symbol + ")"; };
    for (auto& group : functions_) {
        std::ostringstream unit;
        emit_group(unit, group, "function");
        units.push_back({unit.str(), "VALUE " + group.symbol + "(int, VALUE*, VALUE);\n"});
    }
    init << "        VALUE module = " << (ruby_module_.find("::") == std::string::npos ? "rb_define_module(" + quote(ruby_module_) + ")"
                                                                                      : std::string("rb_cObject")) << ";\n";
    if (ruby_module_.find("::") != std::string::npos)
        for (std::size_t start = 0;;) {
            const auto colon = ruby_module_.find("::", start);
            init << "        module = rb_define_module_under(module, " << quote(ruby_module_.substr(start, colon == std::string::npos ? std::string::npos : colon - start)) << ");\n";
            if (colon == std::string::npos) break;
            start = colon + 2;
        }
    init << "        rosetta_rb::initialize_runtime(module);\n";
    // Classes (bases and outer classes first), then their methods.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> defined; // class -> (ruby name, symbol) for secondary bases
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto superclass = plan.bases.empty() ? std::string("rosetta_rb::root_class()") : "rosetta_rb::Info<" + plan.bases.front() + ">::value.klass";
        const auto under = plan.parent.empty() ? std::string("module") : "rosetta_rb::Info<" + plan.parent + ">::value.klass";
        init << "        {\n            VALUE klass = rb_define_class_under(" << under << ", " << quote(plan.ruby) << ", " << superclass << ");\n"
             << "            auto& info = rosetta_rb::Info<" << owner << ">::value;\n"
             << "            rosetta_rb::register_type<" << owner << ">(info, " << quote(ruby_module_ + "::" + plan.qualname) << ", klass);\n"
             << "            info.bases.clear();\n";
        for (const auto& base : plan.bases) init << "            rosetta_rb::add_base<" << owner << ", " << base << ">(info);\n";
        init << "        }\n";
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::ostringstream unit, registration;
        std::string declarations;
        const auto klass = "rosetta_rb::Info<" + owner + ">::value.klass";
        auto& own = defined[owner];
        for (auto& group : plan.methods) {
            emit_group(unit, group, group.name == "[]=" ? "setitem" : group.is_static ? "function" : "method");
            declarations += "VALUE " + group.symbol + "(int, VALUE*, VALUE);\n";
            if (group.is_static) registration << "            rb_define_singleton_method(klass, " << quote(group.name) << ", " << method_cast(group.symbol) << ", -1);\n";
            else {
                registration << "            rb_define_method(klass, " << quote(group.name) << ", " << method_cast(group.symbol) << ", -1);\n";
                own.emplace_back(group.name, group.symbol);
            }
            for (const auto& alias : group.aliases)
                registration << "            rb_define_method(klass, " << quote(alias) << ", " << method_cast(group.symbol) << ", -1);\n";
        }
        for (auto& field : plan.fields) {
            const auto& name = field.field->name;
            unit << "VALUE " << field.symbol << "_get(VALUE self) {\n    return rosetta_rb::guard([&]() -> VALUE {\n"
                 << "        auto* native = rosetta_rb::native<" << owner << ">(self);\n"
                 << "        return rosetta_rb::result<decltype((native->" << name << "))>(native->" << name << ", self, rosetta_rb::Return::"
                 << (copy_type(field.field->type) ? "copy" : "automatic") << ");\n    });\n}\n";
            declarations += "VALUE " + field.symbol + "_get(VALUE);\n";
            registration << "            rb_define_method(klass, " << quote(field.ruby) << ", " << method_cast(field.symbol + "_get") << ", 0);\n";
            own.emplace_back(field.ruby, "");
            if (field.writable) {
                unit << "VALUE " << field.symbol << "_set(VALUE self, VALUE value) {\n    return rosetta_rb::guard([&]() -> VALUE {\n"
                     << "        rosetta_rb::writable(self);\n"
                     << "        rosetta_rb::Param<const std::remove_cv_t<std::remove_reference_t<decltype(std::declval<" << owner << "&>()." << name
                     << ")>>&> converted(value);\n"
                     << "        auto* native = rosetta_rb::native<" << owner << ">(self);\n"
                     << "        rosetta_rb::assign_field(native->" << name << ", converted.get());\n        return value;\n    });\n}\n";
                declarations += "VALUE " + field.symbol + "_set(VALUE, VALUE);\n";
                registration << "            if (std::is_copy_assignable_v<std::remove_cv_t<decltype(std::declval<" << owner << "&>()." << name << ")>>)\n"
                             << "                rb_define_method(klass, " << quote(field.ruby + "=") << ", " << method_cast(field.symbol + "_set") << ", 1);\n";
            }
        }
        if (!plan.constructors.empty()) {
            emit_constructor(unit, plan);
            declarations += "VALUE " + plan.constructor_symbol + "(int, VALUE*, VALUE);\n";
            registration << "            rb_define_method(klass, \"initialize\", " << method_cast(plan.constructor_symbol) << ", -1);\n";
        } else
            registration << "            rb_define_method(klass, \"initialize\", RUBY_METHOD_FUNC(rosetta_rb::uninitialized_method), -1);\n";
        // Methods of secondary bases (Ruby has single inheritance).
        for (std::size_t i = 1; i < plan.bases.size(); ++i) {
            std::set<std::string> own_names;
            for (const auto& item : own) own_names.insert(item.first);
            std::vector<std::string> stack{plan.bases[i]};
            while (!stack.empty()) {
                const auto base = stack.back();
                stack.pop_back();
                for (const auto& [ruby, symbol_name] : defined[base])
                    if (!symbol_name.empty() && own_names.insert(ruby).second)
                        registration << "            rb_define_method(klass, " << quote(ruby) << ", " << method_cast(symbol_name) << ", -1);\n";
                for (const auto& more : classes_.at(base).bases) stack.push_back(more);
            }
        }
        if (!plan.stream.empty()) registration << "            rb_define_method(klass, \"to_s\", RUBY_METHOD_FUNC(rosetta_rb::stream_method<" << owner << ">), 0);\n";
        if (plan.comparable) registration << "            rb_define_method(klass, \"<=>\", RUBY_METHOD_FUNC(rosetta_rb::spaceship_method), 1);\n"
                                          << "            rb_include_module(klass, rb_mComparable);\n";
        if (!plan.each_item.empty()) {
            unit << "VALUE " << plan.symbol << "_each(VALUE self) { return rosetta_rb::each_by_index(self, " << quote(plan.each_size) << ", "
                 << quote(plan.each_item) << "); }\n";
            declarations += "VALUE " + plan.symbol + "_each(VALUE);\n";
            registration << "            rb_define_method(klass, \"each\", " << method_cast(plan.symbol + "_each") << ", 0);\n"
                         << "            rb_include_module(klass, rb_mEnumerable);\n";
        }
        if (std::any_of(plan.methods.begin(), plan.methods.end(), [](const Group& g) { return g.name == "=="; }) &&
            std::none_of(plan.methods.begin(), plan.methods.end(), [](const Group& g) { return g.name == "hash" || g.name == "eql?"; }))
            registration << "            rosetta_rb::define_hash<" << owner << ">(klass);\n";
        for (const auto& constant : constants_) {
            if (constant.owner != owner) continue;
            registration << "            rb_define_const(klass, " << quote(constant.ruby) << ", rosetta_rb::Value<std::remove_cv_t<decltype("
                         << constant.expression << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>("
                         << constant.expression << ")));\n";
        }
        units.push_back({unit.str(), declarations});
        init << "        {\n            VALUE klass = " << klass << ";\n" << registration.str() << "        }\n";
    }
    // Enumerations.
    for (const auto& [qualified, plan] : enum_plans_) {
        const auto& enumeration = *plan.enumeration;
        const bool nested = !enumeration.owner.empty();
        if (!nested && names.count(plan.ruby) && names.at(plan.ruby) != "enum " + qualified) continue;
        const auto under = nested ? "rosetta_rb::Info<" + enumeration.owner + ">::value.klass" : std::string("module");
        init << "        {\n            VALUE enum_module = rb_define_module_under(" << under << ", " << quote(plan.ruby) << ");\n"
             << "            rosetta_rb::define_enum<" << qualified << ">(enum_module, " << (plan.flags ? "true" : "false") << ", {";
        for (std::size_t i = 0; i < plan.members.size(); ++i) {
            const auto& value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::string("0");
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            const auto number = big ? "static_cast<long long>(" + value + "ULL)" : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL";
            init << (i ? ", " : "") << "{" << quote(plan.members[i].first) << ", "
                 << (plan.members[i].second.empty() ? std::string("nullptr") : quote(plan.members[i].second)) << ", " << number << "}";
        }
        init << "});\n";
        // Unscoped top-level enumerators are also module constants, as in C++.
        if (!nested && !enumeration.scoped && flat_)
            for (const auto& [member, constant] : plan.members) {
                if (constant.empty() || names.count(constant)) continue;
                reserve(constant, "enumerator " + qualified + "::" + member);
                init << "            rb_define_const(module, " << quote(constant) << ", rb_const_get(enum_module, rb_intern(" << quote(constant) << ")));\n";
            }
        init << "        }\n";
    }
    // Module functions and constants.
    std::map<std::string, std::vector<const Group*>> submodules;
    for (const auto& group : functions_) {
        if (!group.module.empty()) { submodules[group.module].push_back(&group); continue; }
        init << "        rb_define_module_function(module, " << quote(group.name) << ", " << method_cast(group.symbol) << ", -1);\n";
    }
    for (const auto& [module, groups] : submodules) {
        init << "        {\n            VALUE submodule = module;\n";
        for (std::size_t start = 0;;) {
            const auto dot = module.find('.', start);
            init << "            submodule = rb_define_module_under(submodule, " << quote(camel(module.substr(start, dot == std::string::npos ? std::string::npos : dot - start))) << ");\n";
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        for (const auto* group : groups)
            init << "            rb_define_module_function(submodule, " << quote(group->name) << ", " << method_cast(group->symbol) << ", -1);\n";
        init << "        }\n";
    }
    for (const auto& constant : constants_) {
        if (!constant.owner.empty()) continue;
        init << "        rb_define_const(module, " << quote(constant.ruby) << ", rosetta_rb::Value<std::remove_cv_t<decltype(" << constant.expression
             << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>(" << constant.expression << ")));\n";
    }
    // Using-declarations re-export bound classes and enums under another name.
    for (const auto& reexport : module_.reexports) {
        std::string scope;
        for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
        const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
        if (reexport.kind == "function") {
            for (const auto& group : functions_) {
                if (!group.module.empty() || std::none_of(group.overloads.begin(), group.overloads.end(), [&](const Overload& o) {
                        return o.function->qualified_name() == reexport.target; })) continue;
                Function copy;
                copy.name = flat(reexport.namespaces) ? reexport.name : exported(qualified);
                const auto ruby = renamed_method(qualified, method_safe(snake(copy.name)));
                if (ruby == group.name || names.count(ruby)) break;
                reserve(ruby, "alias of " + reexport.target);
                init << "        rb_define_module_function(module, " << quote(ruby) << ", " << method_cast(group.symbol) << ", -1);\n";
                break;
            }
            continue;
        }
        const auto ruby = renamed_constant(qualified, constant_safe(camel(flat(reexport.namespaces) ? reexport.name : exported(qualified))));
        std::string target;
        if (reexport.kind == "record" || reexport.kind == "type") {
            const auto c = classes_.find(reexport.target);
            if (c != classes_.end() && c->second.parent.empty()) target = c->second.ruby;
        }
        if (target.empty() && (reexport.kind == "enum" || reexport.kind == "type")) {
            const auto e = enum_plans_.find(reexport.target);
            if (e != enum_plans_.end() && e->second.enumeration->owner.empty() && names.count(e->second.ruby) &&
                names.at(e->second.ruby) == "enum " + reexport.target) target = e->second.ruby;
        }
        if (target.empty() || ruby == target || names.count(ruby)) continue;
        reserve(ruby, "alias of " + reexport.target);
        init << "        rb_define_const(module, " << quote(ruby) << ", rb_const_get(module, rb_intern(" << quote(target) << ")));\n";
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Ruby";
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
        for (auto& part : extra) part << prelude << "namespace ROSETTA_RB_MODULE {\n";
        out << "namespace ROSETTA_RB_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) {
            extra[bin[i]] << units[i].code;
            out << units[i].declaration;
        }
        for (auto& part : extra) part << "} // namespace ROSETTA_RB_MODULE\n";
    } else {
        out << "namespace ROSETTA_RB_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "static VALUE rosetta_init(VALUE) {\n    return rosetta_rb::guard([&]() -> VALUE {\n" << init.str()
        << "        return Qnil;\n    });\n}\n} // namespace ROSETTA_RB_MODULE\n"
        << "extern \"C\" {\nRUBY_FUNC_EXPORTED void Init_" << init_name << "(void) {\n"
        << "    ROSETTA_RB_MODULE::rosetta_init(Qnil);\n}\n}\n";
    result.source = out.str();
    for (const auto& part : extra) result.parts.push_back(part.str());
    result.ruby = yard_file();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedRubyFullSource generate_ruby_full(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy,
                                           const RubyFullOptions& options) {
    if (options.parts == 0) throw BindingError("Ruby source parts must be positive");
    return Generator(module, name, skip, policy, options).run();
}
} // namespace rosetta
