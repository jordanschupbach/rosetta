// Full C# backend (--csharp-api full): native classes as C# classes deriving from NativeObject
// (IDisposable), C++ virtual methods as virtual methods C# subclasses override (directors), methods
// and properties in PascalCase, overloads typed for C# and resolved again at run time in C++,
// operators and indexers, containers as List/Dictionary/HashSet/tuples, enums, callbacks as
// Func/Action delegates, and XML documentation comments carrying the C++ documentation.
//
// It writes a C++ source (a shared library with one entry point, rosetta_<module>_call), a C#
// source that loads it with NativeLibrary, and a .csproj. The C++ library is the one every full
// C ABI backend shares (cabi_full.hpp, lisp_full_runtime.hpp); csharp_full_runtime_cs.hpp is the
// C# side.
#include "rosetta/csharp.hpp"
#include "cabi_full.hpp"
#include "csharp_full_runtime_cs.hpp"
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

bool csharp_keyword(const std::string& name) {
    static const std::set<std::string> keywords = {"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char",
        "checked", "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event",
        "explicit", "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int",
        "interface", "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override",
        "params", "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof",
        "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong",
        "unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while"};
    return keywords.count(name) != 0;
}
std::string escaped(const std::string& name) { return csharp_keyword(name) ? "@" + name : name; }
// snake_case, camelCase or PascalCase -> PascalCase.
std::string pascal(const std::string& name) {
    std::string result;
    bool upper = true;
    for (const char c : name) {
        if (c == '_') { upper = true; continue; }
        result += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    if (result.empty()) return "Unnamed";
    if (std::isdigit(static_cast<unsigned char>(result[0]))) result = "_" + result;
    return result;
}
std::string camel(const std::string& name) {
    auto result = pascal(name);
    if (result[0] != '_') result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
    return escaped(result);
}
std::string cs_string(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else result += c;
    }
    return result + '"';
}

// --- documentation (XML doc comments) ---------------------------------------------

std::string xml(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else result += c;
    }
    return result;
}
// Escaped text with `code` spans as <c>code</c>.
std::string inline_xml(const std::string& text) {
    std::string result;
    bool code = false;
    for (char c : text) {
        if (c == '`') { result += code ? "</c>" : "<c>"; code = !code; continue; }
        result += xml(std::string(1, c));
    }
    if (code) result += "</c>";
    return result;
}
std::string doc_block(const DocBlock& block) {
    if (block.kind == "code" || block.kind == "math") return "<code>\n" + xml(block.text) + "\n</code>";
    if (block.kind == "heading") return "<para><b>" + inline_xml(block.text) + "</b></para>";
    return "<para>" + inline_xml(block.text) + "</para>";
}
// /// lines for an XML fragment.
std::string comment(const std::string& fragment, const std::string& indent) {
    std::string out;
    std::istringstream lines(fragment);
    for (std::string line; std::getline(lines, line);) out += indent + "///" + (line.empty() ? "" : " " + line) + "\n";
    return out;
}
struct Doc {
    std::string summary;
    std::vector<std::string> remarks;
    std::vector<std::pair<std::string, std::string>> params;  // name, text
    std::string returns;
    std::vector<std::string> exceptions;
    std::string render(const std::string& indent) const {
        std::string out = "<summary>\n" + summary + "\n</summary>\n";
        if (!remarks.empty()) {
            out += "<remarks>\n";
            for (const auto& remark : remarks) out += remark + "\n";
            out += "</remarks>\n";
        }
        for (const auto& [name, text] : params) out += "<param name=\"" + (name[0] == '@' ? name.substr(1) : name) + "\">" + text + "</param>\n";
        if (!returns.empty()) out += "<returns>" + returns + "</returns>\n";
        for (const auto& text : exceptions) out += "<exception cref=\"NativeException\">" + text + "</exception>\n";
        return comment(out, indent);
    }
};
// Summary, remarks and exceptions of a declaration.
Doc describe(const Documentation& doc, const std::string& fallback) {
    Doc result;
    std::vector<std::string> details;
    for (const auto& block : doc.details) details.push_back(doc_block(block));
    if (!doc.brief.empty()) result.summary = inline_xml(doc.brief);
    else if (!details.empty() && doc.details.front().kind == "paragraph") { result.summary = inline_xml(doc.details.front().text); details.erase(details.begin()); }
    else result.summary = fallback;
    result.remarks = details;
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        result.remarks.push_back("<para><b>" + xml(label) + ":</b> " + inline_xml(one_line(item.text)) + "</para>");
    }
    if (!doc.see.empty()) {
        std::string see = "<para>See also: ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + std::string("<c>") + xml(doc.see[i]) + "</c>";
        result.remarks.push_back(see + ".</para>");
    }
    for (const auto& item : doc.throws)
        result.exceptions.push_back((item.name.empty() ? std::string() : "C++ <c>" + xml(item.name) + "</c>" + (item.text.empty() ? "" : ": ")) + inline_xml(one_line(item.text)));
    return result;
}

