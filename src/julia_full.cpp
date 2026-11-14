// Full Julia backend (--julia-api full): native classes as abstract types (with a hidden concrete
// storage type each, so `circle isa Shape` holds), methods as generic functions dispatching on them,
// runtime overload resolution in C++, Julia subclasses overriding C++ virtual methods (directors,
// @subclass), fields as properties, operators as Base methods, containers, enums as Symbols,
// callbacks, and docstrings carrying the C++ documentation as Julia Markdown.
//
// It writes a C++ source (a shared library with one entry point, rosetta_<module>_call) and a
// Julia module that loads it with Libdl and ccall. The C++ runtime and value encoding are those
// of the full Common Lisp backend (lisp_full_runtime.hpp); julia_full_runtime_julia.hpp is the
// Julia side.
#include "rosetta/julia.hpp"
#include "julia_base_names.hpp"
#include "julia_full_runtime_julia.hpp"
#include "cabi_full.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;
// A Julia string literal (no interpolation).
std::string jl_string(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"' || c == '$') result += '\\';
        if (c == '\n') result += "\\n";
        else result += c;
    }
    return result + '"';
}
// A docstring: a triple-quoted Julia string starting at column 0 (so nothing is dedented).
std::string jl_doc(const std::string& text) {
    std::string result = "\"\"\"\n";
    for (char c : text) {
        if (c == '\\' || c == '"' || c == '$') result += '\\';
        result += c;
    }
    return result + "\n\"\"\"\n";
}
bool julia_keyword(const std::string& name) {
    static const std::set<std::string> keywords = {"baremodule", "begin", "break", "catch", "const", "continue", "do", "else",
        "elseif", "end", "export", "false", "finally", "for", "function", "global", "if", "import", "let", "local", "macro",
        "module", "quote", "return", "struct", "true", "try", "using", "while", "in", "isa",
        // Contextual keywords (abstract, mutable, primitive, type, where, outer, public, var) work as
        // function, argument and field names and stay as written. ans is the REPL's binding in Main.
        "ans",
        // Every Julia module defines its own eval and include.
        "eval", "include"};
    return keywords.count(name) != 0;
}
// The Julia spelling of a C++ name: kept as written; keywords get a trailing underscore.
std::string julia_name(const std::string& name) {
    if (name.empty()) return "unnamed";
    return julia_keyword(name) ? name + "_" : name;
}
bool base_function(const std::string& name) {
    const auto& names = detail::julia_base_names();
    const auto found = names.find(name);
    return found != names.end() && found->second;
}
bool base_name(const std::string& name) { return detail::julia_base_names().count(name) != 0; }
// The Base function of a C++ operator with `arity` operands besides the first ("" when none).
// "(call)" is the functor syntax (obj)(args...).
std::string operator_function(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"Base.:+", "Base.:+"}}, {"operator-", {"Base.:-", "Base.:-"}}, {"operator*", {nullptr, "Base.:*"}},
        {"operator/", {nullptr, "Base.:/"}}, {"operator%", {nullptr, "Base.rem"}}, {"operator==", {nullptr, "Base.:(==)"}},
        {"operator!=", {nullptr, "Base.:(!=)"}}, {"operator<", {nullptr, "Base.:<"}}, {"operator<=", {nullptr, "Base.:<="}},
        {"operator>", {nullptr, "Base.:>"}}, {"operator>=", {nullptr, "Base.:>="}}, {"operator&", {nullptr, "Base.:&"}},
        {"operator|", {nullptr, "Base.:|"}}, {"operator^", {nullptr, "Base.xor"}}, {"operator~", {"Base.:~", nullptr}},
        {"operator!", {"Base.:!", nullptr}}, {"operator[]", {nullptr, "Base.getindex"}}};
    if (name == "operator()") return "(call)";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}


// --- documentation (Julia Markdown) -------------------------------------------

std::string doc_block(const DocBlock& block) {
    if (block.kind == "code") {
        std::string language = block.language;
        if (language.empty() || language == "c++" || language == "cxx" || language == "cc") language = "cpp";
        if (language == "py") language = "python";
        if (language == "jl") language = "julia";
        return "```" + language + "\n" + block.text + "\n```";
    }
    if (block.kind == "math") return "```math\n" + block.text + "\n```";
    if (block.kind == "heading") return "**" + block.text + "**";
    return block.text;
}
// Description of a declaration: brief, details, notes, errors, see also.
std::string description(const Documentation& doc) {
    std::vector<std::string> paragraphs;
    if (!doc.brief.empty()) paragraphs.push_back(doc.brief);
    for (const auto& block : doc.details) paragraphs.push_back(doc_block(block));
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        if (item.name == "warning" || item.name == "note" || item.name == "deprecated")
            paragraphs.push_back("!!! " + item.name + "\n    " + one_line(item.text));
        else paragraphs.push_back("*" + label + ":* " + item.text);
    }
    if (!doc.throws.empty()) {
        std::string errors = "# Throws\n";
        for (const auto& item : doc.throws)
            errors += "\n- " + (item.name.empty() ? std::string() : "`" + item.name + "`" + (item.text.empty() ? "" : ": ")) + one_line(item.text);
        errors += "\n\nC++ exceptions are raised as `NativeError`.";
        paragraphs.push_back(errors);
    }
    if (!doc.see.empty()) {
        std::string see = "See also: ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + std::string("`") + doc.see[i] + "`";
        paragraphs.push_back(see + ".");
    }
    std::string out;
    for (const auto& paragraph : paragraphs) out += (out.empty() ? "" : "\n\n") + paragraph;
    return out;
}

