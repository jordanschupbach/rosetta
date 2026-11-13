// JavaScript backend (--language javascript): a Node-API addon with JavaScript classes (native
// prototypes chained like the C++ bases), runtime overload dispatch, JavaScript subclasses of
// C++ classes through directors, callbacks, containers, and TypeScript declarations (.d.ts)
// whose JSDoc carries the C++ documentation for editors (tsserver, mep).
#include "rosetta/javascript.hpp"
#include "js_full_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
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
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool identifier(const std::string& text) {
    auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; };
    return !text.empty() && start(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return start(c) || (c >= '0' && c <= '9');
    });
}
// Words a declaration (a variable, function, class or parameter name) cannot use in a
// TypeScript declaration file (strict mode). Property names may use them.
bool reserved(const std::string& name) {
    static const std::set<std::string> words = {"break", "case", "catch", "class", "const", "continue", "debugger", "default",
        "delete", "do", "else", "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in",
        "instanceof", "new", "null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void",
        "while", "with", "implements", "interface", "let", "package", "private", "protected", "public", "static", "yield",
        "await", "arguments", "eval"};
    return words.count(name) != 0;
}
std::string declared(const std::string& name) { return reserved(name) ? name + "_" : name; }
// Member names JavaScript gives a meaning of its own.
std::string member_safe(const std::string& name) { return name == "constructor" || name == "__proto__" ? name + "_" : name; }
std::string static_safe(const std::string& name) { return name == "prototype" || name == "caller" || name == "arguments" ? name + "_" : name; }
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
bool is_operator(const std::string& name) {
    return name.rfind("operator", 0) == 0 && (name.size() == 8 || !identifier(name.substr(8)) || name == "operator bool");
}
// The method name of a C++ operator with `arity` operands besides the receiver ("" when none):
// JavaScript has no operator overloading, so `a + b` is `a.plus(b)`.
std::string operator_method(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {nullptr, "plus"}}, {"operator-", {"negate", "minus"}}, {"operator*", {nullptr, "times"}},
        {"operator/", {nullptr, "dividedBy"}}, {"operator%", {nullptr, "remainder"}}, {"operator==", {nullptr, "equals"}},
        {"operator<", {nullptr, "lessThan"}}, {"operator<=", {nullptr, "lessThanOrEqual"}}, {"operator>", {nullptr, "greaterThan"}},
        {"operator>=", {nullptr, "greaterThanOrEqual"}}, {"operator&", {nullptr, "bitAnd"}}, {"operator|", {nullptr, "bitOr"}},
        {"operator^", {nullptr, "bitXor"}}, {"operator~", {"bitNot", nullptr}}, {"operator[]", {nullptr, "at"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
// A 64-bit tag unique to the module, for Node-API object type tags.
std::string module_tag(const std::string& name) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : "rosetta.js:" + name) { hash ^= static_cast<unsigned char>(c); hash *= 1099511628211ULL; }
    std::ostringstream out;
    out << "0x" << std::hex << hash << "ULL";
    return out.str();
}

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;
    bool extension = false;    // a free function (policy extend, or a free operator)
    bool receives_self = false;
};
struct Group {
    std::string name;
    std::vector<Overload> overloads;
    bool is_static = false;
    std::string symbol;        // generated callback
    std::string module;        // policy submodule of a module function ("" = exports)
};
struct FieldPlan { const Field* field; std::string name; std::string getter, setter; bool writable = true; };
struct ConstantPlan { std::string name, expression; const Type* type; const Documentation* documentation; };
struct ClassPlan {
    const Record* record;
    std::string js;            // module-level name (Outer_Inner for nested classes)
    std::string member;        // name inside the outer class
    std::vector<std::string> bases;
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<FieldPlan> fields;
    std::vector<ConstantPlan> constants;
    std::string parent;
    std::string constructor;   // generated callback, empty without a public constructor
    bool director = false;
    bool own_close = false;    // a C++ method named close: no generated close()
};

// --- JSDoc ----------------------------------------------------------------

std::string one_line(std::string text) {
    for (auto& c : text) if (c == '\n') c = ' ';
    return text;
}
std::string doc_block(const DocBlock& block) {
    if (block.kind == "code") {
        std::string language = block.language;
        if (language.empty() || language == "c++" || language == "cxx" || language == "cc") language = "cpp";
        if (language == "py") language = "python";
        if (language == "js") language = "javascript";
        return "```" + language + "\n" + block.text + "\n```";
    }
    if (block.kind == "math") return "```\n" + block.text + "\n```";
    if (block.kind == "heading") return "**" + block.text + "**";
    return block.text;
}
// Markdown description of a declaration: brief, details and notes (tags are separate).
std::string doc_description(const Documentation& doc) {
    std::vector<std::string> paragraphs;
    if (!doc.brief.empty()) paragraphs.push_back(doc.brief);
    for (const auto& block : doc.details) paragraphs.push_back(doc_block(block));
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") continue;
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        paragraphs.push_back("**" + label + ":** " + item.text);
    }
    std::string out;
    for (const auto& paragraph : paragraphs) out += (out.empty() ? "" : "\n\n") + paragraph;
    return out;
}
// The tags after a description: @throws, @deprecated and @see (@param/@returns come from the caller).
std::string doc_tags(const Documentation& doc) {
    std::string out;
    for (const auto& item : doc.throws)
        out += "@throws {Error} " + (item.name.empty() ? std::string() : "`" + item.name + "`" + (item.text.empty() ? "" : ": ")) + one_line(item.text) + "\n";
    for (const auto& item : doc.notes)
        if (item.name == "deprecated") out += "@deprecated " + one_line(item.text) + "\n";
    if (!doc.see.empty()) {
        std::string see;
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + doc.see[i];
        out += "@see " + see + "\n";
    }
    return out;
}
// A JSDoc comment (/** ... */) of the text, each line indented.
std::string jsdoc(const std::string& text, const std::string& indent) {
    if (text.empty()) return {};
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    std::string out = indent + "/**\n";
    bool tags = false;
    for (auto line : lines) {
        for (std::size_t i = 0; (i = line.find("*/", i)) != std::string::npos; i += 3) line.replace(i, 2, "*\\/");
        // A description line starting with @ would read as a tag.
        if (!line.empty() && line.front() == '@') {
            static const char* known[] = {"@param ", "@returns ", "@throws ", "@deprecated", "@see "};
            const bool tag = std::any_of(std::begin(known), std::end(known), [&](const char* k) { return line.rfind(k, 0) == 0; });
            if (tag) tags = true;
            else if (!tags) line = "\\" + line;
        }
        out += indent + " *" + (line.empty() ? "" : " " + line) + "\n";
    }
    return out + indent + " */\n";
}

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, std::size_t parts)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), parts_(parts) { validate_modules(policy_, module_); }
    GeneratedJavaScriptSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    std::size_t parts_ = 1;
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_;
    std::map<std::string, std::string> function_template_names_;
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
    std::map<std::string, std::string> enum_names_; // qualified enum -> JavaScript name
    std::size_t next_ = 0;
    std::vector<std::string> symbols_;               // every generated callback, for split sources

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
    std::string renamed(const std::string& qualified, const std::string& fallback) const {
        const auto found = policy_.rename.find(qualified);
        if (found == policy_.rename.end()) return fallback;
        if (!identifier(found->second)) throw BindingError("JavaScript rename target is not an identifier: " + found->second);
        return found->second;
    }
    std::string symbol() {
        symbols_.push_back("rosetta_js_" + std::to_string(next_++));
        return symbols_.back();
    }
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
    std::string ts_type(const Type& type, Use use) const;
    std::string signature(const Overload& overload, const std::string& name) const;
    std::string js_default(const DefaultValue& value, const Type& type) const;
    std::string return_policy(const Function& function) const;
    std::string class_js_name(const Record& record) const;
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
    void plan_classes();
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    bool callback_supported(const Function& method) const;
    void plan_directors();
    void emit_director(std::ostream& out, const ClassPlan& plan);
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks);
    void emit_group(std::ostream& out, Group& group, bool is_method);
    void emit_constructor(std::ostream& out, ClassPlan& plan);
    std::string ts_parameters(const Overload& overload) const;
    std::string ts_overload_doc(const Overload& overload, const std::string& fallback) const;
    const Documentation& documentation_of(const Overload& overload) const;
    std::string ts_function(const Group& group, const std::string& indent, bool member) const;
    std::string ts_class(const ClassPlan& plan) const;
    std::string class_type(const std::string& owner) const { return classes_.at(owner).js; }
    static std::string director_name(const ClassPlan& plan) {
        std::string name = "rosetta_director_";
        for (const char c : plan.record->qualified_name()) name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        return name;
    }
    // Members of a class, its bases' members included (the class's own come first).
    template<class F> std::vector<std::pair<std::string, std::string>> collect(const std::string& owner, F member, bool skip_first_base) const {
        std::vector<std::pair<std::string, std::string>> result;
        std::set<std::string> taken;
        const std::function<void(const std::string&, unsigned, bool)> walk = [&](const std::string& cls, unsigned depth, bool skip) {
            if (depth > 64) return;
            for (const auto& item : member(classes_.at(cls)))
                if (taken.insert(item.first).second) result.push_back(item);
            const auto& bases = classes_.at(cls).bases;
            for (std::size_t i = skip ? 1 : 0; i < bases.size(); ++i) walk(bases[i], depth + 1, false);
        };
        walk(owner, 0, skip_first_base);
        return result;
    }
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
        return "unsupported JavaScript value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to JavaScript";
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
            declaration == "std::array") {
            if (!type.arguments.empty() && type.arguments[0].kind == TypeKind::template_instance &&
                normalize(type.arguments[0].declaration) == "std::unique_ptr" && declaration != "std::optional")
                return "containers of unique_ptr are unsupported";
            return all(1, Use::element);
        }
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
        return "unsupported JavaScript template type " + type.canonical;
    }
    default:
        return "unsupported JavaScript value type " + type.canonical;
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
        return "unsupported JavaScript pointer type " + type.canonical;
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
        if (!why.empty()) return "unsupported JavaScript result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported JavaScript parameter type: " + why;
    }
    return {};
}

