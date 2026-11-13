// Full Lua backend (--lua-api full): runtime overload dispatch, class tables with inheritance,
// Lua subclasses through directors, operators as metamethods, and a LuaCATS stub that editors
// (lua-language-server, mep) read for hover and completion.
#include "rosetta/lua.hpp"
#include "lua_full_runtime.hpp"
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
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool identifier(const std::string& text) {
    auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    return !text.empty() && start(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return start(c) || (c >= '0' && c <= '9');
    });
}
// Lua keywords cannot be names after `.`; they get a trailing underscore.
std::string lua_safe(const std::string& name) {
    static const std::set<std::string> reserved = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    return reserved.count(name) ? name + "_" : name;
}
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
// The metamethod of a C++ operator with `arity` operands besides self ("" when none).
std::string metamethod(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {nullptr, "__add"}}, {"operator-", {"__unm", "__sub"}}, {"operator*", {nullptr, "__mul"}},
        {"operator/", {nullptr, "__div"}}, {"operator%", {nullptr, "__mod"}}, {"operator==", {nullptr, "__eq"}},
        {"operator<", {nullptr, "__lt"}}, {"operator<=", {nullptr, "__le"}}, {"operator&", {nullptr, "__band"}},
        {"operator|", {nullptr, "__bor"}}, {"operator^", {nullptr, "__bxor"}}, {"operator~", {"__bnot", nullptr}},
        {"operator[]", {nullptr, "__index_op"}}};
    if (name == "operator()") return "__call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
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
    std::string symbol;        // generated C function
    std::string module;        // policy submodule of a module function ("" = module table)
};
struct FieldPlan { const Field* field; std::string name; std::string getter, setter; };
struct ConstantPlan { std::string name, expression; const Type* type; const Documentation* documentation; };
struct ClassPlan {
    const Record* record;
    std::string lua;           // module-level name (Outer_Inner for nested classes)
    std::string member;        // name inside the outer class
    std::vector<std::string> bases;
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<Group> metamethods;
    std::vector<FieldPlan> fields;
    std::vector<ConstantPlan> constants;
    std::string parent;
    std::string constructor;   // generated C function, empty without a public constructor
    bool director = false;
};

// --- LuaCATS ----------------------------------------------------------------

// Prefixes every line with --- (LuaCATS documentation).
std::string meta_lines(const std::string& text) {
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        // A line starting with @ would be read as an annotation.
        if (!line.empty() && line.front() == '@') line = "\\" + line;
        out += "---" + line + "\n";
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}
std::string one_line(std::string text) {
    for (auto& c : text) if (c == '\n') c = ' ';
    return text;
}
std::string meta_block(const DocBlock& block) {
    if (block.kind == "code") {
        std::string language = block.language;
        if (language.empty() || language == "c++" || language == "cxx" || language == "cc") language = "cpp";
        if (language == "py") language = "python";
        return "```" + language + "\n" + block.text + "\n```";
    }
    if (block.kind == "math") return "```\n" + block.text + "\n```";
    if (block.kind == "heading") return "**" + block.text + "**";
    return block.text;
}
// Markdown description of a declaration: brief, details, notes, errors, see also.
std::string meta_description(const Documentation& doc) {
    std::vector<std::string> paragraphs;
    if (!doc.brief.empty()) paragraphs.push_back(doc.brief);
    for (const auto& block : doc.details) paragraphs.push_back(meta_block(block));
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        paragraphs.push_back("**" + label + ":** " + item.text);
    }
    if (!doc.throws.empty()) {
        std::string errors = "**Errors** (raised as Lua errors)";
        for (const auto& item : doc.throws)
            errors += "\n* " + (item.name.empty() ? std::string() : "`" + item.name + "`" + (item.text.empty() ? "" : ": ")) + one_line(item.text);
        paragraphs.push_back(errors);
    }
    if (!doc.see.empty()) {
        std::string see = "See also: ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + std::string("`") + doc.see[i] + "`";
        paragraphs.push_back(see);
    }
    std::string out;
    for (const auto& paragraph : paragraphs) out += (out.empty() ? "" : "\n\n") + paragraph;
    return out;
}

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, std::size_t parts)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), parts_(parts) { validate_modules(policy_, module_); }
    GeneratedLuaSource run();

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
    std::map<std::string, std::string> enum_names_; // qualified enum -> Lua name
    std::size_t next_ = 0;
    std::vector<std::string> symbols_;               // every generated C function, for split sources

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
        if (!identifier(found->second)) throw BindingError("Lua rename target is not an identifier: " + found->second);
        return found->second;
    }
    std::string symbol() {
        symbols_.push_back("rosetta_lua_" + std::to_string(next_++));
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
    std::string lua_type(const Type& type) const;
    std::string signature(const Overload& overload, const std::string& name) const;
    std::string lua_default(const DefaultValue& value, const Type& type) const;
    std::string return_policy(const Function& function) const;
    std::string class_lua_name(const Record& record) const;
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
    std::string meta_function(const Group& group, const std::string& owner, const std::string& prefix, bool method) const;
    std::string meta_class(const ClassPlan& plan) const;
    std::string meta_parameters(const Overload& overload, std::string* annotations) const;
    std::string member_type(const std::string& owner) const { return name_ + "." + classes_.at(owner).lua; }
    static std::string director_name(const ClassPlan& plan) {
        std::string name = "rosetta_director_";
        for (const char c : plan.record->qualified_name()) name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        return name;
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
        return "unsupported Lua value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to Lua";
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
        return "unsupported Lua template type " + type.canonical;
    }
    default:
        return "unsupported Lua value type " + type.canonical;
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
        return "unsupported Lua pointer type " + type.canonical;
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
        if (!why.empty()) return "unsupported Lua result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported Lua parameter type: " + why;
    }
    return {};
}