class Generator : public cabi::CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const JuliaFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedJuliaFullSource run();

private:
    std::string package_;        // the Julia module's name
    JuliaFullOptions options_;

    std::string language() const override { return "Julia"; }
    std::string target_name(const std::string& cpp) const override { return julia_name(cpp); }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return operator_function(cpp, arity); }
    bool free_operator(const std::string& name) const override { return name != "(call)" && name != "Base.getindex"; }
    std::string static_constant_name(const ClassPlan& plan, const std::string& cpp) const override { return plan.name + "_" + cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override;
    std::string signatures(const Group& group, bool is_method) const override {
        std::string text;
        for (const auto& overload : group.overloads)
            text += "    " + signature(overload, group.name, is_method && !overload.extension ? "self::" + classes_.at(overload.owner).name : std::string()) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + signature(overload, plan.name, "") + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract; subclass it in Julia with @subclass mutable struct MyType <: " + plan.name + " ... end";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected; subclass it in Julia with @subclass";
    }
    std::string symbol_prefix() const override { return "rosetta_jl_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_JULIA"; }
    std::string module_prefix() const override { return "rosetta_julia_module_"; }

    static std::string storage(const ClassPlan& plan) { return "_" + plan.name; }
    std::string jl_type(const Type& type) const;
    std::string signature(const Overload& overload, const std::string& name, const std::string& self) const;
    std::string jl_default(const DefaultValue& value, const Type& type) const;
    std::string group_doc(const Group& group, const std::string& shown, const std::string& self) const;
    std::string class_doc(const ClassPlan& plan) const;
    // The type a method of `owner` dispatches on: the abstract type, joined (Union) with the
    // classes that derive from it only through a secondary base (Julia has single inheritance).
    std::string dispatch_type(const std::string& owner) const;
};


// The Julia type of a C++ type, as shown in docstrings.
std::string Generator::jl_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "String";
        return "Union{" + jl_type(pointee) + ", Nothing}";
    }
    if (is_void(target)) return "Nothing";
    if (!conversion(target).empty()) return policy_.conversions.at(conversion(target)) == "std::string" ? "String" : "Any";
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "Bool";
        if (bare == "char") return "String";
        if (bare == "float" || bare == "double" || bare == "long double") return "Float64";
        if (bare == "unsigned long long" || (bare == "unsigned long" && sizeof(long) == 8)) return "Integer";
        return "Int";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? "Symbol" : "Int";
    }
    if (bound(target)) return classes_.at(target.declaration).name;
    if (bare == "std::filesystem::path") return "String";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "Nothing";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? jl_type(target.arguments[i]) : std::string("Any"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "String";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set")
            return "Vector{" + arg(0) + "}";
        if (declaration == "std::map" || declaration == "std::unordered_map") return "Dict{" + arg(0) + ", " + arg(1) + "}";
        if (declaration == "std::optional") return "Union{" + arg(0) + ", Nothing}";
        if (declaration == "std::complex") return "ComplexF64";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "Tuple{";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + jl_type(target.arguments[i]);
            return result + "}";
        }
        if (declaration == "std::variant") {
            std::string result = "Union{";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + jl_type(target.arguments[i]);
            return result + "}";
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string result = "Function (";
            for (std::size_t i = 1; i < function.arguments.size(); ++i) result += (i > 1 ? ", " : "") + jl_type(function.arguments[i]);
            result += ") -> " + (function.arguments.empty() || is_void(function.arguments[0]) ? std::string("Nothing") : jl_type(function.arguments[0]));
            return result;
        }
        if (declaration == "std::chrono::duration") return "Float64";
    }
    return "Any";
}

// Julia names of an overload's parameters (without self), unique within it.
std::vector<std::string> Generator::parameter_names(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    std::vector<std::string> names;
    std::set<std::string> taken{"self", "_Rosetta"};
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : julia_name(cpp);
        while (!taken.insert(name).second) name += "_";
        names.push_back(name);
    }
    return names;
}

// "add(a::Int, b::Int=10) -> Int": how an overload is called. `self` is the receiver's shown
// parameter ("self::Circle", "::Type{Circle}") or empty.
std::string Generator::signature(const Overload& overload, const std::string& name, const std::string& self) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    const auto names = parameter_names(overload);
    std::string result = name + "(";
    std::string separator;
    if (!self.empty()) { result += self; separator = ", "; }
    else if (overload.receives_self) { result += "self::" + jl_type(function.parameters[0]); separator = ", "; }
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        result += separator + names[i - first] + "::" + jl_type(function.parameters[i]);
        separator = ", ";
        if (i >= required) {
            const auto shown = jl_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            result += "=" + (shown.empty() ? std::string("…") : shown);
        }
    }
    result += ")";
    if (function.is_constructor) result += " -> " + classes_.at(overload.owner).name;
    else if (!is_void(function.result)) result += " -> " + jl_type(function.result);
    return result;
}