// The TypeScript spelling of a C++ type crossing the boundary (also used in error messages
// listing signatures). Parameters say what they accept, results what comes back.
std::string Generator::ts_type(const Type& type, Use use) const {
    const bool input = use == Use::parameter || use == Use::callback_result;
    const auto wrapped = [](const std::string& text) { return text.find_first_of("|&=( ") == std::string::npos ? text : "(" + text + ")"; };
    if (use == Use::parameter && type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type))) {
        const auto inner = ts_type(referent(type), Use::element);
        return inner + " | Ref<" + inner + ">";
    }
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return input ? "string | null" : "string";
        return ts_type(pointee, Use::element) + " | null";
    }
    if (is_void(target)) return "void";
    if (!conversion(target).empty()) {
        auto to = normalize(policy_.conversions.at(conversion(target)));
        if (to == "std::string") return "string";
        if (to.rfind("std::vector<", 0) == 0) {
            const auto element = to.substr(12, to.size() - 13);
            if (element == "std::string") return "string[]";
            if (element == "bool") return "boolean[]";
            return "number[]";
        }
        return "any";
    }
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "boolean";
        if (bare == "char") return "string";
        return "number";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return "number";
        return input ? found->second + " | keyof typeof " + found->second : found->second;
    }
    if (bound(target)) return class_type(target.declaration);
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "null";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? ts_type(target.arguments[i], Use::element) : std::string("any"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set")
            return wrapped(arg(0)) + "[]";
        if (declaration == "std::map" || declaration == "std::unordered_map") {
            const auto key = target.arguments.empty() ? std::string() : normalize(strip_cv(target.arguments[0].canonical));
            const bool text = !target.arguments.empty() && target.arguments[0].kind == TypeKind::template_instance &&
                normalize(target.arguments[0].declaration).rfind("std::basic_string", 0) == 0;
            return text ? "Record<string, " + arg(1) + ">" : "Map<" + arg(0) + ", " + arg(1) + ">";
        }
        if (declaration == "std::optional") return arg(0) + " | null";
        if (declaration == "std::complex") return "[number, number]";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0) + " | null";
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "[";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + ts_type(target.arguments[i], Use::element);
            return result + "]";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " | " : "") + ts_type(target.arguments[i], Use::element);
            return result;
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string result = "(";
            for (std::size_t i = 1; i < function.arguments.size(); ++i)
                result += (i > 1 ? ", " : "") + std::string("a") + std::to_string(i) + ": " + ts_type(function.arguments[i], Use::callback_argument);
            result += ") => ";
            result += function.arguments.empty() ? std::string("void") : ts_type(function.arguments[0], Use::callback_result);
            return "(" + result + ")";
        }
        if (declaration == "std::chrono::duration") return "number";
    }
    return "any";
}

std::string Generator::signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    std::string result = name + "(";
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        if (i > first) result += ", ";
        const auto& parameter = i < function.parameter_names.size() && !function.parameter_names[i].empty()
            ? function.parameter_names[i] : "arg" + std::to_string(i - first);
        result += parameter + (i >= required ? "?" : "") + ": " + ts_type(function.parameters[i], Use::parameter);
    }
    result += ")";
    if (!function.is_constructor && !is_void(function.result)) result += ": " + ts_type(function.result, Use::result);
    return result;
}

std::string Generator::js_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "float" || value.kind == "constant") return value.value;
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "null";
    if (value.kind == "string") return value.value;
    if (value.kind == "char") return "\"" + value.value + "\"";
    if (value.kind == "enum") {
        const auto& target = referent(type);
        const auto found = enum_names_.find(target.declaration);
        const auto label = renamed(target.declaration + "::" + value.value, value.value);
        return found != enum_names_.end() ? found->second + "." + label : label;
    }
    if (value.kind == "default") {
        const auto& target = referent(type);
        if (bound(target)) return "new " + class_type(target.declaration) + "()";
        const auto shown = ts_type(target, Use::result);
        if (shown.size() > 2 && shown.compare(shown.size() - 2, 2, "[]") == 0) return "[]";
        if (shown.rfind("Record<", 0) == 0) return "{}";
        if (shown.rfind("Map<", 0) == 0) return "new Map()";
        if (shown == "string") return "\"\"";
        if (shown == "number") return "0";
        if (shown == "boolean") return "false";
    }
    return {};
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_js::Return::copy" : "rosetta_js::Return::automatic";
    return "rosetta_js::Return::" + found->second;
}

std::string Generator::class_js_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, instantiated->second);
    if (!record.owner.empty()) return renamed(qualified, record.name);
    return renamed(qualified, flat(record.namespaces) ? record.name : exported(qualified));
}

