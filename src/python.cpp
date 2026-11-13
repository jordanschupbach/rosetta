#include "rosetta/python.hpp"
#include "python_runtime.hpp"
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
// --- Docstrings -------------------------------------------------------------
//
// Doxygen comments become NumPy-style docstrings: summary, extended description, then
// Parameters/Returns/Raises/See Also/Notes and the comment's own @par sections. Functions with
// one overload start with a text signature ("name($self, a, b=1.0)\n--\n\n") so
// inspect.signature() works on them.

std::string indent_lines(const std::string& text, const std::string& indent) {
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        out += (out.empty() && start == 0 ? "" : "\n") + (line.empty() ? std::string() : indent + line);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

std::string python_exception(std::string name) {
    if (name.rfind("std::", 0) == 0) name.erase(0, 5);
    if (name == "invalid_argument" || name == "domain_error" || name == "length_error") return "ValueError";
    if (name == "out_of_range") return "IndexError";
    if (name == "overflow_error") return "OverflowError";
    return "RuntimeError";
}

std::string section_title(std::string title) {
    while (!title.empty() && (title.back() == ':' || title.back() == ' ')) title.pop_back();
    std::string lower;
    for (const char c : title) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "example" || lower == "examples" || lower == "usage") return "Examples";
    if (lower == "reference" || lower == "references") return "References";
    if (lower == "note" || lower == "notes") return "Notes";
    if (lower == "warning" || lower == "warnings") return "Warnings";
    if (lower == "see also") return "See Also";
    return title;
}

// One documentation block as docstring text.
std::string block_text(const DocBlock& block) {
    if (block.kind == "math") return ".. math::\n\n" + indent_lines(block.text, "    ");
    if (block.kind != "code") return block.text;
    std::string language = block.language;
    if (language == "py" || language == "python" || language == "pycon") {
        if (block.text.rfind(">>>", 0) == 0) return block.text;
        language = "python";
    } else if (language.empty() || language == "cpp" || language == "c++" || language == "cxx" || language == "cc") {
        language = "cpp";
    }
    return ".. code-block:: " + language + "\n\n" + indent_lines(block.text, "    ");
}

// Ordered NumPy sections; a repeated title appends to the existing section.
struct Sections {
    std::vector<std::pair<std::string, std::string>> items;
    void add(const std::string& title, const std::string& text) {
        if (text.empty()) return;
        for (auto& item : items)
            if (item.first == title) { item.second += "\n" + text; return; }
        items.emplace_back(title, text);
    }
    std::string render() const {
        static const std::vector<std::string> order = {"Parameters", "Attributes", "Returns", "Raises", "Warnings", "See Also",
                                                       "Notes", "References", "Examples"};
        std::vector<const std::pair<std::string, std::string>*> sorted;
        for (const auto& name : order)
            for (const auto& item : items) if (item.first == name) sorted.push_back(&item);
        for (const auto& item : items)
            if (std::find(order.begin(), order.end(), item.first) == order.end()) sorted.push_back(&item);
        std::string out;
        for (const auto* item : sorted)
            out += (out.empty() ? "" : "\n\n") + item->first + "\n" + std::string(item->first.size(), '-') + "\n" + item->second;
        return out;
    }
};

std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool python_identifier(const std::string& text) {
    auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    return !text.empty() && start(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return start(c) || (c >= '0' && c <= '9');
    });
}
// Python keywords cannot be attribute names in source code; SWIG's convention prefixes '_'.
std::string keyword_safe(const std::string& name) {
    static const std::set<std::string> keywords = {"False", "None", "True", "and", "as", "assert", "async", "await", "break",
        "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in",
        "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"};
    return keywords.count(name) ? "_" + name : name;
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

// Python dunder names for C++ operators by arity (member operators: arity excludes this).
std::string operator_name(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"__pos__", "__add__"}}, {"operator-", {"__neg__", "__sub__"}}, {"operator*", {nullptr, "__mul__"}},
        {"operator/", {nullptr, "__truediv__"}}, {"operator%", {nullptr, "__mod__"}}, {"operator==", {nullptr, "__eq__"}},
        {"operator!=", {nullptr, "__ne__"}}, {"operator<", {nullptr, "__lt__"}}, {"operator<=", {nullptr, "__le__"}},
        {"operator>", {nullptr, "__gt__"}}, {"operator>=", {nullptr, "__ge__"}}, {"operator+=", {nullptr, "__iadd__"}},
        {"operator-=", {nullptr, "__isub__"}}, {"operator*=", {nullptr, "__imul__"}}, {"operator/=", {nullptr, "__itruediv__"}},
        {"operator%=", {nullptr, "__imod__"}}, {"operator&", {nullptr, "__and__"}}, {"operator|", {nullptr, "__or__"}},
        {"operator^", {nullptr, "__xor__"}}, {"operator<<", {nullptr, "__lshift__"}}, {"operator>>", {nullptr, "__rshift__"}},
        {"operator&=", {nullptr, "__iand__"}}, {"operator|=", {nullptr, "__ior__"}}, {"operator^=", {nullptr, "__ixor__"}},
        {"operator~", {"__invert__", nullptr}}, {"operator[]", {nullptr, "__getitem__"}}, {"operator bool", {"__bool__", nullptr}}};
    if (name == "operator()") return "__call__";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