std::string Generator::jl_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "nothing";
    if (value.kind == "string") return value.value;
    if (value.kind == "char") return "\"" + value.value + "\"";
    if (value.kind == "enum") return ":" + value.value;
    if (value.kind == "default") {
        const auto& target = referent(type);
        if (bound(target)) return classes_.at(target.declaration).name + "()";
        const auto shown = jl_type(target);
        if (shown.rfind("Vector{", 0) == 0) return shown + "()";
        if (shown.rfind("Dict{", 0) == 0) return shown + "()";
        if (shown == "String") return "\"\"";
        if (shown == "Float64") return "0.0";
        if (shown == "Int" || shown == "Integer") return "0";
        if (shown == "Bool") return "false";
        if (shown.rfind("Union{", 0) == 0) return "nothing";
    }
    return {};
}

std::string Generator::dispatch_type(const std::string& owner) const {
    std::vector<std::string> extra;
    for (const auto& other : class_order_)
        if (other != owner && derives(other, owner) && !primary_derives(other, owner)) {
            // Only the topmost such class: its primary descendants are covered by its own type.
            bool covered = false;
            for (const auto& listed : extra) if (primary_derives(other, listed)) covered = true;
            if (!covered) extra.push_back(other);
        }
    if (extra.empty()) return classes_.at(owner).name;
    std::string result = "Union{" + classes_.at(owner).name;
    for (const auto& other : extra) result += ", " + classes_.at(other).name;
    return result + "}";
}


// The docstring of a function, method or constructor group: each overload's signature (indented,
// the Julia convention), the C++ documentation, then the arguments and result.
std::string Generator::group_doc(const Group& group, const std::string& shown, const std::string& self) const {
    std::string text;
    std::set<std::string> signatures;
    for (const auto& overload : group.overloads) {
        const auto line = signature(overload, shown, overload.extension && !overload.receives_self ? std::string() : self);
        if (signatures.insert(line).second) text += "    " + line + "\n";
    }
    std::string about;
    std::set<std::string> seen;
    for (const auto& overload : group.overloads) {
        const auto& documentation = overload.function->documentation;
        if (documentation.empty() || !seen.insert(plain_documentation(documentation)).second) continue;
        const auto part = description(documentation);
        if (!part.empty()) about += (about.empty() ? "" : "\n\n") + part;
    }
    if (about.empty()) about = "C++ `" + group.overloads.front().function->qualified_name() + "`.";
    text += "\n" + about;
    std::string arguments;
    std::set<std::string> listed;
    std::string returns;
    for (const auto& overload : group.overloads) {
        const auto& function = *overload.function;
        const auto names = parameter_names(overload);
        const std::size_t first = overload.receives_self ? 1 : 0;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            std::string about_parameter;
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            for (const auto& item : function.documentation.params) if (item.name == cpp) about_parameter = one_line(item.text);
            const auto line = "- `" + names[i - first] + "::" + jl_type(function.parameters[i]) + "`" + (about_parameter.empty() ? "" : ": " + about_parameter);
            if (listed.insert(line).second) arguments += "\n" + line;
        }
        if (!function.is_constructor && !is_void(function.result) && !function.documentation.returns.empty() && returns.empty())
            returns = "`" + jl_type(function.result) + "`: " + one_line(function.documentation.returns);
    }
    if (!arguments.empty()) text += "\n\n# Arguments" + arguments;
    if (!returns.empty()) text += "\n\n# Returns\n" + returns;
    return text;
}