// C++ operators and their C# spelling ("" = not bound). Indexers are "this[]", the call operator "Invoke".
std::string cs_operator(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"+", "+"}}, {"operator-", {"-", "-"}}, {"operator*", {nullptr, "*"}}, {"operator/", {nullptr, "/"}},
        {"operator%", {nullptr, "%"}}, {"operator==", {nullptr, "=="}}, {"operator!=", {nullptr, "!="}}, {"operator<", {nullptr, "<"}},
        {"operator<=", {nullptr, "<="}}, {"operator>", {nullptr, ">"}}, {"operator>=", {nullptr, ">="}}, {"operator&", {nullptr, "&"}},
        {"operator|", {nullptr, "|"}}, {"operator^", {nullptr, "^"}}, {"operator~", {"~", nullptr}}, {"operator!", {"!", nullptr}},
        {"operator[]", {nullptr, "this[]"}}};
    if (name == "operator()") return "Invoke";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
// C# requires comparison operators in pairs.
std::string partner(const std::string& op) {
    if (op == "==") return "!=";
    if (op == "!=") return "==";
    if (op == "<") return ">";
    if (op == ">") return "<";
    if (op == "<=") return ">=";
    if (op == ">=") return "<=";
    return {};
}
bool is_symbol_operator(const std::string& name) { return !name.empty() && !std::isalpha(static_cast<unsigned char>(name[0])) && name != "this[]"; }

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const CSharpFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedCSharpFullSource run();

private:
    std::string namespace_;
    CSharpFullOptions options_;
    std::ostringstream cs_;

    std::string language() const override { return "C#"; }
    std::string target_name(const std::string& cpp) const override { return escaped(cpp); }
    std::string method_name(const std::string& cpp) const override {
        auto name = pascal(cpp);
        static const std::set<std::string> reserved = {"GetType", "GetHashCode", "Finalize", "MemberwiseClone", "Dispose", "IsDisposed", "ReferenceEquals"};
        return reserved.count(name) ? name + "_" : name;
    }
    std::string field_name(const std::string& cpp) const override { return method_name(cpp); }
    std::string type_name(const std::string& cpp) const override { return escaped(cpp.find("__") == std::string::npos ? cpp : pascal(cpp)); }
    // Members are PascalCase, so their renames are too; enum labels keep the spelling given.
    std::string rename_target(const std::string& name, const std::string& fallback) const override {
        return !fallback.empty() && std::isupper(static_cast<unsigned char>(fallback[0])) ? method_name(name) : escaped(name);
    }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return cs_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return method_name(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken{"self"};
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
        for (const auto& overload : group.overloads) text += "    " + signature(overload, group.name, overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    new " + signature(overload, plan.name, overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override { return plan.name + " is abstract: derive a C# class from it"; }
    std::string protected_message(const ClassPlan& plan) const override { return plan.name + " constructor is protected: call it from a C# subclass"; }
    std::string symbol_prefix() const override { return "rosetta_cs_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_CSHARP"; }
    std::string module_prefix() const override { return "rosetta_csharp_module_"; }

    // The C# type of a C++ type: parameter types accept any sequence (IEnumerable<T>) or
    // dictionary; results are concrete (List<T>, Dictionary<K, V>, HashSet<T>).
    std::string cs_type(const Type& type, bool parameter) const;
    // The C# type of a policy conversion's target, spelled as C++ text ("std::vector<double>").
    std::string cs_spelled(std::string cpp, bool parameter) const;
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    std::string parameter_type(const Type& type) const { return writeback(type) ? "ref " + cs_type(type, false) : cs_type(type, true); }
    std::string result_type(const Function& function) const { return is_void(function.result) ? "void" : cs_type(function.result, false); }
    // "Ret Name(T a, U b)" with the first `arity` parameters.
    std::string signature(const Overload& overload, const std::string& name, std::size_t arity) const;
    std::string cs_default(const DefaultValue& value, const Type& type) const;
    Doc overload_doc(const Overload& overload, std::size_t arity) const;
    bool value_type(const std::string& cs) const;
    // A .NET type's name, qualified with global:: when a generated type of the namespace hides it.
    std::string bcl(const std::string& name, const std::string& space) const { return types_.count(name) ? "global::" + space + "." + name : name; }
    std::set<std::string> types_;  // the generated type names

    struct Member { std::string text; bool is_virtual; std::string result; };
    std::map<std::string, std::map<std::string, Member>> members_;  // class -> signature key -> member
    const Member* inherited(const std::string& owner, const std::string& key) const;
    void emit_class(const std::string& owner);
    void emit_overloads(const ClassPlan* plan, const Group& group, bool is_method, const std::string& indent);
};

bool Generator::value_type(const std::string& cs) const {
    static const std::set<std::string> values = {"bool", "char", "sbyte", "byte", "short", "ushort", "int", "uint", "long", "ulong",
        "float", "double"};
    if (values.count(cs) || cs == bcl("Complex", "System.Numerics") || cs.rfind("(", 0) == 0 || cs.rfind(bcl("ValueTuple", "System"), 0) == 0) return true;
    for (const auto& [qualified, name] : enum_names_) if (name == cs) return true;
    return false;
}

std::string Generator::cs_type(const Type& type, bool parameter) const {
    const auto& target = referent(type);
    const auto nullable = [&](const std::string& text) { return text.empty() || text.back() == '?' ? text : text + "?"; };
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return parameter ? "string" : "string?";
        return nullable(cs_type(pointee, parameter));
    }
    if (is_void(target)) return "void";
    if (!conversion(target).empty()) return cs_spelled(policy_.conversions.at(conversion(target)), parameter);
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        static const std::map<std::string, std::string> builtins = {{"bool", "bool"}, {"char", "char"}, {"signed char", "sbyte"},
            {"unsigned char", "byte"}, {"short", "short"}, {"unsigned short", "ushort"}, {"int", "int"}, {"unsigned int", "uint"},
            {"long", "long"}, {"unsigned long", "ulong"}, {"long long", "long"}, {"unsigned long long", "ulong"}, {"float", "float"},
            {"double", "double"}, {"long double", "double"}};
        const auto found = builtins.find(bare);
        return found != builtins.end() ? found->second : "object?";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? found->second : "long";
    }
    if (bound(target)) return classes_.at(target.declaration).name;
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "object?";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i, bool as_parameter) { return i < target.arguments.size() ? cs_type(target.arguments[i], as_parameter) : std::string("object?"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "string";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray")
            return parameter ? bcl("IEnumerable", "System.Collections.Generic") + "<" + arg(0, true) + ">"
                             : bcl("List", "System.Collections.Generic") + "<" + arg(0, false) + ">";
        if (declaration == "std::set" || declaration == "std::unordered_set")
            return parameter ? bcl("IEnumerable", "System.Collections.Generic") + "<" + arg(0, true) + ">"
                             : bcl("HashSet", "System.Collections.Generic") + "<" + arg(0, false) + ">";
        if (declaration == "std::map" || declaration == "std::unordered_map")
            return bcl(parameter ? "IReadOnlyDictionary" : "Dictionary", "System.Collections.Generic") + "<" + arg(0, false) + ", " + arg(1, false) + ">";
        if (declaration == "std::optional") return nullable(arg(0, parameter));
        if (declaration == "std::complex") return bcl("Complex", "System.Numerics");
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return nullable(arg(0, parameter));
        if (declaration == "std::pair" || declaration == "std::tuple") {
            const auto count = target.arguments.size();
            if (count == 0) return bcl("ValueTuple", "System");
            if (count > 7) return "object?";
            std::string items;
            for (std::size_t i = 0; i < count; ++i) items += (i ? ", " : "") + cs_type(target.arguments[i], parameter);
            return count == 1 ? bcl("ValueTuple", "System") + "<" + items + ">" : "(" + items + ")";
        }
        if (declaration == "std::variant") return "object?";
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string items;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) items += (i > 1 ? ", " : "") + cs_type(function.arguments[i], false);
            const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
            if (returns) return bcl("Func", "System") + "<" + items + (items.empty() ? "" : ", ") + cs_type(function.arguments[0], true) + ">";
            return bcl("Action", "System") + (items.empty() ? "" : "<" + items + ">");
        }
        if (declaration == "std::chrono::duration") return "double";
    }
    return "object?";
}

