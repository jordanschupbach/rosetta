// Full Common Lisp backend (--lisp-api full): CLOS classes with inheritance, generic functions,
// runtime overload dispatch in C++, Lisp subclasses overriding C++ virtual methods (directors),
// containers, enums as keywords, callbacks, and docstrings carrying the C++ documentation.
//
// It writes a C++ source (a shared library with one entry point, rosetta_<module>_call) and a
// Lisp interface that loads it through CFFI. See lisp_full_runtime.hpp for the value encoding.
#include "rosetta/common_lisp.hpp"
#include "lisp_full_runtime.hpp"
#include "lisp_full_runtime_lisp.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
// A Lisp string literal.
std::string lisp_string(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        result += c;
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
// The Lisp spelling of a C++ name: snake_case and CamelCase become kebab-case.
std::string lisp_name(const std::string& name) {
    // "a__b" (flattened namespaces) keeps a double hyphen.
    if (const auto at = name.find("__"); at != std::string::npos && at > 0)
        return lisp_name(name.substr(0, at)) + "--" + lisp_name(name.substr(at + 2));
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool upper = std::isupper(static_cast<unsigned char>(c));
        if (upper && i > 0) {
            const char before = name[i - 1];
            const bool after_lower = std::islower(static_cast<unsigned char>(before)) || std::isdigit(static_cast<unsigned char>(before));
            const bool acronym_end = std::isupper(static_cast<unsigned char>(before)) && i + 1 < name.size() &&
                                     std::islower(static_cast<unsigned char>(name[i + 1]));
            if ((after_lower || acronym_end) && !out.empty() && out.back() != '-') out += '-';
        }
        if (c == '_') { if (!out.empty() && out.back() != '-') out += '-'; continue; }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    return out.empty() ? "unnamed" : out;
}
// Symbol text for the reader: plain when it reads back as the same (upcased) symbol.
std::string symbol_text(const std::string& name) {
    static const std::set<std::string> operators = {"+", "-", "*", "/", "=", "<", "<=", ">", ">="};
    if (operators.count(name)) return name;
    const bool plain = !name.empty() && std::isalpha(static_cast<unsigned char>(name[0])) &&
        std::all_of(name.begin(), name.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '*' || c == '%'; });
    if (plain) return name;
    std::string upper;
    for (char c : name) {
        if (c == '|' || c == '\\') upper += '\\';
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return "|" + upper + "|";
}
// Lambda-list variable names: CL constants cannot be bound.
std::string variable_name(std::string name) {
    static const std::set<std::string> constants = {"t", "nil", "pi", "most-positive-fixnum", "most-negative-fixnum",
        "call-arguments-limit", "lambda-parameters-limit", "multiple-values-limit", "array-rank-limit",
        "array-dimension-limit", "array-total-size-limit", "char-code-limit", "lambda-list-keywords", "args"};
    if (constants.count(name)) name += "-arg";
    return name;
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
// The generic function of a C++ operator with `arity` operands besides the first ("" when none).
std::string operator_generic(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {nullptr, "+"}}, {"operator-", {"-", "-"}}, {"operator*", {nullptr, "*"}},
        {"operator/", {nullptr, "/"}}, {"operator%", {nullptr, "mod"}}, {"operator==", {nullptr, "="}},
        {"operator<", {nullptr, "<"}}, {"operator<=", {nullptr, "<="}}, {"operator&", {nullptr, "logand"}},
        {"operator|", {nullptr, "logior"}}, {"operator^", {nullptr, "logxor"}}, {"operator~", {"lognot", nullptr}},
        {"operator[]", {nullptr, "aref"}}};
    if (name == "operator()") return "funcall";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
// The COMMON-LISP function an operator generic falls back to for Lisp numbers.
std::string operator_fallback(const std::string& generic) {
    static const std::map<std::string, std::string> fallback = {{"+", "+"}, {"-", "-"}, {"*", "*"}, {"/", "/"}, {"mod", "mod"},
        {"=", "="}, {"<", "<"}, {"<=", "<="}, {"logand", "logand"}, {"logior", "logior"}, {"logxor", "logxor"}, {"lognot", "lognot"},
        {"aref", "aref"}, {"funcall", "funcall"}};
    const auto found = fallback.find(generic);
    return found == fallback.end() ? "" : found->second;
}

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;
    bool extension = false;    // a free function (policy extend, or a free operator)
    bool receives_self = false;
};
struct Group {
    std::string name;          // Lisp name (generic function, or function)
    std::vector<Overload> overloads;
    bool is_static = false;
    std::string symbol;        // generated C++ function
    std::size_t index = 0;     // its index in the native function table
    std::string module;        // policy submodule of a module function ("" = the main package)
};
struct FieldPlan { const Field* field; std::string name; std::string getter, setter; std::size_t getter_index = 0, setter_index = 0; };
struct ConstantPlan { std::string name, expression; const Type* type; const Documentation* documentation; std::string symbol; std::size_t index = 0; };
struct ClassPlan {
    const Record* record;
    std::string lisp;          // Lisp class name (outer-inner for nested classes)
    std::vector<std::string> bases;
    std::vector<Overload> constructors;
    std::vector<Group> methods;  // instance methods (generic functions) and static methods
    std::vector<Group> operators;
    std::vector<FieldPlan> fields;
    std::vector<ConstantPlan> constants;
    std::string parent;
    std::string constructor;   // generated C++ function, empty without a public constructor
    std::size_t constructor_index = 0;
    bool make_function = true; // make-<class> (unless a function of that name exists)
    bool director = false;
};

// --- documentation -----------------------------------------------------------

std::string doc_block(const DocBlock& block) {
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
std::string one_line(std::string text) {
    for (auto& c : text) if (c == '\n') c = ' ';
    return text;
}
// Description of a declaration: brief, details, notes, errors, see also.
std::string description(const Documentation& doc, const std::string& package) {
    std::vector<std::string> paragraphs;
    if (!doc.brief.empty()) paragraphs.push_back(doc.brief);
    for (const auto& block : doc.details) paragraphs.push_back(doc_block(block));
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        paragraphs.push_back(label + ": " + item.text);
    }
    if (!doc.throws.empty()) {
        std::string errors = "Errors (signalled as " + package + ":native-error):";
        for (const auto& item : doc.throws)
            errors += "\n  " + (item.name.empty() ? std::string() : item.name + (item.text.empty() ? "" : ": ")) + one_line(item.text);
        paragraphs.push_back(errors);
    }
    if (!doc.see.empty()) {
        std::string see = "See also: ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + doc.see[i];
        paragraphs.push_back(see);
    }
    std::string out;
    for (const auto& paragraph : paragraphs) out += (out.empty() ? "" : "\n\n") + paragraph;
    return out;
}

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const LispFullOptions& options)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), options_(options) { validate_modules(policy_, module_); }
    GeneratedLispSource run();