// LuaCATS spelling of a C++ type (also used in error messages listing signatures).
std::string Generator::lua_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "string";
        const auto inner = lua_type(pointee);
        return inner.find_first_of("| ") == std::string::npos ? inner + "?" : "(" + inner + ")?";
    }
    if (is_void(target)) return "nil";
    if (!conversion(target).empty()) return policy_.conversions.at(conversion(target)) == "std::string" ? "string" : "any";
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "boolean";
        if (bare == "char") return "string";
        if (bare == "float" || bare == "double" || bare == "long double") return "number";
        return "integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? name_ + "." + found->second : "integer";
    }
    if (bound(target)) return member_type(target.declaration);
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "nil";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? lua_type(target.arguments[i]) : std::string("any"); };
        const auto wrapped = [](const std::string& text) { return text.find_first_of("| ") == std::string::npos ? text : "(" + text + ")"; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set")
            return wrapped(arg(0)) + "[]";
        if (declaration == "std::map" || declaration == "std::unordered_map") return "table<" + arg(0) + ", " + arg(1) + ">";
        if (declaration == "std::optional") return wrapped(arg(0)) + "?";
        if (declaration == "std::complex") return "number[]";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "[";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + lua_type(target.arguments[i]);
            return result + "]";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? "|" : "") + lua_type(target.arguments[i]);
            return result;
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string result = "fun(";
            for (std::size_t i = 1; i < function.arguments.size(); ++i)
                result += (i > 1 ? ", " : "") + std::string("a") + std::to_string(i) + ": " + lua_type(function.arguments[i]);
            result += ")";
            if (!function.arguments.empty() && !is_void(function.arguments[0])) result += ": " + lua_type(function.arguments[0]);
            return result;
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
        result += parameter + (i >= required ? "?" : "") + ": " + lua_type(function.parameters[i]);
    }
    result += ")";
    if (!function.is_constructor && !is_void(function.result)) result += ": " + lua_type(function.result);
    return result;
}

std::string Generator::lua_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "float" || value.kind == "constant") return value.value;
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "nil";
    if (value.kind == "string") return value.value;
    if (value.kind == "char") return "\"" + value.value + "\"";
    if (value.kind == "enum") {
        const auto& target = referent(type);
        const auto found = enum_names_.find(target.declaration);
        const auto label = renamed(target.declaration + "::" + value.value, value.value);
        return found != enum_names_.end() ? name_ + "." + found->second + "." + label : label;
    }
    if (value.kind == "default") {
        const auto& target = referent(type);
        if (bound(target)) return member_type(target.declaration) + "()";
        const auto shown = lua_type(target);
        if (shown.size() > 2 && (shown.compare(shown.size() - 2, 2, "[]") == 0 || shown.rfind("table<", 0) == 0 || shown.front() == '[')) return "{}";
        if (shown == "string") return "\"\"";
        if (shown == "number" || shown == "integer") return "0";
        if (shown == "boolean") return "false";
    }
    return {};
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_lua::Return::copy" : "rosetta_lua::Return::automatic";
    return "rosetta_lua::Return::" + found->second;
}

std::string Generator::class_lua_name(const Record& record) const {
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
            throw BindingError("unknown class in Lua policy: " + selection.first);
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
        if (!record.complete) { if (listed) reject(owner, "Lua requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed Lua results are not yet supported"); continue;
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
        plan.member = lua_safe(class_lua_name(*plan.record));
        plan.parent = plan.record->owner;
    }
    for (auto& [owner, plan] : classes_) {
        std::string name = plan.member;
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            name = classes_.at(parent).member + "_" + name;
        plan.lua = name;
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

// A C++ subclass whose virtual methods call the Lua object's overrides, or the native implementation.
void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << director_name(plan) << " final : " << owner << ", rosetta_lua::Director {\n"
        << "    template<class... A> explicit " << director_name(plan) << "(A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    for (const auto* method : methods) {
        if (!callback_supported(*method)) continue;
        std::string key = method->name + "(";
        for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
        key += method->is_const ? ")const" : ")";
        const bool is_pure = pure[key];
        if (method->access == "private" && !is_pure) continue;
        const auto lname = renamed(method->qualified_name(), lua_safe(method->name));
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
        out << "        if (rosetta_has_self()) {\n"
            << "            lua_State* L = rosetta_thread();\n"
            << "            if (rosetta_override(L, " << quote(lname) << "))\n"
            << "                return rosetta_lua::invoke_override<" << result << types << ">(L" << arguments << ");\n"
            << "        }\n";
        if (is_pure) out << "        rosetta_lua::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (const std::exception& error) { std::fprintf(stderr, \"error in noexcept Lua override: %s\\n\", error.what()); }\n"
                << "        catch (...) {}\n        std::terminate();\n";
        out << "    }\n";
    }
    out << "};\n";
}