void Generator::plan_classes() {
    for (const auto& requested : policy_.templates) {
        const auto found = std::find_if(module_.records.begin(), module_.records.end(), [&](const Record& record) {
            return !record.template_name.empty() && normalize(record.qualified_name()) == normalize(requested.second);
        });
        if (!identifier(requested.first)) throw BindingError("template binding name is not an identifier: " + requested.first);
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
            throw BindingError("unknown class in JavaScript policy: " + selection.first);
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
        if (!record.complete) { if (listed) reject(owner, "JavaScript requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed JavaScript results are not yet supported"); continue;
        }
        ClassPlan plan;
        plan.record = &record;
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
        plan.member = static_safe(class_js_name(*plan.record));
        plan.parent = plan.record->owner;
    }
    for (auto& [owner, plan] : classes_) {
        std::string name = plan.member;
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            name = classes_.at(parent).member + "_" + name;
        plan.js = name;
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

bool Generator::callback_supported(const Function& method) const {
    if (!method.unsupported_reason.empty() || !type_problem(method.result, Use::callback_result).empty()) return false;
    for (const auto& parameter : method.parameters)
        if (!type_problem(parameter, Use::callback_argument).empty()) return false;
    return !is_operator(method.name);
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
        for (const auto* method : methods)
            if (method->is_pure && !callback_supported(*method)) possible = false;
        const bool constructible = std::any_of(record.constructors.begin(), record.constructors.end(), [](const Function& constructor) {
            return !constructor.is_deleted && constructor.access != "private";
        });
        plan.director = possible && constructible;
    }
}

// A C++ subclass whose virtual methods call the JavaScript object's overrides, or the native implementation.
void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << director_name(plan) << " final : " << owner << ", rosetta_js::Director {\n"
        << "    template<class... A> explicit " << director_name(plan) << "(A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    for (const auto* method : methods) {
        if (!callback_supported(*method)) continue;
        std::string key = method->name + "(";
        for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
        key += method->is_const ? ")const" : ")";
        const bool is_pure = pure[key];
        if (method->access == "private" && !is_pure) continue;
        const auto jname = renamed(method->qualified_name(), member_safe(method->name));
        std::string parameters, arguments, types, call_arguments;
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            const auto& type = method->parameters[i];
            const auto name = "a" + std::to_string(i);
            parameters += (i ? ", " : "") + type.canonical + " " + name;
            const bool forward = type.kind == TypeKind::rvalue_reference || (type.kind != TypeKind::lvalue_reference && type.kind != TypeKind::pointer);
            const auto passed = forward ? "std::move(" + name + ")" : name;
            arguments += ", " + passed;
            call_arguments += (i ? ", " : "") + passed;
            types += ", " + type.canonical;
        }
        const auto result = method->result.canonical;
        out << "    " << result << " " << method->name << "(" << parameters << ")" << (method->is_const ? " const" : "")
            << (method->is_noexcept ? " noexcept" : "") << " override {\n";
        if (method->is_noexcept) out << "        try {\n";
        out << "        {\n"
            << "            rosetta_js::Override found(this, " << quote(jname) << ", &rosetta_js::Info<" << owner << ">::value);\n"
            << "            if (found) return rosetta_js::invoke_override<" << result << types << ">(found" << arguments << ");\n"
            << "        }\n";
        if (is_pure) out << "        rosetta_js::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (const std::exception& error) { std::fprintf(stderr, \"error in noexcept JavaScript override: %s\\n\", error.what()); }\n"
                << "        catch (...) {}\n        std::terminate();\n";
        out << "    }\n";
    }
    out << "};\n";
}

// One overload of a group: arguments are checked (several overloads) and converted, and the
// native call's result returned. kind: "method" (receiver `this`), "constructor" or "function";
// an extension (free function attached to a class) with receives_self takes `this` as its first argument.
void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks) {
    const auto& function = *overload.function;
    const std::size_t skip = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - skip;
    const std::size_t required = function.parameters.size() - function.default_arguments - skip;
    const std::string indent = "            ";
    const auto owner = overload.owner;
    out << indent << "{\n"
        << indent << "    const int n = std::max(given, " << required << "); (void)n;\n"
        << indent << "    if (raw >= " << required << " && given <= " << count;
    if (checks && overload.receives_self) out << "\n" << indent << "        && rosetta_js::Param<" << function.parameters[0].canonical << ">::check(env, call.self, false)";
    for (std::size_t i = 0; i < count && checks; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || rosetta_js::Param<" << function.parameters[i + skip].canonical
            << ">::check(env, call.arg(" << i << "), strict))";
    out << ") {\n";
    if (kind == "method") out << indent << "        auto* native = rosetta_js::native<" << owner << ">(env, call.self);\n";
    if (overload.receives_self)
        out << indent << "        rosetta_js::converting() = \"this\";\n"
            << indent << "        rosetta_js::Param<" << function.parameters[0].canonical << "> self_param(env, call.self);\n"
            << indent << "        rosetta_js::converting() = nullptr;\n";
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& name = i + skip < function.parameter_names.size() ? function.parameter_names[i + skip] : std::string{};
            out << indent << "            rosetta_js::converting() = " << quote(name.empty() ? "argument " + std::to_string(i + 1) : "argument '" + name + "'") << ";\n"
                << indent << "            rosetta_js::Param<" << function.parameters[i + skip].canonical << "> p" << i << "(env, call.arg(" << i << "));\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (arity > 0) out << indent << "            rosetta_js::converting() = nullptr;\n";
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            if (plan.director)
                out << indent << "            if (subclass) {\n"
                    << indent << "                auto* created = new " << director_name(plan) << "(" << arguments << ");\n"
                    << indent << "                return rosetta_js::adopt<" << owner << ">(call, static_cast<" << owner << "*>(created), static_cast<rosetta_js::Director*>(created));\n"
                    << indent << "            }\n";
            if (plan.record->abstract)
                out << indent << "            rosetta_js::type_fail(" << quote(plan.js + " is abstract; implement it in JavaScript with class Mine extends " + plan.js + " {...}") << ");\n";
            else if (function.access != "public")
                out << indent << "            rosetta_js::type_fail(" << quote(plan.js + " constructor is protected; subclass it with class Mine extends " + plan.js + " {...}") << ");\n";
            else
                out << indent << "            return rosetta_js::adopt<" << owner << ">(call, new " << owner << "(" << arguments << "), nullptr);\n";
        } else {
            std::string body = kind == "method" ? "return native->" + function.name + "(" + arguments + ");"
                                                : "return ::" + function.qualified_name() + "(" + arguments + ");";
            // On a JavaScript subclass object, a virtual method runs the native implementation, so
            // that an override calling super.method() does not dispatch back to itself.
            if (kind == "method" && function.is_virtual && policy_.directors != "none")
                body = "if (rosetta_js::is_director(env, call.self)) { " + (function.is_pure
                    ? "rosetta_js::pure_virtual(" + quote(owner + "::" + function.name) + ");"
                    : "return native->" + owner + "::" + function.name + "(" + arguments + ");") + " } " + body;
            std::string writebacks;
            for (std::size_t i = 0; i < arity; ++i) {
                const auto& type = function.parameters[i + skip];
                if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                    writebacks += indent + "            p" + std::to_string(i) + ".writeback(env, call.arg(" + std::to_string(i) + "));\n";
            }
            const auto invocation = "rosetta_js::call(env, [&]() -> decltype(auto) { " + body + " }, " + (parent ? "call.self" : "nullptr") + ", " + policy + ")";
            if (writebacks.empty()) out << indent << "            return " << invocation << ";\n";
            else out << indent << "            napi_value results = " << invocation << ";\n" << writebacks << indent << "            return results;\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
}

void Generator::emit_group(std::ostream& out, Group& group, bool is_method) {
    group.symbol = symbol();
    const bool single = group.overloads.size() == 1;
    out << "napi_value " << group.symbol << "(napi_env env, napi_callback_info info) {\n"
        << "    return rosetta_js::boundary(env, [&]() -> napi_value {\n"
        << "        rosetta_js::Call call(env, info);\n";
    if (is_method) {
        const auto& owner = group.overloads.front().owner;
        out << "        if (!rosetta_js::is_instance<" << owner << ">(env, call.self)) rosetta_js::wrong_receiver("
            << quote(classes_.at(owner).js) << ", " << quote(group.name) << ");\n";
    }
    out << "        const int raw = call.count(), given = call.given(); (void)raw;\n"
        << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads)
        emit_overload(out, overload, overload.extension ? "function" : is_method ? "method" : "function", !single);
    out << "        }\n";
    std::string signatures;
    for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
    out << "        rosetta_js::no_overload(call, " << quote(group.name) << ", " << quote(signatures) << ");\n"
        << "    });\n}\n";
}