bool binary_operator(const std::string& python) {
    static const std::set<std::string> binary = {"__add__", "__sub__", "__mul__", "__truediv__", "__mod__", "__eq__", "__ne__",
        "__lt__", "__le__", "__gt__", "__ge__", "__and__", "__or__", "__xor__", "__lshift__", "__rshift__"};
    return binary.count(python) != 0;
}
bool slot_name(const std::string& python) {
    return python.size() > 4 && python.rfind("__", 0) == 0 && python.substr(python.size() - 2) == "__" &&
        python != "__init__" && python != "__enter__" && python != "__exit__" && python != "__copy__" && python != "__deepcopy__";
}

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;       // qualified class for constructors and methods
    bool extension = false;  // free function attached to a class
    bool receives_self = false;
};
struct Group {
    std::string name;        // Python name
    std::vector<Overload> overloads;
    bool is_static = false;
    std::string module;      // policy submodule of a module function ("" = top level)
};
struct ClassPlan {
    const Record* record;
    std::string python;      // attribute name
    std::string qualname;    // dotted name below the module
    std::string symbol;      // C++ identifier prefix
    std::vector<std::string> bases; // exported ancestor classes (qualified C++ names)
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<const Field*> fields;
    bool director = false;
    std::string parent;      // qualified outer class when nested and exported
};

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, std::size_t parts)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), parts_(parts) { validate_modules(policy_, module_); }

    GeneratedPythonSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    std::size_t parts_ = 1;
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;      // by qualified name
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_; // specialization -> Python name
    std::map<std::string, std::string> function_template_names_; // function specialization -> Python name
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
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
    std::string renamed(const std::string& qualified, const std::string& fallback) const {
        const auto found = policy_.rename.find(qualified);
        if (found == policy_.rename.end()) return fallback;
        if (!python_identifier(found->second)) throw BindingError("Python rename target is not an identifier: " + found->second);
        return found->second;
    }
    std::string symbol(const std::string& prefix) { return prefix + std::to_string(next_++); }
    bool bound(const Type& type) const {
        return type.kind == TypeKind::record && classes_.count(type.declaration);
    }
    std::string conversion(const Type& type) const {
        const auto spelling = normalize(strip_cv(type.canonical));
        for (const auto& item : policy_.conversions)
            if (normalize(item.first) == spelling) return item.first;
        // Clang omits defaulted template arguments (ns::Vector<> for ns::Vector<double>):
        // the spelled arguments must then be a prefix of the policy's.
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
    // as_base: copied as a base subobject, where abstractness does not matter.
    bool copyable_record(const std::string& name, unsigned depth = 0, bool as_base = false);
    bool copyable_type(const Type& type, unsigned depth);
    std::string value_problem(const Type& type, Use use) const;
    std::string type_problem(const Type& type, Use use) const;
    std::string function_problem(const Function& function, bool self_parameter) const;
    std::string display(const Type& type) const;
    std::string signature(const Overload& overload, const std::string& python) const;
    std::string documentation(const std::vector<Overload>& overloads, const std::string& python, bool method = false) const;
    std::string python_default(const DefaultValue& value, const Type& type) const;
    std::string text_signature(const Overload& overload, const std::string& python, bool method = false) const;
    std::string parameter_name(const Function& function, std::size_t index) const;
    std::string numpy_doc(const std::vector<Overload>& overloads, const std::vector<const Field*>& fields,
                          const Documentation* own) const;
    std::string record_documentation(const ClassPlan& plan) const;
    std::string field_documentation(const Field& field) const;
    std::string return_policy(const Function& function) const;
    std::string class_python_name(const Record& record) const;
    // A reference (or field) of a policy copy type is returned as a copy.
    bool copy_type(const Type& type) const {
        if (type.kind == TypeKind::pointer || type.kind == TypeKind::rvalue_reference) return false;
        const auto& target = referent(type);
        return target.kind == TypeKind::record && policy_.copy_types.count(target.declaration) != 0;
    }
    // Flat naming applies unless the policy qualifies the declaring namespace.
    bool flat(const std::vector<std::string>& namespaces) const {
        if (!flat_) return false;
        std::string scope;
        for (const auto& part : namespaces) scope += (scope.empty() ? "" : "::") + part;
        for (const auto& pattern : policy_.qualify) if (policy_match(pattern, scope)) return false;
        return true;
    }
    void plan_classes();
    void plan_directors();
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    std::string emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& python,
                              const std::string& indent);
    std::string emit_group(std::ostream& out, const Group& group, const std::string& kind);
    std::pair<std::string, std::string> exception_handlers(const std::string& qualified) const;
    std::string emit_constructor(std::ostream& out, const ClassPlan& plan);
    void emit_director(std::ostream& out, const ClassPlan& plan);
};

// Conservative copyability from declarations: std::is_copy_constructible cannot see that
// a member like std::vector<std::unique_ptr<T>> fails to copy.
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
        // An abstract base copies like a concrete one: check its members alone.
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
    copyable_cache_[name] = true; // provisional for recursive types // provisional for recursive types
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
    if (move_declared && !copy_declared) result = false; // implicitly deleted copy constructor
    for (const auto& field : record.fields) if (result && !copyable_type(field.type, depth + 1)) result = false;
    for (const auto& base : record.bases)
        if (result && !copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) result = false;
    copyable_cache_[name] = result;
    return result;
}

std::string Generator::value_problem(const Type& original, Use) const {
    const auto& type = original;
    if (!conversion(type).empty()) return {};
    const auto bare = normalize(strip_cv(type.canonical));
    switch (type.kind) {
    case TypeKind::builtin: {
        static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
            "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double"};
        if (supported.count(bare)) return {};
        return "unsupported Python value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to Python";
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
        return "unsupported Python template type " + type.canonical;
    }
    default:
        return "unsupported Python value type " + type.canonical;
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
        return "unsupported Python pointer type " + type.canonical;
    }
    if (type.kind == TypeKind::function) return "function types are unsupported";
    return value_problem(type, use);
}

std::string Generator::function_problem(const Function& function, bool self_parameter) const {
    // Python wrappers call each arity directly (never through a function pointer), so C++
    // overload resolution handles defaulted parameters next to templates or other overloads.
    if (!function.unsupported_reason.empty() && function.unsupported_reason.rfind("default calls with ", 0) != 0)
        return function.unsupported_reason;
    if (function.is_deleted) return "deleted function";
    if (function.default_arguments > function.parameters.size()) return "invalid default argument metadata";
    if (!function.is_constructor) {
        auto why = type_problem(function.result, Use::result);
        if (!why.empty()) return "unsupported Python result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported Python parameter type: " + why;
    }
    return {};
}

std::string Generator::display(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "str";
        return display(pointee) + " | None";
    }
    if (is_void(target)) return "None";
    if (!conversion(target).empty()) {
        const auto& converted = policy_.conversions.at(conversion(target));
        if (converted.find("vector") != std::string::npos) return "list";
        return converted;
    }
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "bool";
        if (bare == "char") return "str";
        if (bare == "float" || bare == "double" || bare == "long double") return "float";
        return "int";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enums_.find(target.declaration);
        if (found != enums_.end()) {
            const auto& enumeration = *found->second;
            return renamed(enumeration.qualified_name(), flat(enumeration.namespaces) ? enumeration.name : exported(enumeration.qualified_name()));
        }
        return "int";
    }
    if (bound(target)) return classes_.at(target.declaration).qualname;
    if (bare == "std::filesystem::path") return "str | os.PathLike";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? display(target.arguments[i]) : std::string("object"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "str";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray")
            return std::string(policy_.sequences == "tuple" ? "tuple" : "list") + "[" + arg(0) + "]";
        if (declaration == "std::set" || declaration == "std::unordered_set") return "set[" + arg(0) + "]";
        if (declaration == "std::map" || declaration == "std::unordered_map") return "dict[" + arg(0) + ", " + arg(1) + "]";
        if (declaration == "std::optional") return arg(0) + " | None";
        if (declaration == "std::complex") return "complex";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "tuple[";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + display(target.arguments[i]);
            return result + "]";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " | " : "") + display(target.arguments[i]);
            return result;
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& signature = target.arguments[0];
            std::string result = "Callable[[";
            for (std::size_t i = 1; i < signature.arguments.size(); ++i) result += (i > 1 ? ", " : "") + display(signature.arguments[i]);
            return result + "], " + display(signature.arguments[0]) + "]";
        }
        if (declaration == "std::chrono::duration") return "float";
    }
    return strip_cv(target.canonical);
}

std::string Generator::signature(const Overload& overload, const std::string& python) const {
    const auto& function = *overload.function;
    std::string result = python + "(";
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        if (i > first) result += ", ";
        const auto& name = i < function.parameter_names.size() && !function.parameter_names[i].empty()
            ? function.parameter_names[i] : "arg" + std::to_string(i - first);
        result += name + ": " + display(function.parameters[i]);
        if (i >= required) result += " = ...";
    }
    result += ")";
    if (!function.is_constructor) result += " -> " + display(function.result);
    return result;
}

std::string Generator::parameter_name(const Function& function, std::size_t index) const {
    return index < function.parameter_names.size() && !function.parameter_names[index].empty()
        ? function.parameter_names[index] : "arg" + std::to_string(index);
}

// A default value Python can evaluate in a text signature, or "" to show `...`.
std::string Generator::python_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "float") return value.value;
    if (value.kind == "bool") return value.value == "true" ? "True" : "False";
    if (value.kind == "null") return "None";
    if (value.kind == "string") return value.value;
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "enum") {
        // The enumerator's integer: text signatures cannot name module attributes reliably.
        const auto& target = referent(type);
        for (const auto& enumeration : module_.enumerations) {
            if (enumeration.qualified_name() != target.declaration) continue;
            for (std::size_t i = 0; i < enumeration.enumerators.size() && i < enumeration.enumerator_values.size(); ++i)
                if (enumeration.enumerators[i] == value.value) return enumeration.enumerator_values[i];
        }
    }
    return {};
}