// One overload of a group: arguments are checked (several overloads) and converted, and the
// native call's results pushed. kind: "method" (self at 1), "constructor" (class at 1) or "function".
void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks) {
    const auto& function = *overload.function;
    const std::size_t skip = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - skip;
    const std::size_t required = function.parameters.size() - function.default_arguments - skip;
    const int base = kind == "method" || kind == "constructor" || overload.receives_self ? 2 : 1;
    const std::string indent = "            ";
    const auto owner = overload.owner;
    out << indent << "{\n"
        << indent << "    const int raw = top - " << base - 1 << ";\n"
        << indent << "    int given = raw; while (given > 0 && lua_isnil(L, " << base - 1 << " + given)) --given;\n"
        << indent << "    const int n = std::max(given, " << required << "); (void)n;\n"
        << indent << "    if (raw >= " << required << " && given <= " << count;
    if (checks && kind == "method") out << "\n" << indent << "        && rosetta_lua::is_instance<" << owner << ">(L, 1)";
    if (checks && overload.receives_self) out << "\n" << indent << "        && rosetta_lua::Param<" << function.parameters[0].canonical << ">::check(L, 1, false)";
    for (std::size_t i = 0; i < count && checks; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || rosetta_lua::Param<" << function.parameters[i + skip].canonical
            << ">::check(L, " << base + static_cast<int>(i) << ", strict))";
    out << ") {\n";
    if (kind == "method") out << indent << "        auto* native = rosetta_lua::native<" << owner << ">(L, 1);\n";
    if (overload.receives_self)
        out << indent << "        rosetta_lua::converting() = \"self\";\n"
            << indent << "        rosetta_lua::Param<" << function.parameters[0].canonical << "> self_param(L, 1);\n"
            << indent << "        rosetta_lua::converting() = nullptr;\n";
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& name = i + skip < function.parameter_names.size() ? function.parameter_names[i + skip] : std::string{};
            out << indent << "            rosetta_lua::converting() = " << quote(name.empty() ? "argument " + std::to_string(i + 1) : "argument '" + name + "'") << ";\n"
                << indent << "            rosetta_lua::Param<" << function.parameters[i + skip].canonical << "> p" << i << "(L, " << base + static_cast<int>(i) << ");\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (arity > 0) out << indent << "            rosetta_lua::converting() = nullptr;\n";
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            if (plan.director)
                out << indent << "            if (subclass) {\n"
                    << indent << "                auto* created = new " << director_name(plan) << "(" << arguments << ");\n"
                    << indent << "                rosetta_lua::push_pointer(L, static_cast<" << owner << "*>(created), &rosetta_lua::Info<" << owner << ">::value, rosetta_lua::Ownership::owned, 0);\n"
                    << indent << "                rosetta_lua::adopt_instance(L, -1, 1, created);\n"
                    << indent << "                return 1;\n"
                    << indent << "            }\n";
            if (plan.record->abstract)
                out << indent << "            rosetta_lua::fail(" << quote(plan.lua + " is abstract; implement it in Lua with " + plan.lua + ":extend{...}") << ");\n";
            else if (function.access != "public")
                out << indent << "            rosetta_lua::fail(" << quote(plan.lua + " constructor is protected; subclass it with " + plan.lua + ":extend{...}") << ");\n";
            else
                out << indent << "            rosetta_lua::push_pointer(L, new " << owner << "(" << arguments << "), &rosetta_lua::Info<" << owner
                    << ">::value, rosetta_lua::Ownership::owned, 0);\n"
                    << indent << "            if (subclass) rosetta_lua::adopt_instance(L, -1, 1, nullptr);\n"
                    << indent << "            return 1;\n";
        } else {
            std::string body = kind == "method" ? "return native->" + function.name + "(" + arguments + ");"
                                                : "return ::" + function.qualified_name() + "(" + arguments + ");";
            // On a Lua subclass object, a virtual method runs the native implementation, so that an
            // override calling Base.method(self) does not dispatch back to itself.
            if (kind == "method" && function.is_virtual && policy_.directors != "none")
                body = "if (rosetta_lua::is_director(L, 1)) { " + (function.is_pure
                    ? "rosetta_lua::pure_virtual(" + quote(owner + "::" + function.name) + ");"
                    : "return native->" + owner + "::" + function.name + "(" + arguments + ");") + " } " + body;
            std::string writebacks;
            for (std::size_t i = 0; i < arity; ++i) {
                const auto& type = function.parameters[i + skip];
                if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                    writebacks += indent + "            p" + std::to_string(i) + ".writeback(L, " + std::to_string(base + static_cast<int>(i)) + ");\n";
            }
            const auto invocation = "rosetta_lua::call(L, [&]() -> decltype(auto) { " + body + " }, " + (parent ? "1" : "0") + ", " + policy + ")";
            if (writebacks.empty()) out << indent << "            return " << invocation << ";\n";
            else out << indent << "            const int results = " << invocation << ";\n" << writebacks << indent << "            return results;\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
}

void Generator::emit_group(std::ostream& out, Group& group, bool is_method) {
    group.symbol = symbol();
    const bool single = group.overloads.size() == 1;
    out << "int " << group.symbol << "(lua_State* L) {\n"
        << "    return rosetta_lua::boundary(L, [&]() -> int {\n";
    // Lua passes the operand twice to unary metamethods.
    if (group.name == "__unm" || group.name == "__bnot") out << "        lua_settop(L, 1);\n";
    // obj.method(...) instead of obj:method(...) passes no object.
    if (is_method && group.name.rfind("__", 0) != 0) {
        const auto& owner = group.overloads.front().owner;
        const auto& lua = classes_.at(owner).lua;
        out << "        if (!rosetta_lua::is_instance<" << owner << ">(L, 1)) rosetta_lua::fail("
            << quote(lua + "." + group.name + ": expected a " + lua + " object as self; call it as object:" + group.name + "(...)") << ");\n";
    }
    out << "        const int top = lua_gettop(L);\n"
        << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads) emit_overload(out, overload, overload.extension ? "function" : is_method ? "method" : "function", !single);
    out << "        }\n";
    if (group.name == "__eq") {
        // Objects no operator== overload compares are unequal.
        out << "        lua_pushboolean(L, 0);\n        return 1;\n";
    } else {
        std::string signatures;
        for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
        out << "        rosetta_lua::no_overload(L, " << quote(group.name) << ", " << quote(signatures) << ", " << (is_method ? 2 : 1) << ");\n";
    }
    out << "    });\n}\n";
}

void Generator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor = symbol();
    const bool single = plan.constructors.size() == 1;
    if (plan.director) emit_director(out, plan);
    out << "int " << plan.constructor << "(lua_State* L) {\n"
        << "    return rosetta_lua::boundary(L, [&]() -> int {\n"
        << "        const bool subclass = rosetta_lua::subclass_call(L, 1); (void)subclass;\n"
        << "        const int top = lua_gettop(L);\n"
        << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", !single);
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, plan.lua) + "\n";
    out << "        }\n        rosetta_lua::no_overload(L, " << quote(plan.lua) << ", " << quote(signatures) << ", 2);\n"
        << "    });\n}\n";
}