void Generator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor = symbol();
    const auto owner = plan.record->qualified_name();
    const bool single = plan.constructors.size() == 1;
    if (plan.director) emit_director(out, plan);
    out << "napi_value " << plan.constructor << "(napi_env env, napi_callback_info info) {\n"
        << "    return rosetta_js::boundary(env, [&]() -> napi_value {\n"
        << "        rosetta_js::Call call(env, info, true);\n"
        << "        const bool subclass = rosetta_js::subclass_call(call, &rosetta_js::Info<" << owner << ">::value); (void)subclass;\n"
        << "        const int raw = call.count(), given = call.given(); (void)raw;\n"
        << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", !single);
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, "new " + plan.js) + "\n";
    out << "        }\n        rosetta_js::no_overload(call, " << quote(plan.js) << ", " << quote(signatures) << ");\n"
        << "    });\n}\n";
}

// --- TypeScript declarations ----------------------------------------------------

std::string Generator::ts_parameters(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::string list;
    std::set<std::string> used;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first) : declared(cpp);
        if (!used.insert(name).second) name = "arg" + std::to_string(i - first);
        list += (i > first ? ", " : "") + name + (i >= required ? "?" : "") + ": " + ts_type(function.parameters[i], Use::parameter);
    }
    return list;
}

// An overload's documentation; an undocumented override inherits its base method's (as Doxygen does).
const Documentation& Generator::documentation_of(const Overload& overload) const {
    const auto& function = *overload.function;
    if (!function.documentation.empty() || !function.is_virtual || overload.owner.empty() || overload.extension) return function.documentation;
    const Documentation* found = nullptr;
    const std::function<void(const std::string&, unsigned)> visit = [&](const std::string& owner, unsigned depth) {
        const auto record = records_.find(owner);
        if (found || depth > 64 || record == records_.end()) return;
        for (const auto& base : record->second->bases) {
            const auto name = base.type.declaration.empty() ? base.type.canonical : base.type.declaration;
            const auto base_record = records_.find(name);
            if (base_record == records_.end()) continue;
            for (const auto& method : base_record->second->methods) {
                if (method.name != function.name || method.parameters.size() != function.parameters.size() || method.documentation.empty()) continue;
                bool same = true;
                for (std::size_t i = 0; i < method.parameters.size(); ++i)
                    same = same && method.parameters[i].canonical == function.parameters[i].canonical;
                if (same) { found = &method.documentation; return; }
            }
            visit(name, depth + 1);
            if (found) return;
        }
    };
    visit(overload.owner, 0);
    return found ? *found : function.documentation;
}

// JSDoc of one overload: its description, @param (with defaults), @returns and the other tags.
std::string Generator::ts_overload_doc(const Overload& overload, const std::string& fallback) const {
    const auto& function = *overload.function;
    const auto& documentation = documentation_of(overload);
    std::string text = doc_description(documentation);
    if (text.empty()) text = fallback;
    std::string tags;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::set<std::string> used;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first) : declared(cpp);
        if (!used.insert(name).second) name = "arg" + std::to_string(i - first);
        std::string description;
        for (const auto& item : documentation.params) if (item.name == cpp) description = one_line(item.text);
        if (i >= required) {
            const auto shown = js_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            if (!shown.empty()) description += (description.empty() ? "" : " ") + std::string("(default: `") + shown + "`)";
        }
        if (!description.empty()) tags += "@param " + name + " " + description + "\n";
    }
    if (!function.is_constructor && !is_void(function.result) && !documentation.returns.empty())
        tags += "@returns " + one_line(documentation.returns) + "\n";
    tags += doc_tags(documentation);
    if (!tags.empty()) text += (text.empty() ? "" : "\n") + tags;
    return text;
}

// A function or method: each overload is a declaration with its own JSDoc (the first one falls
// back to the C++ name when no overload is documented).
std::string Generator::ts_function(const Group& group, const std::string& indent, bool member) const {
    std::string out;
    const bool documented = std::any_of(group.overloads.begin(), group.overloads.end(), [&](const Overload& o) { return !documentation_of(o).empty(); });
    std::set<std::string> seen;
    for (std::size_t i = 0; i < group.overloads.size(); ++i) {
        const auto& overload = group.overloads[i];
        const auto& function = *overload.function;
        const auto parameters = ts_parameters(overload);
        const auto result = ts_type(function.result, Use::result);
        if (!seen.insert(parameters + "->" + result).second) continue;
        const auto fallback = documented || i > 0 ? std::string() : "C++ `" + function.qualified_name() + "`";
        out += jsdoc(ts_overload_doc(overload, fallback), indent);
        if (member) out += indent + (group.is_static ? "static " : "") + group.name + "(" + parameters + "): " + result + ";\n";
        else out += indent + (reserved(group.name) ? "declare function " + declared(group.name) : "export declare function " + group.name) +
                     "(" + parameters + "): " + result + ";\n";
    }
    return out;
}