std::string Generator::text_signature(const Overload& overload, const std::string& python, bool method) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::vector<std::string> parameters;
    if (overload.receives_self || method) parameters.push_back("$self");
    std::size_t positional_only = 0; // parameters that cannot be passed by keyword
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& written = i < function.parameter_names.size() ? function.parameter_names[i] : std::string{};
        auto name = written;
        if (written.empty() || keyword_safe(written) != written) {
            name = written.empty() ? "arg" + std::to_string(i - first) : keyword_safe(written);
            positional_only = parameters.size() + 1;
        }
        if (i >= required) {
            const auto value = i < function.default_values.size() ? python_default(function.default_values[i], function.parameters[i])
                                                                    : std::string();
            name += "=" + (value.empty() ? std::string("...") : value);
        }
        parameters.push_back(name);
    }
    std::string result = python + "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        result += (i ? ", " : "") + parameters[i];
        if (i + 1 == positional_only) result += ", /";
    }
    return result + ")";
}

std::string Generator::numpy_doc(const std::vector<Overload>& overloads, const std::vector<const Field*>& fields,
                                 const Documentation* own) const {
    std::string prose;
    const auto paragraph = [&](const std::string& text) {
        if (!text.empty()) prose += (prose.empty() ? "" : "\n\n") + text;
    };
    Sections sections;
    std::set<std::string> seen_docs;
    std::vector<const Documentation*> docs;
    if (own && !own->empty()) docs.push_back(own);
    for (const auto& overload : overloads) {
        const auto& documentation = overload.function->documentation;
        if (!documentation.empty() && seen_docs.insert(plain_documentation(documentation)).second) docs.push_back(&documentation);
    }
    std::vector<std::string> deprecated;
    for (const auto* documentation : docs) {
        paragraph(documentation->brief);
        std::string section;
        for (const auto& block : documentation->details) {
            if (block.kind == "heading") { section = section_title(block.text); continue; }
            if (section.empty()) paragraph(block_text(block));
            else sections.add(section, (sections.items.empty() || sections.items.back().first != section ? "" : "\n") + block_text(block));
        }
        std::string warnings, notes;
        for (const auto& note : documentation->notes) {
            if (note.name == "deprecated") { deprecated.push_back(note.text); continue; }
            if (note.name == "warning") { warnings += (warnings.empty() ? "" : "\n\n") + note.text; continue; }
            std::string label;
            if (note.name == "pre") label = "Precondition: ";
            else if (note.name == "post") label = "Postcondition: ";
            else if (note.name == "invariant") label = "Invariant: ";
            else if (note.name != "note" && note.name != "remark") {
                label = note.name;
                label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                label += ": ";
            }
            notes += (notes.empty() ? "" : "\n\n") + label + note.text;
        }
        sections.add("Warnings", warnings);
        sections.add("Notes", notes);
        std::string see;
        for (const auto& target : documentation->see) see += (see.empty() ? "" : "\n") + target;
        sections.add("See Also", see);
    }
    for (const auto& text : deprecated) prose = ".. deprecated::\n" + indent_lines(text, "    ") + (prose.empty() ? "" : "\n\n" + prose);

    // Parameters, merged across overloads by name.
    struct Entry { std::string name, type, text; };
    std::vector<Entry> parameters;
    std::string returns;
    std::set<std::string> return_types;
    std::string raises;
    std::set<std::string> raised;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const auto& documentation = function.documentation;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp_name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            const auto name = cpp_name.empty() ? "arg" + std::to_string(i - first) : keyword_safe(cpp_name);
            auto type = display(function.parameters[i]);
            if (i >= required) {
                const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
                auto shown = python_default(value, function.parameters[i]);
                if (value.kind == "enum")
                    shown = type + "." + renamed(referent(function.parameters[i]).declaration + "::" + value.value, keyword_safe(value.value));
                if (value.kind == "constant") shown = "`" + value.value + "`";
                type += shown.empty() ? ", optional" : ", default " + shown;
            }
            std::string text;
            for (const auto& item : documentation.params)
                if (item.name == cpp_name) text = item.text;
            const auto existing = std::find_if(parameters.begin(), parameters.end(), [&](const Entry& e) { return e.name == name; });
            if (existing == parameters.end()) parameters.push_back({name, type, text});
            else {
                if (existing->type != type && existing->type.find(" or " + type) == std::string::npos) existing->type += " or " + type;
                if (existing->text.empty()) existing->text = text;
            }
        }
        if (!function.is_constructor) {
            const auto type = display(function.result);
            std::string text = documentation.returns;
            for (const auto& value : documentation.retvals)
                text += (text.empty() ? "" : "\n") + std::string("- `") + value.name + "`" + (value.text.empty() ? "" : ": " + value.text);
            if ((type != "None" || !text.empty()) && return_types.insert(type + "\n" + text).second)
                returns += (returns.empty() ? "" : "\n") + type + (text.empty() ? "" : "\n" + indent_lines(text, "    "));
        }
        for (const auto& item : documentation.throws) {
            const auto exception = python_exception(item.name);
            auto text = item.text;
            if (exception == "RuntimeError" && !item.name.empty()) text = "`" + item.name + "`" + (text.empty() ? "" : ": " + text);
            if (raised.insert(exception + "\n" + text).second)
                raises += (raises.empty() ? "" : "\n") + exception + (text.empty() ? "" : "\n" + indent_lines(text, "    "));
        }
    }
    std::string parameter_text;
    for (const auto& entry : parameters)
        parameter_text += (parameter_text.empty() ? "" : "\n") + entry.name + " : " + entry.type +
            (entry.text.empty() ? "" : "\n" + indent_lines(entry.text, "    "));
    sections.add("Parameters", parameter_text);
    std::string attributes;
    for (const auto* field : fields) {
        auto text = field->documentation.brief;
        for (const auto& block : field->documentation.details)
            if (block.kind != "heading") text += (text.empty() ? "" : "\n") + block_text(block);
        attributes += (attributes.empty() ? "" : "\n") + renamed(field->name, keyword_safe(field->name)) + " : " +
            display(field->type) + (text.empty() ? "" : "\n" + indent_lines(text, "    "));
    }
    sections.add("Attributes", attributes);
    sections.add("Returns", returns);
    sections.add("Raises", raises);
    const auto rendered = sections.render();
    if (rendered.empty()) return prose;
    return prose.empty() ? rendered : prose + "\n\n" + rendered;
}

std::string Generator::documentation(const std::vector<Overload>& overloads, const std::string& python, bool method) const {
    std::string body;
    std::string override_text;
    for (const auto& overload : overloads) {
        const auto found = policy_.docs.find(overload.function->qualified_name());
        if (found != policy_.docs.end()) { override_text = found->second; break; }
    }
    body = override_text.empty() ? numpy_doc(overloads, {}, nullptr) : override_text;
    if (overloads.size() == 1)
        return text_signature(overloads.front(), python, method) + "\n--\n\n" + body;
    std::string signatures;
    for (const auto& overload : overloads) signatures += signature(overload, python) + "\n";
    return signatures + (body.empty() ? "" : "\n" + body);
}