std::string Generator::cs_spelled(std::string cpp, bool parameter) const {
    const auto trim = [](std::string text) {
        const auto first = text.find_first_not_of(" \t");
        const auto last = text.find_last_not_of(" \t");
        return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
    };
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (cpp.rfind("const ", 0) == 0) cpp = trim(cpp.substr(6));
    if (cpp.rfind("::", 0) == 0) cpp = cpp.substr(2);
    static const std::map<std::string, std::string> scalars = {{"bool", "bool"}, {"char", "char"}, {"signed char", "sbyte"},
        {"unsigned char", "byte"}, {"short", "short"}, {"unsigned short", "ushort"}, {"int", "int"}, {"unsigned int", "uint"},
        {"long", "long"}, {"unsigned long", "ulong"}, {"long long", "long"}, {"unsigned long long", "ulong"}, {"float", "float"},
        {"double", "double"}, {"long double", "double"}, {"std::size_t", "ulong"}, {"size_t", "ulong"}, {"std::int64_t", "long"},
        {"std::uint64_t", "ulong"}, {"std::int32_t", "int"}, {"std::uint32_t", "uint"}, {"std::string", "string"},
        {"std::string_view", "string"}, {"std::filesystem::path", "string"}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return found->second;
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') return "object?";
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
    const auto arg = [&](std::size_t i, bool as_parameter) { return i < arguments.size() ? cs_spelled(arguments[i], as_parameter) : std::string("object?"); };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::valarray")
        return parameter ? bcl("IEnumerable", "System.Collections.Generic") + "<" + arg(0, true) + ">"
                         : bcl("List", "System.Collections.Generic") + "<" + arg(0, false) + ">";
    if (name == "std::set" || name == "std::unordered_set")
        return parameter ? bcl("IEnumerable", "System.Collections.Generic") + "<" + arg(0, true) + ">"
                         : bcl("HashSet", "System.Collections.Generic") + "<" + arg(0, false) + ">";
    if (name == "std::map" || name == "std::unordered_map")
        return bcl(parameter ? "IReadOnlyDictionary" : "Dictionary", "System.Collections.Generic") + "<" + arg(0, false) + ", " + arg(1, false) + ">";
    if (name == "std::optional") {
        const auto inner = arg(0, parameter);
        return inner.back() == '?' ? inner : inner + "?";
    }
    if (name == "std::complex") return bcl("Complex", "System.Numerics");
    if ((name == "std::pair" || name == "std::tuple") && arguments.size() >= 2 && arguments.size() <= 7) {
        std::string items;
        for (std::size_t i = 0; i < arguments.size(); ++i) items += (i ? ", " : "") + arg(i, parameter);
        return "(" + items + ")";
    }
    return "object?";
}

std::string Generator::signature(const Overload& overload, const std::string& name, std::size_t arity) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string result = (function.is_constructor ? std::string() : result_type(function) + " ") + name + "(";
    for (std::size_t i = first; i < arity && i < function.parameters.size(); ++i)
        result += (i > first ? ", " : "") + parameter_type(function.parameters[i]) + " " + names[i - first];
    return result + ")";
}