std::string Generator::class_doc(const ClassPlan& plan) const {
    std::string text = "    " + plan.name + (plan.bases.empty() ? std::string(" <: NativeObject") : " <: " + classes_.at(plan.bases.front()).name) + "\n\n";
    auto about = description(plan.record->documentation);
    if (about.empty()) about = "C++ class `" + plan.record->qualified_name() + "`.";
    text += about;
    if (plan.bases.size() > 1) {
        text += "\n\nAlso derives (in C++) from ";
        for (std::size_t i = 1; i < plan.bases.size(); ++i) text += (i > 1 ? ", " : "") + std::string("`") + classes_.at(plan.bases[i]).name + "`";
        text += ": its methods accept this class.";
    }
    if (!plan.constructors.empty()) {
        text += "\n\n# Constructors";
        std::set<std::string> seen;
        for (const auto& overload : plan.constructors) {
            const auto line = signature(overload, plan.name, "");
            if (seen.insert(line).second) text += "\n- `" + line + "`";
        }
    } else text += plan.record->abstract ? "\n\nAbstract: create objects of a derived class" + std::string(plan.director ? ", or subclass it in Julia." : ".")
                                         : "\n\nNo public constructor.";
    if (plan.director) {
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
            const auto jname = renamed(method->qualified_name(), julia_name(method->name));
            if (!is_pure && !bound_names.count(jname)) continue;
            if (listed.insert(jname).second) (is_pure ? required : optional).emplace_back(jname, method);
        }
        if (!required.empty() || !optional.empty()) {
            const auto& example = !required.empty() ? required.front() : optional.front();
            std::string parameters;
            for (std::size_t i = 0; i < example.second->parameters.size(); ++i) {
                const auto cpp = i < example.second->parameter_names.size() ? example.second->parameter_names[i] : std::string();
                parameters += ", " + (cpp.empty() ? "arg" + std::to_string(i + 1) : julia_name(cpp));
            }
            const bool arguments = std::any_of(plan.constructors.begin(), plan.constructors.end(),
                                               [](const Overload& overload) { return !overload.function->parameters.empty(); });
            const auto owner_of = [&](const std::string& name) { return base_function(name) ? "Base." + name : package_ + "." + name; };
            text += "\n\n# Subclassing\nJulia subclasses override the C++ virtual methods, also when C++ calls them:\n\n"
                    "```julia\n@subclass mutable struct My" + plan.name + " <: " + plan.name + "\n    state::Int\nend\n" +
                    owner_of(example.first) + "(self::My" + plan.name + parameters + ") = ...\nobject = My" + plan.name + "(0" +
                    (arguments ? "; args = (constructor arguments...,)" : "") + ")\n```";
            auto names = [](const std::vector<std::pair<std::string, const Function*>>& methods) {
                std::string out;
                for (const auto& [name, method] : methods) out += (out.empty() ? "`" : ", `") + name + "`";
                return out;
            };
            if (!required.empty()) text += "\n\nMust override: " + names(required) + ".";
            if (!optional.empty()) text += "\n\nMay override (`supercall` runs the C++ one): " + names(optional) + ".";
        }
    }
    std::vector<const FieldPlan*> fields;
    for (const auto& field : plan.fields) fields.push_back(&field);
    if (!fields.empty()) {
        text += "\n\n# Fields (properties)";
        for (const auto* field : fields) {
            const auto about_field = one_line(field->field->documentation.brief);
            text += "\n- `" + field->name + "::" + jl_type(field->field->type) + "`" + (about_field.empty() ? "" : ": " + about_field);
        }
    }
    return text;
}

GeneratedJuliaFullSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = options_.package;
    if (package_.empty()) {
        package_ = name_;
        package_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(package_[0])));
    }
    if (!identifier(package_) || julia_keyword(package_)) throw BindingError("Julia module name must be an identifier: " + package_);
    GeneratedJuliaFullSource result;
    prepare();
    // Julia has one namespace for types, functions and constants of a module.
    std::map<std::string, std::string> taken;
    const auto reserve = [&](const std::string& jname, const std::string& what) {
        const auto inserted = taken.emplace(jname, what);
        if (!inserted.second) throw BindingError("Julia export name collision: " + jname + " (" + inserted.first->second + " and " + what + ")");
    };
    const auto available = [&](const std::string& jname) { return !taken.count(jname); };
    for (const char* runtime : {"NativeObject", "NativeError", "NativeFunction", "supercall", "subclass", "close", "eval", "include", "_Rosetta"})
        reserve(runtime, "Rosetta runtime");
    reserve(package_, "the module itself");
    {
        std::set<std::string> collided;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!available(plan.name) || !available(storage(plan))) {
                if (!flat_) throw BindingError("Julia export name collision: " + plan.name);
                reject(owner, "class name collides with another export");
                collided.insert(owner);
                continue;
            }
            reserve(plan.name, "class " + owner);
            reserve(storage(plan), "storage type of " + owner);
        }
        // Classes whose name collided are dropped (with everything that names them as a base).
        drop_classes(collided);
        for (auto it = taken.begin(); it != taken.end();) {
            const auto& what = it->second;
            const auto owner = what.rfind("class ", 0) == 0 ? what.substr(6) : what.rfind("storage type of ", 0) == 0 ? what.substr(16) : std::string();
            if (!owner.empty() && !classes_.count(owner)) it = taken.erase(it);
            else ++it;
        }
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto jname = renamed(qualified, julia_name(nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) jname = classes_.at(enumeration.owner).name + "_" + jname;
        if (!available(jname)) {
            if (!flat_) throw BindingError("Julia export name collision: " + jname);
            reject(qualified, "enum name collides with another export");
            continue;
        }
        reserve(jname, "enum " + qualified);
        enum_names_[qualified] = jname;
    }

    // Constructors, methods, operators, fields and class constants; module functions.
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::set<std::string> submodules;
    for (const auto& group : functions) if (!group.module.empty()) submodules.insert(group.module);
    for (const auto& submodule : submodules) {
        if (!identifier(submodule) || julia_keyword(submodule)) throw BindingError("Julia submodule name must be an identifier: " + submodule);
        if (!available(submodule)) throw BindingError("Julia submodule name collides with another export: " + submodule);
        reserve(submodule, "submodule " + submodule);
    }

    // Generic functions: the names of instance and static methods, and module functions.
    // name -> whether a free function (untyped methods) uses it.
    std::map<std::string, bool> generics;
    for (const auto& owner : class_order_)
        for (const auto& group : classes_.at(owner).methods) generics.emplace(group.name, false);
    for (const auto& group : functions) if (group.module.empty()) generics[group.name] = true;
    // Generic functions directors call (virtual methods Julia subclasses override).
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director) continue;
        std::map<std::string, bool> pure;
        for (const auto* method : virtuals(*plan.record, pure)) {
            if (!callback_supported(*method)) continue;
            std::string key = method->name + "(";
            for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
            key += method->is_const ? ")const" : ")";
            if (method->access == "private" && !pure[key]) continue;
            generics.emplace(renamed(method->qualified_name(), julia_name(method->name)), false);
        }
    }
    // A generic named like a Base function extends it when all its methods are typed on bound
    // classes; one with free functions stays a function of this module (not exported) and adds
    // forwarding Base methods for its typed methods.
    for (auto it = generics.begin(); it != generics.end();) {
        const auto& [name, untyped] = *it;
        if (base_function(name) && !untyped) { ++it; continue; } // Base.name
        if (!available(name)) {
            if (!flat_) throw BindingError("Julia export name collision: " + name + " (" + taken.at(name) + ")");
            reject(name, "function name collides with " + taken.at(name));
            it = generics.erase(it);
            continue;
        }
        reserve(name, "function " + name);
        ++it;
    }
    // Drop methods and functions whose generic was rejected.
    for (const auto& owner : class_order_) {
        auto& methods = classes_.at(owner).methods;
        methods.erase(std::remove_if(methods.begin(), methods.end(), [&](const Group& g) { return !generics.count(g.name); }), methods.end());
    }
    functions.erase(std::remove_if(functions.begin(), functions.end(), [&](const Group& g) { return g.module.empty() && !generics.count(g.name); }), functions.end());

    // Constant namespace-scope variables.
    std::vector<ConstantPlan> constants;
    for (auto& constant : plan_constants([&](const Variable& variable) {
             const auto qualified = variable.qualified_name();
             return renamed(qualified, julia_name(flat(variable.namespaces) ? variable.name : exported(qualified)));
         })) {
        if (!available(constant.name)) { reject(constant.expression.substr(2), "constant name collides with another export"); continue; }
        reserve(constant.name, "constant " + constant.expression.substr(2));
        constants.push_back(constant);
    }
    for (const auto& owner : class_order_)
        for (auto& constant : classes_.at(owner).constants) {
            if (!available(constant.name)) { reject(constant.expression, "constant name collides with another export"); constant.name.clear(); continue; }
            reserve(constant.name, "constant " + constant.expression);
        }
    for (const auto& owner : class_order_) {
        auto& list = classes_.at(owner).constants;
        list.erase(std::remove_if(list.begin(), list.end(), [](const ConstantPlan& c) { return c.name.empty(); }), list.end());
    }
    check_diagnostics();

    // Using-declarations re-export bound entities under another namespace's name.
    struct Alias { std::string name, target; };
    std::vector<Alias> aliases;
    {
        std::map<std::string, std::string> function_names;
        for (const auto& group : functions)
            if (group.module.empty())
                for (const auto& overload : group.overloads) function_names.emplace(overload.function->qualified_name(), group.name);
        for (const auto& reexport : module_.reexports) {
            std::string target;
            if (reexport.kind == "record" || reexport.kind == "type") { const auto c = classes_.find(reexport.target); if (c != classes_.end()) target = c->second.name; }
            if (reexport.kind == "enum" || reexport.kind == "type") { const auto e = enum_names_.find(reexport.target); if (e != enum_names_.end()) target = e->second; }
            else if (reexport.kind == "function") { const auto f = function_names.find(reexport.target); if (f != function_names.end()) target = f->second; }
            if (target.empty()) continue;
            std::string scope;
            for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
            const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
            const auto jname = renamed(qualified, julia_name(flat(reexport.namespaces) ? reexport.name : exported(qualified)));
            if (jname == target || !available(jname)) continue;
            reserve(jname, "alias of " + reexport.target);
            aliases.push_back({jname, target});
        }
    }

    emit_cpp(result.source, result.parts, functions, constants, "Julia module " + package_ + " (" + name_ + ", full API).");

    // --------------------------------------------------------------- Julia module
    const auto library = options_.library.empty() ? name_ : options_.library;
    std::string environment;
    for (char c : name_) environment += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    environment += "_JULIA_LIBRARY";
    std::ostringstream jl;
    std::vector<std::string> exports = {"NativeObject", "NativeError", "NativeFunction", "@subclass", "supercall"};
    const auto exporting = [&](const std::string& name) { if (!base_name(name)) exports.push_back(name); };
    for (const auto& owner : class_order_) exporting(classes_.at(owner).name);
    for (const auto& [qualified, jname] : enum_names_) exporting(jname);
    for (const auto& [name, untyped] : generics) if (!base_function(name)) exporting(name);
    for (const auto& constant : constants) exporting(constant.name);
    for (const auto& owner : class_order_) for (const auto& constant : classes_.at(owner).constants) exporting(constant.name);
    for (const auto& alias : aliases) exporting(alias.name);
    for (const auto& submodule : submodules) exporting(submodule);

    jl << "# Generated by Rosetta: Julia interface to the native module `" << name_ << "` (full API).\n"
       << "# `using " << package_ << "` loads " << library << ".so (.dylib, .dll) from this file's directory (or ../deps,\n"
       << "# ../lib), or the file " << environment << " names.\n\n"
       << jl_doc("    " + package_ + "\n\nNative C++ module `" + name_ + "`, bound by Rosetta. Classes are abstract types (subtypes of\n"
                 "`NativeObject`); their methods are functions taking the object first, fields are properties,\n"
                 "and C++ exceptions are raised as `NativeError`. Names that Base also exports are not exported:\n"
                 "methods on this module's classes extend the Base function, other functions are `" + package_ + ".name`.")
       << "module " << package_ << "\n";
    {
        std::string runtime = detail::julia_full_runtime_julia;
        const auto replace = [&](const std::string& from, const std::string& to) {
            for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
        };
        replace("@PKG@", package_);
        replace("@C@", "rosetta_" + name_);
        replace("@LIB@", library);
        replace("@ENV@", environment);
        jl << runtime;
    }
    jl << "\nusing ._Rosetta: NativeObject, NativeError, NativeFunction, @subclass, supercall\n\nexport ";
    for (std::size_t i = 0; i < exports.size(); ++i) jl << (i ? (i % 8 == 0 ? ",\n    " : ", ") : "") << exports[i];
    jl << "\n";

    jl << "\n# ------------------------------------------------------------------ enums\n";
    for (const auto& [qualified, jname] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        std::string values, members;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
            values += std::string(i ? ", " : "") + "`:" + label + "` (" + enumeration.enumerator_values[i] + ")";
            members += std::string(i ? ", " : "") + "Symbol(" + jl_string(label) + ")";
        }
        auto text = description(enumeration.documentation);
        if (text.empty()) text = "C++ enumeration `" + qualified + "`.";
        text = "    " + jname + "\n\n" + text + "\n\nValues: " + values + ". C++ returns these Symbols; arguments also accept the integer values.";
        jl << "\n" << jl_doc(text) << "const " << jname << " = (" << members << (enumeration.enumerators.size() == 1 ? ",)" : ")") << "\n";
    }

    jl << "\n# ------------------------------------------------------------------ classes\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const auto super = plan.bases.empty() ? std::string("NativeObject") : classes_.at(plan.bases.front()).name;
        jl << "\n" << jl_doc(class_doc(plan)) << "abstract type " << plan.name << " <: " << super << " end\n"
           << "mutable struct " << storage(plan) << " <: " << plan.name << "\n"
           << "    _rosetta_holder::UInt64\n    _rosetta_parent::Any\n    _rosetta_id::UInt64\n"
           << "    " << storage(plan) << "(::_Rosetta.Raw) = new(0, nothing, 0)\n"
           << "end\n";
    }
    jl << "\n_Rosetta.CLASSES[] = Any[";
    for (std::size_t i = 0; i < class_order_.size(); ++i) jl << (i ? ", " : "") << storage(classes_.at(class_order_[i]));
    jl << "]\n";

    // Methods of a generic: the function a definition names (Base.f, or this module's f).
    const auto target = [&](const std::string& name) { return base_function(name) && !generics.at(name) ? "Base." + name : name; };
    const auto forward_to_base = [&](const std::string& name) { return base_function(name) && generics.at(name); };

    jl << "\n# ------------------------------------------------------------------ constructors\n";
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        if (plan.constructors.empty()) continue;
        Group group{plan.name, plan.constructors, false, {}, 0, {}, 0};
        jl << "\n_Rosetta.register_constructor(" << plan.name << ", " << storage(plan) << ", " << plan.constructor_index << ", " << (plan.director ? "true" : "false") << ")\n"
           << jl_doc(group_doc(group, plan.name, ""));
        if (plan.constructors.size() == 1 && plan.constructors.front().function->default_arguments == 0) {
            const auto names = parameter_names(plan.constructors.front());
            std::string list;
            for (const auto& name : names) list += (list.empty() ? "" : ", ") + name;
            jl << plan.name << "(" << list << ") = _Rosetta.construct(" << plan.name << ", (" << list << (names.size() == 1 ? ",))" : "))") << "\n";
        } else jl << plan.name << "(args...) = _Rosetta.construct(" << plan.name << ", args)\n";
    }

    jl << "\n# ------------------------------------------------------------------ functions\n";
    for (const auto& [name, untyped] : generics) {
        if (base_function(name) && !untyped) continue;
        std::vector<std::string> owners;
        for (const auto& owner : class_order_)
            for (const auto& group : classes_.at(owner).methods)
                if (group.name == name) owners.push_back("`" + classes_.at(owner).name + "`" + (group.is_static ? " (static)" : ""));
        // The generic function's own docstring, only where the methods' docstrings don't say it:
        // a generic only Julia subclasses implement, or a name Base also has.
        std::string about = "    " + name + "\n\n";
        if (owners.empty() && !untyped) about += "Generic function; Julia subclasses of native classes define its methods.";
        if (untyped && base_function(name)) about += std::string("Not exported (Base has `") + name + "`): call `" + package_ + "." + name + "`.";
        if (!owners.empty() && untyped && base_function(name)) {
            about += " Methods for ";
            for (std::size_t i = 0; i < owners.size(); ++i) about += (i ? ", " : "") + owners[i];
            about += ".";
        }
        if (about.size() > name.size() + 6) jl << "\n" << jl_doc(about);
        jl << "function " << name << " end\n";
    }
    std::vector<std::string> generic_functions(generic_order_.size());
    for (std::size_t i = 0; i < generic_order_.size(); ++i) generic_functions[i] = target(generic_order_[i]);

    // Instance, static and operator methods of each class.
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const auto dispatch = dispatch_type(owner);
        for (const auto& group : plan.methods) {
            const auto function = target(group.name);
            if (group.is_static) {
                jl << "\n" << jl_doc(group_doc(group, group.name, "::Type{" + plan.name + "}") + "\n\n(Static method of `" + plan.name + "`.)")
                   << function << "(::Type{<:" << plan.name << "}, args...) = _Rosetta.call(" << group.index << ", args)\n";
                if (forward_to_base(group.name)) jl << "Base." << group.name << "(T::Type{<:" << plan.name << "}, args...) = " << group.name << "(T, args...)\n";
                continue;
            }
            jl << "\n" << jl_doc(group_doc(group, group.name, "self::" + plan.name))
               << function << "(self::" << dispatch << ", args...) = _Rosetta.call(" << group.index << ", (self, args...))\n";
            if (forward_to_base(group.name)) jl << "Base." << group.name << "(self::" << dispatch << ", args...) = " << group.name << "(self, args...)\n";
        }
        for (const auto& group : plan.operators) {
            std::set<std::size_t> arities;
            for (const auto& overload : group.overloads) {
                const auto& f = *overload.function;
                const std::size_t base = overload.extension ? 0 : 1;
                for (std::size_t n = f.parameters.size() - f.default_arguments; n <= f.parameters.size(); ++n) arities.insert(n + base);
            }
            const auto doc = jl_doc(group_doc(group, group.name == "(call)" ? "(self::" + plan.name + ")" : group.name, "self::" + plan.name));
            for (const auto arity : arities) {
                std::string parameters, arguments;
                for (std::size_t i = 0; i < arity; ++i) {
                    const auto name = i == group.operand ? std::string("self") : "x" + std::to_string(i + 1);
                    parameters += (i ? ", " : "") + name + (i == group.operand ? "::" + dispatch : std::string());
                    arguments += (i ? ", " : "") + name;
                }
                const auto tuple = "(" + arguments + (arity == 1 ? ",)" : ")");
                if (group.name == "(call)") {
                    std::string rest;
                    for (std::size_t i = 1; i < arity; ++i) rest += (i > 1 ? ", " : "") + std::string("x") + std::to_string(i + 1);
                    jl << "\n" << doc << "(self::" << dispatch << ")(" << rest << ") = _Rosetta.call(" << group.index << ", " << tuple << ")\n";
                } else jl << "\n" << doc << group.name << "(" << parameters << ") = _Rosetta.call(" << group.index << ", " << tuple << ")\n";
            }
            // (Class, Class) is ambiguous between a method on the first and a free operator on the
            // second operand: define it once, with the group that has the member overloads.
            if (group.operand == 1 && arities.count(2)) {
                for (const auto& other : plan.operators)
                    if (other.name == group.name && other.operand == 0)
                        jl << group.name << "(x1::" << dispatch << ", self::" << dispatch << ") = _Rosetta.call(" << other.index << ", (x1, self))\n";
            }
        }
    }
    // Fields: properties, with the fields of base classes.
    for (const auto& owner : class_order_) {
        std::vector<std::pair<std::string, const FieldPlan*>> all;
        std::set<std::string> names;
        const std::function<void(const std::string&, unsigned)> collect = [&](const std::string& cls, unsigned depth) {
            if (depth > 64) return;
            for (const auto& field : classes_.at(cls).fields) if (names.insert(field.name).second) all.emplace_back(cls, &field);
            for (const auto& base : classes_.at(cls).bases) collect(base, depth + 1);
        };
        collect(owner, 0);
        if (all.empty()) continue;
        const auto& plan = classes_.at(owner);
        const auto table = "_FIELDS_" + plan.name;
        const auto dispatch = dispatch_type(owner);
        jl << "\nconst " << table << " = Dict{Symbol,Tuple{Int,Int}}(";
        for (std::size_t i = 0; i < all.size(); ++i)
            jl << (i ? ", " : "") << "Symbol(" << jl_string(all[i].second->name) << ") => (" << all[i].second->getter_index << ", " << all[i].second->setter_index << ")";
        jl << ")\n"
           << "Base.getproperty(self::" << dispatch << ", name::Symbol) = _Rosetta.get_property(self, name, " << table << ")\n"
           << "Base.setproperty!(self::" << dispatch << ", name::Symbol, value) = _Rosetta.set_property!(self, name, value, " << table << ")\n"
           << "Base.propertynames(self::" << dispatch << ", private::Bool=false) = _Rosetta.property_names(self, " << table << ")\n";
    }
    // show uses to_string() when the class has one callable without arguments.
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const bool printable = std::any_of(plan.methods.begin(), plan.methods.end(), [](const Group& group) {
            return (group.name == "to_string" || group.name == "str") && !group.is_static && std::any_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) {
                return o.function->parameters.size() == o.function->default_arguments + (o.receives_self ? 1 : 0);
            });
        });
        if (!printable) continue;
        const auto printer = std::find_if(plan.methods.begin(), plan.methods.end(), [](const Group& g) { return g.name == "to_string" || g.name == "str"; });
        jl << "Base.show(io::IO, self::" << dispatch_type(owner) << ") = _Rosetta.show_with(io, self, " << target(printer->name) << ")\n";
    }

    jl << "\n# ------------------------------------------------------------------ module functions\n";
    const auto emit_function = [&](const Group& group, const std::string& prefix) {
        const auto name = group.module.empty() ? target(group.name) : group.name;
        jl << "\n" << jl_doc(group_doc(group, group.name, ""));
        const auto& overload = group.overloads.front();
        const bool shared = generics.count(group.name) && std::any_of(class_order_.begin(), class_order_.end(), [&](const std::string& owner) {
            const auto& methods = classes_.at(owner).methods;
            return std::any_of(methods.begin(), methods.end(), [&](const Group& g) { return g.name == group.name; });
        });
        if (group.overloads.size() == 1 && !shared && group.module.empty()) {
            // One method per arity, with the C++ parameter names (for signature help).
            const auto names = parameter_names(overload);
            const std::size_t required = overload.function->parameters.size() - overload.function->default_arguments;
            for (std::size_t arity = required; arity <= names.size(); ++arity) {
                std::string list;
                for (std::size_t i = 0; i < arity; ++i) list += (i ? ", " : "") + names[i];
                jl << name << "(" << list << ") = " << prefix << "call(" << group.index << ", (" << list << (arity == 1 ? ",))" : "))") << "\n";
            }
        } else jl << name << "(args...) = " << prefix << "call(" << group.index << ", args)\n";
    };
    for (const auto& group : functions) if (group.module.empty()) emit_function(group, "_Rosetta.");
    for (const auto& submodule : submodules) {
        jl << "\n" << jl_doc("    " + package_ + "." + submodule + "\n\nFunctions of the C++ namespace mapped to submodule `" + submodule + "` of `" + name_ + "`.")
           << "module " << submodule << "\nimport .._Rosetta\n";
        std::set<std::string> names;
        for (const auto& group : functions) if (group.module == submodule) names.insert(group.name);
        jl << "export ";
        std::size_t i = 0;
        for (const auto& name : names) jl << (i++ ? ", " : "") << name;
        jl << "\n";
        for (const auto& name : names) jl << "function " << name << " end\n";
        for (const auto& group : functions) if (group.module == submodule) emit_function(group, "_Rosetta.");
        jl << "end # module " << submodule << "\n";
    }

    jl << "\n# ------------------------------------------------------------------ constants and aliases\n";
    const auto emit_constant = [&](const ConstantPlan& constant) {
        auto text = constant.documentation ? description(*constant.documentation) : std::string();
        if (text.empty()) text = "C++ constant `" + normalize(constant.expression) + "`.";
        jl << "\n" << jl_doc("    " + constant.name + "::" + jl_type(*constant.type) + "\n\n" + text) << constant.name << " = nothing\n";
    };
    for (const auto& owner : class_order_) for (const auto& constant : classes_.at(owner).constants) emit_constant(constant);
    for (const auto& constant : constants) emit_constant(constant);
    for (const auto& alias : aliases) jl << "const " << alias.name << " = " << alias.target << "\n";

    jl << "\nfunction __init__()\n    _Rosetta.open_module()\n"
       << "    _Rosetta.GENERICS[] = Any[";
    for (std::size_t i = 0; i < generic_functions.size(); ++i) jl << (i ? ", " : "") << generic_functions[i];
    jl << "]\n";
    for (const auto& owner : class_order_)
        for (const auto& constant : classes_.at(owner).constants)
            jl << "    global " << constant.name << " = _Rosetta.call(" << constant.index << ", ())\n";
    for (const auto& constant : constants) jl << "    global " << constant.name << " = _Rosetta.call(" << constant.index << ", ())\n";
    jl << "    nothing\nend\n\nend # module " << package_ << "\n";
    result.interface = jl.str();
    result.project = "name = " + jl_string(package_) + "\n";
    {
        // A stable UUID (version 5 shape) from the package name, so regeneration keeps it.
        unsigned long long hash1 = 1469598103934665603ULL, hash2 = 1099511628211ULL;
        for (char c : "rosetta-julia:" + package_) { hash1 = (hash1 ^ static_cast<unsigned char>(c)) * 1099511628211ULL; hash2 = (hash2 + static_cast<unsigned char>(c)) * 6364136223846793005ULL; }
        char uuid[64];
        std::snprintf(uuid, sizeof uuid, "%08llx-%04llx-5%03llx-%04llx-%012llx", hash1 >> 32, (hash1 >> 16) & 0xffff, hash1 & 0xfff,
                      0x8000 | ((hash2 >> 48) & 0x3fff), hash2 & 0xffffffffffffULL);
        result.project += std::string("uuid = \"") + uuid + "\"\nversion = \"0.1.0\"\n\n[deps]\nLibdl = \"8f399da3-3557-5675-b5ff-fb832c97cbdb\"\n";
    }
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedJuliaFullSource generate_julia_full(const Module& module, const std::string& name, bool skip_unsupported,
                                             const BindingPolicy& policy, const JuliaFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Julia source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