std::string Generator::field_documentation(const Field& field) const {
    std::string text = field.documentation.brief;
    for (const auto& block : field.documentation.details)
        if (block.kind != "heading") text += (text.empty() ? "" : "\n\n") + block_text(block);
    return text + (text.empty() ? "" : "\n\n") + "Type: " + display(field.type);
}

std::string enum_documentation(const Enumeration& enumeration) {
    std::string text = enumeration.documentation.brief;
    for (const auto& block : enumeration.documentation.details)
        if (block.kind != "heading") text += (text.empty() ? "" : "\n\n") + block_text(block);
    for (const auto& note : enumeration.documentation.notes) text += (text.empty() ? "" : "\n\n") + note.text;
    return text;
}

std::string Generator::record_documentation(const ClassPlan& plan) const {
    const auto& record = *plan.record;
    const auto found = policy_.docs.find(record.qualified_name());
    std::string body = found != policy_.docs.end() ? found->second : numpy_doc(plan.constructors, plan.fields, &record.documentation);
    const auto short_name = plan.qualname.substr(plan.qualname.rfind('.') == std::string::npos ? 0 : plan.qualname.rfind('.') + 1);
    if (plan.constructors.size() == 1)
        return text_signature(plan.constructors.front(), short_name) + "\n--\n\n" + body;
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += signature(overload, plan.python) + "\n";
    return signatures + (body.empty() ? "" : "\n" + body);
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_py::Return::copy" : "rosetta_py::Return::automatic";
    return "rosetta_py::Return::" + found->second;
}

std::string Generator::class_python_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, instantiated->second);
    if (!record.owner.empty()) return renamed(qualified, keyword_safe(record.name));
    return renamed(qualified, keyword_safe(flat(record.namespaces) ? record.name : exported(qualified)));
}

void Generator::plan_classes() {
    for (const auto& requested : policy_.templates) {
        const auto found = std::find_if(module_.records.begin(), module_.records.end(), [&](const Record& record) {
            return !record.template_name.empty() && normalize(record.qualified_name()) == normalize(requested.second);
        });
        if (!python_identifier(requested.first)) throw BindingError("template binding name is not an identifier: " + requested.first);
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
            throw BindingError("unknown class in Python policy: " + selection.first);
    for (const auto& record : module_.records) records_[record.qualified_name()] = &record;
    // Selection: explicit policy classes, every class with select:all, or instantiated templates.
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
        if (!record.complete) { if (listed) reject(owner, "Python requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed Python results are not yet supported"); continue;
        }
        ClassPlan plan;
        plan.record = &record;
        plan.symbol = symbol("rosetta_class_");
        classes_.emplace(owner, std::move(plan));
    }
    // Nested classes are exported only inside exported outer classes.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = classes_.begin(); it != classes_.end();) {
            const auto& outer = it->second.record->owner;
            if (!outer.empty() && !classes_.count(outer)) { it = classes_.erase(it); changed = true; }
            else ++it;
        }
    }
    for (auto& [owner, plan] : classes_) {
        plan.python = class_python_name(*plan.record);
        plan.parent = plan.record->owner;
    }
    for (auto& [owner, plan] : classes_) {
        std::string qualname = plan.python;
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            qualname = classes_.at(parent).python + "." + qualname;
        plan.qualname = qualname;
    }
    // Exported ancestors, looking through classes that are not exported.
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
    // Drop bases that another listed base already derives from (Python needs a consistent MRO).
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
    // Creation order: bases and outer classes first.
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

// Virtual methods visible in a class (most-derived declaration first), with pure status.
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
            if (found == records_.end()) return false; // unknown base: virtuals cannot be enumerated
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
        // Directors need a constructor callable by a subclass.
        const bool constructible = std::any_of(record.constructors.begin(), record.constructors.end(), [](const Function& constructor) {
            return !constructor.is_deleted && constructor.access != "private";
        });
        plan.director = possible && constructible;
    }
}

std::string Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind,
                                     const std::string& python, const std::string& indent) {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - first;
    const std::size_t required = function.parameters.size() - function.default_arguments - first;
    out << indent << "{\n";
    out << indent << "    static const char* const names[] = {";
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string{};
        out << (i > first ? ", " : "") << quote(name);
    }
    if (count == 0) out << "nullptr";
    out << "};\n";
    out << indent << "    PyObject* a[" << std::max<std::size_t>(count, 1) << "]; Py_ssize_t n = 0;\n";
    out << indent << "    if (rosetta_py::collect(args, kwargs, names, " << count << ", " << required << ", a, &n)";
    if (overload.receives_self) out << " && rosetta_py::Param<" << function.parameters[0].canonical << ">::check(self, false)";
    for (std::size_t i = 0; i < count; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || rosetta_py::Param<" << function.parameters[i + first].canonical
            << ">::check(a[" << i << "], strict))";
    out << ") {\n";
    const auto owner = overload.owner;
    if (kind == "method") out << indent << "        auto* native = rosetta_py::native<" << owner << ">(self);\n";
    if (overload.receives_self) out << indent << "        rosetta_py::Param<" << function.parameters[0].canonical << "> self_param(self);\n";
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            out << indent << "            rosetta_py::Param<" << function.parameters[i + first].canonical << "> p" << i << "(a[" << i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        std::string writebacks;
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& type = function.parameters[i + first];
            if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                writebacks += indent + "            p" + std::to_string(i) + ".writeback(a[" + std::to_string(i) + "]);\n";
        }
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            out << indent << "            " << owner << "* created = nullptr;\n";
            if (plan.director) {
                out << indent << "            if (director) created = new " << plan.symbol << "_director(self" << (arguments.empty() ? "" : ", ") << arguments << ");\n";
                out << indent << "            else ";
            } else out << indent << "            ";
            if (plan.record->abstract) out << "rosetta_py::fail(PyExc_TypeError, " << quote(plan.qualname + " is abstract; subclass it and implement its pure virtual methods") << ");\n";
            else out << "created = new " << owner << "(" << arguments << ");\n";
            out << writebacks;
            out << indent << "            object->ptr = created; object->info = &rosetta_py::Info<" << owner << ">::value;\n"
                << indent << "            object->owned = 1; object->director = " << (plan.director ? "director" : "false") << ";\n";
            if (plan.director) out << indent << "            if (director) static_cast<rosetta_py::Director*>(static_cast<" << plan.symbol << "_director*>(created))->rosetta_self = self;\n";
            out << indent << "            return 0;\n";
        } else {
            std::string body;
            if (kind == "method") {
                const auto direct = "native->" + function.name + "(" + arguments + ")";
                if (function.is_virtual && policy_.directors != "none") {
                    if (function.is_pure)
                        body = "if (rosetta_py::is_director(self)) rosetta_py::pure_virtual(" + quote(owner + "::" + function.name) + "); return " + direct + ";";
                    else
                        body = "if (rosetta_py::is_director(self)) return native->" + owner + "::" + function.name + "(" + arguments + "); return " + direct + ";";
                } else body = "return " + direct + ";";
            } else {
                body = "return ::" + function.qualified_name() + "(" + arguments + ");";
            }
            out << indent << "            PyObject* result = rosetta_py::call([&]() -> decltype(auto) { " << body << " }, "
                << (parent ? "self" : "nullptr") << ", " << policy << ");\n";
            if (!writebacks.empty()) out << indent << "            if (result) {\n" << writebacks << indent << "            }\n";
            out << indent << "            return result;\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
    (void)python;
    return {};
}