std::string Generator::cs_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant" || value.kind == "string") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "null";
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "enum") {
        const auto& target = referent(type);
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? found->second + "." + value.value : value.value;
    }
    if (value.kind == "default") return "default";
    return {};
}

// The documentation of one C# overload: the C++ function's, with its first `arity` parameters.
Doc Generator::overload_doc(const Overload& overload, std::size_t arity) const {
    const auto& function = *overload.function;
    auto doc = describe(function.documentation, "C++ <c>" + xml(function.qualified_name()) + "</c>.");
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string omitted;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : function.documentation.params) if (item.name == cpp) text = inline_xml(one_line(item.text));
        if (i < arity) { doc.params.emplace_back(names[i - first], text); continue; }
        const auto shown = cs_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        omitted += (omitted.empty() ? "" : ", ") + std::string("<c>") + xml(names[i - first]) + (shown.empty() ? "" : " = " + xml(shown)) + "</c>";
    }
    if (!omitted.empty()) doc.remarks.push_back("<para>Uses the C++ default for " + omitted + ".</para>");
    if (!function.is_constructor && !is_void(function.result) && !function.documentation.returns.empty())
        doc.returns = inline_xml(one_line(function.documentation.returns));
    return doc;
}

const Generator::Member* Generator::inherited(const std::string& owner, const std::string& key) const {
    for (auto cls = owner; !classes_.at(cls).bases.empty();) {
        cls = classes_.at(cls).bases.front();
        const auto found = members_.find(cls);
        if (found == members_.end()) continue;
        const auto member = found->second.find(key);
        if (member != found->second.end()) return &member->second;
    }
    return nullptr;
}