private:
    const Module& module_;
    std::string name_;
    std::string package_;        // the public package's name in the interface (lower case)
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    LispFullOptions options_;
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_;
    std::map<std::string, std::string> function_template_names_;
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
    std::map<std::string, std::string> enum_names_; // qualified enum -> Lisp type name
    std::vector<std::string> symbols_;               // every generated C++ function, in table order (from 1)
    std::map<std::string, std::size_t> generic_index_; // generic function -> index in *generics* (overrides)
    std::vector<std::string> generic_order_;

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
    // The Lisp name of a C++ declaration: a policy rename is taken as written (it may already be Lisp).
    std::string renamed(const std::string& qualified, const std::string& fallback) const {
        const auto found = policy_.rename.find(qualified);
        if (found == policy_.rename.end()) return fallback;
        if (found->second.empty()) throw BindingError("empty Lisp rename target for " + qualified);
        return lisp_name(found->second);
    }
    std::string symbol(std::size_t* index) {
        symbols_.push_back("rosetta_lisp_" + std::to_string(symbols_.size() + 1));
        if (index) *index = symbols_.size();
        return symbols_.back();
    }
    std::string sym(const std::string& name) const { return package_ + ":" + symbol_text(name); }
    std::size_t generic(const std::string& name) {
        const auto found = generic_index_.find(name);
        if (found != generic_index_.end()) return found->second;
        generic_index_[name] = generic_order_.size();
        generic_order_.push_back(name);
        return generic_order_.size() - 1;
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
    std::string lisp_type(const Type& type) const;
    std::vector<std::string> parameter_names(const Overload& overload) const;
    std::string signature(const Overload& overload, const std::string& name, bool self) const;
    std::string lisp_default(const DefaultValue& value, const Type& type) const;
    std::string return_policy(const Function& function) const;
    std::string class_lisp_name(const Record& record) const;
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
    std::set<std::string> bound_method_names(const std::string& owner) const;
    void emit_director(std::ostream& out, const ClassPlan& plan);
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks);
    void emit_group(std::ostream& out, Group& group, bool is_method);
    void emit_constructor(std::ostream& out, ClassPlan& plan);
    std::string group_doc(const Group& group, const std::string& shown, bool method) const;
    std::string class_doc(const ClassPlan& plan) const;
    std::string lambda_list(const Group& group, std::string* call) const;
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
        return "unsupported Lisp value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to Lisp";
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
        return "unsupported Lisp template type " + type.canonical;
    }
    default:
        return "unsupported Lisp value type " + type.canonical;
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
        return "unsupported Lisp pointer type " + type.canonical;
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
        if (!why.empty()) return "unsupported Lisp result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported Lisp parameter type: " + why;
    }
    return {};
}

// Lisp type specifier of a C++ type, as shown in docstrings and error messages.
std::string Generator::lisp_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "string";
        return "(or null " + lisp_type(pointee) + ")";
    }
    if (is_void(target)) return "null";
    if (!conversion(target).empty()) return policy_.conversions.at(conversion(target)) == "std::string" ? "string" : "t";
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "boolean";
        if (bare == "char") return "character";
        if (bare == "float" || bare == "double" || bare == "long double") return "double-float";
        return "integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? found->second : "integer";
    }
    if (bound(target)) return classes_.at(target.declaration).lisp;
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "null";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? lisp_type(target.arguments[i]) : std::string("t"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set")
            return "(vector " + arg(0) + ")";
        if (declaration == "std::map" || declaration == "std::unordered_map") return "(hash-table " + arg(0) + " " + arg(1) + ")";
        if (declaration == "std::optional") return "(or null " + arg(0) + ")";
        if (declaration == "std::complex") return "complex";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "(list";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += " " + lisp_type(target.arguments[i]);
            return result + ")";
        }
        if (declaration == "std::variant") {
            std::string result = "(or";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += " " + lisp_type(target.arguments[i]);
            return result + ")";
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string result = "(function (";
            for (std::size_t i = 1; i < function.arguments.size(); ++i) result += (i > 1 ? " " : "") + lisp_type(function.arguments[i]);
            result += ") " + (function.arguments.empty() || is_void(function.arguments[0]) ? std::string("t") : lisp_type(function.arguments[0])) + ")";
            return result;
        }
        if (declaration == "std::chrono::duration") return "real";
    }
    return "t";
}

// Lisp names of an overload's parameters (without self), unique within it.
std::vector<std::string> Generator::parameter_names(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    std::vector<std::string> names;
    std::set<std::string> taken{"self", "value"};
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first) : variable_name(lisp_name(cpp));
        while (!taken.insert(name).second) name += "*";
        names.push_back(name);
    }
    return names;
}

// "(name self a &optional (b 10)) → type": how an overload is called.
std::string Generator::signature(const Overload& overload, const std::string& name, bool self) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    const auto names = parameter_names(overload);
    std::string result = "(" + name;
    if (self) result += " " + classes_.at(overload.owner).lisp;
    else if (overload.receives_self) result += " " + lisp_type(function.parameters[0]);
    bool optional = false;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& parameter = names[i - first];
        if (i >= required) {
            if (!optional) { result += " &optional"; optional = true; }
            const auto shown = lisp_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            result += shown.empty() ? " " + parameter : " (" + parameter + " " + shown + ")";
        } else result += " " + parameter;
    }
    result += ")";
    if (function.is_constructor) result += " → " + classes_.at(overload.owner).lisp;
    else if (!is_void(function.result)) result += " → " + lisp_type(function.result);
    return result;
}

std::string Generator::lisp_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant") return value.value;
    if (value.kind == "float") {
        // The shortest text that reads back as the same double, in Lisp's double-float syntax
        // (0.95d0, 1d-6): a bare 0.95 would read as a single-float.
        auto text = value.value;
        while (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (text.empty() || *end != '\0' || !std::isfinite(number)) return text;
        char buffer[64];
        for (int precision = 1; precision <= 17; ++precision) {
            std::snprintf(buffer, sizeof buffer, "%.*g", precision, number);
            if (std::strtod(buffer, nullptr) == number) break;
        }
        text = buffer;
        const auto exponent = text.find('e');
        if (exponent == std::string::npos) return text + "d0";
        auto mantissa = text.substr(0, exponent);
        auto power = text.substr(exponent + 1);
        if (!power.empty() && power[0] == '+') power.erase(0, 1);
        const bool negative = !power.empty() && power[0] == '-';
        if (negative) power.erase(0, 1);
        while (power.size() > 1 && power[0] == '0') power.erase(0, 1);
        return mantissa + "d" + (negative ? "-" : "") + power;
    }
    if (value.kind == "bool") return value.value == "true" ? "t" : "nil";
    if (value.kind == "null") return "nil";
    if (value.kind == "string") return value.value;
    if (value.kind == "char") return "\"" + value.value + "\"";
    if (value.kind == "enum") return ":" + lisp_name(value.value);
    if (value.kind == "default") {
        const auto& target = referent(type);
        if (bound(target)) return "(make-" + classes_.at(target.declaration).lisp + ")";
        const auto shown = lisp_type(target);
        if (shown.rfind("(vector", 0) == 0) return "#()";
        if (shown.rfind("(hash-table", 0) == 0 || shown.rfind("(list", 0) == 0) return "nil";
        if (shown == "string") return "\"\"";
        if (shown == "double-float") return "0d0";
        if (shown == "integer") return "0";
        if (shown == "boolean") return "nil";
    }
    return {};
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_lisp::Return::copy" : "rosetta_lisp::Return::automatic";
    return "rosetta_lisp::Return::" + found->second;
}

std::string Generator::class_lisp_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, lisp_name(instantiated->second));
    if (!record.owner.empty()) return renamed(qualified, lisp_name(record.name));
    return renamed(qualified, lisp_name(flat(record.namespaces) ? record.name : exported(qualified)));
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
            throw BindingError("unknown class in Lisp policy: " + selection.first);
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
        if (!record.complete) { if (listed) reject(owner, "Lisp requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed Lisp results are not yet supported"); continue;
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
    for (auto& [owner, plan] : classes_) plan.parent = plan.record->owner;
    for (auto& [owner, plan] : classes_) {
        std::string name = class_lisp_name(*plan.record);
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            name = class_lisp_name(*classes_.at(parent).record) + "-" + name;
        plan.lisp = name;
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

// Lisp names of the instance methods bound for a class and its bases.
std::set<std::string> Generator::bound_method_names(const std::string& owner) const {
    std::set<std::string> names;
    const std::function<void(const std::string&, unsigned)> walk = [&](const std::string& cls, unsigned depth) {
        if (depth > 64) return;
        for (const auto& group : classes_.at(cls).methods) if (!group.is_static) names.insert(group.name);
        for (const auto& base : classes_.at(cls).bases) walk(base, depth + 1);
    };
    walk(owner, 0);
    return names;
}

// A C++ subclass whose virtual methods call the generic functions on the Lisp object.
void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    const auto bound_names = bound_method_names(owner);
    out << "struct " << director_name(plan) << " final : " << owner << ", rosetta_lisp::Director {\n"
        << "    template<class... A> explicit " << director_name(plan) << "(A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    for (const auto* method : methods) {
        if (!callback_supported(*method)) continue;
        std::string key = method->name + "(";
        for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
        key += method->is_const ? ")const" : ")";
        const bool is_pure = pure[key];
        if (method->access == "private" && !is_pure) continue;
        const auto lname = renamed(method->qualified_name(), lisp_name(method->name));
        // Without a native method to fall back on, only pure virtual methods go to Lisp.
        if (!is_pure && !bound_names.count(lname)) continue;
        const auto index = generic(lname);
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
        out << "        if (rosetta_has_self())\n"
            << "            return rosetta_lisp::invoke_override<" << result << types << ">(*this, " << index << arguments << ");\n";
        if (is_pure) out << "        rosetta_lisp::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (const std::exception& error) { std::fprintf(stderr, \"error in noexcept Lisp override: %s\\n\", error.what()); }\n"
                << "        catch (...) {}\n        std::terminate();\n";
        out << "    }\n";
    }
    out << "};\n";
}