std::string Generator::ts_class(const ClassPlan& plan) const {
    const std::string indent = "    ";
    std::string out;
    auto description = doc_description(plan.record->documentation);
    if (description.empty()) description = "C++ class `" + plan.record->qualified_name() + "`";
    if (plan.constructors.empty())
        description += plan.record->abstract
            ? std::string("\n\nAbstract: create objects of a derived class") + (plan.director ? ", or implement it in JavaScript with `class Mine extends " + plan.js + " {...}`." : ".")
            : "\n\nNo public constructor.";
    else if (plan.director)
        description += "\n\nSubclass it in JavaScript (`class Mine extends " + plan.js + " {...}`): methods of the subclass override the C++ virtual methods.";
    const auto tags = doc_tags(plan.record->documentation);
    out += jsdoc(description + (tags.empty() ? "" : "\n" + tags), "");
    const auto name = declared(plan.js);
    out += std::string(reserved(plan.js) ? "declare " : "export declare ") + (plan.record->abstract ? "abstract " : "") + "class " + name;
    if (!plan.bases.empty()) out += " extends " + class_type(plan.bases.front());
    out += " {\n";
    // Constructors.
    const bool protected_only = plan.record->abstract || std::all_of(plan.constructors.begin(), plan.constructors.end(),
        [](const Overload& o) { return o.function->access != "public"; });
    if (plan.constructors.empty()) out += indent + "private constructor();\n";
    std::set<std::string> seen;
    for (const auto& overload : plan.constructors) {
        const auto parameters = ts_parameters(overload);
        if (!seen.insert(parameters).second) continue;
        out += jsdoc(ts_overload_doc(overload, ""), indent);
        out += indent + (protected_only && !plan.record->abstract ? "protected " : "") + "constructor(" + parameters + ");\n";
    }
    // Fields, own and of the bases JavaScript does not inherit through the prototype chain.
    const std::function<void(const ClassPlan&, unsigned, bool, std::set<std::string>&)> members =
        [&](const ClassPlan& current, unsigned depth, bool own, std::set<std::string>& taken) {
        if (depth > 64) return;
        for (const auto& field : current.fields) {
            if (!taken.insert(field.name).second) continue;
            out += jsdoc(doc_description(field.field->documentation), indent);
            out += indent + (field.writable ? "" : "readonly ") + field.name + ": " + ts_type(field.field->type, Use::result) + ";\n";
        }
        for (const auto& group : current.methods) {
            if (!taken.insert((group.is_static ? "static " : "") + group.name).second) continue;
            out += ts_function(group, indent, true);
        }
        (void)own;
        for (std::size_t i = depth == 0 ? 1 : 0; i < current.bases.size(); ++i) members(classes_.at(current.bases[i]), depth + 1, false, taken);
    };
    std::set<std::string> taken;
    members(plan, 0, true, taken);
    for (const auto& constant : plan.constants) {
        out += jsdoc(constant.documentation ? doc_description(*constant.documentation) : std::string(), indent);
        out += indent + "static readonly " + constant.name + ": " + ts_type(*constant.type, Use::result) + ";\n";
    }
    for (const auto& owner : class_order_) {
        const auto& nested = classes_.at(owner);
        if (nested.parent == plan.record->qualified_name())
            out += indent + "static readonly " + nested.member + ": typeof " + nested.js + ";\n";
    }
    for (const auto& [qualified, js] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        if (enumeration.owner == plan.record->qualified_name())
            out += indent + "static readonly " + static_safe(renamed(qualified, enumeration.name)) + ": typeof " + js + ";\n";
    }
    if (plan.bases.empty() && !plan.own_close)
        out += jsdoc("Releases the native object now instead of when it is garbage collected; later calls fail.", indent) +
               indent + "close(): void;\n";
    out += "}\n";
    if (reserved(plan.js)) out += "export { " + name + " as " + plan.js + " };\n";
    return out + "\n";
}

GeneratedJavaScriptSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    GeneratedJavaScriptSource result;
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
    plan_directors();
    for (auto it = enums_.begin(); it != enums_.end();) {
        if (!it->second->owner.empty() && !classes_.count(it->second->owner)) it = enums_.erase(it);
        else ++it;
    }
    // Enumeration names come first: types in signatures and declarations refer to them.
    std::map<std::string, std::string> names; // module-level JavaScript name -> what
    const auto reserve = [&](const std::string& jname, const std::string& what) {
        const auto inserted = names.emplace(jname, what);
        if (!inserted.second) throw BindingError("JavaScript export name collision: " + jname + " (" + inserted.first->second + " and " + what + ")");
    };
    reserve("ref", "Rosetta runtime");
    reserve("Ref", "Rosetta runtime");
    for (const auto& owner : class_order_) reserve(classes_.at(owner).js, "class " + owner);
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        auto jname = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) jname = classes_.at(enumeration.owner).js + "_" + jname;
        if (names.count(jname)) {
            if (!flat_) throw BindingError("JavaScript export name collision: " + jname);
            reject(qualified, "enum name collides with another export");
            continue;
        }
        reserve(jname, "enum " + qualified);
        enum_names_[qualified] = jname;
    }

    // Free functions attached to classes as methods (policy extend), and free operators.
    std::map<std::string, std::vector<Group>> extensions;
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner) && excluded(owner)) continue;
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [jname, target] : methods) {
            Group group;
            group.name = member_safe(jname);
            bool any = false, any_static = false;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const bool self = !function.parameters.empty() && [&, cls = owner] {
                    const auto& first = function.parameters[0];
                    const auto& base = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
                    const auto& bases = classes_.at(cls).bases;
                    return base.kind == TypeKind::record && (base.declaration == cls ||
                        std::find(bases.begin(), bases.end(), base.declaration) != bases.end());
                }();
                auto why = function_problem(function, self);
                if (!why.empty()) { reject(owner + "." + jname, why); continue; }
                if (!self) any_static = true;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            if (group.is_static) group.name = static_safe(group.name);
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    // Free operators become methods of the exported class of their first operand.
    std::map<std::string, std::vector<std::pair<std::string, Overload>>> free_operators; // class -> (method, overload)
    for (const auto& function : module_.functions) {
        if (!is_operator(function.name) || extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        const auto arity = function.parameters.size();
        if (arity == 0 || arity > 2) continue;
        const auto method = operator_method(function.name, arity - 1);
        if (method.empty() || method == "call") continue;
        const auto& first = function.parameters[0];
        const auto& target = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
        if (!bound(target)) continue;
        auto why = function_problem(function, true);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        free_operators[target.declaration].push_back({method, {&function, target.declaration, true, true}});
    }

    // Methods, constructors, fields and constants.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        const auto config = policy_.classes.find(owner);
        const bool constructors = config == policy_.classes.end() || config->second.constructors;
        if (config != policy_.classes.end()) {
            for (const auto& requested : config->second.methods) {
                const auto found = std::find_if(record.methods.begin(), record.methods.end(), [&](const Function& f) { return f.name == requested; });
                if (found == record.methods.end()) throw BindingError("unknown JavaScript policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("JavaScript policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown JavaScript policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("JavaScript policy field is not public: " + owner + "::" + requested);
            }
        }
        if (constructors && record.public_destructor && (!record.abstract || plan.director)) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || (constructor.access != "public" && !(plan.director && constructor.access == "protected"))) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                auto why = function_problem(constructor, false);
                if (!why.empty()) { reject(owner + "::" + record.name, why); continue; }
                plan.constructors.push_back({&constructor, owner});
            }
        }
        std::map<std::string, std::size_t> group_index;
        const auto add = [&](const std::string& jname, const Overload& overload, bool is_static) {
            const auto found = group_index.find(jname);
            if (found == group_index.end()) {
                group_index[jname] = plan.methods.size();
                plan.methods.push_back({jname, {overload}, is_static, {}, {}});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a JavaScript name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        std::vector<std::pair<std::string, Overload>> operators;
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            if (is_operator(method.name)) {
                if (method.is_static) continue;
                const auto jname = operator_method(method.name, method.parameters.size());
                if (jname.empty()) continue; // assignment, conversion and other operators stay C++-only
                auto why = function_problem(method, false);
                if (!why.empty()) { reject(method.qualified_name(), why); continue; }
                operators.push_back({jname, {&method, owner}});
                continue;
            }
            const auto jname = method.is_static ? static_safe(renamed(method.qualified_name(), member_safe(method.name)))
                                                : renamed(method.qualified_name(), member_safe(method.name));
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(jname, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner]) for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        // Operators take the names no method uses.
        std::set<std::string> methods_named;
        for (const auto& group : plan.methods) methods_named.insert(group.name);
        for (auto item : free_operators[owner]) operators.push_back(item);
        for (const auto& [jname, overload] : operators) {
            if (methods_named.count(jname)) {
                reject(overload.function->qualified_name(), "operator method name " + jname + " is taken by a method of the same name");
                continue;
            }
            add(jname, overload, false);
        }
        plan.own_close = group_index.count("close") != 0;
        std::set<std::string> taken;
        for (const auto& group : plan.methods) taken.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported JavaScript field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported JavaScript field type", 0) == 0 ? why : "unsupported JavaScript field type: " + why); continue; }
            const auto jname = renamed(qualified, member_safe(field.name));
            if (!taken.insert(jname).second) { reject(qualified, "field name collides with a method of the same name"); continue; }
            FieldPlan planned{&field, jname, {}, {}};
            planned.writable = !field.type.is_const && !readonly(qualified, owner) && field.type.kind != TypeKind::lvalue_reference;
            plan.fields.push_back(planned);
        }
        std::set<std::string> static_taken;
        for (const auto& group : plan.methods) if (group.is_static) static_taken.insert(group.name);
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to JavaScript"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            const auto jname = static_safe(renamed(qualified, variable.name));
            if (!static_taken.insert(jname).second) { reject(qualified, "constant name collides with another member"); continue; }
            plan.constants.push_back({jname, qualified, &variable.type, &variable.documentation});
        }
    }

    // Module functions grouped by JavaScript name.
    std::vector<Group> functions;
    std::map<std::string, std::size_t> function_index;
    for (const auto& function : module_.functions) {
        if (extension_functions_.count(function.qualified_name()) || is_operator(function.name)) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        const auto specialization = function_template_names_.find(function.qualified_name());
        if (!function.template_name.empty() && specialization == function_template_names_.end()) continue;
        const auto submodule = policy_module(policy_, function.namespaces);
        const auto jname = specialization != function_template_names_.end() ? specialization->second
            : renamed(function.qualified_name(), !submodule.empty() || flat(function.namespaces) ? function.name : exported(function.qualified_name()));
        // A submodule is a namespace in the declarations, where reserved words cannot be re-exported.
        if (!submodule.empty() && reserved(jname)) { reject(function.qualified_name(), "reserved word as a submodule function name"); continue; }
        const auto key = submodule + "." + jname;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions.size();
            functions.push_back({jname, {{&function, {}}}, false, {}, submodule});
        } else functions[found->second].overloads.push_back({&function, {}});
    }
    // Submodules are export names too; their functions are unique within each submodule.
    std::set<std::string> submodules;
    for (const auto& group : functions) {
        if (group.module.empty()) reserve(group.name, "function " + group.overloads.front().function->qualified_name());
        else if (submodules.insert(group.module).second) reserve(group.module, "submodule " + group.module);
    }

    // Constant namespace-scope variables.
    std::vector<ConstantPlan> constants;
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to JavaScript"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto jname = renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
        if (names.count(jname)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(jname, "constant " + qualified);
        constants.push_back({jname, "::" + qualified, &variable.type, &variable.documentation});
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for JavaScript";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Using-declarations re-export bound entities under another namespace's name.
    std::vector<std::pair<std::string, std::string>> aliases; // JavaScript alias -> JavaScript target
    {
        std::map<std::string, std::string> function_names;
        for (const auto& group : functions)
            if (group.module.empty())
                for (const auto& overload : group.overloads) function_names.emplace(overload.function->qualified_name(), group.name);
        for (const auto& reexport : module_.reexports) {
            std::string target;
            if (reexport.kind == "record" || reexport.kind == "type") { const auto c = classes_.find(reexport.target); if (c != classes_.end()) target = c->second.js; }
            if (reexport.kind == "enum" || reexport.kind == "type") { const auto e = enum_names_.find(reexport.target); if (e != enum_names_.end()) target = e->second; }
            else if (reexport.kind == "function") { const auto f = function_names.find(reexport.target); if (f != function_names.end()) target = f->second; }
            if (target.empty()) continue;
            std::string scope;
            for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
            const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
            const auto jname = renamed(qualified, flat(reexport.namespaces) ? reexport.name : exported(qualified));
            if (jname == target || names.count(jname)) continue;
            reserve(jname, "alias of " + reexport.target);
            aliases.emplace_back(jname, target);
        }
    }

    // C++ sources: the prelude (runtime and type traits), then one unit per function group or class.
    struct Unit { std::string code; };
    std::vector<Unit> units;
    std::ostringstream out;
    out << "// Generated by Rosetta: JavaScript (Node-API) module " << name_ << ".\n"
        << "#ifndef NAPI_VERSION\n#define NAPI_VERSION 8\n#endif\n#include <node_api.h>\n";
    for (const auto* header : {"algorithm", "array", "chrono", "climits", "cmath", "complex", "cstdint", "cstdio", "cstring", "deque",
                               "exception", "filesystem", "functional", "limits", "list", "map", "memory", "new", "optional", "set",
                               "stdexcept", "string", "string_view", "thread", "tuple", "typeindex", "type_traits", "unordered_map",
                               "unordered_set", "utility", "valarray", "variant", "vector"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n'
        << "#define ROSETTA_JS_MODULE rosetta_js_module_" << name_ << '\n'
        << "#define ROSETTA_JS_TAG " << module_tag(name_) << '\n';
    for (const auto* part : detail::js_full_runtime_parts) out << part;
    out << "namespace ROSETTA_JS_MODULE { namespace rosetta_js {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(napi_env env, napi_value value, bool strict) { return Value<" << to << ">::check(env, value, strict); }\n"
            << "    static " << from << " load(napi_env env, napi_value value) { return " << from << "(Value<" << to << ">::load(env, value)); }\n"
            << "    static napi_value push(napi_env env, const " << from << "& value) { return Value<" << to << ">::push(env, " << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\n";

    for (auto& group : functions) {
        std::ostringstream unit;
        emit_group(unit, group, false);
        units.push_back({unit.str()});
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::ostringstream unit;
        for (auto& group : plan.methods) emit_group(unit, group, !group.is_static && !(group.overloads.front().extension && !group.overloads.front().receives_self));
        for (auto& field : plan.fields) {
            const auto& f = *field.field;
            field.getter = symbol();
            unit << "napi_value " << field.getter << "(napi_env env, napi_callback_info info) {\n"
                 << "    return rosetta_js::boundary(env, [&]() -> napi_value {\n"
                 << "        rosetta_js::Call call(env, info);\n"
                 << "        auto* native = rosetta_js::native<" << owner << ">(env, call.self);\n"
                 << "        return rosetta_js::result<decltype((native->" << f.name << "))>(env, native->" << f.name
                 << ", call.self, rosetta_js::Return::" << (copy_type(f.type) ? "copy" : "automatic") << ");\n    });\n}\n";
            field.setter = symbol();
            unit << "napi_value " << field.setter << "(napi_env env, napi_callback_info info) {\n"
                 << "    return rosetta_js::boundary(env, [&]() -> napi_value {\n"
                 << "        rosetta_js::Call call(env, info);\n"
                 << "        auto* native = rosetta_js::native<" << owner << ">(env, call.self);\n";
            if (field.writable)
                unit << "        using F = std::remove_cv_t<std::remove_reference_t<decltype(native->" << f.name << ")>>;\n"
                     << "        if constexpr (rosetta_js::bound_v<F>) {\n"
                     << "            // obj.field.x = v assigns the modified field object to itself.\n"
                     << "            if (rosetta_js::is_instance<F>(env, call.arg(0)) && rosetta_js::native<F>(env, call.arg(0)) == &native->" << f.name << ") return rosetta_js::undefined(env);\n"
                     << "        }\n"
                     << "        rosetta_js::converting() = " << quote("field '" + field.name + "'") << ";\n"
                     << "        rosetta_js::Param<const F&> converted(env, call.arg(0));\n"
                     << "        rosetta_js::converting() = nullptr;\n"
                     << "        rosetta_js::assign_field(native->" << f.name << ", converted.get());\n";
            else
                unit << "        (void)native;\n        rosetta_js::type_fail(" << quote("field " + field.name + " is read-only") << ");\n";
            unit << "        return rosetta_js::undefined(env);\n    });\n}\n";
        }
        if (!plan.constructors.empty()) emit_constructor(unit, plan);
        units.push_back({unit.str()});
    }

    // Module setup: classes, prototype chains, functions, enums, constants and aliases.
    std::ostringstream setup, registration;
    for (const auto& owner : class_order_) {
        registration << "        rosetta_js::register_type<" << owner << ">(" << quote(classes_.at(owner).js) << ");\n";
        for (const auto& base : classes_.at(owner).bases) registration << "        rosetta_js::add_base<" << owner << ", " << base << ">();\n";
    }
    for (const auto& [qualified, jname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        std::string entries;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
            const auto& value = enumeration.enumerator_values[i];
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            entries += "{" + quote(label) + ", " + (big ? "static_cast<long long>(" + value + "ULL)"
                : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL") + "}, ";
        }
        registration << "        rosetta_js::EnumTable<" << qualified << ">::entries = {" << entries << "};\n";
    }
    setup << "napi_value dispose = rosetta_js::symbol(env, \"dispose\", false);\n"
          << "if (rosetta_js::type_of(env, dispose) != napi_symbol) dispose = nullptr;\n"
          << "napi_value inspect = rosetta_js::symbol(env, \"nodejs.util.inspect.custom\", true); (void)inspect;\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        // Methods and fields of the bases after the first (the first is the prototype chain's).
        const auto methods = collect(owner, [](const ClassPlan& p) {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& group : p.methods) if (!group.is_static) items.emplace_back(group.name, group.symbol);
            return items;
        }, true);
        const auto statics = collect(owner, [](const ClassPlan& p) {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& group : p.methods) if (group.is_static) items.emplace_back(group.name, group.symbol);
            return items;
        }, true);
        std::vector<std::pair<std::string, std::pair<std::string, std::string>>> fields;
        {
            std::set<std::string> taken;
            const std::function<void(const std::string&, unsigned, bool)> walk = [&](const std::string& cls, unsigned depth, bool skip) {
                if (depth > 64) return;
                for (const auto& field : classes_.at(cls).fields)
                    if (taken.insert(field.name).second) fields.push_back({field.name, {field.getter, field.setter}});
                const auto& bases = classes_.at(cls).bases;
                for (std::size_t i = skip ? 1 : 0; i < bases.size(); ++i) walk(bases[i], depth + 1, false);
            };
            walk(owner, 0, true);
        }
        setup << "{ // " << owner << "\n"
              << "    std::vector<napi_property_descriptor> properties = {\n";
        for (const auto& [jname, function] : methods) setup << "        rosetta_js::method(" << quote(jname) << ", " << function << "),\n";
        for (const auto& [jname, function] : statics)
            setup << "        rosetta_js::method(" << quote(jname) << ", " << function << ", static_cast<napi_property_attributes>(napi_default_method | napi_static)),\n";
        for (const auto& [jname, accessors] : fields)
            setup << "        rosetta_js::accessor(" << quote(jname) << ", " << accessors.first << ", " << accessors.second << "),\n";
        if (plan.bases.empty() && !plan.own_close) setup << "        rosetta_js::method(\"close\", rosetta_js::object_close),\n";
        setup << "    };\n";
        const bool has_to_string = std::any_of(methods.begin(), methods.end(), [](const auto& m) { return m.first == "to_string"; });
        if (plan.bases.empty()) {
            setup << "    if (dispose) { napi_property_descriptor d = rosetta_js::method(nullptr, rosetta_js::object_close); d.name = dispose; properties.push_back(d); }\n";
            if (!has_to_string) setup << "    properties.push_back(rosetta_js::method(\"toString\", rosetta_js::object_to_string));\n";
        }
        if (has_to_string)
            setup << "    properties.push_back(rosetta_js::method(\"toString\", rosetta_js::to_string_method));\n"
                  << "    { napi_property_descriptor d = rosetta_js::method(nullptr, rosetta_js::to_string_method); d.name = inspect; properties.push_back(d); }\n";
        for (const auto& constant : plan.constants)
            setup << "    { napi_property_descriptor d{" << quote(constant.name) << ", nullptr, nullptr, nullptr, nullptr, "
                  << "rosetta_js::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(env, static_cast<std::remove_cv_t<decltype("
                  << constant.expression << ")>>(" << constant.expression << "), nullptr, rosetta_js::Return::copy), "
                  << "static_cast<napi_property_attributes>(napi_enumerable | napi_static), nullptr}; properties.push_back(d); }\n";
        setup << "    napi_value cls;\n";
        if (!plan.constructor.empty())
            setup << "    cls = rosetta_js::define_class(env, " << quote(plan.js) << ", " << plan.constructor << ", nullptr, properties);\n";
        else
            setup << "    cls = rosetta_js::define_class(env, " << quote(plan.js) << ", rosetta_js::refuse, const_cast<char*>("
                  << quote(plan.record->abstract ? plan.js + " is abstract" : plan.js + " has no public constructor") << "), properties);\n";
        setup << "    const auto id = rosetta_js::Info<" << owner << ">::value.id;\n"
              << "    rosetta_js::check(env, napi_create_reference(env, cls, 1, &s.constructors[id]));\n"
              << "    rosetta_js::check(env, napi_create_reference(env, rosetta_js::get(env, cls, \"prototype\"), 1, &s.prototypes[id]));\n"
              << "    rosetta_js::set(env, exports, " << quote(plan.js) << ", cls);\n";
        if (!plan.parent.empty())
            setup << "    rosetta_js::set(env, rosetta_js::deref(env, s.constructors[rosetta_js::Info<" << plan.parent << ">::value.id]), "
                  << quote(plan.member) << ", cls);\n";
        setup << "}\n";
    }
    // Prototype chains: the first base's prototype (and static side) is the derived class's parent.
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (plan.bases.empty()) continue;
        setup << "rosetta_js::set_prototype_of(env, rosetta_js::deref(env, s.prototypes[rosetta_js::Info<" << owner << ">::value.id]), "
              << "rosetta_js::deref(env, s.prototypes[rosetta_js::Info<" << plan.bases.front() << ">::value.id]));\n"
              << "rosetta_js::set_prototype_of(env, rosetta_js::deref(env, s.constructors[rosetta_js::Info<" << owner << ">::value.id]), "
              << "rosetta_js::deref(env, s.constructors[rosetta_js::Info<" << plan.bases.front() << ">::value.id]));\n";
    }
    // Top-level functions go on exports; each policy submodule is a plain object of its functions.
    std::map<std::string, std::vector<const Group*>> by_module;
    for (const auto& group : functions) by_module[group.module].push_back(&group);
    for (const auto& [submodule, groups] : by_module) {
        setup << "{\n";
        if (submodule.empty()) setup << "    napi_value target = exports;\n";
        else setup << "    napi_value target;\n    rosetta_js::check(env, napi_create_object(env, &target));\n";
        setup << "    const napi_property_descriptor functions[] = {\n";
        for (const auto* group : groups)
            setup << "        rosetta_js::method(" << quote(group->name) << ", " << group->symbol << ", napi_default_jsproperty),\n";
        setup << "    };\n    rosetta_js::check(env, napi_define_properties(env, target, sizeof(functions) / sizeof(functions[0]), functions));\n";
        if (!submodule.empty()) setup << "    rosetta_js::set(env, exports, " << quote(submodule) << ", target);\n";
        setup << "}\n";
    }
    for (const auto& [qualified, jname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        setup << "{\n    napi_value e = rosetta_js::enum_object(env, {\n";
        for (const auto& enumerator : enumeration.enumerators) {
            const auto label = renamed(qualified + "::" + enumerator, enumerator);
            setup << "        {" << quote(label) << ", rosetta_js::Value<" << qualified << ">::push(env, " << qualified << "::" << enumerator << ")},\n";
        }
        setup << "    });\n    rosetta_js::set(env, exports, " << quote(jname) << ", e);\n";
        if (!enumeration.owner.empty())
            setup << "    rosetta_js::set(env, rosetta_js::deref(env, s.constructors[rosetta_js::Info<" << enumeration.owner << ">::value.id]), "
                  << quote(static_safe(renamed(qualified, enumeration.name))) << ", e);\n";
        setup << "}\n";
    }
    for (const auto& constant : constants)
        setup << "rosetta_js::set(env, exports, " << quote(constant.name) << ", rosetta_js::result<std::remove_cv_t<decltype(" << constant.expression
              << ")>>(env, static_cast<std::remove_cv_t<decltype(" << constant.expression << ")>>(" << constant.expression
              << "), nullptr, rosetta_js::Return::copy));\n";
    for (const auto& [alias, target] : aliases)
        setup << "rosetta_js::set(env, exports, " << quote(alias) << ", rosetta_js::get(env, exports, " << quote(target) << "));\n";

    // Split sources by size, as the Python, R and Lua backends do.
    std::vector<std::ostringstream> extra(parts_ > 1 ? parts_ - 1 : 0);
    if (!extra.empty()) {
        std::vector<std::size_t> order(units.size()), sizes(extra.size(), 0), bin(units.size(), 0);
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return units[a].code.size() > units[b].code.size(); });
        for (const auto index : order) {
            if (units[index].code.empty()) continue;
            const auto smallest = static_cast<std::size_t>(std::min_element(sizes.begin(), sizes.end()) - sizes.begin());
            bin[index] = smallest;
            sizes[smallest] += units[index].code.size();
        }
        const auto prelude = out.str();
        for (auto& part : extra) part << prelude << "namespace ROSETTA_JS_MODULE {\n";
        out << "namespace ROSETTA_JS_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) extra[bin[i]] << units[i].code;
        for (const auto& name : symbols_) out << "napi_value " << name << "(napi_env, napi_callback_info);\n";
        for (auto& part : extra) part << "} // namespace ROSETTA_JS_MODULE\n";
    } else {
        out << "namespace ROSETTA_JS_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "napi_value rosetta_js_init(napi_env env, napi_value exports) {\n"
        << "    return rosetta_js::boundary(env, [&]() -> napi_value {\n"
        << "        static const bool registered = [] {\n" << registration.str() << "        return true;\n        }();\n"
        << "        (void)registered;\n"
        << "        auto& s = rosetta_js::open_env(env);\n"
        << "        {\n"
        << "            napi_value ref_class;\n"
        << "            rosetta_js::check(env, napi_define_class(env, \"Ref\", NAPI_AUTO_LENGTH, rosetta_js::ref_constructor, nullptr, 0, nullptr, &ref_class));\n"
        << "            rosetta_js::check(env, napi_create_reference(env, ref_class, 1, &s.ref_class));\n"
        << "            rosetta_js::set(env, exports, \"Ref\", ref_class);\n"
        << "            const napi_property_descriptor ref = rosetta_js::method(\"ref\", rosetta_js::make_ref, napi_default_jsproperty);\n"
        << "            rosetta_js::check(env, napi_define_properties(env, exports, 1, &ref));\n"
        << "        }\n";
    {
        std::istringstream lines(setup.str());
        for (std::string line; std::getline(lines, line);) out << (line.empty() ? "" : "        ") << line << '\n';
    }
    out << "        return exports;\n    });\n}\n"
        << "} // namespace ROSETTA_JS_MODULE\n"
        << "NAPI_MODULE_INIT() { return ROSETTA_JS_MODULE::rosetta_js_init(env, exports); }\n";
    result.source = out.str();
    for (auto& part : extra) result.parts.push_back(part.str());

    // TypeScript declarations.
    std::ostringstream types;
    types << "// Generated by Rosetta: TypeScript declarations for the `" << name_ << "` native Node.js module, with the\n"
          << "// C++ documentation as JSDoc (hover, completion and type checking in editors). It is never\n"
          << "// loaded at run time: require() loads the compiled addon (" << name_ << ".node).\n\n"
          << jsdoc("A mutable cell for C++ reference parameters: a native function that takes a non-const\nreference reads `cell.value` and stores the updated value back in it.", "")
          << "export declare class Ref<T = any> {\n    constructor(value: T);\n    value: T;\n}\n"
          << jsdoc("Creates a Ref cell for a C++ reference parameter.\n@param value The initial value.", "")
          << "export declare function ref<T>(value: T): Ref<T>;\n\n";
    for (const auto& [qualified, jname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        auto description = doc_description(enumeration.documentation);
        if (description.empty()) description = "C++ enumeration `" + qualified + "`";
        description += "\n\nArguments of this type also accept the enumerator names as strings.";
        types << jsdoc(description, "") << (reserved(jname) ? "declare enum " + declared(jname) : "export declare enum " + jname) << " {\n";
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
            types << "    " << (identifier(label) ? label : "\"" + label + "\"") << " = " << enumeration.enumerator_values[i] << ",\n";
        }
        types << "}\n";
        if (reserved(jname)) types << "export { " << declared(jname) << " as " << jname << " };\n";
        types << "\n";
    }
    for (const auto& owner : class_order_) types << ts_class(classes_.at(owner));
    std::map<std::string, std::string> submodule_types;
    for (const auto& group : functions) {
        if (!group.module.empty()) {
            // Inside `export declare namespace`, declarations are plain exported functions.
            auto declaration = ts_function(group, "    ", false);
            for (std::size_t at; (at = declaration.find("export declare function ")) != std::string::npos;)
                declaration.replace(at, 24, "export function ");
            submodule_types[group.module] += declaration;
            continue;
        }
        types << ts_function(group, "", false);
        if (reserved(group.name)) types << "export { " << declared(group.name) << " as " << group.name << " };\n";
    }
    if (!functions.empty()) types << "\n";
    for (const auto& [submodule, declarations] : submodule_types)
        types << "/** Functions of the C++ namespace mapped to `" << submodule << "` by the binding policy. */\n"
              << "export declare namespace " << submodule << " {\n" << declarations << "}\n\n";
    for (const auto& constant : constants) {
        types << jsdoc(doc_description(*constant.documentation), "")
              << (reserved(constant.name) ? "declare const " + declared(constant.name) : "export declare const " + constant.name)
              << ": " << ts_type(*constant.type, Use::result) << ";\n";
        if (reserved(constant.name)) types << "export { " << declared(constant.name) << " as " << constant.name << " };\n";
    }
    for (const auto& [alias, target] : aliases) types << "export { " << declared(target) << " as " << alias << " };\n";
    result.types = types.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedJavaScriptSource generate_javascript(const Module& module, const std::string& name, bool skip_unsupported,
                                              const BindingPolicy& policy, std::size_t parts) {
    if (parts == 0 || parts > 256) throw BindingError("JavaScript source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, parts).run();
}
} // namespace rosetta