// LuaCATS parameters of an overload: the Lua parameter list, and @param lines.
std::string Generator::meta_parameters(const Overload& overload, std::string* annotations) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::string list;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        auto name = cpp.empty() ? "arg" + std::to_string(i - first) : lua_safe(cpp);
        if (!identifier(name)) name = "arg" + std::to_string(i - first);
        list += (i > first ? ", " : "") + name;
        if (!annotations) continue;
        std::string text;
        for (const auto& item : function.documentation.params) if (item.name == cpp) text = one_line(item.text);
        if (i >= required) {
            const auto shown = lua_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            if (!shown.empty()) text += (text.empty() ? "" : " ") + std::string("(default: `") + shown + "`)";
        }
        *annotations += "---@param " + name + (i >= required ? "?" : "") + " " + lua_type(function.parameters[i]) + (text.empty() ? "" : " " + text) + "\n";
    }
    return list;
}

// A function or method: description, @param/@return of the first overload, the others as @overload.
std::string Generator::meta_function(const Group& group, const std::string& owner, const std::string& prefix, bool method) const {
    std::string out;
    std::set<std::string> seen;
    std::string description;
    for (const auto& overload : group.overloads) {
        const auto& documentation = overload.function->documentation;
        if (documentation.empty() || !seen.insert(plain_documentation(documentation)).second) continue;
        const auto text = meta_description(documentation);
        if (!text.empty()) description += (description.empty() ? "" : "\n\n") + text;
    }
    if (description.empty()) description = "C++ `" + group.overloads.front().function->qualified_name() + "`";
    out += meta_lines(description);
    const auto& main = group.overloads.front();
    std::string annotations;
    const auto parameters = meta_parameters(main, &annotations);
    out += annotations;
    const auto& function = *main.function;
    if (!is_void(function.result)) {
        std::string text = one_line(function.documentation.returns);
        out += "---@return " + lua_type(function.result) + (text.empty() ? "" : " # " + text) + "\n";
    }
    std::set<std::string> overloads{signature(main, "fun")};
    for (std::size_t i = 1; i < group.overloads.size(); ++i) {
        const auto line = signature(group.overloads[i], "fun");
        if (overloads.insert(line).second) out += "---@overload " + line + "\n";
    }
    (void)owner;
    out += "function " + prefix + (method ? ":" : ".") + group.name + "(" + parameters + ") end\n\n";
    return out;
}

std::string Generator::meta_class(const ClassPlan& plan) const {
    const auto type = name_ + "." + plan.lua;
    std::string out;
    auto description = meta_description(plan.record->documentation);
    if (description.empty()) description = "C++ class `" + plan.record->qualified_name() + "`";
    if (plan.constructors.empty())
        description += plan.record->abstract
            ? std::string("\n\nAbstract: create objects of a derived class") + (plan.director ? ", or implement it in Lua with `" + plan.lua + ":extend{...}`." : ".")
            : "\n\nNo public constructor.";
    if (plan.director) description += "\n\nSubclass it in Lua with `" + plan.lua + ":extend{...}`: methods of the subclass override the C++ virtual methods.";
    out += meta_lines(description);
    out += "---@class " + type;
    for (std::size_t i = 0; i < plan.bases.size(); ++i) out += (i ? ", " : ": ") + member_type(plan.bases[i]);
    out += "\n";
    for (const auto& field : plan.fields) {
        const auto text = one_line(field.field->documentation.brief);
        out += "---@field " + field.name + " " + lua_type(field.field->type) + (text.empty() ? "" : " " + text) + "\n";
    }
    std::set<std::string> constructors;
    for (const auto& overload : plan.constructors) {
        const auto line = signature(overload, "fun") + ": " + type;
        if (constructors.insert(line).second) out += "---@overload " + line + "\n";
    }
    out += name_ + "." + plan.lua + " = {}\n\n";
    for (const auto& constant : plan.constants) {
        const auto text = constant.documentation ? meta_description(*constant.documentation) : std::string();
        if (!text.empty()) out += meta_lines(text);
        out += "---@type " + lua_type(*constant.type) + "\n" + type + "." + constant.name + " = nil\n\n";
    }
    if (plan.director)
        out += meta_lines("A Lua subclass of " + plan.lua + ": its methods override the C++ virtual methods.\n\n"
                          "```lua\nlocal Mine = " + name_ + "." + plan.lua + ":extend{}\nfunction Mine:method(...) ... end\nlocal object = Mine(...)\n```") +
               "---@param methods? table\n---@return table\nfunction " + type + ":extend(methods) end\n\n";
    for (const auto& group : plan.methods) out += meta_function(group, plan.record->qualified_name(), type, !group.is_static);
    return out;
}

GeneratedLuaSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    GeneratedLuaSource result;
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
    // Enumeration names come first: types in signatures and stubs refer to them.
    std::map<std::string, std::string> names; // module-level Lua name -> what
    const auto reserve = [&](const std::string& lname, const std::string& what) {
        const auto inserted = names.emplace(lname, what);
        if (!inserted.second) throw BindingError("Lua export name collision: " + lname + " (" + inserted.first->second + " and " + what + ")");
    };
    reserve("ref", "Rosetta runtime");
    for (const auto& owner : class_order_) reserve(classes_.at(owner).lua, "class " + owner);
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        auto lname = lua_safe(renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) lname = classes_.at(enumeration.owner).lua + "_" + lname;
        if (names.count(lname)) {
            if (!flat_) throw BindingError("Lua export name collision: " + lname);
            reject(qualified, "enum name collides with another export");
            continue;
        }
        reserve(lname, "enum " + qualified);
        enum_names_[qualified] = lname;
    }

    // Free functions attached to classes as methods (policy extend), and free operators.
    std::map<std::string, std::vector<Group>> extensions;
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner) && excluded(owner)) continue;
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [lname, target] : methods) {
            Group group;
            group.name = lua_safe(lname);
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
                if (!why.empty()) { reject(owner + "." + lname, why); continue; }
                if (!self) any_static = true;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    // Free operators become metamethods of the exported classes among their operands.
    std::map<std::string, std::vector<std::pair<std::string, Overload>>> free_operators; // class -> (metamethod, overload)
    for (const auto& function : module_.functions) {
        if (!is_operator(function.name) || extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        const auto arity = function.parameters.size();
        if (arity == 0 || arity > 2) continue;
        const auto meta = metamethod(function.name, arity - 1);
        if (meta.empty() || meta == "__index_op" || meta == "__call") continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        std::set<std::string> owners;
        for (const auto& parameter : function.parameters) {
            const auto& target = parameter.kind == TypeKind::pointer && !parameter.arguments.empty() ? parameter.arguments[0] : referent(parameter);
            if (bound(target)) owners.insert(target.declaration);
        }
        for (const auto& owner : owners) free_operators[owner].push_back({meta, {&function, owner, true, false}});
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
                if (found == record.methods.end()) throw BindingError("unknown Lua policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Lua policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown Lua policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Lua policy field is not public: " + owner + "::" + requested);
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
        std::map<std::string, std::size_t> group_index, meta_index;
        const auto add = [&](const std::string& lname, const Overload& overload, bool is_static) {
            const auto found = group_index.find(lname);
            if (found == group_index.end()) {
                group_index[lname] = plan.methods.size();
                plan.methods.push_back({lname, {overload}, is_static, {}, {}});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a Lua name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        const auto add_meta = [&](const std::string& meta, const Overload& overload) {
            const auto found = meta_index.find(meta);
            if (found == meta_index.end()) {
                meta_index[meta] = plan.metamethods.size();
                plan.metamethods.push_back({meta, {overload}, false, {}, {}});
            } else plan.metamethods[found->second].overloads.push_back(overload);
        };
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            if (is_operator(method.name)) {
                if (method.is_static) continue;
                const auto meta = metamethod(method.name, method.parameters.size());
                if (meta.empty()) continue; // assignment, conversion and other operators stay C++-only
                auto why = function_problem(method, false);
                if (!why.empty()) { reject(method.qualified_name(), why); continue; }
                add_meta(meta, {&method, owner});
                continue;
            }
            const auto lname = renamed(method.qualified_name(), lua_safe(method.name));
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(lname, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner]) for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        for (const auto& [meta, overload] : free_operators[owner]) add_meta(meta, overload);
        std::set<std::string> taken{"extend"};
        for (const auto& group : plan.methods) taken.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported Lua field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported Lua field type", 0) == 0 ? why : "unsupported Lua field type: " + why); continue; }
            const auto lname = renamed(qualified, lua_safe(field.name));
            if (!taken.insert(lname).second) { reject(qualified, "field name collides with a method of the same name"); continue; }
            plan.fields.push_back({&field, lname, {}, {}});
        }
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to Lua"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            const auto lname = renamed(qualified, lua_safe(variable.name));
            if (!taken.insert(lname).second) { reject(qualified, "constant name collides with another member"); continue; }
            plan.constants.push_back({lname, qualified, &variable.type, &variable.documentation});
        }
    }

    // Module functions grouped by Lua name.
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
        const auto lname = specialization != function_template_names_.end() ? specialization->second
            : renamed(function.qualified_name(), lua_safe(!submodule.empty() || flat(function.namespaces) ? function.name
                                                                                    : exported(function.qualified_name())));
        const auto key = submodule + "." + lname;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions.size();
            functions.push_back({lname, {{&function, {}}}, false, {}, submodule});
        } else functions[found->second].overloads.push_back({&function, {}});
    }
    // Submodules are module fields too; their functions are unique within each submodule.
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
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to Lua"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto lname = renamed(qualified, lua_safe(flat(variable.namespaces) ? variable.name : exported(qualified)));
        if (names.count(lname)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(lname, "constant " + qualified);
        constants.push_back({lname, "::" + qualified, &variable.type, &variable.documentation});
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Lua";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Using-declarations re-export bound entities under another namespace's name.
    std::vector<std::pair<std::string, std::string>> aliases; // Lua alias -> Lua target
    {
        std::map<std::string, std::string> function_names;
        for (const auto& group : functions)
            if (group.module.empty())
                for (const auto& overload : group.overloads) function_names.emplace(overload.function->qualified_name(), group.name);
        for (const auto& reexport : module_.reexports) {
            std::string target;
            if (reexport.kind == "record" || reexport.kind == "type") { const auto c = classes_.find(reexport.target); if (c != classes_.end()) target = c->second.lua; }
            if (reexport.kind == "enum" || reexport.kind == "type") { const auto e = enum_names_.find(reexport.target); if (e != enum_names_.end()) target = e->second; }
            else if (reexport.kind == "function") { const auto f = function_names.find(reexport.target); if (f != function_names.end()) target = f->second; }
            if (target.empty()) continue;
            std::string scope;
            for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
            const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
            const auto lname = renamed(qualified, lua_safe(flat(reexport.namespaces) ? reexport.name : exported(qualified)));
            if (lname == target || names.count(lname)) continue;
            reserve(lname, "alias of " + reexport.target);
            aliases.emplace_back(lname, target);
        }
    }

    // C++ sources: the prelude (runtime and type traits), then one unit per function group or class.
    struct Unit { std::string code; };
    std::vector<Unit> units;
    std::ostringstream out;
    out << "// Generated by Rosetta: Lua module " << name_ << " (full API).\n"
        << "extern \"C\" {\n#include <lua.h>\n#include <lauxlib.h>\n}\n";
    for (const auto* header : {"algorithm", "array", "chrono", "climits", "cmath", "complex", "cstdio", "cstring", "deque", "exception", "filesystem",
                               "functional", "limits", "list", "map", "memory", "new", "optional", "set", "stdexcept", "string", "string_view",
                               "tuple", "typeindex", "type_traits", "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n'
        << "#define ROSETTA_LUA_MODULE rosetta_lua_module_" << name_ << '\n'
        << "#define ROSETTA_LUA_CALLABLE " << quote("rosetta.callable:" + name_) << '\n';
    for (const auto* part : detail::lua_full_runtime_parts) out << part;
    out << "namespace ROSETTA_LUA_MODULE { namespace rosetta_lua {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(lua_State* L, int index, bool strict) { return Value<" << to << ">::check(L, index, strict); }\n"
            << "    static " << from << " load(lua_State* L, int index) { return " << from << "(Value<" << to << ">::load(L, index)); }\n"
            << "    static void push(lua_State* L, const " << from << "& value) { Value<" << to << ">::push(L, " << to << "(value)); }\n"
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
        for (auto& group : plan.methods) emit_group(unit, group, !group.is_static);
        for (auto& group : plan.metamethods) emit_group(unit, group, true);
        for (auto& field : plan.fields) {
            const auto& f = *field.field;
            const auto qualified = owner + "::" + f.name;
            field.getter = symbol();
            unit << "int " << field.getter << "(lua_State* L) {\n    return rosetta_lua::boundary(L, [&]() -> int {\n"
                 << "        auto* native = rosetta_lua::native<" << owner << ">(L, 1);\n"
                 << "        rosetta_lua::result<decltype((native->" << f.name << "))>(L, native->" << f.name
                 << ", 1, rosetta_lua::Return::" << (copy_type(f.type) ? "copy" : "automatic") << ");\n        return 1;\n    });\n}\n";
            const bool writable = !f.type.is_const && !readonly(qualified, owner) && f.type.kind != TypeKind::lvalue_reference;
            field.setter = symbol();
            unit << "int " << field.setter << "(lua_State* L) {\n    return rosetta_lua::boundary(L, [&]() -> int {\n"
                 << "        auto* native = rosetta_lua::native<" << owner << ">(L, 1);\n";
            if (writable)
                unit << "        using F = std::remove_cv_t<std::remove_reference_t<decltype(native->" << f.name << ")>>;\n"
                     << "        if constexpr (rosetta_lua::bound_v<F>) {\n"
                     << "            // obj.field.x = v assigns the modified field object to itself.\n"
                     << "            if (rosetta_lua::is_instance<F>(L, 2) && rosetta_lua::native<F>(L, 2) == &native->" << f.name << ") return 0;\n"
                     << "        }\n"
                     << "        rosetta_lua::converting() = " << quote("field '" + field.name + "'") << ";\n"
                     << "        rosetta_lua::Param<const F&> converted(L, 2);\n"
                     << "        rosetta_lua::converting() = nullptr;\n"
                     << "        rosetta_lua::assign_field(native->" << f.name << ", converted.get());\n";
            else
                unit << "        (void)native;\n        rosetta_lua::fail(" << quote("field " + field.name + " is read-only") << ");\n";
            unit << "        return 0;\n    });\n}\n";
        }
        if (!plan.constructors.empty()) emit_constructor(unit, plan);
        units.push_back({unit.str()});
    }

    // Module setup: classes (metatables, class tables), functions, enums and constants.
    std::ostringstream setup, registration;
    for (const auto& owner : class_order_) {
        registration << "        rosetta_lua::register_type<" << owner << ">(" << quote(classes_.at(owner).lua) << ");\n";
        for (const auto& base : classes_.at(owner).bases) registration << "        rosetta_lua::add_base<" << owner << ", " << base << ">();\n";
    }
    for (const auto& [qualified, lname] : enum_names_) {
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
        registration << "        rosetta_lua::EnumTable<" << qualified << ">::entries = {" << entries << "};\n";
    }
    // Members of a class, its bases' members included (the class's own come first).
    const auto collect = [&](const std::string& owner, auto member) {
        std::vector<std::pair<std::string, std::string>> result;
        std::set<std::string> taken;
        const std::function<void(const std::string&, unsigned)> walk = [&](const std::string& cls, unsigned depth) {
            if (depth > 64) return;
            for (const auto& item : member(classes_.at(cls)))
                if (taken.insert(item.first).second) result.push_back(item);
            for (const auto& base : classes_.at(cls).bases) walk(base, depth + 1);
        };
        walk(owner, 0);
        return result;
    };
    const auto registry = [](std::ostream& stream, const std::vector<std::pair<std::string, std::string>>& items) {
        stream << "            static const luaL_Reg members[] = {";
        for (const auto& [lname, function] : items) stream << "{" << quote(lname) << ", " << function << "}, ";
        stream << "{nullptr, nullptr}};\n            luaL_setfuncs(L, members, 0);\n";
    };
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const auto methods = collect(owner, [](const ClassPlan& p) {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& group : p.methods) items.emplace_back(group.name, group.symbol);
            return items;
        });
        const auto getters = collect(owner, [](const ClassPlan& p) {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& field : p.fields) items.emplace_back(field.name, field.getter);
            return items;
        });
        const auto setters = collect(owner, [](const ClassPlan& p) {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& field : p.fields) items.emplace_back(field.name, field.setter);
            return items;
        });
        const auto metamethods = collect(owner, [](const ClassPlan& p) {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& group : p.metamethods) items.emplace_back(group.name, group.symbol);
            return items;
        });
        setup << "    { // " << owner << "\n"
              << "        lua_newtable(L);\n        const int cls = lua_gettop(L);\n"
              << "        lua_pushboolean(L, 1);\n        lua_rawsetp(L, cls, &rosetta_lua::native_key());\n";
        if (!methods.empty()) { setup << "        {\n"; registry(setup, methods); setup << "        }\n"; }
        if (std::none_of(methods.begin(), methods.end(), [](const auto& m) { return m.first == "extend"; }))
            setup << "        lua_pushcfunction(L, rosetta_lua::class_extend);\n        lua_setfield(L, cls, \"extend\");\n";
        for (const auto& constant : plan.constants)
            setup << "        rosetta_lua::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(L, static_cast<std::remove_cv_t<decltype("
                  << constant.expression << ")>>(" << constant.expression << "), 0, rosetta_lua::Return::copy);\n"
                  << "        lua_setfield(L, cls, " << quote(constant.name) << ");\n";
        // Instance metatable.
        setup << "        lua_newtable(L);\n        const int meta = lua_gettop(L);\n"
              << "        lua_pushboolean(L, 1);\n        lua_rawsetp(L, meta, &rosetta_lua::tag_key());\n"
              << "        lua_pushliteral(L, " << quote(plan.lua) << ");\n        lua_setfield(L, meta, \"__name\");\n"
              << "        lua_pushvalue(L, cls);\n        lua_newtable(L);\n";
        if (!getters.empty()) { setup << "        {\n"; registry(setup, getters); setup << "        }\n"; }
        std::string index_op;
        for (const auto& [meta, function] : metamethods) if (meta == "__index_op") index_op = function;
        if (index_op.empty()) setup << "        lua_pushnil(L);\n";
        else setup << "        lua_pushcfunction(L, " << index_op << ");\n";
        setup << "        lua_pushcclosure(L, rosetta_lua::object_index, 3);\n        lua_setfield(L, meta, \"__index\");\n"
              << "        lua_newtable(L);\n";
        if (!setters.empty()) { setup << "        {\n"; registry(setup, setters); setup << "        }\n"; }
        setup << "        lua_pushliteral(L, " << quote(plan.lua) << ");\n"
              << "        lua_pushcclosure(L, rosetta_lua::object_newindex, 2);\n        lua_setfield(L, meta, \"__newindex\");\n"
              << "        lua_pushcfunction(L, rosetta_lua::object_gc);\n        lua_setfield(L, meta, \"__gc\");\n"
              << "        lua_pushcfunction(L, rosetta_lua::object_gc);\n        lua_setfield(L, meta, \"__close\");\n";
        bool has_eq = false;
        for (const auto& [meta, function] : metamethods) {
            if (meta == "__index_op") continue;
            has_eq = has_eq || meta == "__eq";
            setup << "        lua_pushcfunction(L, " << function << ");\n        lua_setfield(L, meta, " << quote(meta) << ");\n";
        }
        if (!has_eq) setup << "        lua_pushcfunction(L, rosetta_lua::object_eq);\n        lua_setfield(L, meta, \"__eq\");\n";
        // tostring(obj) uses a to_string() method when the class has one; #obj uses size().
        const auto method_symbol = [&](const std::string& lname) -> std::string {
            for (const auto& [name, function] : methods) if (name == lname) return function;
            return {};
        };
        // Whether the class (or a base) has an instance method `lname` callable without arguments.
        const std::function<bool(const std::string&, const std::string&, unsigned)> zero_arity_in =
            [&](const std::string& c, const std::string& lname, unsigned depth) -> bool {
            if (depth > 64) return false;
            for (const auto& group : classes_.at(c).methods)
                if (group.name == lname && !group.is_static)
                    return std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                        return o.function->parameters.size() == o.function->default_arguments + (o.receives_self ? 1 : 0);
                    });
            for (const auto& base : classes_.at(c).bases) if (zero_arity_in(base, lname, depth + 1)) return true;
            return false;
        };
        const auto zero_arity = [&](const std::string& lname) { return zero_arity_in(owner, lname, 0); };
        const auto to_string = method_symbol("to_string");
        if (!to_string.empty() && zero_arity("to_string"))
            setup << "        lua_pushcfunction(L, " << to_string << ");\n";
        else setup << "        lua_pushcfunction(L, rosetta_lua::object_tostring);\n";
        setup << "        lua_setfield(L, meta, \"__tostring\");\n";
        const auto size = method_symbol("size");
        if (!size.empty() && zero_arity("size")) setup << "        lua_pushcfunction(L, " << size << ");\n        lua_pushcclosure(L, rosetta_lua::unary, 1);\n        lua_setfield(L, meta, \"__len\");\n";
        setup << "        lua_rawsetp(L, LUA_REGISTRYINDEX, &rosetta_lua::Info<" << owner << ">::value);\n";
        // Class metatable: calling the class constructs an object.
        setup << "        lua_createtable(L, 0, 2);\n";
        if (!plan.constructor.empty()) setup << "        lua_pushcfunction(L, " << plan.constructor << ");\n";
        else setup << "        lua_pushliteral(L, " << quote(plan.record->abstract ? plan.lua + " is abstract" : plan.lua + " has no public constructor") << ");\n"
                   << "        lua_pushcclosure(L, rosetta_lua::refuse, 1);\n";
        setup << "        lua_setfield(L, -2, \"__call\");\n"
              << "        lua_pushliteral(L, " << quote(plan.lua) << ");\n        lua_setfield(L, -2, \"__name\");\n"
              << "        lua_setmetatable(L, cls);\n"
              << "        lua_pushvalue(L, cls);\n        lua_setfield(L, module, " << quote(plan.lua) << ");\n";
        if (!plan.parent.empty())
            setup << "        lua_getfield(L, module, " << quote(classes_.at(plan.parent).lua) << ");\n"
                  << "        lua_pushvalue(L, cls);\n        lua_setfield(L, -2, " << quote(plan.member) << ");\n        lua_pop(L, 1);\n";
        setup << "        lua_settop(L, cls - 1);\n    }\n";
    }
    // Top-level functions go into the module table; each policy submodule is a table of its functions.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> module_items;
    for (const auto& group : functions) module_items[group.module].emplace_back(group.name, group.symbol);
    for (const auto& [submodule, items] : module_items) {
        setup << "    {\n";
        if (!submodule.empty()) setup << "        lua_createtable(L, 0, " << items.size() << ");\n";
        std::ostringstream reg;
        registry(reg, items);
        setup << reg.str();
        if (!submodule.empty()) setup << "        lua_setfield(L, module, " << quote(submodule) << ");\n";
        setup << "    }\n";
    }
    for (const auto& [qualified, lname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        setup << "    lua_createtable(L, 0, " << enumeration.enumerators.size() << ");\n";
        for (const auto& enumerator : enumeration.enumerators) {
            const auto label = renamed(qualified + "::" + enumerator, enumerator);
            setup << "    rosetta_lua::Value<" << qualified << ">::push(L, " << qualified << "::" << enumerator << ");\n"
                  << "    lua_setfield(L, -2, " << quote(label) << ");\n";
        }
        if (!enumeration.owner.empty()) {
            const auto member = lua_safe(renamed(qualified, enumeration.name));
            setup << "    lua_getfield(L, module, " << quote(classes_.at(enumeration.owner).lua) << ");\n"
                  << "    lua_pushvalue(L, -2);\n    lua_setfield(L, -2, " << quote(member) << ");\n    lua_pop(L, 1);\n";
        }
        setup << "    lua_setfield(L, module, " << quote(lname) << ");\n";
    }
    for (const auto& constant : constants)
        setup << "    rosetta_lua::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(L, static_cast<std::remove_cv_t<decltype("
              << constant.expression << ")>>(" << constant.expression << "), 0, rosetta_lua::Return::copy);\n"
              << "    lua_setfield(L, module, " << quote(constant.name) << ");\n";
    for (const auto& [alias, target] : aliases)
        setup << "    lua_getfield(L, module, " << quote(target) << ");\n    lua_setfield(L, module, " << quote(alias) << ");\n";

    // Split sources by size, as the Python and R backends do.
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
        for (auto& part : extra) part << prelude << "namespace ROSETTA_LUA_MODULE {\n";
        out << "namespace ROSETTA_LUA_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) extra[bin[i]] << units[i].code;
        for (const auto& name : symbols_) out << "int " << name << "(lua_State*);\n";
        for (auto& part : extra) part << "} // namespace ROSETTA_LUA_MODULE\n";
    } else {
        out << "namespace ROSETTA_LUA_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "int rosetta_lua_open(lua_State* L) {\n"
        << "    return rosetta_lua::boundary(L, [&]() -> int {\n"
        << "        static const bool registered = [] {\n" << registration.str() << "        return true;\n        }();\n"
        << "        (void)registered;\n"
        << "        rosetta_lua::open_state(L);\n"
        << "        lua_settop(L, 0);\n"
        << "        lua_newtable(L);\n        const int module = lua_gettop(L);\n"
        << "        lua_pushcfunction(L, rosetta_lua::make_ref);\n        lua_setfield(L, module, \"ref\");\n";
    // Indent the setup code into the lambda.
    {
        std::istringstream lines(setup.str());
        for (std::string line; std::getline(lines, line);) out << (line.empty() ? "" : "    ") << line << '\n';
    }
    out << "        lua_settop(L, module);\n        return 1;\n    });\n}\n"
        << "} // namespace ROSETTA_LUA_MODULE\n"
        << "extern \"C\"\n#if defined(_WIN32)\n__declspec(dllexport)\n#else\n__attribute__((visibility(\"default\")))\n#endif\n"
        << "int luaopen_" << name_ << "(lua_State* L) { return ROSETTA_LUA_MODULE::rosetta_lua_open(L); }\n";
    result.source = out.str();
    for (auto& part : extra) result.parts.push_back(part.str());

    // LuaCATS stub.
    std::ostringstream meta;
    meta << "---@meta " << name_ << "\n"
         << "-- Generated by Rosetta: documentation for the `" << name_ << "` native Lua module (hover and\n"
         << "-- completion in editors). It is never loaded at run time; require(\"" << name_ << "\") loads the\n"
         << "-- compiled module.\n\n"
         << "---@class " << name_ << "\nlocal " << name_ << " = {}\n\n"
         << "---A mutable cell for C++ reference parameters: a native function that takes a non-const\n"
         << "---reference updates `cell.value`.\n"
         << "---@param value any The initial value.\n---@return " << name_ << ".ref\n"
         << "function " << name_ << ".ref(value) end\n\n"
         << "---@class " << name_ << ".ref\n---@field value any\n\n";
    for (const auto& [qualified, lname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        auto description = meta_description(enumeration.documentation);
        if (description.empty()) description = "C++ enumeration `" + qualified + "`";
        description += "\n\nArguments of this type also accept the enumerator names as strings.";
        meta << meta_lines(description) << "---@enum " << name_ << "." << lname << "\n" << name_ << "." << lname << " = {\n";
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
            meta << "    " << (identifier(label) && lua_safe(label) == label ? label : "[\"" + label + "\"]") << " = " << enumeration.enumerator_values[i] << ",\n";
        }
        meta << "}\n\n";
    }
    for (const auto& owner : class_order_) meta << meta_class(classes_.at(owner));
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.parent.empty()) meta << name_ << "." << classes_.at(plan.parent).lua << "." << plan.member << " = " << name_ << "." << plan.lua << "\n";
    }
    for (const auto& [qualified, lname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        if (!enumeration.owner.empty())
            meta << name_ << "." << classes_.at(enumeration.owner).lua << "." << lua_safe(renamed(qualified, enumeration.name)) << " = " << name_ << "." << lname << "\n";
    }
    meta << "\n";
    for (const auto& group : functions) if (group.module.empty()) meta << meta_function(group, {}, name_, false);
    for (const auto& submodule : submodules) {
        meta << "---Functions of the C++ namespace mapped to `" << submodule << "` by the binding policy.\n"
             << name_ << "." << submodule << " = {}\n\n";
        for (const auto& group : functions)
            if (group.module == submodule) meta << meta_function(group, {}, name_ + "." + submodule, false);
    }
    for (const auto& constant : constants) {
        const auto text = meta_description(*constant.documentation);
        if (!text.empty()) meta << meta_lines(text);
        meta << "---@type " << lua_type(*constant.type) << "\n" << name_ << "." << constant.name << " = nil\n\n";
    }
    for (const auto& [alias, target] : aliases) meta << name_ << "." << alias << " = " << name_ << "." << target << "\n";
    meta << "return " << name_ << "\n";
    result.meta = meta.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedLuaSource generate_lua_full(const Module& module, const std::string& name, bool skip_unsupported,
                                     const BindingPolicy& policy, std::size_t parts) {
    if (parts == 0 || parts > 256) throw BindingError("Lua source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, parts).run();
}
} // namespace rosetta