// One overload of a group. kind: "method" (self is argument 0), "constructor" (argument 0 is
// the Lisp object id of a subclass instance, or 0) or "function".
void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks) {
    const auto& function = *overload.function;
    const std::size_t skip = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - skip;
    const std::size_t required = function.parameters.size() - function.default_arguments - skip;
    const std::size_t base = kind == "method" || kind == "constructor" || overload.receives_self ? 1 : 0;
    const std::string indent = "            ";
    const auto owner = overload.owner;
    out << indent << "{\n"
        << indent << "    const std::size_t n = c.args.size() - " << base << ";\n"
        << indent << "    if (c.args.size() >= " << base << " && n >= " << required << " && n <= " << count;
    if (checks && kind == "method") out << "\n" << indent << "        && rosetta_lisp::is_instance<" << owner << ">(c.args[0])";
    if (checks && overload.receives_self) out << "\n" << indent << "        && rosetta_lisp::Param<" << function.parameters[0].canonical << ">::check(c.args[0], false)";
    for (std::size_t i = 0; i < count && checks; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || rosetta_lisp::Param<" << function.parameters[i + skip].canonical
            << ">::check(c.args[" << base + i << "], strict))";
    out << ") {\n";
    if (kind == "method") out << indent << "        auto* native = rosetta_lisp::native<" << owner << ">(c.args[0]);\n";
    if (overload.receives_self)
        out << indent << "        rosetta_lisp::converting() = \"self\";\n"
            << indent << "        rosetta_lisp::Param<" << function.parameters[0].canonical << "> self_param(c.args[0]);\n"
            << indent << "        rosetta_lisp::converting() = nullptr;\n";
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& name = i + skip < function.parameter_names.size() ? function.parameter_names[i + skip] : std::string{};
            out << indent << "            rosetta_lisp::converting() = " << quote(name.empty() ? "argument " + std::to_string(i + 1) : "argument '" + lisp_name(name) + "'") << ";\n"
                << indent << "            rosetta_lisp::Param<" << function.parameters[i + skip].canonical << "> p" << i << "(c.args[" << base + i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (arity > 0) out << indent << "            rosetta_lisp::converting() = nullptr;\n";
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            if (plan.director)
                out << indent << "            if (subclass) {\n"
                    << indent << "                auto* created = new " << director_name(plan) << "(" << arguments << ");\n"
                    << indent << "                c.result = rosetta_lisp::object(static_cast<" << owner << "*>(created), &rosetta_lisp::Info<" << owner << ">::value, rosetta_lisp::Ownership::owned, -1);\n"
                    << indent << "                c.result.holder->director = created;\n"
                    << indent << "                created->rosetta_holder = c.result.holder;\n"
                    << indent << "                created->rosetta_id = subclass;\n"
                    << indent << "                return;\n"
                    << indent << "            }\n";
            if (plan.record->abstract)
                out << indent << "            rosetta_lisp::fail(" << quote(plan.lisp + " is abstract; subclass it in Lisp with (defclass my-class (" + sym(plan.lisp) + ") ())") << ");\n";
            else if (function.access != "public")
                out << indent << "            rosetta_lisp::fail(" << quote(plan.lisp + " constructor is protected; subclass it in Lisp") << ");\n";
            else
                out << indent << "            c.result = rosetta_lisp::object(new " << owner << "(" << arguments << "), &rosetta_lisp::Info<" << owner
                    << ">::value, rosetta_lisp::Ownership::owned, -1);\n"
                    << indent << "            return;\n";
        } else {
            std::string body = kind == "method" ? "return native->" + function.name + "(" + arguments + ");"
                                                : "return ::" + function.qualified_name() + "(" + arguments + ");";
            // On a Lisp subclass object, a virtual method runs the native implementation: the
            // generic function reached it because no Lisp method overrides it (or one called
            // CALL-NEXT-METHOD), so dispatching virtually again would come back to Lisp.
            if (kind == "method" && function.is_virtual && policy_.directors != "none")
                body = "if (rosetta_lisp::is_director(c.args[0])) { " + (function.is_pure
                    ? "rosetta_lisp::pure_virtual(" + quote(owner + "::" + function.name) + ");"
                    : "return native->" + owner + "::" + function.name + "(" + arguments + ");") + " } " + body;
            std::string writebacks;
            for (std::size_t i = 0; i < arity; ++i) {
                const auto& type = function.parameters[i + skip];
                if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                    writebacks += indent + "            p" + std::to_string(i) + ".writeback(c, c.args[" + std::to_string(base + i) + "]);\n";
            }
            out << indent << "            rosetta_lisp::call(c, [&]() -> decltype(auto) { " << body << " }, " << (parent ? "0" : "-1") << ", " << policy << ");\n"
                << writebacks << indent << "            return;\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
}

void Generator::emit_group(std::ostream& out, Group& group, bool is_method) {
    group.symbol = symbol(&group.index);
    const bool single = group.overloads.size() == 1;
    out << "void " << group.symbol << "(rosetta_lisp::Call& c) {\n"
        << "    for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "        const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads) emit_overload(out, overload, overload.extension ? "function" : is_method ? "method" : "function", !single);
    out << "    }\n";
    std::string signatures;
    for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name, is_method && !overload.extension) + "\n";
    out << "    rosetta_lisp::no_overload(c, " << quote("(" + group.name + " ...)") << ", " << quote(signatures) << ", " << (is_method ? 1 : 0) << ");\n"
        << "}\n";
}

void Generator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor = symbol(&plan.constructor_index);
    const bool single = plan.constructors.size() == 1;
    if (plan.director) emit_director(out, plan);
    out << "void " << plan.constructor << "(rosetta_lisp::Call& c) {\n"
        << "    const unsigned long long subclass = c.args.empty() || c.args[0].kind != rosetta_lisp::tag_int ? 0 : static_cast<unsigned long long>(c.args[0].integer);\n"
        << "    (void)subclass;\n"
        << "    for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "        const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", !single);
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, "make-" + plan.lisp, false) + "\n";
    out << "    }\n    rosetta_lisp::no_overload(c, " << quote("(make-" + plan.lisp + " ...)") << ", " << quote(signatures) << ", 1);\n"
        << "}\n";
}

// The docstring of a function, method or constructor group: the C++ documentation, then each
// overload's signature with its parameters and result.
std::string Generator::group_doc(const Group& group, const std::string& shown, bool method) const {
    std::string text;
    std::set<std::string> seen;
    for (const auto& overload : group.overloads) {
        const auto& documentation = overload.function->documentation;
        if (documentation.empty() || !seen.insert(plain_documentation(documentation)).second) continue;
        const auto part = description(documentation, package_);
        if (!part.empty()) text += (text.empty() ? "" : "\n\n") + part;
    }
    if (text.empty()) text = "C++ " + group.overloads.front().function->qualified_name() + ".";
    std::set<std::string> signatures;
    std::string block;
    for (const auto& overload : group.overloads) {
        const auto line = signature(overload, shown, method && !overload.extension);
        if (!signatures.insert(line).second) continue;
        block += "\n  " + line;
        const auto& function = *overload.function;
        const auto names = parameter_names(overload);
        const std::size_t first = overload.receives_self ? 1 : 0;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            std::string about;
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            for (const auto& item : function.documentation.params) if (item.name == cpp) about = one_line(item.text);
            block += "\n    " + names[i - first] + "  " + lisp_type(function.parameters[i]) + (about.empty() ? "" : "  " + about);
        }
        if (!function.is_constructor && !is_void(function.result) && !function.documentation.returns.empty())
            block += "\n    → " + one_line(function.documentation.returns);
    }
    return text + "\n" + block;
}