// try/catch lines translating C++ exceptions by the first matching policy rule.
std::pair<std::string, std::string> Generator::exception_handlers(const std::string& qualified) const {
    static const std::set<std::string> known = {"ArithmeticError", "AttributeError", "BufferError", "EOFError", "FileExistsError",
        "FileNotFoundError", "IndexError", "KeyError", "LookupError", "MemoryError", "NotImplementedError", "OSError",
        "OverflowError", "PermissionError", "RuntimeError", "TimeoutError", "TypeError", "ValueError", "ZeroDivisionError"};
    for (const auto& [pattern, map] : policy_.exceptions) {
        if (!policy_match(pattern, qualified)) continue;
        std::string catches;
        for (const auto& [cpp, python] : map) {
            if (!known.count(python)) throw BindingError("policy exceptions: unsupported Python exception " + python);
            catches += "        } catch (const " + cpp + "& error) { rosetta_py::fail(PyExc_" + python + ", error.what());\n";
        }
        return {"        try {\n", catches + "        }\n"};
    }
    return {};
}

std::string Generator::emit_group(std::ostream& out, const Group& group, const std::string& kind) {
    const auto name = symbol("rosetta_function_");
    const bool single = group.overloads.size() == 1;
    const auto handlers = exception_handlers(group.overloads.front().function->qualified_name());
    out << "PyObject* " << name << "(PyObject* self, PyObject* args, PyObject* kwargs) {\n"
        << "    return rosetta_py::guard<PyObject*>([&]() -> PyObject* {\n";
    if (kind != "method") out << "        (void)self;\n";
    out << handlers.first;
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads)
        emit_overload(out, overload, overload.extension ? "function" : kind, group.name, "            ");
    out << "        }\n";
    if (binary_operator(group.name)) out << "        Py_RETURN_NOTIMPLEMENTED;\n";
    else {
        std::string signatures;
        for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
        out << "        rosetta_py::no_overload(" << quote(group.name) << ", " << quote(signatures) << ", args, kwargs);\n";
    }
    out << handlers.second << "    }, nullptr);\n}\n";
    return name;
}

std::string Generator::emit_constructor(std::ostream& out, const ClassPlan& plan) {
    const auto name = plan.symbol + "_init";
    const auto owner = plan.record->qualified_name();
    const bool single = plan.constructors.size() == 1;
    const auto handlers = exception_handlers(owner + "::" + plan.record->name);
    out << "static int " << name << "(PyObject* self, PyObject* args, PyObject* kwargs) {\n"
        << "    return rosetta_py::guard<int>([&]() -> int {\n" << handlers.first
        << "        auto* object = rosetta_py::instance(self);\n"
        << "        if (object->ptr) rosetta_py::fail(PyExc_RuntimeError, \"native object already initialized\");\n";
    if (plan.director)
        out << "        const bool director = Py_TYPE(self) != rosetta_py::Info<" << owner << ">::value.type;\n";
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", plan.python, "            ");
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, plan.python) + "\n";
    out << "        }\n        rosetta_py::no_overload(" << quote(plan.qualname) << ", " << quote(signatures) << ", args, kwargs);\n"
        << handlers.second << "    }, -1);\n}\n";
    return name;
}

void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << plan.symbol << "_director final : " << owner << ", rosetta_py::Director {\n"
        << "    template<class... A> explicit " << plan.symbol << "_director(PyObject*, A&&... arguments) : " << owner
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
        if (method->access == "private" && !is_pure) continue; // a private base implementation cannot be called
        const auto python = renamed(method->qualified_name(), keyword_safe(method->name));
        std::string parameters, arguments, types;
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            parameters += (i ? ", " : "") + method->parameters[i].canonical + " a" + std::to_string(i);
            const auto& type = method->parameters[i];
            const bool forward = type.kind == TypeKind::rvalue_reference || (type.kind != TypeKind::lvalue_reference && type.kind != TypeKind::pointer);
            arguments += std::string(", ") + (forward ? "std::move(a" + std::to_string(i) + ")" : "a" + std::to_string(i));
            types += ", " + type.canonical;
        }
        const auto result = method->result.canonical;
        out << "    " << result << " " << method->name << "(" << parameters << ")" << (method->is_const ? " const" : "")
            << (method->is_noexcept ? " noexcept" : "") << " override {\n";
        if (method->is_noexcept) out << "        try {\n";
        out << "        rosetta_py::Gil gil;\n"
            << "        rosetta_py::Ref method = rosetta_py::override_for(rosetta_self, rosetta_py::Info<" << owner << ">::value.type, "
            << quote(python) << ");\n"
            << "        if (method) return rosetta_py::invoke_python<" << result << types << ">(method.get()" << arguments << ");\n";
        std::string call_arguments;
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            const auto& type = method->parameters[i];
            const bool forward = type.kind == TypeKind::rvalue_reference || (type.kind != TypeKind::lvalue_reference && type.kind != TypeKind::pointer);
            call_arguments += std::string(i ? ", " : "") + (forward ? "std::move(a" + std::to_string(i) + ")" : "a" + std::to_string(i));
        }
        if (is_pure) out << "        rosetta_py::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (const rosetta_py::python_error&) { rosetta_py::Gil gil; PyErr_WriteUnraisable(rosetta_self); }\n"
                << "        std::terminate();\n";
        out << "    }\n";
    }
    out << "};\n";
}