// The C# overloads of a group: one per C++ overload and arity (default arguments), duplicates
// of an earlier C# signature dropped (C++ resolves the call at run time anyway).
void Generator::emit_overloads(const ClassPlan* plan, const Group& group, bool is_method, const std::string& indent) {
    const auto owner = plan ? plan->record->qualified_name() : std::string();
    auto& members = members_[owner];
    std::set<std::string> seen;
    for (const auto& overload : group.overloads) {
        const auto& function = *overload.function;
        const bool instance = plan && is_method && (!overload.extension || overload.receives_self);
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        const auto names = parameter_names(overload);
        const auto result = result_type(function);
        for (std::size_t arity = std::max(required, first); arity <= function.parameters.size(); ++arity) {
            std::string parameters, types, arguments = instance ? "this" : "", slots, writebacks;
            for (std::size_t i = first; i < arity; ++i) {
                const auto& type = function.parameters[i];
                const auto name = names[i - first];
                parameters += (i > first ? ", " : "") + parameter_type(type) + " " + name;
                types += parameter_type(type) + ",";
                if (writeback(type)) {
                    const auto slot = "_slot" + std::to_string(i - first);
                    slots += indent + "    var " + slot + " = new Rosetta.Slot(" + name + ");\n";
                    writebacks += indent + "    " + name + " = Rosetta.To<" + cs_type(type, false) + ">(" + slot + ".Value);\n";
                    arguments += (arguments.empty() ? "" : ", ") + slot;
                } else arguments += (arguments.empty() ? "" : ", ") + name;
            }
            std::string name = group.name;
            const bool op = is_symbol_operator(name);
            const auto key = (op ? "operator" : "") + name + "(" + types + ")";
            if (!seen.insert(key).second || members.count(key)) continue;
            auto doc = overload_doc(overload, arity);
            const auto call = "Rosetta.Call(" + std::to_string(group.index) + (arguments.empty() ? "" : ", " + arguments) + ")";
            const auto returned = result == "void" ? call : "Rosetta.To<" + result + ">(" + call + ")";
            std::string head;
            bool is_virtual = false;
            if (op) {
                // Operators: static, with this class at the bound operand.
                std::string operands;
                std::vector<std::string> operand_names;
                const std::size_t count = function.parameters.size() + (overload.extension ? 0 : 1);
                for (std::size_t i = 0; i < count; ++i) {
                    const bool self = i == group.operand && !overload.extension;
                    auto type = self ? plan->name : parameter_type(function.parameters[overload.extension ? i : i - 1]);
                    if (type.rfind("ref ", 0) == 0) type = type.substr(4);
                    // == and != take null (and compare it by reference).
                    if ((name == "==" || name == "!=") && !value_type(type) && type.back() != '?') type += "?";
                    const auto operand = (overload.extension ? i == group.operand : self) ? std::string("self") : "x" + std::to_string(i + 1);
                    operands += (i ? ", " : "") + type + " " + operand;
                    operand_names.push_back(operand);
                }
                std::string list;
                for (const auto& operand : operand_names) list += (list.empty() ? "" : ", ") + operand;
                const auto native = "Rosetta.Call(" + std::to_string(group.index) + ", " + list + ")";
                cs_ << "\n" << Doc{doc.summary, doc.remarks, {}, doc.returns, doc.exceptions}.render(indent);
                std::string guard;
                if ((name == "==" || name == "!=") && result == "bool" && operand_names.size() == 2) {
                    bool references = true;
                    for (std::size_t i = 0; i < count; ++i) {
                        const bool self = i == group.operand && !overload.extension;
                        const auto type = self ? plan->name : parameter_type(function.parameters[overload.extension ? i : i - 1]);
                        if (value_type(type)) references = false;
                    }
                    if (references)
                        guard = "(object?)" + operand_names[0] + " is null || (object?)" + operand_names[1] + " is null ? " +
                                std::string(name == "==" ? "" : "!") + "ReferenceEquals(" + operand_names[0] + ", " + operand_names[1] + ") : ";
                }
                cs_ << indent << "public static " << result << " operator " << name << "(" << operands << ") => " << guard
                    << "Rosetta.To<" << result << ">(" << native << ");\n";
                members[key] = {name, false, result};
                // The required partner of a comparison operator, from this one.
                const auto other = partner(name);
                if (!other.empty() && operand_names.size() == 2) {
                    const auto partner_key = "operator" + other + "(" + types + ")";
                    bool declared = false;
                    for (const auto& candidate : plan->operators) if (candidate.name == other) declared = true;
                    if (!declared && !members.count(partner_key) && seen.insert(partner_key).second) {
                        std::string body;
                        if (name == "==" || name == "!=") body = "!(" + operand_names[0] + " " + name + " " + operand_names[1] + ")";
                        else body = operand_names[1] + " " + name + " " + operand_names[0];
                        std::string swapped = operands;
                        if (name != "==" && name != "!=") {
                            // a > b is b < a: same operand types, swapped.
                            const auto comma = operands.find(", ");
                            const auto first_operand = operands.substr(0, comma), second_operand = operands.substr(comma + 2);
                            const auto type_of = [](const std::string& text) { return text.substr(0, text.rfind(' ')); };
                            if (type_of(first_operand) != type_of(second_operand)) continue;
                        }
                        cs_ << indent << "/// <summary>The opposite of <c>" << xml(name) << "</c>.</summary>\n"
                            << indent << "public static " << result << " operator " << other << "(" << swapped << ") => " << body << ";\n";
                        members[partner_key] = {other, false, result};
                    }
                }
                continue;
            }
            if (name == "this[]") {
                cs_ << "\n" << doc.render(indent) << indent << "public " << result << " this[" << parameters << "] => " << returned << ";\n";
                members[key] = {name, false, result};
                continue;
            }
            const bool to_string = name == "ToString" && arity == first && result == "string" && instance;
            const auto* base = plan ? inherited(owner, key) : nullptr;
            std::string modifiers = "public ";
            if (!instance) modifiers += "static ";
            if (to_string) modifiers += "override ";
            else if (instance && function.is_virtual) {
                is_virtual = true;
                const auto class_named = [&](const std::string& shown) {
                    const auto bare = shown.empty() || shown.back() != '?' ? shown : shown.substr(0, shown.size() - 1);
                    for (const auto& cls : class_order_) if (classes_.at(cls).name == bare) return cls;
                    return std::string();
                };
                // C# overrides keep the result type, or narrow it to a derived class.
                const bool covariant = base && (base->result == result ||
                    (!class_named(result).empty() && !class_named(base->result).empty() && primary_derives(class_named(result), class_named(base->result))));
                modifiers += base && base->is_virtual && covariant ? "override " : base ? "new virtual " : "virtual ";
            } else if (base) modifiers += "new ";
            head = modifiers + result + " " + name + "(" + parameters + ")";
            cs_ << "\n" << doc.render(indent);
            if (slots.empty()) cs_ << indent << head << " => " << returned << ";\n";
            else {
                cs_ << indent << head << "\n" << indent << "{\n" << slots;
                if (result == "void") cs_ << indent << "    " << call << ";\n" << writebacks;
                else cs_ << indent << "    var _result = " << returned << ";\n" << writebacks << indent << "    return _result;\n";
                cs_ << indent << "}\n";
            }
            members[key] = {name, is_virtual || to_string, result};
        }
    }
}