std::string Generator::class_doc(const ClassPlan& plan) const {
    auto text = description(plan.record->documentation, package_);
    if (text.empty()) text = "C++ class " + plan.record->qualified_name() + ".";
    if (!plan.constructors.empty() && !plan.make_function) {
        text += "\n\nConstructors (make-" + plan.lisp + " is a different function; use MAKE-INSTANCE):";
        std::set<std::string> seen;
        for (const auto& overload : plan.constructors) {
            auto line = signature(overload, "make-" + plan.lisp, false);
            const auto open = line.find(' '), close = line.find(')');
            line = "(make-instance '" + plan.lisp + " :args (list" + (open < close ? line.substr(open, close - open) : std::string()) + "))";
            if (seen.insert(line).second) text += "\n  " + line;
        }
    } else if (!plan.constructors.empty()) {
        text += "\n\nConstructors:";
        std::set<std::string> seen;
        for (const auto& overload : plan.constructors) {
            const auto line = signature(overload, "make-" + plan.lisp, false);
            if (seen.insert(line).second) text += "\n  " + line;
        }
    } else text += plan.record->abstract ? "\n\nAbstract: create objects of a derived class" + std::string(plan.director ? ", or subclass it in Lisp." : ".")
                                         : "\n\nNo public constructor.";
    if (plan.director) {
        // The virtual methods a Lisp subclass can override (as emit_director selects them), the
        // pure ones first: the example overrides one of those.
        std::map<std::string, bool> pure;
        const auto bound_names = bound_method_names(plan.record->qualified_name());
        std::vector<std::pair<std::string, const Function*>> required, optional;
        std::set<std::string> listed;
        for (const auto* method : virtuals(*plan.record, pure)) {
            if (!callback_supported(*method)) continue;
            std::string key = method->name + "(";
            for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
            key += method->is_const ? ")const" : ")";
            const bool is_pure = pure[key];
            if (method->access == "private" && !is_pure) continue;
            const auto lname = renamed(method->qualified_name(), lisp_name(method->name));
            if (!is_pure && !bound_names.count(lname)) continue;
            if (listed.insert(lname).second) (is_pure ? required : optional).emplace_back(lname, method);
        }
        if (!required.empty() || !optional.empty()) {
        const auto& example = !required.empty() ? required.front() : optional.front();
        std::string parameters;
        for (std::size_t i = 0; i < example.second->parameters.size(); ++i) {
            const auto cpp = i < example.second->parameter_names.size() ? example.second->parameter_names[i] : std::string();
            parameters += " " + (cpp.empty() ? "arg" + std::to_string(i + 1) : variable_name(cpp));
        }
        const bool arguments = std::any_of(plan.constructors.begin(), plan.constructors.end(),
                                           [](const Overload& overload) { return !overload.function->parameters.empty(); });
        text += "\n\nSubclass it in Lisp; methods of the subclass override the C++ virtual methods:\n"
                "  (defclass my-" + plan.lisp + " (" + sym(plan.lisp) + ") ())\n"
                "  (" + package_ + ":define-override " + sym(example.first) + " ((self my-" + plan.lisp + ")" + parameters + ") ...)\n"
                "  (make-instance 'my-" + plan.lisp + (arguments ? " :args (list ...constructor arguments...))" : ")");
        auto names = [](const std::vector<std::pair<std::string, const Function*>>& methods) {
            std::string out;
            for (const auto& [name, method] : methods) out += (out.empty() ? "" : ", ") + name;
            return out;
        };
        if (!required.empty()) text += "\nMust override: " + names(required) + ".";
        if (!optional.empty()) text += "\nMay override (CALL-NEXT-METHOD runs the C++ one): " + names(optional) + ".";
        }
    }
    if (!plan.fields.empty()) {
        text += "\n\nFields (readers, and SETF where writable):";
        for (const auto& field : plan.fields) {
            const auto about = one_line(field.field->documentation.brief);
            text += "\n  " + field.name + "  " + lisp_type(field.field->type) + (about.empty() ? "" : "  " + about);
        }
    }
    return text;
}

// The lambda list of a function group, and (in *call) the argument list passed to the native call.
std::string Generator::lambda_list(const Group& group, std::string* call) const {
    if (group.overloads.size() != 1) { *call = "args"; return "(&rest args)"; }
    const auto& overload = group.overloads.front();
    const auto& function = *overload.function;
    const auto names = parameter_names(overload);
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments - first;
    std::string list = "(", self = overload.receives_self ? "self" : "";
    if (!self.empty()) list += "self";
    for (std::size_t i = 0; i < required; ++i) list += (list.size() > 1 ? " " : "") + names[i];
    if (required == names.size()) {
        std::string all = self;
        for (const auto& name : names) all += (all.empty() ? "" : " ") + name;
        *call = "(list" + (all.empty() ? std::string() : " " + all) + ")";
        return list + ")";
    }
    list += (list.size() > 1 ? " " : "") + std::string("&optional");
    for (std::size_t i = required; i < names.size(); ++i) list += " (" + names[i] + " nil " + names[i] + "-p)";
    // Only the arguments given reach C++, which evaluates the defaults.
    std::string cond = "(cond";
    for (std::size_t i = names.size(); i > required; --i) {
        std::string items = self;
        for (std::size_t j = 0; j < i; ++j) items += (items.empty() ? "" : " ") + names[j];
        cond += " (" + names[i - 1] + "-p (list " + items + "))";
    }
    std::string items = self;
    for (std::size_t j = 0; j < required; ++j) items += (items.empty() ? "" : " ") + names[j];
    cond += " (t (list" + (items.empty() ? std::string() : " " + items) + ")))";
    *call = cond;
    return list + ")";
}

GeneratedLispSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = lisp_name(name_);
    GeneratedLispSource result;
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
    // Names: functions (constructors, static methods, functions, accessors) share one namespace,
    // types (classes, enums) another. Generic functions may also have a function method on T.
    std::map<std::string, std::string> functions_taken, types_taken;
    const auto reserve = [&](std::map<std::string, std::string>& space, const std::string& lname, const std::string& what) {
        const auto inserted = space.emplace(lname, what);
        if (!inserted.second) throw BindingError("Lisp export name collision: " + lname + " (" + inserted.first->second + " and " + what + ")");
    };
    for (const char* runtime : {"native-error", "native-error-kind", "native-error-message", "ref", "ref-value", "with-object", "define-override"})
        reserve(functions_taken, runtime, "Rosetta runtime");
    reserve(types_taken, "native-object", "Rosetta runtime");
    reserve(types_taken, "native-error", "Rosetta runtime");
    reserve(types_taken, "ref", "Rosetta runtime");
    for (const auto& owner : class_order_) {
        reserve(types_taken, classes_.at(owner).lisp, "class " + owner);
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        auto lname = renamed(qualified, lisp_name(nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) lname = classes_.at(enumeration.owner).lisp + "-" + lname;
        if (types_taken.count(lname)) {
            if (!flat_) throw BindingError("Lisp export name collision: " + lname);
            reject(qualified, "enum name collides with another export");
            continue;
        }
        reserve(types_taken, lname, "enum " + qualified);
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
            group.name = lisp_name(lname);
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
    // Free operators become methods of the generic operator functions on their first operand's
    // class (or on T, beside the COMMON-LISP fallback, when that operand is not a native object).
    std::map<std::string, std::vector<std::pair<std::string, Overload>>> free_operators; // class ("" for T) -> (generic, overload)
    for (const auto& function : module_.functions) {
        if (!is_operator(function.name) || extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        const auto arity = function.parameters.size();
        if (arity == 0 || arity > 2) continue;
        const auto generic_name = operator_generic(function.name, arity - 1);
        if (generic_name.empty() || generic_name == "aref" || generic_name == "funcall") continue;
        auto why = function_problem(function, false);
        if (why.empty() == false) { reject(function.qualified_name(), why); continue; }
        const auto& first = function.parameters[0];
        const auto& target = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
        free_operators[bound(target) ? target.declaration : std::string()].push_back({generic_name, {&function, bound(target) ? target.declaration : std::string(), true, false}});
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
                if (found == record.methods.end()) throw BindingError("unknown Lisp policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Lisp policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown Lisp policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Lisp policy field is not public: " + owner + "::" + requested);
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
        std::map<std::string, std::size_t> group_index, operator_index;
        const auto add = [&](const std::string& lname, const Overload& overload, bool is_static) {
            const auto found = group_index.find(lname);
            if (found == group_index.end()) {
                group_index[lname] = plan.methods.size();
                plan.methods.push_back({lname, {overload}, is_static, {}, 0, {}});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a Lisp name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        const auto add_operator = [&](const std::string& generic_name, const Overload& overload) {
            const auto found = operator_index.find(generic_name);
            if (found == operator_index.end()) {
                operator_index[generic_name] = plan.operators.size();
                plan.operators.push_back({generic_name, {overload}, false, {}, 0, {}});
            } else plan.operators[found->second].overloads.push_back(overload);
        };
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            if (is_operator(method.name)) {
                if (method.is_static) continue;
                const auto generic_name = operator_generic(method.name, method.parameters.size());
                if (generic_name.empty()) continue; // assignment, conversion and other operators stay C++-only
                auto why = function_problem(method, false);
                if (!why.empty()) { reject(method.qualified_name(), why); continue; }
                add_operator(generic_name, {&method, owner});
                continue;
            }
            const auto lname = renamed(method.qualified_name(), lisp_name(method.name));
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(lname, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner]) for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        for (const auto& [generic_name, overload] : free_operators[owner]) add_operator(generic_name, overload);
        std::set<std::string> taken;
        for (const auto& group : plan.methods) taken.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported Lisp field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported Lisp field type", 0) == 0 ? why : "unsupported Lisp field type: " + why); continue; }
            const auto lname = renamed(qualified, lisp_name(field.name));
            if (!taken.insert(lname).second) { reject(qualified, "field name collides with a method of the same name"); continue; }
            plan.fields.push_back({&field, lname, {}, {}});
        }
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to Lisp"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            const auto lname = "+" + plan.lisp + "-" + renamed(qualified, lisp_name(variable.name)) + "+";
            plan.constants.push_back({lname, qualified, &variable.type, &variable.documentation, {}, 0});
        }
    }

    // Module functions grouped by Lisp name.
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
        const auto lname = specialization != function_template_names_.end() ? lisp_name(specialization->second)
            : renamed(function.qualified_name(), lisp_name(!submodule.empty() || flat(function.namespaces) ? function.name
                                                                                     : exported(function.qualified_name())));
        const auto key = submodule + "." + lname;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions.size();
            functions.push_back({lname, {{&function, {}}}, false, {}, 0, submodule.empty() ? std::string() : lisp_name(submodule)});
        } else functions[found->second].overloads.push_back({&function, {}});
    }
    // Each policy submodule is its own package, PACKAGE.SUBMODULE, beside PACKAGE.rosetta.
    std::set<std::string> submodules;
    for (const auto& group : functions)
        if (!group.module.empty() && submodules.insert(group.module).second && group.module == "rosetta")
            throw BindingError("Lisp submodule name rosetta is reserved for the runtime package " + package_ + ".rosetta");

    // Generic functions: instance methods and fields of every class; operators.
    struct GenericPlan { std::vector<std::pair<std::string, std::string>> owners; bool setf = false; }; // (class, kind)
    std::map<std::string, GenericPlan> generics;
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& group : plan.methods)
            if (!group.is_static) generics[group.name].owners.push_back({owner, "method"});
        for (const auto& field : plan.fields) {
            generics[field.name].owners.push_back({owner, "field"});
            generics[field.name].setf = true;
        }
        for (const auto& group : plan.operators) generics[group.name].owners.push_back({owner, "operator"});
    }
    for (const auto& [owner, list] : free_operators)
        if (owner.empty()) for (const auto& item : list) generics[item.first];
    // Generic functions directors call (virtual methods Lisp subclasses override).
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director) continue;
        std::map<std::string, bool> pure;
        for (const auto* method : virtuals(*plan.record, pure)) {
            if (!callback_supported(*method)) continue;
            const auto lname = renamed(method->qualified_name(), lisp_name(method->name));
            std::string key = method->name + "(";
            for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
            key += method->is_const ? ")const" : ")";
            if (method->access == "private" && !pure[key]) continue;
            generics[lname];
        }
    }
    // Names in the function namespace: generic functions, constructors, static methods, functions.
    std::map<std::string, std::size_t> function_with_generic; // function group index -> shares a generic's name
    for (const auto& [name, plan] : generics) reserve(functions_taken, name, "generic function " + name);
    std::set<std::string> function_names;
    for (const auto& group : functions) if (group.module.empty()) function_names.insert(group.name);
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        // A C++ function make_<class> keeps its name; the class is made with MAKE-INSTANCE.
        if (function_names.count("make-" + plan.lisp) || generics.count("make-" + plan.lisp)) plan.make_function = false;
        if (!plan.constructors.empty() && plan.make_function) reserve(functions_taken, "make-" + plan.lisp, "constructor of " + owner);
        for (const auto& group : plan.methods)
            if (group.is_static) reserve(functions_taken, plan.lisp + "-" + group.name, "static method " + owner + "::" + group.name);
    }
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const auto& group = functions[i];
        if (!group.module.empty()) continue; // its own package: no generic or name clashes
        if (generics.count(group.name)) {
            // A function named like a generic function becomes its method on T.
            const bool zero = std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                return o.function->parameters.size() == o.function->default_arguments;
            });
            if (zero) reject(group.overloads.front().function->qualified_name(), "a function callable without arguments cannot share the name of generic function " + group.name);
            function_with_generic[group.name] = i;
            continue;
        }
        reserve(functions_taken, group.name, "function " + group.overloads.front().function->qualified_name());
    }

    // Constant namespace-scope variables.
    std::vector<ConstantPlan> constants;
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to Lisp"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto lname = "+" + renamed(qualified, lisp_name(flat(variable.namespaces) ? variable.name : exported(qualified))) + "+";
        if (functions_taken.count(lname)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(functions_taken, lname, "constant " + qualified);
        constants.push_back({lname, "::" + qualified, &variable.type, &variable.documentation, {}, 0});
    }
    for (const auto& owner : class_order_)
        for (const auto& constant : classes_.at(owner).constants) reserve(functions_taken, constant.name, "constant " + constant.expression);
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Lisp";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Using-declarations re-export bound entities under another namespace's name.
    struct Alias { std::string name, target, kind; };
    std::vector<Alias> aliases;
    {
        std::map<std::string, std::string> function_names;
        for (const auto& group : functions)
            if (group.module.empty())
                for (const auto& overload : group.overloads) function_names.emplace(overload.function->qualified_name(), group.name);
        for (const auto& reexport : module_.reexports) {
            std::string target, kind;
            if (reexport.kind == "record" || reexport.kind == "type") { const auto c = classes_.find(reexport.target); if (c != classes_.end()) { target = c->second.lisp; kind = "class"; } }
            if (reexport.kind == "enum" || reexport.kind == "type") { const auto e = enum_names_.find(reexport.target); if (e != enum_names_.end()) { target = e->second; kind = "enum"; } }
            else if (reexport.kind == "function") { const auto f = function_names.find(reexport.target); if (f != function_names.end() && !generics.count(f->second)) { target = f->second; kind = "function"; } }
            if (target.empty()) continue;
            std::string scope;
            for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
            const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
            const auto lname = renamed(qualified, lisp_name(flat(reexport.namespaces) ? reexport.name : exported(qualified)));
            if (lname == target) continue;
            auto& space = kind == "function" ? functions_taken : types_taken;
            if (space.count(lname)) continue;
            if (kind == "class" && classes_.count(reexport.target) && !classes_.at(reexport.target).constructors.empty() && functions_taken.count("make-" + lname)) continue;
            reserve(space, lname, "alias of " + reexport.target);
            if (kind == "class" && classes_.count(reexport.target) && !classes_.at(reexport.target).constructors.empty()) reserve(functions_taken, "make-" + lname, "alias of " + reexport.target);
            aliases.push_back({lname, target, kind});
        }
    }

    // --------------------------------------------------------------- C++ source
    struct Unit { std::string code; };
    std::vector<Unit> units;
    std::ostringstream out;
    out << "// Generated by Rosetta: Common Lisp module " << name_ << " (full API).\n";
    for (const auto* header : {"algorithm", "array", "cerrno", "chrono", "climits", "cmath", "complex", "cstdint", "cstdio", "cstdlib", "cstring", "deque",
                               "exception", "filesystem", "functional", "iostream", "limits", "list", "map", "memory", "mutex", "new", "optional", "set",
                               "stdexcept", "string", "string_view", "tuple", "typeindex", "type_traits", "unordered_map", "unordered_set",
                               "utility", "valarray", "variant", "vector"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n'
        << "#define ROSETTA_LISP_MODULE rosetta_lisp_module_" << name_ << '\n';
    for (const auto* part : detail::lisp_full_runtime_parts) out << part;
    out << "namespace ROSETTA_LISP_MODULE { namespace rosetta_lisp {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(const Node& n, bool strict) { return Value<" << to << ">::check(n, strict); }\n"
            << "    static " << from << " load(const Node& n) { return " << from << "(Value<" << to << ">::load(n)); }\n"
            << "    static Node make(const " << from << "& value) { return Value<" << to << ">::make(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\n";

    for (auto& group : functions) {
        std::ostringstream unit;
        emit_group(unit, group, false);
        units.push_back({unit.str()});
    }
    std::vector<Group> t_operators; // free operators whose first operand is not a native object
    {
        std::map<std::string, std::size_t> index;
        for (const auto& [generic_name, overload] : free_operators[""]) {
            const auto found = index.find(generic_name);
            if (found == index.end()) { index[generic_name] = t_operators.size(); t_operators.push_back({generic_name, {overload}, false, {}, 0, {}}); }
            else t_operators[found->second].overloads.push_back(overload);
        }
        std::ostringstream unit;
        for (auto& group : t_operators) emit_group(unit, group, false);
        units.push_back({unit.str()});
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::ostringstream unit;
        for (auto& group : plan.methods) emit_group(unit, group, !group.is_static);
        for (auto& group : plan.operators) emit_group(unit, group, true);
        for (auto& field : plan.fields) {
            const auto& f = *field.field;
            const auto qualified = owner + "::" + f.name;
            field.getter = symbol(&field.getter_index);
            unit << "void " << field.getter << "(rosetta_lisp::Call& c) {\n"
                 << "    if (c.args.size() != 1) rosetta_lisp::fail(" << quote("(" + field.name + " object) takes no further arguments") << ");\n"
                 << "    auto* native = rosetta_lisp::native<" << owner << ">(c.args[0]);\n"
                 << "    c.result = rosetta_lisp::result<decltype((native->" << f.name << "))>(c, native->" << f.name
                 << ", 0, rosetta_lisp::Return::" << (copy_type(f.type) ? "copy" : "automatic") << ");\n}\n";
            const bool writable = !f.type.is_const && !readonly(qualified, owner) && f.type.kind != TypeKind::lvalue_reference;
            field.setter = symbol(&field.setter_index);
            unit << "void " << field.setter << "(rosetta_lisp::Call& c) {\n"
                 << "    if (c.args.size() != 2) rosetta_lisp::fail(" << quote("(setf (" + field.name + " object) value) takes no further arguments") << ");\n"
                 << "    auto* native = rosetta_lisp::native<" << owner << ">(c.args[0]);\n";
            if (writable)
                unit << "    using F = std::remove_cv_t<std::remove_reference_t<decltype(native->" << f.name << ")>>;\n"
                     << "    if constexpr (rosetta_lisp::bound_v<F>) {\n"
                     << "        // (setf (field object) (field object)) after changing the field object in place.\n"
                     << "        if (rosetta_lisp::is_instance<F>(c.args[1]) && rosetta_lisp::native<F>(c.args[1]) == &native->" << f.name << ") return;\n"
                     << "    }\n"
                     << "    rosetta_lisp::converting() = " << quote("field " + field.name) << ";\n"
                     << "    rosetta_lisp::Param<const F&> converted(c.args[1]);\n"
                     << "    rosetta_lisp::converting() = nullptr;\n"
                     << "    rosetta_lisp::assign_field(native->" << f.name << ", converted.get());\n";
            else
                unit << "    (void)native;\n    rosetta_lisp::fail(" << quote("field " + field.name + " is read-only") << ");\n";
            unit << "}\n";
        }
        for (auto& constant : plan.constants) {
            constant.symbol = symbol(&constant.index);
            unit << "void " << constant.symbol << "(rosetta_lisp::Call& c) {\n"
                 << "    c.result = rosetta_lisp::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(c, static_cast<std::remove_cv_t<decltype("
                 << constant.expression << ")>>(" << constant.expression << "), -1, rosetta_lisp::Return::copy);\n}\n";
        }
        if (!plan.constructors.empty()) emit_constructor(unit, plan);
        units.push_back({unit.str()});
    }
    {
        std::ostringstream unit;
        for (auto& constant : constants) {
            constant.symbol = symbol(&constant.index);
            unit << "void " << constant.symbol << "(rosetta_lisp::Call& c) {\n"
                 << "    c.result = rosetta_lisp::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(c, static_cast<std::remove_cv_t<decltype("
                 << constant.expression << ")>>(" << constant.expression << "), -1, rosetta_lisp::Return::copy);\n}\n";
        }
        units.push_back({unit.str()});
    }

    // Registration: classes (in the order of the Lisp class vector), bases, enums.
    std::ostringstream registration;
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& owner = class_order_[i];
        registration << "        rosetta_lisp::register_type<" << owner << ">(" << quote(classes_.at(owner).lisp) << ", " << i << ");\n";
        for (const auto& base : classes_.at(owner).bases) registration << "        rosetta_lisp::add_base<" << owner << ", " << base << ">();\n";
    }
    for (const auto& [qualified, lname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        std::string entries;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], lisp_name(enumeration.enumerators[i]));
            const auto& value = enumeration.enumerator_values[i];
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            entries += "{" + quote(label) + ", " + (big ? "static_cast<long long>(" + value + "ULL)"
                : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL") + "}, ";
        }
        registration << "        rosetta_lisp::EnumTable<" << qualified << ">::entries = {" << entries << "};\n";
    }

    // Split sources by size, as the Python, R and Lua backends do.
    std::vector<std::ostringstream> extra(options_.parts > 1 ? options_.parts - 1 : 0);
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
        for (auto& part : extra) part << prelude << "namespace ROSETTA_LISP_MODULE {\n";
        out << "namespace ROSETTA_LISP_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) extra[bin[i]] << units[i].code;
        for (const auto& name : symbols_) out << "void " << name << "(rosetta_lisp::Call&);\n";
        for (auto& part : extra) part << "} // namespace ROSETTA_LISP_MODULE\n";
    } else {
        out << "namespace ROSETTA_LISP_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "const rosetta_lisp::Function table[] = {rosetta_lisp::call_native";
    for (const auto& name : symbols_) out << ",\n    " << name;
    out << "};\n"
        << "} // namespace ROSETTA_LISP_MODULE\n"
        << "#if defined(_WIN32)\n#define ROSETTA_LISP_EXPORT extern \"C\" __declspec(dllexport)\n#else\n"
        << "#define ROSETTA_LISP_EXPORT extern \"C\" __attribute__((visibility(\"default\")))\n#endif\n"
        << "ROSETTA_LISP_EXPORT int rosetta_" << name_ << "_call(unsigned index, const unsigned char* in, std::size_t size, unsigned char** out, std::size_t* out_size) {\n"
        << "    return ROSETTA_LISP_MODULE::rosetta_lisp::run(ROSETTA_LISP_MODULE::table, sizeof(ROSETTA_LISP_MODULE::table) / sizeof(ROSETTA_LISP_MODULE::table[0]), index, in, size, out, out_size);\n}\n"
        << "ROSETTA_LISP_EXPORT void rosetta_" << name_ << "_free(void* pointer) { std::free(pointer); }\n"
        << "ROSETTA_LISP_EXPORT void* rosetta_" << name_ << "_alloc(std::size_t size) { return std::malloc(size ? size : 1); }\n"
        << "ROSETTA_LISP_EXPORT void rosetta_" << name_ << "_release(void* holder, int now) {\n"
        << "    ROSETTA_LISP_MODULE::rosetta_lisp::release(static_cast<ROSETTA_LISP_MODULE::rosetta_lisp::Holder*>(holder), now);\n}\n"
        << "ROSETTA_LISP_EXPORT int rosetta_" << name_ << "_init(int version, ROSETTA_LISP_MODULE::rosetta_lisp::InvokeFn invoke, ROSETTA_LISP_MODULE::rosetta_lisp::RetainFn retain) {\n"
        << "    using namespace ROSETTA_LISP_MODULE;\n"
        << "    if (version != 1) return 1;\n"
        << "    static const bool registered = [] {\n" << registration.str() << "        return true;\n    }();\n"
        << "    (void)registered;\n"
        << "    rosetta_lisp::Hooks::invoke = invoke;\n"
        << "    rosetta_lisp::Hooks::retain = retain;\n"
        << "    return 0;\n}\n";
    result.source = out.str();
    for (auto& part : extra) result.parts.push_back(part.str());

    // --------------------------------------------------------------- Lisp interface
    std::ostringstream lisp, exports;
    std::set<std::string> exported_names;
    const auto exporting = [&](const std::string& lname) { if (exported_names.insert(lname).second) exports << "\n   #:" << symbol_text(lname); };
    for (const char* runtime : {"native-object", "native-error", "native-error-kind", "native-error-message", "ref", "ref-value", "with-object", "define-override"})
        exporting(runtime);
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        exporting(plan.lisp);
        if (!plan.constructors.empty() && plan.make_function) exporting("make-" + plan.lisp);
        for (const auto& group : plan.methods) if (group.is_static) exporting(plan.lisp + "-" + group.name);
        for (const auto& constant : plan.constants) exporting(constant.name);
    }
    for (const auto& [qualified, lname] : enum_names_) exporting(lname);
    for (const auto& [name, plan] : generics) exporting(name);
    for (const auto& group : functions) if (group.module.empty()) exporting(group.name);
    for (const auto& constant : constants) exporting(constant.name);
    for (const auto& alias : aliases) {
        exporting(alias.name);
        if (alias.kind == "class" && functions_taken.count("make-" + alias.name)) exporting("make-" + alias.name);
    }

    const auto library = options_.library.empty() ? name_ : options_.library;
    std::string environment;
    for (char c : name_) environment += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    environment += "_LISP_LIBRARY";
    lisp << ";;;; Generated by Rosetta: Common Lisp interface to the native module `" << name_ << "` (full API).\n"
         << ";;;; Load it with (asdf:load-system \"" << package_ << "\"), or LOAD this file; it loads " << library << ".so (.dylib)\n"
         << ";;;; from its own directory, or the file " << environment << " names.\n"
         << ";;;; Symbols are exported from the package " << package_;
    if (!options_.nicknames.empty()) {
        lisp << " (nickname";
        for (std::size_t i = 0; i < options_.nicknames.size(); ++i) lisp << (i ? ", " : " ") << options_.nicknames[i];
        lisp << ")";
    }
    lisp << ".\n\n"
         << "(eval-when (:compile-toplevel :load-toplevel :execute)\n"
         << "  (require \"asdf\")\n"
         << "  (unless (and (find-package \"CFFI\") (find-package \"TRIVIAL-GARBAGE\"))\n"
         << "    (funcall (intern \"LOAD-SYSTEM\" \"ASDF\") \"cffi\")\n"
         << "    (funcall (intern \"LOAD-SYSTEM\" \"ASDF\") \"trivial-garbage\")))\n\n"
         << "(defpackage #:" << package_ << "\n  (:use)\n";
    if (!options_.nicknames.empty()) {
        lisp << "  (:nicknames";
        for (const auto& nickname : options_.nicknames) lisp << " #:" << nickname;
        lisp << ")\n";
    }
    lisp << "  (:documentation " << lisp_string("Native C++ module " + name_ + ", bound by Rosetta.") << ")\n"
         << "  (:export" << exports.str() << "))\n\n"
         ;
    for (const auto& submodule : submodules) {
        std::set<std::string> seen;
        lisp << "(defpackage #:" << package_ << "." << submodule << "\n  (:use)\n";
        if (!options_.nicknames.empty()) {
            lisp << "  (:nicknames";
            for (const auto& nickname : options_.nicknames) lisp << " #:" << nickname << "." << submodule;
            lisp << ")\n";
        }
        lisp << "  (:documentation " << lisp_string("Functions of the C++ namespace mapped to submodule " + submodule + " of " + name_ + ".") << ")\n"
             << "  (:export";
        for (const auto& group : functions)
            if (group.module == submodule && seen.insert(group.name).second) lisp << "\n   #:" << symbol_text(group.name);
        lisp << "))\n\n";
    }
    lisp << "(defpackage #:" << package_ << ".rosetta\n  (:use #:common-lisp)\n"
         << "  (:documentation " << lisp_string("Rosetta runtime of " + package_ + " (internal).") << "))\n\n"
         << "(in-package #:" << package_ << ".rosetta)\n";
    {
        std::string runtime = detail::lisp_full_runtime_lisp;
        const auto replace = [&](const std::string& from, const std::string& to) {
            for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
        };
        replace("@PKG@", package_);
        replace("@C@", "rosetta_" + name_);
        replace("@LIB@", library);
        replace("@ENV@", environment);
        lisp << runtime;
    }
    lisp << "\n(open-module)\n";

    lisp << "\n;;; ------------------------------------------------------------------ enums\n";
    for (const auto& [qualified, lname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        auto text = description(enumeration.documentation, package_);
        if (text.empty()) text = "C++ enumeration " + qualified + ".";
        std::string values, members;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], lisp_name(enumeration.enumerators[i]));
            values += std::string(i ? ", " : "") + ":" + label + " (" + enumeration.enumerator_values[i] + ")";
            members += " :" + symbol_text(label);
        }
        text += "\n\nValues: " + values + ". Arguments also accept the integer values.";
        lisp << "\n(deftype " << sym(lname) << " ()\n  " << lisp_string(text) << "\n  '(member" << members << "))\n";
    }

    lisp << "\n;;; ------------------------------------------------------------------ classes\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        lisp << "\n(defclass " << sym(plan.lisp) << " (";
        if (plan.bases.empty()) lisp << sym("native-object");
        for (std::size_t i = 0; i < plan.bases.size(); ++i) lisp << (i ? " " : "") << sym(classes_.at(plan.bases[i]).lisp);
        lisp << ") ()\n  (:documentation " << lisp_string(class_doc(plan)) << "))\n";
    }
    lisp << "\n(setf *classes* (vector";
    for (const auto& owner : class_order_) lisp << "\n  '" << sym(classes_.at(owner).lisp);
    lisp << "))\n";

    lisp << "\n;;; ------------------------------------------------------------------ constructors\n";
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        if (plan.constructors.empty()) continue;
        Group group{"make-" + plan.lisp, plan.constructors, false, {}, 0, {}};
        std::string call;
        const auto list = lambda_list(group, &call);
        lisp << "\n(register-constructor '" << sym(plan.lisp) << " " << plan.constructor_index << " " << (plan.director ? "t" : "nil") << ")\n";
        if (!plan.make_function) continue;
        lisp << "(defun " << sym(group.name) << " " << list << "\n  " << lisp_string(group_doc(group, group.name, false)) << "\n"
             << "  (make-instance '" << sym(plan.lisp) << " :args " << call << "))\n";
    }

    lisp << "\n;;; ------------------------------------------------------------------ generic functions\n";
    for (const auto& [name, plan] : generics) {
        std::string about;
        std::vector<std::string> owners;
        std::set<std::string> listed;
        for (const auto& [owner, kind] : plan.owners)
            if (listed.insert(owner).second) owners.push_back(classes_.at(owner).lisp + (kind == "field" ? " (field)" : ""));
        if (!owners.empty()) {
            about = "Methods: ";
            for (std::size_t i = 0; i < owners.size(); ++i) about += (i ? ", " : "") + owners[i];
            about += ".";
        } else about = "Generic function; Lisp subclasses of native classes define its methods.";
        if (function_with_generic.count(name)) about += "\nAlso a function of other arguments (its method on T).";
        if (!operator_fallback(name).empty()) about += "\nOn Lisp numbers it is CL:" + operator_fallback(name) + ".";
        const auto index = generic_index_.find(name);
        (void)index;
        lisp << "\n(defgeneric " << sym(name) << " (self &rest args)\n  (:documentation " << lisp_string(about) << "))\n";
        if (plan.setf)
            lisp << "(defgeneric (setf " << sym(name) << ") (value self &rest args)\n  (:documentation " << lisp_string("Sets field " + name + ".") << "))\n";
    }
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& group : plan.methods) {
            if (group.is_static) continue;
            lisp << "\n(defmethod " << sym(group.name) << " ((self " << sym(plan.lisp) << ") &rest args)\n  "
                 << lisp_string(group_doc(group, group.name, true)) << "\n  (call " << group.index << " (cons self args)))\n";
        }
        for (const auto& group : plan.operators) {
            lisp << "\n(defmethod " << sym(group.name) << " ((self " << sym(plan.lisp) << ") &rest args)\n  "
                 << lisp_string(group_doc(group, group.name, true)) << "\n  (call " << group.index << " (cons self args)))\n";
        }
        for (const auto& field : plan.fields) {
            const auto about = description(field.field->documentation, package_);
            const auto type = lisp_type(field.field->type);
            lisp << "\n(defmethod " << sym(field.name) << " ((self " << sym(plan.lisp) << ") &rest args)\n  "
                 << lisp_string((about.empty() ? "Field " + field.name + " of " + plan.lisp + "." : about) + "\n\n  (" + field.name + " " + plan.lisp + ") → " + type +
                                "\n  (setf (" + field.name + " " + plan.lisp + ") " + type + ")")
                 << "\n  (call " << field.getter_index << " (cons self args)))\n"
                 << "(defmethod (setf " << sym(field.name) << ") (value (self " << sym(plan.lisp) << ") &rest args)\n"
                 << "  (call " << field.setter_index << " (list* self value args))\n  value)\n";
        }
    }
    for (const auto& group : t_operators) {
        const auto fallback = operator_fallback(group.name);
        lisp << "\n(defmethod " << sym(group.name) << " ((self t) &rest args)\n  " << lisp_string(group_doc(group, group.name, false)) << "\n"
             << "  (if (some (lambda (x) (typep x '" << sym("native-object") << ")) args)\n"
             << "      (call " << group.index << " (cons self args))\n"
             << "      (apply #'" << fallback << " self args)))\n";
    }
    for (const auto& [name, plan] : generics) {
        const auto fallback = operator_fallback(name);
        const bool has_t = std::any_of(t_operators.begin(), t_operators.end(), [&, generic_name = name](const Group& g) { return g.name == generic_name; });
        if (!fallback.empty() && !has_t && !plan.owners.empty())
            lisp << "\n(defmethod " << sym(name) << " ((self t) &rest args)\n  (apply #'" << fallback << " self args))\n";
    }
    // print-object uses to_string() when the class has one callable without arguments.
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const bool printable = std::any_of(plan.methods.begin(), plan.methods.end(), [](const Group& group) {
            return group.name == "to-string" && !group.is_static && std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                return o.function->parameters.size() == o.function->default_arguments + (o.receives_self ? 1 : 0);
            });
        });
        if (printable)
            lisp << "\n(defmethod print-object ((object " << sym(plan.lisp) << ") stream)\n"
                 << "  (if (and (slot-value object 'holder) (not *print-readably*))\n"
                 << "      (print-unreadable-object (object stream :type t)\n"
                 << "        (write-string (handler-case (" << sym("to-string") << " object) (error () \"?\")) stream))\n"
                 << "      (call-next-method)))\n";
    }

    lisp << "\n;;; ------------------------------------------------------------------ static methods and functions\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& group : plan.methods) {
            if (!group.is_static) continue;
            const auto shown = plan.lisp + "-" + group.name;
            std::string call;
            const auto list = lambda_list(group, &call);
            lisp << "\n(defun " << sym(shown) << " " << list << "\n  " << lisp_string(group_doc(group, shown, false) + "\n\n(Static method of " + plan.lisp + ".)") << "\n"
                 << "  (call " << group.index << " " << call << "))\n";
        }
    }
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const auto& group = functions[i];
        if (!group.module.empty()) {
            const auto qualified = package_ + "." + group.module + ":" + symbol_text(group.name);
            std::string call;
            const auto list = lambda_list(group, &call);
            lisp << "\n(defun " << qualified << " " << list << "\n  " << lisp_string(group_doc(group, group.name, false)) << "\n"
                 << "  (call " << group.index << " " << call << "))\n";
            continue;
        }
        if (generics.count(group.name)) {
            lisp << "\n(defmethod " << sym(group.name) << " ((self t) &rest args)\n  " << lisp_string(group_doc(group, group.name, false))
                 << "\n  (call " << group.index << " (cons self args)))\n";
            continue;
        }
        std::string call;
        const auto list = lambda_list(group, &call);
        lisp << "\n(defun " << sym(group.name) << " " << list << "\n  " << lisp_string(group_doc(group, group.name, false)) << "\n"
             << "  (call " << group.index << " " << call << "))\n";
    }

    lisp << "\n;;; ------------------------------------------------------------------ constants and aliases\n";
    const auto emit_constant = [&](const ConstantPlan& constant) {
        auto text = constant.documentation ? description(*constant.documentation, package_) : std::string();
        if (text.empty()) text = "C++ constant " + constant.expression + ".";
        lisp << "\n(defparameter " << sym(constant.name) << " (call " << constant.index << " nil)\n  " << lisp_string(text + "\n\nType: " + lisp_type(*constant.type)) << ")\n";
    };
    for (const auto& owner : class_order_) for (const auto& constant : classes_.at(owner).constants) emit_constant(constant);
    for (const auto& constant : constants) emit_constant(constant);
    for (const auto& alias : aliases) {
        if (alias.kind == "function")
            lisp << "\n(setf (fdefinition '" << sym(alias.name) << ") (fdefinition '" << sym(alias.target) << "))\n";
        else if (alias.kind == "class") {
            lisp << "\n(setf (find-class '" << sym(alias.name) << ") (find-class '" << sym(alias.target) << "))\n";
            if (functions_taken.count("make-" + alias.name) && functions_taken.at("make-" + alias.name).rfind("alias of ", 0) == 0)
                lisp << "(setf (fdefinition '" << sym("make-" + alias.name) << ") (fdefinition '" << sym("make-" + alias.target) << "))\n";
        } else lisp << "\n(deftype " << sym(alias.name) << " () '" << sym(alias.target) << ")\n";
    }
    lisp << "\n(setf *generics* (vector";
    for (const auto& name : generic_order_) lisp << "\n  #'" << sym(name);
    lisp << "))\n";
    result.interface = lisp.str();
    // The ASDF system definition.
    result.system = ";;;; Generated by Rosetta: ASDF system of the native module `" + name_ + "`.\n"
                    "(asdf:defsystem " + lisp_string(package_) + "\n"
                    "  :description " + lisp_string("Common Lisp bindings to the native C++ module " + name_ + " (Rosetta).") + "\n"
                    "  :depends-on (\"cffi\" \"trivial-garbage\")\n"
                    "  :components ((:file " + lisp_string(options_.interface_name.empty() ? package_ : options_.interface_name) + ")))\n";
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedLispSource generate_common_lisp_full(const Module& module, const std::string& name, bool skip_unsupported,
                                              const BindingPolicy& policy, const LispFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Lisp source parts must be from 1 to 256");
    for (const auto& nickname : options.nicknames)
        if (nickname.empty() || !std::all_of(nickname.begin(), nickname.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.'; }))
            throw BindingError("Lisp package nickname must be letters, digits, '-' or '.': " + nickname);
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