GeneratedPythonSource Generator::run() {
    // A dotted name (package._native) names the module inside a package; PyInit_ uses its last part.
    {
        std::size_t start = 0;
        while (true) {
            const auto dot = name_.find('.', start);
            if (!python_identifier(name_.substr(start, dot - start)))
                throw BindingError("module name must be a Python identifier or dotted path of identifiers");
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }
    for (const auto& rule : policy_.enum_flags)
        if (std::none_of(module_.enumerations.begin(), module_.enumerations.end(), [&](const Enumeration& e) { return e.qualified_name() == rule.first; }))
            throw BindingError("enum flag policy refers to an unknown enum: " + rule.first);
    GeneratedPythonSource result;
    // Frontend diagnostics for declarations this backend exports itself.
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
    // Enumerations nested in classes that are not exported are not visible.
    for (auto it = enums_.begin(); it != enums_.end();) {
        if (!it->second->owner.empty() && !classes_.count(it->second->owner)) it = enums_.erase(it);
        else ++it;
    }
    // Free functions attached to classes as methods.
    std::map<std::string, std::vector<Group>> extensions;
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner) && excluded(owner)) continue;
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [python, target] : methods) {
            Group group;
            group.name = python;
            bool any = false, any_static = false;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const std::string& target_owner = owner;
                const bool self = !function.parameters.empty() && [&] {
                    const auto& first = function.parameters[0];
                    const auto& base = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
                    const auto& bases = classes_.at(target_owner).bases;
                    return base.kind == TypeKind::record && (base.declaration == target_owner ||
                        std::find(bases.begin(), bases.end(), base.declaration) != bases.end());
                }();
                auto why = function_problem(function, self);
                if (!why.empty()) { reject(owner + "." + python, why); continue; }
                if (!self) any_static = true;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    plan_directors();

    // Methods, constructors and fields.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        const auto config = policy_.classes.find(owner);
        const bool constructors = config == policy_.classes.end() || config->second.constructors;
        if (config != policy_.classes.end()) {
            for (const auto& requested : config->second.methods) {
                const auto found = std::find_if(record.methods.begin(), record.methods.end(), [&](const Function& f) { return f.name == requested; });
                if (found == record.methods.end()) throw BindingError("unknown Python policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Python policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown Python policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("Python policy field is not public: " + owner + "::" + requested);
            }
        }
        if (constructors && record.public_destructor) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || (constructor.access != "public" && !(plan.director && constructor.access == "protected"))) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                // Move constructors duplicate copies for Python callers.
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                auto why = function_problem(constructor, false);
                if (!why.empty()) { reject(owner + "::" + record.name, why); continue; }
                plan.constructors.push_back({&constructor, owner});
            }
        }
        std::map<std::string, std::size_t> group_index;
        std::set<std::string> members = {"__init__", "__new__", "__del__", "__dict__", "__weakref__", "__class__"};
        const auto add = [&](const std::string& python, const Overload& overload, bool is_static) {
            const auto found = group_index.find(python);
            if (found == group_index.end()) {
                group_index[python] = plan.methods.size();
                plan.methods.push_back({python, {overload}, is_static, {}});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a Python name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            std::string python;
            if (method.name.rfind("operator", 0) == 0 && (method.name.size() == 8 || !python_identifier(method.name.substr(8)) || method.name == "operator bool")) {
                python = operator_name(method.name, method.parameters.size());
                if (python.empty()) continue; // assignment, conversion and other operators stay C++-only
                if (python == "__getitem__" && method.parameters.size() != 1) continue;
            } else {
                python = keyword_safe(method.name);
            }
            python = renamed(method.qualified_name(), python);
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(python, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner]) {
            for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        }
        for (const auto& group : plan.methods) members.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            // Lvalue reference fields are read-only and borrow from their owner, like reference results.
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported Python field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported Python field type", 0) == 0 ? why : "unsupported Python field type: " + why); continue; }
            const auto python = renamed(qualified, keyword_safe(field.name));
            if (!members.insert(python).second) throw BindingError("reserved or duplicate Python member: " + python);
            plan.fields.push_back(&field);
        }
    }

    // Module functions grouped by Python name.
    std::vector<Group> functions;
    std::map<std::string, std::size_t> function_index;
    for (const auto& function : module_.functions) {
        if (extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        const auto specialization = function_template_names_.find(function.qualified_name());
        if (!function.template_name.empty() && specialization == function_template_names_.end()) continue;
        const auto submodule = policy_module(policy_, function.namespaces);
        const auto python = specialization != function_template_names_.end() ? specialization->second
            : renamed(function.qualified_name(), keyword_safe(!submodule.empty() || flat(function.namespaces) ? function.name
                                                                                 : exported(function.qualified_name())));
        const auto key = submodule + "." + python;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions.size();
            functions.push_back({python, {{&function, {}}}, false, submodule});
        } else functions[found->second].overloads.push_back({&function, {}});
    }

    // Module-level names must be unique.
    std::map<std::string, std::string> names;
    const auto reserve = [&](const std::string& python, const std::string& what) {
        const auto inserted = names.emplace(python, what);
        if (!inserted.second) throw BindingError("Python export name collision: " + python + " (" + inserted.first->second + " and " + what + ")");
    };
    // Submodules are module-level names too; their functions are unique within each submodule.
    std::map<std::string, std::set<std::string>> submodule_names;
    for (const auto& group : functions) {
        if (group.module.empty()) { reserve(group.name, "function " + group.overloads.front().function->qualified_name()); continue; }
        if (!submodule_names.count(group.module)) reserve(group.module, "submodule " + group.module);
        submodule_names[group.module].insert(group.name);
    }
    for (const auto& owner : class_order_) if (classes_.at(owner).parent.empty()) reserve(classes_.at(owner).python, "class " + owner);

    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Python";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Wrapper code is collected in units (a class, or a module function) that a split
    // build spreads over several sources; `out` is the prelude every source repeats.
    struct Unit { std::string code, declaration; };
    std::vector<Unit> units;
    std::ostringstream out, init;
    std::string module_namespace = "rosetta_module_";
    for (const char c : name_) module_namespace += c == '.' ? '_' : c;
    out << "// Generated by Rosetta: CPython 3.12+ native extension.\n#define PY_SSIZE_T_CLEAN\n#include <Python.h>\n"
        << "#if PY_VERSION_HEX < 0x030C0000\n#error Rosetta requires CPython 3.12 or newer\n#endif\n";
    for (const auto* header : {"array", "chrono", "cmath", "complex", "cstring", "deque", "filesystem", "functional", "limits",
                               "list", "map", "memory", "new", "optional", "set", "stdexcept", "string", "string_view", "tuple",
                               "typeindex", "type_traits", "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n';
    out << "#define ROSETTA_PY_MODULE " << module_namespace << '\n';
    for (const auto* part : detail::python_runtime_parts) out << part;
    out << "namespace ROSETTA_PY_MODULE { namespace rosetta_py {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(PyObject* value, bool strict) { return Value<" << to << ">::check(value, strict); }\n"
            << "    static " << from << " load(PyObject* value) { return " << from << "(Value<" << to << ">::load(value)); }\n"
            << "    static PyObject* cast(const " << from << "& value) { return Value<" << to << ">::cast(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\nnamespace ROSETTA_PY_MODULE {\n";

    for (const auto& owner : class_order_) if (classes_.at(owner).director) emit_director(out, classes_.at(owner));
    out << "}\n";

    std::ostringstream module_methods;
    std::map<std::string, std::ostringstream> submodule_methods;
    for (const auto& group : functions) {
        std::ostringstream unit;
        const auto symbol_name = emit_group(unit, group, "function");
        units.push_back({unit.str(), "PyObject* " + symbol_name + "(PyObject*, PyObject*, PyObject*);\n"});
        (group.module.empty() ? module_methods : submodule_methods[group.module])
            << "    {" << quote(group.name) << ", reinterpret_cast<PyCFunction>(reinterpret_cast<void(*)()>(" << symbol_name
            << ")), METH_VARARGS | METH_KEYWORDS, " << quote(documentation(group.overloads, group.name)) << "},\n";
    }

    init << "        rosetta_py::sequence_results_as_tuple = " << (policy_.sequences == "tuple" ? "true" : "false") << ";\n"
         << "        rosetta_py::state(module)->types = rosetta_py::checked(PyList_New(0));\n";
    if (policy_.sequences == "vector") init << "        rosetta_py::install_vector_types(module);\n";
    init
         << "        rosetta_py::initialize_runtime(&rosetta_root_spec, &rosetta_callable_spec);\n"
         << "        rosetta_py::add_object(module, \"_NativeObject\", Py_NewRef(reinterpret_cast<PyObject*>(rosetta_py::root_type())));\n";
    // Policy submodules: plain modules holding their functions, also registered in sys.modules
    // so `from package.module.data import f` works.
    {
        std::size_t index = 0;
        for (const auto& [submodule, methods] : submodule_methods) {
            (void)methods;
            init << "        {\n"
                 << "            PyObject* submodule = rosetta_py::checked(PyModule_New(" << quote(name_ + "." + submodule) << "));\n"
                 << "            if (PyModule_AddFunctions(submodule, rosetta_submodule_functions_" << index << ") < 0) { Py_DECREF(submodule); throw rosetta_py::python_error{}; }\n"
                 << "            if (PyDict_SetItemString(PyImport_GetModuleDict(), " << quote(name_ + "." + submodule) << ", submodule) < 0) { Py_DECREF(submodule); throw rosetta_py::python_error{}; }\n"
                 << "            rosetta_py::add_object(module, " << quote(submodule) << ", submodule);\n"
                 << "        }\n";
            ++index;
        }
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        std::ostringstream unit, methods, getset;
        std::vector<std::string> slots;
        for (const auto& group : plan.methods) {
            const bool is_static = group.is_static;
            const auto symbol_name = emit_group(unit, group, is_static ? "function" : "method");
            methods << "    {" << quote(group.name) << ", reinterpret_cast<PyCFunction>(reinterpret_cast<void(*)()>(" << symbol_name
                    << ")), METH_VARARGS | METH_KEYWORDS" << (is_static ? " | METH_STATIC" : "") << ", "
                    << quote(documentation(group.overloads, group.name, !is_static)) << "},\n";
            if (slot_name(group.name)) slots.push_back(group.name);
        }
        for (const auto* field : plan.fields) {
            const auto qualified = owner + "::" + field->name;
            const auto python = renamed(qualified, keyword_safe(field->name));
            const auto field_symbol = symbol("rosetta_field_");
            unit << "static PyObject* " << field_symbol << "_get(PyObject* self, void*) {\n"
                << "    return rosetta_py::guard<PyObject*>([&]() -> PyObject* {\n"
                << "        auto* native = rosetta_py::native<" << owner << ">(self);\n"
                << "        return rosetta_py::result<decltype((native->" << field->name << "))>(native->" << field->name
                << ", self, rosetta_py::Return::" << (copy_type(field->type) ? "copy" : "automatic") << ");\n    }, nullptr);\n}\n";
            const bool writable = !field->type.is_const && !readonly(qualified, owner) && field->type.kind != TypeKind::lvalue_reference;
            if (writable)
                unit << "static int " << field_symbol << "_set(PyObject* self, PyObject* value, void*) {\n"
                    << "    return rosetta_py::guard<int>([&]() -> int {\n"
                    << "        if (!value) rosetta_py::fail(PyExc_AttributeError, \"cannot delete native fields\");\n"
                    << "        auto* native = rosetta_py::native<" << owner << ">(self);\n"
                    << "        rosetta_py::Param<const std::remove_cv_t<std::remove_reference_t<decltype(native->" << field->name
                    << ")>>&> converted(value);\n"
                    << "        rosetta_py::assign_field(native->" << field->name << ", converted.get());\n        return 0;\n    }, -1);\n}\n";
            const auto setter = writable ? "std::is_copy_assignable_v<std::remove_cv_t<decltype(" + owner + "::" + field->name + ")>> ? "
                + field_symbol + "_set : nullptr" : std::string("nullptr");
            getset << "    {" << quote(python) << ", " << field_symbol << "_get, " << setter
                   << ", " << quote(field_documentation(*field)) << ", nullptr},\n";
        }
        const auto init_symbol = plan.constructors.empty() ? std::string("rosetta_py::abstract_init") : emit_constructor(unit, plan);
        const auto doc = record_documentation(plan);
        unit << "static PyMethodDef " << plan.symbol << "_methods[] = {\n" << methods.str() << "    {nullptr, nullptr, 0, nullptr}};\n"
            << "static PyGetSetDef " << plan.symbol << "_getset[] = {\n" << getset.str() << "    {nullptr, nullptr, nullptr, nullptr, nullptr}};\n"
            << "static PyType_Slot " << plan.symbol << "_slots[] = {\n"
            << "    {Py_tp_init, reinterpret_cast<void*>(&" << init_symbol << ")},\n"
            << "    {Py_tp_traverse, reinterpret_cast<void*>(&rosetta_py::traverse)},\n"
            << "    {Py_tp_clear, reinterpret_cast<void*>(&rosetta_py::clear)},\n"
            << "    {Py_tp_methods, " << plan.symbol << "_methods},\n    {Py_tp_getset, " << plan.symbol << "_getset},\n"
            << "    {Py_tp_doc, const_cast<char*>(" << quote(doc) << ")},\n    {0, nullptr}};\n"
            << "PyType_Spec " << plan.symbol << "_spec = {" << quote(name_ + "." + plan.qualname)
            << ", 0, 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, " << plan.symbol << "_slots};\n";
        units.push_back({unit.str(), "extern PyType_Spec " + plan.symbol + "_spec;\n"});
        // Registration and type creation.
        init << "        {\n            auto& info = rosetta_py::Info<" << owner << ">::value;\n"
             << "            rosetta_py::register_type<" << owner << ">(info, " << quote(plan.qualname) << ");\n"
             << "            info.bases.clear();\n";
        for (const auto& base : plan.bases) init << "            rosetta_py::add_base<" << owner << ", " << base << ">(info);\n";
        if (plan.bases.empty())
            init << "            rosetta_py::Ref bases(PyTuple_Pack(1, reinterpret_cast<PyObject*>(rosetta_py::root_type())));\n";
        else {
            init << "            rosetta_py::Ref bases(PyTuple_Pack(" << plan.bases.size();
            for (const auto& base : plan.bases) init << ", reinterpret_cast<PyObject*>(rosetta_py::Info<" << base << ">::value.type)";
            init << "));\n";
        }
        init << "            if (!bases) throw rosetta_py::python_error{};\n"
             << "            info.type = rosetta_py::make_type(module, &" << plan.symbol << "_spec, bases.get());\n"
             << "            PyObject* type = reinterpret_cast<PyObject*>(info.type);\n"
             << "            rosetta_py::keep(module, type);\n"
             << "            Py_DECREF(type);\n";
        for (const auto& slot : slots) init << "            rosetta_py::install_slot(type, " << quote(slot) << ");\n";
        if (plan.parent.empty())
            init << "            rosetta_py::add_object(module, " << quote(plan.python) << ", Py_NewRef(type));\n";
        else
            init << "            rosetta_py::add_object(reinterpret_cast<PyObject*>(rosetta_py::Info<" << plan.parent
                 << ">::value.type), " << quote(plan.python) << ", Py_NewRef(type));\n";
        // Constant static members become class attributes.
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to Python"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            init << "            rosetta_py::add_object(type, " << quote(renamed(qualified, keyword_safe(variable.name)))
                 << ", rosetta_py::Value<std::remove_cv_t<decltype(" << qualified << ")>>::cast(static_cast<std::remove_cv_t<decltype("
                 << qualified << ")>>(" << qualified << ")));\n";
        }
        init << "        }\n";
    }
    std::map<std::string, std::string> enum_names; // qualified enum -> module attribute
    // Enumerations: IntEnum (IntFlag with a flag policy) plus SWIG-style constants in flat mode.
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        const auto python = renamed(qualified, keyword_safe(nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        std::string qualname = python;
        if (nested) qualname = classes_.at(enumeration.owner).qualname + "." + python;
        if (!nested) {
            if (names.count(python)) { if (!flat_) throw BindingError("Python export name collision: " + python); }
            else reserve(python, "enum " + qualified);
        }
        const auto target = nested ? "reinterpret_cast<PyObject*>(rosetta_py::Info<" + enumeration.owner + ">::value.type)" : std::string("module");
        std::string signed_members, unsigned_members;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto& value = enumeration.enumerator_values[i];
            const auto member = renamed(qualified + "::" + enumeration.enumerators[i], keyword_safe(enumeration.enumerators[i]));
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            if (big) unsigned_members += "{" + quote(member) + ", " + value + "ULL}, ";
            else signed_members += "{" + quote(member) + ", " + (value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL") + "}, ";
        }
        const bool flags = policy_.enum_flags.count(qualified) != 0;
        init << "        {\n            PyObject* enum_type = rosetta_py::make_enum(module, " << quote(flags ? "IntFlag" : "IntEnum") << ", "
             << quote(python) << ", " << quote(qualname) << ", {" << signed_members << "}, {" << unsigned_members << "});\n"
             << "            rosetta_py::keep(module, enum_type);\n";
        if (const auto doc = enum_documentation(enumeration); !doc.empty())
            init << "            if (PyObject_SetAttrString(enum_type, \"__doc__\", rosetta_py::Ref(rosetta_py::checked(PyUnicode_FromString("
                 << quote(doc) << "))).get()) < 0) throw rosetta_py::python_error{};\n";
        init
             << "            Py_DECREF(enum_type);\n"
             << "            rosetta_py::enum_types()[std::type_index(typeid(" << qualified << "))] = enum_type;\n";
        if (!(flat_ && !nested && names.at(python) != "enum " + qualified)) {
            init << "            rosetta_py::add_object(" << target << ", " << quote(python) << ", Py_NewRef(enum_type));\n";
            if (!nested) enum_names[qualified] = python;
        }
        if (flat_) {
            for (const auto& enumerator : enumeration.enumerators) {
                const auto member = renamed(qualified + "::" + enumerator, keyword_safe(enumerator));
                const auto constant = enumeration.scoped ? renamed(qualified + "::" + enumerator, python + "_" + keyword_safe(enumerator)) : member;
                if (!nested && !enumeration.scoped) {
                    if (names.count(constant)) { reject(qualified + "::" + enumerator, "enumerator name collides with another export"); continue; }
                    reserve(constant, "enumerator " + qualified + "::" + enumerator);
                } else if (!nested) {
                    if (names.count(constant)) { reject(qualified + "::" + enumerator, "enumerator name collides with another export"); continue; }
                    reserve(constant, "enumerator " + qualified + "::" + enumerator);
                }
                init << "            rosetta_py::add_object(" << target << ", " << quote(constant)
                     << ", rosetta_py::checked(PyObject_GetAttrString(enum_type, " << quote(member) << ")));\n";
            }
        }
        init << "        }\n";
    }
    // Constant namespace-scope variables.
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to Python"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto python = renamed(qualified, keyword_safe(flat(variable.namespaces) ? variable.name : exported(qualified)));
        if (names.count(python)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(python, "constant " + qualified);
        init << "        rosetta_py::add_object(module, " << quote(python) << ", rosetta_py::Value<std::remove_cv_t<decltype(::" << qualified
             << ")>>::cast(static_cast<std::remove_cv_t<decltype(::" << qualified << ")>>(::" << qualified << ")));\n";
    }
    // Using-declarations re-export bound entities under another namespace's name (ns__Name in flat mode).
    {
        std::map<std::string, std::string> function_names;
        for (const auto& group : functions)
            for (const auto& overload : group.overloads) function_names.emplace(overload.function->qualified_name(), group.name);
        const auto alias = [&](const std::string& name, const std::string& target) {
            if (name == target || names.count(name)) return;
            reserve(name, "alias of " + target);
            init << "        rosetta_py::add_object(module, " << quote(name) << ", rosetta_py::checked(PyObject_GetAttrString(module, "
                 << quote(target) << ")));\n";
        };
        for (const auto& reexport : module_.reexports) {
            std::string scope;
            for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
            const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
            const auto python = renamed(qualified, keyword_safe(flat(reexport.namespaces) ? reexport.name : exported(qualified)));
            if (reexport.kind == "record" || reexport.kind == "type") {
                const auto c = classes_.find(reexport.target);
                if (c != classes_.end() && c->second.parent.empty()) alias(python, c->second.python);
            }
            if (reexport.kind == "enum" || reexport.kind == "type") {
                const auto e = enum_names.find(reexport.target);
                if (e == enum_names.end()) continue;
                alias(python, e->second);
                // Flat mode also exports Enum_member constants for scoped enums.
                for (const auto& enumeration : module_.enumerations) {
                    if (enumeration.qualified_name() != reexport.target || !flat_ || !enumeration.scoped) continue;
                    for (const auto& enumerator : enumeration.enumerators) {
                        const auto member = keyword_safe(enumerator);
                        const auto constant = renamed(reexport.target + "::" + enumerator, e->second + "_" + member);
                        if (names.count(constant)) alias(python + "_" + member, constant);
                    }
                }
            }
            if (reexport.kind == "function") {
                const auto f = function_names.find(reexport.target);
                if (f != function_names.end()) alias(python, f->second);
            }
        }
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for Python";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Units go to parts_ - 1 extra sources by size (largest first, into the smallest
    // source), keeping declaration order within each; module init stays in source.
    std::vector<std::ostringstream> extra(parts_ > 1 ? parts_ - 1 : 0);
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
        for (auto& part : extra) part << prelude << "namespace ROSETTA_PY_MODULE {\n";
        out << "namespace ROSETTA_PY_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) {
            extra[bin[i]] << units[i].code;
            out << units[i].declaration;
        }
        for (auto& part : extra) part << "} // namespace ROSETTA_PY_MODULE\n";
    } else {
        out << "namespace ROSETTA_PY_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "static PyType_Spec rosetta_root_spec = {" << quote(name_ + "._NativeObject")
        << ", static_cast<int>(sizeof(rosetta_py::Instance)), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, rosetta_py::root_slots};\n"
        << "static PyType_Spec rosetta_callable_spec = {" << quote(name_ + "._NativeCallable")
        << ", static_cast<int>(sizeof(rosetta_py::Callable)), 0, Py_TPFLAGS_DEFAULT, rosetta_py::callable_slots};\n"
        ;
    {
        std::size_t index = 0;
        for (const auto& [submodule, methods] : submodule_methods)
            out << "static PyMethodDef rosetta_submodule_functions_" << index++ << "[] = {\n" << methods.str() << "    {nullptr, nullptr, 0, nullptr}};\n";
    }
    out << "static PyMethodDef rosetta_functions[] = {\n" << module_methods.str()
        << "    {\"_set_doc\", rosetta_py::set_doc, METH_VARARGS, \"_set_doc(obj, text)\\n--\\n\\nSet obj.__doc__, including for this module's (read-only) functions and methods.\"},\n"
        << "    {nullptr, nullptr, 0, nullptr}};\n"
        << "static int rosetta_exec(PyObject* module) {\n    return rosetta_py::guard<int>([&]() -> int {\n" << init.str()
        << "        return 0;\n    }, -1);\n}\n"
        << "static PyModuleDef_Slot rosetta_slots[] = {\n{Py_mod_exec, reinterpret_cast<void*>(&rosetta_exec)},\n"
        << "{Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},\n"
        << "#if PY_VERSION_HEX >= 0x030D0000\n{Py_mod_gil, Py_MOD_GIL_USED},\n#endif\n{0, nullptr}};\n"
        << "static PyModuleDef rosetta_module = {PyModuleDef_HEAD_INIT, " << quote(name_)
        << ", nullptr, static_cast<Py_ssize_t>(sizeof(rosetta_py::ModuleState)), rosetta_functions, rosetta_slots, "
        << "rosetta_py::module_traverse, rosetta_py::module_clear, rosetta_py::module_free};\n"
        << "} // namespace ROSETTA_PY_MODULE\n"
        << "PyMODINIT_FUNC PyInit_" << name_.substr(name_.rfind('.') + 1) << "() { return PyModuleDef_Init(&ROSETTA_PY_MODULE::rosetta_module); }\n";
    result.source = out.str();
    for (const auto& part : extra) result.parts.push_back(part.str());
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedPythonSource generate_python(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy,
                                      std::size_t parts) {
    if (parts == 0) throw BindingError("Python source parts must be positive");
    return Generator(module, name, skip, policy, parts).run();
}
} // namespace rosetta