void Generator::emit_class(const std::string& owner) {
    auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto base = plan.bases.empty() ? std::string("NativeObject") : classes_.at(plan.bases.front()).name;
    auto doc = describe(record.documentation, "C++ class <c>" + xml(owner) + "</c>.");
    if (plan.bases.size() > 1) {
        std::string also;
        for (std::size_t i = 1; i < plan.bases.size(); ++i) also += (i > 1 ? ", " : "") + std::string("<see cref=\"") + classes_.at(plan.bases[i]).name + "\"/>";
        doc.remarks.push_back("<para>Also derives (in C++) from " + also + ": it converts implicitly to them.</para>");
    }
    if (record.abstract) doc.remarks.push_back(std::string("<para>Abstract in C++: ") + (plan.director ? "derive a C# class from it and override its abstract methods." : "C++ returns objects of derived classes.") + "</para>");
    else if (plan.director) doc.remarks.push_back("<para>C# classes deriving from it override its virtual methods, also when C++ calls them.</para>");
    cs_ << "\n" << doc.render("") << "public partial class " << plan.name << " : " << base << "\n{\n"
        << "    internal " << plan.name << "(NativeRaw raw) : base(raw) { }\n";
    // Constructors: one per C++ constructor and arity.
    std::set<std::string> seen;
    for (const auto& overload : plan.constructors) {
        const auto& function = *overload.function;
        const auto names = parameter_names(overload);
        const std::size_t required = function.parameters.size() - function.default_arguments;
        for (std::size_t arity = required; arity <= function.parameters.size(); ++arity) {
            std::string parameters, types, arguments;
            for (std::size_t i = 0; i < arity; ++i) {
                parameters += (i ? ", " : "") + cs_type(function.parameters[i], true) + " " + names[i];
                types += cs_type(function.parameters[i], true) + ",";
                arguments += ", " + names[i];
            }
            if (!seen.insert(types).second) continue;
            const auto access = record.abstract || function.access != "public" ? "protected " : "public ";
            cs_ << "\n" << overload_doc(overload, arity).render("    ")
                << "    " << access << plan.name << "(" << parameters << ") : base(default(NativeRaw))\n"
                << "    {\n        Rosetta.Construct(this, typeof(" << plan.name << "), " << (plan.director ? "true" : "false") << ", "
                << plan.constructor_index << arguments << ");\n    }\n";
        }
    }
    // Implicit conversions to the secondary bases (C# has single inheritance).
    for (const auto& other : class_order_) {
        if (other == owner || !derives(owner, other) || primary_derives(owner, other)) continue;
        if (!plan.bases.empty() && derives(plan.bases.front(), other)) continue; // the primary base converts
        const auto& target = classes_.at(other).name;
        cs_ << "\n    /// <summary>This object as a <see cref=\"" << target << "\"/> (a C++ base class).</summary>\n"
            << "    public static implicit operator " << target << "?(" << plan.name << "? self) => Rosetta.View(self, () => new "
            << target << "(default(NativeRaw)));\n";
    }
    for (const auto& group : plan.methods) emit_overloads(&plan, group, !group.is_static, "    ");
    for (const auto& group : plan.operators) emit_overloads(&plan, group, true, "    ");
    // Fields: properties.
    auto& members = members_[owner];
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto type = cs_type(f.type, false);
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        const auto key = "property " + field.name;
        const auto* hidden = inherited(owner, key);
        auto about = describe(f.documentation, "C++ field <c>" + xml(owner + "::" + f.name) + "</c>.");
        cs_ << "\n" << about.render("    ") << "    public " << (hidden ? "new " : "") << type << " " << field.name
            << "\n    {\n        get => Rosetta.To<" << type << ">(Rosetta.Call(" << field.getter_index << ", this));\n";
        if (writable) cs_ << "        set => Rosetta.Call(" << field.setter_index << ", this, " << "value);\n";
        cs_ << "    }\n";
        members[key] = {field.name, false, type};
    }
    for (const auto& constant : plan.constants) {
        const auto type = cs_type(*constant.type, false);
        auto about = describe(*constant.documentation, "C++ constant <c>" + xml(constant.expression) + "</c>.");
        cs_ << "\n" << about.render("    ") << "    public static " << type << " " << constant.name << " => Rosetta.To<" << type
            << ">(Rosetta.Call(" << constant.index << "));\n";
    }
    cs_ << "}\n";
}

GeneratedCSharpFullSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    namespace_ = options_.managed_namespace;
    if (namespace_.empty()) namespace_ = pascal(name_);
    for (std::size_t start = 0;;) {
        const auto dot = namespace_.find('.', start);
        const auto part = namespace_.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!identifier(part) || csharp_keyword(part)) throw BindingError("C# namespace must be dotted identifiers: " + namespace_);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    const auto functions_class = options_.functions_class.empty() ? std::string("Functions") : options_.functions_class;
    GeneratedCSharpFullSource result;
    prepare();
    // Type names share the namespace.
    std::map<std::string, std::string> taken;
    const auto reserve = [&](const std::string& name, const std::string& what) {
        const auto inserted = taken.emplace(name, what);
        if (!inserted.second) throw BindingError("C# type name collision: " + name + " (" + inserted.first->second + " and " + what + ")");
    };
    const auto available = [&](const std::string& name) { return !taken.count(name); };
    for (const char* runtime : {"NativeObject", "NativeException", "NativeFunction", "NativeRaw", "Rosetta", "RosettaRuntime"})
        reserve(runtime, "Rosetta runtime");
    reserve(functions_class, "the module functions");
    {
        std::set<std::string> collided;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!available(plan.name)) {
                if (!flat_) throw BindingError("C# type name collision: " + plan.name);
                reject(owner, "class name collides with another type");
                collided.insert(owner);
                continue;
            }
            reserve(plan.name, "class " + owner);
        }
        drop_classes(collided);
        for (auto it = taken.begin(); it != taken.end();) {
            const auto owner = it->second.rfind("class ", 0) == 0 ? it->second.substr(6) : std::string();
            if (!owner.empty() && !classes_.count(owner)) it = taken.erase(it);
            else ++it;
        }
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, type_name(nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        if (!available(name)) {
            if (!flat_) throw BindingError("C# type name collision: " + name);
            reject(qualified, "enum name collides with another type");
            continue;
        }
        reserve(name, "enum " + qualified);
        enum_names_[qualified] = name;
    }
    for (const auto& [name, what] : taken) types_.insert(name);
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::set<std::string> submodules;
    for (const auto& group : functions) if (!group.module.empty()) submodules.insert(group.module);
    for (const auto& submodule : submodules) {
        if (!identifier(submodule) || csharp_keyword(submodule)) throw BindingError("C# submodule name must be an identifier: " + submodule);
        if (!available(submodule)) throw BindingError("C# submodule name collides with another type: " + submodule);
        reserve(submodule, "submodule " + submodule);
        types_.insert(submodule);
    }
    // Members named like their class (not allowed in C#), or like a member of another kind.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::set<std::string> methods;
        for (auto& group : plan.methods) {
            if (group.name == plan.name) group.name += "_";
            methods.insert(group.name);
        }
        for (auto& field : plan.fields) {
            if (field.name == plan.name || methods.count(field.name)) field.name += "Field";
            methods.insert(field.name);
        }
        for (auto& constant : plan.constants) {
            if (constant.name == plan.name || methods.count(constant.name)) constant.name += "Value";
            if (!methods.insert(constant.name).second) { reject(constant.expression, "constant name collides with another member"); constant.name.clear(); }
        }
        auto& list = plan.constants;
        list.erase(std::remove_if(list.begin(), list.end(), [](const ConstantPlan& c) { return c.name.empty(); }), list.end());
    }
    std::set<std::string> function_names;
    for (const auto& group : functions) if (group.module.empty()) function_names.insert(group.name);
    std::vector<ConstantPlan> constants;
    for (auto& constant : plan_constants([&](const Variable& variable) {
             const auto qualified = variable.qualified_name();
             return renamed(qualified, method_name(flat(variable.namespaces) ? variable.name : exported(qualified)));
         })) {
        if (function_names.count(constant.name) || constant.name == functions_class) { reject(constant.expression.substr(2), "constant name collides with a function"); continue; }
        function_names.insert(constant.name);
        constants.push_back(constant);
    }
    check_diagnostics();
    emit_cpp(result.source, result.parts, functions, constants, "C# namespace " + namespace_ + " (" + name_ + ", full API).");

    // --------------------------------------------------------------- C# source
    const auto library = options_.library.empty() ? name_ : options_.library;
    std::string environment;
    for (char c : name_) environment += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    environment += "_CSHARP_LIBRARY";
    cs_ << "// <auto-generated>\n// Generated by Rosetta: C# interface to the native module `" << name_ << "` (full API).\n"
        << "// It loads " << library << ".so (.dylib, .dll) from the application's directory (or lib/, ../lib),\n"
        << "// or the file " << environment << " names.\n// </auto-generated>\n"
        << "#nullable enable annotations\n"
        << "#pragma warning disable CS0108, CS0114, CS0660, CS0661, CS1591, CS1573, CS8981\n"
        << "using System;\nusing System.Collections;\nusing System.Collections.Generic;\nusing System.Globalization;\nusing System.IO;\n"
        << "using System.Linq;\nusing System.Linq.Expressions;\nusing System.Numerics;\nusing System.Reflection;\n"
        << "using System.Runtime.CompilerServices;\nusing System.Runtime.ExceptionServices;\nusing System.Runtime.InteropServices;\n"
        << "using System.Text;\nusing " << namespace_ << ".RosettaRuntime;\n\nnamespace " << namespace_ << "\n{\n";
    const auto runtime_text = [&](std::string runtime) {
        const auto replace = [&](const std::string& from, const std::string& to) {
            for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
        };
        replace("@NS@", namespace_);
        replace("@C@", "rosetta_" + name_);
        replace("@LIB@", library);
        replace("@ENV@", environment);
        return runtime;
    };
    cs_ << runtime_text(detail::csharp_full_runtime_cs);

    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        bool big = false;
        for (const auto& value : enumeration.enumerator_values)
            if (!value.empty() && value[0] != '-' && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"))) big = true;
        cs_ << "\n" << describe(enumeration.documentation, "C++ enumeration <c>" + xml(qualified) + "</c>.").render("")
            << "public enum " << name << " : " << (big ? "ulong" : "long") << "\n{\n";
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], escaped(enumeration.enumerators[i]));
            const auto& value = enumeration.enumerator_values[i];
            const auto literal = value == "-9223372036854775808" ? std::string("long.MinValue") : value;
            cs_ << "    /// <summary>C++ <c>" << xml(enumeration.enumerators[i]) << "</c> (" << xml(value) << ").</summary>\n    "
                << label << " = " << literal << ",\n";
        }
        cs_ << "}\n";
    }
    for (const auto& owner : class_order_) emit_class(owner);

    const auto emit_functions = [&](const std::string& submodule, const std::string& cls) {
        bool any = false;
        for (const auto& group : functions) if (group.module == submodule) any = true;
        if (!any && (!submodule.empty() || constants.empty())) return;
        const auto about = submodule.empty() ? "The functions and constants of native module <c>" + xml(name_) + "</c>."
                                             : "The functions of the C++ namespace mapped to submodule <c>" + xml(submodule) + "</c>.";
        cs_ << "\n/// <summary>" << about << "</summary>\npublic static partial class " << cls << "\n{\n";
        for (const auto& group : functions) if (group.module == submodule) emit_overloads(nullptr, group, false, "    ");
        if (submodule.empty())
            for (const auto& constant : constants) {
                const auto type = cs_type(*constant.type, false);
                auto doc = describe(*constant.documentation, "C++ constant <c>" + xml(normalize(constant.expression)) + "</c>.");
                cs_ << "\n" << doc.render("    ") << "    public static " << type << " " << constant.name << " => Rosetta.To<" << type
                    << ">(Rosetta.Call(" << constant.index << "));\n";
            }
        cs_ << "}\n";
    };
    emit_functions("", functions_class);
    for (const auto& submodule : submodules) emit_functions(submodule, submodule);
    cs_ << "}\n\nnamespace " << namespace_ << ".RosettaRuntime\n{\n"
        << "using System;\nusing System.Collections;\nusing System.Collections.Generic;\nusing System.Globalization;\nusing System.IO;\n"
        << "using System.Linq;\nusing System.Linq.Expressions;\nusing System.Numerics;\nusing System.Reflection;\n"
        << "using System.Runtime.CompilerServices;\nusing System.Runtime.ExceptionServices;\nusing System.Runtime.InteropServices;\n"
        << "using System.Text;\n";
    cs_ << runtime_text(detail::csharp_full_runtime_internal_cs);
    cs_ << "\ninternal static partial class Rosetta\n{\n    // The class of each class index C++ sends.\n"
        << "    internal static readonly Func<NativeObject>[] Classes = {";
    for (std::size_t i = 0; i < class_order_.size(); ++i)
        cs_ << (i ? "," : "") << "\n        () => new global::" << namespace_ << "." << classes_.at(class_order_[i]).name << "(default(NativeRaw))";
    cs_ << "\n    };\n    // The virtual methods C++ calls on C# subclass objects, by override index.\n"
        << "    internal static readonly string[] Generics = {";
    for (std::size_t i = 0; i < generic_order_.size(); ++i) cs_ << (i ? ", " : " ") << cs_string(generic_order_[i]);
    cs_ << " };\n}\n}\n";
    result.interface = cs_.str();

    const auto& framework = options_.framework.empty() ? std::string("net10.0") : options_.framework;
    result.project = "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
                     "  <!-- Generated by Rosetta: the C# interface to native module " + name_ + ". -->\n"
                     "  <PropertyGroup>\n"
                     "    <TargetFramework>" + framework + "</TargetFramework>\n"
                     "    <RootNamespace>" + namespace_ + "</RootNamespace>\n"
                     "    <AssemblyName>" + namespace_ + "</AssemblyName>\n"
                     "    <Nullable>enable</Nullable>\n"
                     "    <ImplicitUsings>disable</ImplicitUsings>\n"
                     "    <GenerateDocumentationFile>true</GenerateDocumentationFile>\n"
                     "  </PropertyGroup>\n"
                     "</Project>\n";
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedCSharpFullSource generate_csharp_full(const Module& module, const std::string& name, bool skip_unsupported,
                                               const BindingPolicy& policy, const CSharpFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("C# source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
