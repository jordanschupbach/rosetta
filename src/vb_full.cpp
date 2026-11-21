// Visual Basic backend (--language visual-basic): native classes as VB classes inheriting NativeObject
// (IDisposable), C++ virtual methods as Overridable methods VB subclasses override (directors),
// methods and properties in PascalCase, overloads typed for VB and resolved again at run time in C++,
// operators and default properties, containers as List/Dictionary/HashSet/tuples, enums, callbacks
// as Func/Action delegates, and XML documentation comments carrying the C++ documentation.
//
// It follows the full C# backend (csharp_full.cpp) closely: same C++ library (cabi_full.hpp,
// lisp_full_runtime.hpp), same runtime design (vb_full_runtime.hpp is the VB port of the C# one).
// What differs is Visual Basic itself:
// - names are case-insensitive, so collisions, overload keys and parameter names compare folded;
// - keywords are escaped with [brackets], and many more words are keywords (End, Next, Error, ...);
// - a member whose name a base class declares needs Overloads (or Shadows across member kinds),
//   and overrides cannot narrow the result type;
// - operators are spelled =, <>, Mod, And, Or, Xor and Not; indexers are Default properties;
// - nullable annotations exist only for value types (T?).
#include "rosetta/visual_basic.hpp"
#include "cabi_full.hpp"
#include "vb_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// A name as Visual Basic compares it: without [brackets], case-folded.
std::string fold(const std::string& name) {
    std::string text = name;
    text.erase(std::remove(text.begin(), text.end(), '['), text.end());
    text.erase(std::remove(text.begin(), text.end(), ']'), text.end());
    return lower(text);
}
std::string unbracket(std::string name) {
    name.erase(std::remove(name.begin(), name.end(), '['), name.end());
    name.erase(std::remove(name.begin(), name.end(), ']'), name.end());
    return name;
}
bool vb_keyword(const std::string& name) {
    static const std::set<std::string> keywords = {"addhandler", "addressof", "alias", "and", "andalso", "as", "boolean", "byref",
        "byte", "byval", "call", "case", "catch", "cbool", "cbyte", "cchar", "cdate", "cdbl", "cdec", "char", "cint", "class",
        "clng", "cobj", "const", "continue", "csbyte", "cshort", "csng", "cstr", "ctype", "cuint", "culng", "cushort", "date",
        "decimal", "declare", "default", "delegate", "dim", "directcast", "do", "double", "each", "else", "elseif", "end",
        "endif", "enum", "erase", "error", "event", "exit", "false", "finally", "for", "friend", "function", "get", "gettype",
        "getxmlnamespace", "global", "gosub", "goto", "handles", "if", "implements", "imports", "in", "inherits", "integer",
        "interface", "is", "isnot", "let", "lib", "like", "long", "loop", "me", "mod", "module", "mustinherit", "mustoverride",
        "mybase", "myclass", "nameof", "namespace", "narrowing", "new", "next", "not", "nothing", "notinheritable",
        "notoverridable", "object", "of", "on", "operator", "option", "optional", "or", "orelse", "out", "overloads",
        "overridable", "overrides", "paramarray", "partial", "private", "property", "protected", "public", "raiseevent",
        "readonly", "redim", "rem", "removehandler", "resume", "return", "sbyte", "select", "set", "shadows", "shared", "short",
        "single", "static", "step", "stop", "string", "structure", "sub", "synclock", "then", "throw", "to", "true", "try",
        "trycast", "typeof", "uinteger", "ulong", "ushort", "using", "variant", "wend", "when", "while", "widening", "with",
        "withevents", "writeonly", "xor"};
    return keywords.count(lower(name)) != 0;
}
std::string escaped(const std::string& name) { return vb_keyword(name) ? "[" + name + "]" : name; }
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
std::string vb_string(const std::string& text) {
    std::string result = "\"";
    for (char c : text) result += c == '"' ? std::string("\"\"") : std::string(1, c);
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
// ''' lines for an XML fragment.
std::string comment(const std::string& fragment, const std::string& indent) {
    std::string out;
    std::istringstream lines(fragment);
    for (std::string line; std::getline(lines, line);) out += indent + "'''" + (line.empty() ? "" : " " + line) + "\n";
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
        for (const auto& [name, text] : params) out += "<param name=\"" + unbracket(name) + "\">" + text + "</param>\n";
        if (!returns.empty()) out += "<returns>" + returns + "</returns>\n";
        for (const auto& text : exceptions) out += "<exception cref=\"NativeException\">" + text + "</exception>\n";
        return comment(out, indent);
    }
};
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

// C++ operators and their VB spelling ("" = not bound). Indexers are "this[]" (a Default property),
// the call operator the method Invoke.
std::string vb_operator(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"+", "+"}}, {"operator-", {"-", "-"}}, {"operator*", {nullptr, "*"}}, {"operator/", {nullptr, "/"}},
        {"operator%", {nullptr, "Mod"}}, {"operator==", {nullptr, "="}}, {"operator!=", {nullptr, "<>"}}, {"operator<", {nullptr, "<"}},
        {"operator<=", {nullptr, "<="}}, {"operator>", {nullptr, ">"}}, {"operator>=", {nullptr, ">="}}, {"operator&", {nullptr, "And"}},
        {"operator|", {nullptr, "Or"}}, {"operator^", {nullptr, "Xor"}}, {"operator~", {"Not", nullptr}}, {"operator!", {"Not", nullptr}},
        {"operator[]", {nullptr, "this[]"}}};
    if (name == "operator()") return "Invoke";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
// VB requires comparison operators in pairs.
std::string partner(const std::string& op) {
    if (op == "=") return "<>";
    if (op == "<>") return "=";
    if (op == "<") return ">";
    if (op == ">") return "<";
    if (op == "<=") return ">=";
    if (op == ">=") return "<=";
    return {};
}
bool is_symbol_operator(const std::string& name) {
    static const std::set<std::string> operators = {"+", "-", "*", "/", "Mod", "=", "<>", "<", ">", "<=", ">=", "And", "Or", "Xor", "Not"};
    return operators.count(name) != 0;
}

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const VisualBasicOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedVisualBasicSource run();

private:
    std::string namespace_;
    VisualBasicOptions options_;
    std::ostringstream vb_;

    std::string language() const override { return "Visual Basic"; }
    std::string target_name(const std::string& cpp) const override { return escaped(cpp); }
    std::string method_name(const std::string& cpp) const override {
        auto name = pascal(cpp);
        // Members of NativeObject and Object, and names generated code relies on.
        static const std::set<std::string> reserved = {"gettype", "gethashcode", "finalize", "memberwiseclone", "dispose", "isdisposed",
            "referenceequals", "rosetta", "nativeraw", "rosettaholder", "rosettaparent", "rosettaid", "rosettaowns"};
        return escaped(reserved.count(lower(name)) ? name + "_" : name);
    }
    std::string field_name(const std::string& cpp) const override { return method_name(cpp); }
    std::string type_name(const std::string& cpp) const override { return escaped(cpp.find("__") == std::string::npos ? cpp : pascal(cpp)); }
    std::string rename_target(const std::string& name, const std::string& fallback) const override {
        const auto bare = unbracket(fallback);
        return !bare.empty() && std::isupper(static_cast<unsigned char>(bare[0])) ? method_name(name) : escaped(name);
    }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return vb_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return method_name(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        // Generated bodies name Rosetta, Me and locals; a function's own name is its result variable.
        std::set<std::string> taken{"self", "rosetta", "_result", "nativeraw", fold(method_name(function.name))};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : camel(cpp);
            while (!taken.insert(fold(name)).second) name = unbracket(name) + "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + signature(overload, unbracket(group.name), overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    New " + signature(overload, unbracket(plan.name), overload.function->parameters.size()) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override { return unbracket(plan.name) + " is abstract: inherit it in a Visual Basic class"; }
    std::string protected_message(const ClassPlan& plan) const override { return unbracket(plan.name) + " constructor is protected: call it from a Visual Basic subclass"; }
    std::string symbol_prefix() const override { return "rosetta_vb_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_VB"; }
    std::string module_prefix() const override { return "rosetta_vb_module_"; }

    // The VB type of a C++ type: parameter types accept any sequence (IEnumerable(Of T)) or
    // dictionary; results are concrete (List(Of T), Dictionary(Of K, V), HashSet(Of T)).
    std::string vb_type(const Type& type, bool parameter) const;
    std::string vb_spelled(std::string cpp, bool parameter) const;
    std::string nullable(const std::string& text) const { return value_type(text) && text.back() != '?' ? text + "?" : text; }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    std::string parameter_type(const Type& type) const { return writeback(type) ? vb_type(type, false) : vb_type(type, true); }
    std::string parameter(const Type& type, const std::string& name) const {
        return (writeback(type) ? "ByRef " : "") + name + " As " + parameter_type(type);
    }
    std::string result_type(const Function& function) const { return is_void(function.result) ? "" : vb_type(function.result, false); }
    std::string signature(const Overload& overload, const std::string& name, std::size_t arity) const;
    std::string vb_default(const DefaultValue& value, const Type& type) const;
    Doc overload_doc(const Overload& overload, std::size_t arity) const;
    bool value_type(const std::string& vb) const;
    std::string generic(const std::string& name, const std::string& space, const std::string& arguments = {}) const {
        return bcl(name, space) + (arguments.empty() ? "" : "(Of " + arguments + ")");
    }
    // A .NET type's name, qualified with Global. when a generated type of the namespace hides it.
    std::string bcl(const std::string& name, const std::string& space) const { return types_.count(lower(name)) ? "Global." + space + "." + name : name; }
    std::set<std::string> types_;  // the generated type names, folded

    struct Member { std::string text; bool is_virtual; std::string result; };
    std::map<std::string, std::map<std::string, Member>> members_;  // class -> folded signature key -> member
    std::map<std::string, std::map<std::string, std::string>> names_;  // class -> folded member name -> kind
    const Member* inherited(const std::string& owner, const std::string& key) const;
    // The kind ("method", "property", "default") of a member named `name` in a base class of
    // `owner` (or in NativeObject and Object), "" if there is none.
    std::string inherited_kind(const std::string& owner, const std::string& name) const;
    // Overloads, or Shadows across member kinds, for a member whose name a base class has.
    std::string hiding(const std::string& owner, const std::string& name, const std::string& kind) const {
        const auto base = inherited_kind(owner, fold(name));
        if (base.empty()) return {};
        return base == kind && kind != "property" ? "Overloads " : "Shadows ";
    }
    void emit_class(const std::string& owner);
    void emit_overloads(const ClassPlan* plan, const std::string& owner, const Group& group, bool is_method, const std::string& indent);
    std::string indexer_name(const std::string& owner) const;
};

bool Generator::value_type(const std::string& vb) const {
    static const std::set<std::string> values = {"Boolean", "Char", "SByte", "Byte", "Short", "UShort", "Integer", "UInteger", "Long", "ULong",
        "Single", "Double"};
    if (values.count(vb) || vb == bcl("Complex", "System.Numerics") || vb.rfind("(", 0) == 0 || vb.rfind(bcl("ValueTuple", "System"), 0) == 0) return true;
    for (const auto& [qualified, name] : enum_names_) if (name == vb) return true;
    return false;
}

std::string Generator::vb_type(const Type& type, bool parameter) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "String";
        return nullable(vb_type(pointee, parameter));
    }
    if (is_void(target)) return "";
    if (!conversion(target).empty()) return vb_spelled(policy_.conversions.at(conversion(target)), parameter);
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        static const std::map<std::string, std::string> builtins = {{"bool", "Boolean"}, {"char", "Char"}, {"signed char", "SByte"},
            {"unsigned char", "Byte"}, {"short", "Short"}, {"unsigned short", "UShort"}, {"int", "Integer"}, {"unsigned int", "UInteger"},
            {"long", "Long"}, {"unsigned long", "ULong"}, {"long long", "Long"}, {"unsigned long long", "ULong"}, {"float", "Single"},
            {"double", "Double"}, {"long double", "Double"}};
        const auto found = builtins.find(bare);
        return found != builtins.end() ? found->second : "Object";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? found->second : "Long";
    }
    if (bound(target)) return classes_.at(target.declaration).name;
    if (bare == "std::filesystem::path") return "String";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "Object";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i, bool as_parameter) { return i < target.arguments.size() ? vb_type(target.arguments[i], as_parameter) : std::string("Object"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "String";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray")
            return parameter ? generic("IEnumerable", "System.Collections.Generic", arg(0, true)) : generic("List", "System.Collections.Generic", arg(0, false));
        if (declaration == "std::set" || declaration == "std::unordered_set")
            return parameter ? generic("IEnumerable", "System.Collections.Generic", arg(0, true)) : generic("HashSet", "System.Collections.Generic", arg(0, false));
        if (declaration == "std::map" || declaration == "std::unordered_map")
            return generic(parameter ? "IReadOnlyDictionary" : "Dictionary", "System.Collections.Generic", arg(0, false) + ", " + arg(1, false));
        if (declaration == "std::optional") return nullable(arg(0, parameter));
        if (declaration == "std::complex") return bcl("Complex", "System.Numerics");
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return nullable(arg(0, parameter));
        if (declaration == "std::pair" || declaration == "std::tuple") {
            const auto count = target.arguments.size();
            if (count == 0) return bcl("ValueTuple", "System");
            if (count > 7) return "Object";
            std::string items;
            for (std::size_t i = 0; i < count; ++i) items += (i ? ", " : "") + vb_type(target.arguments[i], parameter);
            return count == 1 ? generic("ValueTuple", "System", items) : "(" + items + ")";
        }
        if (declaration == "std::variant") return "Object";
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::string items;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) items += (i > 1 ? ", " : "") + vb_type(function.arguments[i], false);
            const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
            if (returns) return generic("Func", "System", items + (items.empty() ? "" : ", ") + vb_type(function.arguments[0], true));
            return generic("Action", "System", items);
        }
        if (declaration == "std::chrono::duration") return "Double";
    }
    return "Object";
}

std::string Generator::vb_spelled(std::string cpp, bool parameter) const {
    const auto trim = [](std::string text) {
        const auto first = text.find_first_not_of(" \t");
        const auto last = text.find_last_not_of(" \t");
        return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
    };
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (cpp.rfind("const ", 0) == 0) cpp = trim(cpp.substr(6));
    if (cpp.rfind("::", 0) == 0) cpp = cpp.substr(2);
    static const std::map<std::string, std::string> scalars = {{"bool", "Boolean"}, {"char", "Char"}, {"signed char", "SByte"},
        {"unsigned char", "Byte"}, {"short", "Short"}, {"unsigned short", "UShort"}, {"int", "Integer"}, {"unsigned int", "UInteger"},
        {"long", "Long"}, {"unsigned long", "ULong"}, {"long long", "Long"}, {"unsigned long long", "ULong"}, {"float", "Single"},
        {"double", "Double"}, {"long double", "Double"}, {"std::size_t", "ULong"}, {"size_t", "ULong"}, {"std::int64_t", "Long"},
        {"std::uint64_t", "ULong"}, {"std::int32_t", "Integer"}, {"std::uint32_t", "UInteger"}, {"std::string", "String"},
        {"std::string_view", "String"}, {"std::filesystem::path", "String"}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return found->second;
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') return "Object";
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
    const auto arg = [&](std::size_t i, bool as_parameter) { return i < arguments.size() ? vb_spelled(arguments[i], as_parameter) : std::string("Object"); };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::valarray")
        return parameter ? generic("IEnumerable", "System.Collections.Generic", arg(0, true)) : generic("List", "System.Collections.Generic", arg(0, false));
    if (name == "std::set" || name == "std::unordered_set")
        return parameter ? generic("IEnumerable", "System.Collections.Generic", arg(0, true)) : generic("HashSet", "System.Collections.Generic", arg(0, false));
    if (name == "std::map" || name == "std::unordered_map")
        return generic(parameter ? "IReadOnlyDictionary" : "Dictionary", "System.Collections.Generic", arg(0, false) + ", " + arg(1, false));
    if (name == "std::optional") return nullable(arg(0, parameter));
    if (name == "std::complex") return bcl("Complex", "System.Numerics");
    if ((name == "std::pair" || name == "std::tuple") && arguments.size() >= 2 && arguments.size() <= 7) {
        std::string items;
        for (std::size_t i = 0; i < arguments.size(); ++i) items += (i ? ", " : "") + arg(i, parameter);
        return "(" + items + ")";
    }
    return "Object";
}

std::string Generator::signature(const Overload& overload, const std::string& name, std::size_t arity) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string result = name + "(";
    for (std::size_t i = first; i < arity && i < function.parameters.size(); ++i)
        result += (i > first ? ", " : "") + parameter(function.parameters[i], unbracket(names[i - first]));
    result += ")";
    const auto returned = result_type(function);
    return function.is_constructor || returned.empty() ? result : result + " As " + returned;
}

std::string Generator::vb_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant" || value.kind == "string") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "True" : "False";
    if (value.kind == "null") return "Nothing";
    if (value.kind == "char") return "\"" + value.value + "\"c";
    if (value.kind == "enum") {
        const auto& target = referent(type);
        const auto found = enum_names_.find(target.declaration);
        return found != enum_names_.end() ? unbracket(found->second) + "." + value.value : value.value;
    }
    if (value.kind == "default") return "Nothing";
    return {};
}

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
        const auto shown = vb_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        omitted += (omitted.empty() ? "" : ", ") + std::string("<c>") + xml(unbracket(names[i - first])) + (shown.empty() ? "" : " = " + xml(shown)) + "</c>";
    }
    if (!omitted.empty()) doc.remarks.push_back("<para>Uses the C++ default for " + omitted + ".</para>");
    if (!function.is_constructor && !is_void(function.result) && !function.documentation.returns.empty())
        doc.returns = inline_xml(one_line(function.documentation.returns));
    return doc;
}

const Generator::Member* Generator::inherited(const std::string& owner, const std::string& key) const {
    if (!classes_.count(owner)) return nullptr;
    for (auto cls = owner; !classes_.at(cls).bases.empty();) {
        cls = classes_.at(cls).bases.front();
        const auto found = members_.find(cls);
        if (found == members_.end()) continue;
        const auto member = found->second.find(key);
        if (member != found->second.end()) return &member->second;
    }
    return nullptr;
}

std::string Generator::inherited_kind(const std::string& owner, const std::string& name) const {
    if (classes_.count(owner))
        for (auto cls = owner; !classes_.at(cls).bases.empty();) {
            cls = classes_.at(cls).bases.front();
            const auto found = names_.find(cls);
            if (found == names_.end()) continue;
            const auto member = found->second.find(name);
            if (member != found->second.end()) return member->second;
        }
    // NativeObject (classes) and Object (every class, the function classes too).
    static const std::map<std::string, std::string> root = {{"tostring", "method"}, {"equals", "method"}, {"dispose", "method"},
        {"isdisposed", "property"}};
    const auto found = root.find(name);
    if (found == root.end() || (!classes_.count(owner) && (name == "dispose" || name == "isdisposed"))) return {};
    return found->second;
}

// The default property's name: Item, unless a member of the class (or a base) is named so.
std::string Generator::indexer_name(const std::string& owner) const {
    for (const auto* candidate : {"Item", "Element", "Indexed"}) {
        const auto kind = inherited_kind(owner, lower(candidate));
        bool own = false;
        const auto& plan = classes_.at(owner);
        for (const auto& group : plan.methods) if (fold(group.name) == lower(candidate)) own = true;
        for (const auto& field : plan.fields) if (fold(field.name) == lower(candidate)) own = true;
        for (const auto& constant : plan.constants) if (fold(constant.name) == lower(candidate)) own = true;
        if (!own && (kind.empty() || kind == "default")) return candidate;
    }
    return "IndexedValue";
}

// The VB overloads of a group: one per C++ overload and arity (default arguments), duplicates of an
// earlier VB signature dropped (C++ resolves the call at run time anyway). VB signatures ignore
// ByRef and compare names case-insensitively.
void Generator::emit_overloads(const ClassPlan* plan, const std::string& owner, const Group& group, bool is_method, const std::string& indent) {
    auto& members = members_[owner];
    auto& names = names_[owner];
    std::set<std::string> seen;
    for (const auto& overload : group.overloads) {
        const auto& function = *overload.function;
        const bool instance = plan && is_method && (!overload.extension || overload.receives_self);
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        const auto parameter_names_ = parameter_names(overload);
        const auto result = result_type(function);
        for (std::size_t arity = std::max(required, first); arity <= function.parameters.size(); ++arity) {
            std::string parameters, types, arguments = instance ? "Me" : "", slots, writebacks;
            for (std::size_t i = first; i < arity; ++i) {
                const auto& type = function.parameters[i];
                const auto name = parameter_names_[i - first];
                parameters += (i > first ? ", " : "") + parameter(type, name);
                types += lower(parameter_type(type)) + ",";
                if (writeback(type)) {
                    const auto slot = "_slot" + std::to_string(i - first);
                    slots += indent + "    Dim " + slot + " As New Rosetta.Slot(" + name + ")\n";
                    writebacks += indent + "    " + name + " = Rosetta.Cast(Of " + vb_type(type, false) + ")(" + slot + ".Value)\n";
                    arguments += (arguments.empty() ? "" : ", ") + slot;
                } else arguments += (arguments.empty() ? "" : ", ") + name;
            }
            const std::string name = group.name;
            const bool op = is_symbol_operator(name);
            const auto key = (op ? "operator " : "") + fold(name) + "(" + types + ")";
            if (!seen.insert(key).second || members.count(key)) continue;
            auto doc = overload_doc(overload, arity);
            const auto call = "Rosetta.Run(" + std::to_string(group.index) + (arguments.empty() ? "" : ", " + arguments) + ")";
            const auto returned = result.empty() ? call : "Rosetta.Cast(Of " + result + ")(" + call + ")";
            if (op) {
                // Operators: Shared, with this class at the bound operand; no ByRef operands.
                std::string operands;
                std::vector<std::string> operand_names, operand_types;
                const std::size_t count = function.parameters.size() + (overload.extension ? 0 : 1);
                for (std::size_t i = 0; i < count; ++i) {
                    const bool self = i == group.operand && !overload.extension;
                    const auto type = self ? plan->name : parameter_type(function.parameters[overload.extension ? i : i - 1]);
                    const auto operand = (overload.extension ? i == group.operand : self) ? std::string("self") : "x" + std::to_string(i + 1);
                    operands += (i ? ", " : "") + operand + " As " + type;
                    operand_names.push_back(operand);
                    operand_types.push_back(type);
                }
                std::string list;
                for (const auto& operand : operand_names) list += (list.empty() ? "" : ", ") + operand;
                const auto native = "Rosetta.Cast(Of " + result + ")(Rosetta.Run(" + std::to_string(group.index) + ", " + list + "))";
                std::string body = native;
                // = and <> take Nothing (and compare it by reference).
                if ((name == "=" || name == "<>") && result == "Boolean" && operand_names.size() == 2 &&
                    !value_type(operand_types[0]) && !value_type(operand_types[1]))
                    body = "If(" + operand_names[0] + " Is Nothing OrElse " + operand_names[1] + " Is Nothing, " + operand_names[0] +
                           (name == "=" ? " Is " : " IsNot ") + operand_names[1] + ", " + native + ")";
                vb_ << "\n" << Doc{doc.summary, doc.remarks, {}, doc.returns, doc.exceptions}.render(indent)
                    << indent << "Public Shared Operator " << name << "(" << operands << ") As " << result << "\n"
                    << indent << "    Return " << body << "\n" << indent << "End Operator\n";
                members[key] = {name, false, result};
                // The required partner of a comparison operator, from this one.
                const auto other = partner(name);
                if (!other.empty() && operand_names.size() == 2) {
                    const auto partner_key = "operator " + fold(other) + "(" + types + ")";
                    bool declared = false;
                    for (const auto& candidate : plan->operators) if (candidate.name == other) declared = true;
                    if (!declared && !members.count(partner_key) && seen.insert(partner_key).second) {
                        // a > b is b < a: only for operands of one type.
                        if (name != "=" && name != "<>" && operand_types[0] != operand_types[1]) continue;
                        const auto partner_body = name == "=" || name == "<>"
                            ? "Not (" + operand_names[0] + " " + name + " " + operand_names[1] + ")"
                            : operand_names[1] + " " + name + " " + operand_names[0];
                        vb_ << indent << "''' <summary>The opposite of <c>" << xml(name) << "</c>.</summary>\n"
                            << indent << "Public Shared Operator " << other << "(" << operands << ") As " << result << "\n"
                            << indent << "    Return " << partner_body << "\n" << indent << "End Operator\n";
                        members[partner_key] = {other, false, result};
                    }
                }
                continue;
            }
            if (name == "this[]") {
                // A Default property (its parameters cannot be ByRef).
                std::string indexed;
                for (std::size_t i = first; i < arity; ++i)
                    indexed += (i > first ? ", " : "") + parameter_names_[i - first] + " As " + parameter_type(function.parameters[i]);
                const auto property = indexer_name(owner);
                vb_ << "\n" << doc.render(indent) << indent << "Default Public " << hiding(owner, property, "default")
                    << "ReadOnly Property " << property << "(" << indexed << ") As " << result << "\n"
                    << indent << "    Get\n" << indent << "        Return " << returned << "\n" << indent << "    End Get\n"
                    << indent << "End Property\n";
                members[key] = {name, false, result};
                names[lower(property)] = "default";
                continue;
            }
            const bool to_string = fold(name) == "tostring" && arity == first && result == "String" && instance;
            const auto* base = plan ? inherited(owner, key) : nullptr;
            const auto hides = hiding(owner, name, "method");
            std::string modifiers = "Public ";
            bool is_virtual = false;
            if (!instance) modifiers += "Shared " + hides;
            else if (to_string) modifiers += "Overrides ";
            else if (function.is_virtual) {
                is_virtual = true;
                // VB overrides keep the result type exactly.
                if (base && base->is_virtual && base->result == result && hides == "Overloads ") modifiers += "Overrides ";
                else modifiers += hides + "Overridable ";
            } else modifiers += hides;
            const auto head = modifiers + (result.empty() ? "Sub " : "Function ") + name + "(" + parameters + ")" +
                              (result.empty() ? "" : " As " + result);
            vb_ << "\n" << doc.render(indent) << indent << head << "\n" << slots;
            if (slots.empty()) vb_ << indent << "    " << (result.empty() ? call : "Return " + returned) << "\n";
            else if (result.empty()) vb_ << indent << "    " << call << "\n" << writebacks;
            else vb_ << indent << "    Dim _result = " << returned << "\n" << writebacks << indent << "    Return _result\n";
            vb_ << indent << (result.empty() ? "End Sub\n" : "End Function\n");
            members[key] = {name, is_virtual || to_string, result};
            names[fold(name)] = "method";
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
        for (std::size_t i = 1; i < plan.bases.size(); ++i) also += (i > 1 ? ", " : "") + std::string("<see cref=\"") + unbracket(classes_.at(plan.bases[i]).name) + "\"/>";
        doc.remarks.push_back("<para>Also derives (in C++) from " + also + ": it converts implicitly to them.</para>");
    }
    if (record.abstract) doc.remarks.push_back(std::string("<para>Abstract in C++: ") + (plan.director ? "inherit it in a Visual Basic class and override its abstract methods." : "C++ returns objects of derived classes.") + "</para>");
    else if (plan.director) doc.remarks.push_back("<para>Visual Basic classes inheriting it override its Overridable methods, also when C++ calls them.</para>");
    vb_ << "\n" << doc.render("") << "Partial Public Class " << plan.name << "\n    Inherits " << base << "\n"
        << "    Friend Sub New(raw As NativeRaw)\n        MyBase.New(raw)\n    End Sub\n";
    // Constructors: one per C++ constructor and arity.
    std::set<std::string> seen;
    for (const auto& overload : plan.constructors) {
        const auto& function = *overload.function;
        const auto names = parameter_names(overload);
        const std::size_t required = function.parameters.size() - function.default_arguments;
        for (std::size_t arity = required; arity <= function.parameters.size(); ++arity) {
            std::string parameters, types, arguments;
            for (std::size_t i = 0; i < arity; ++i) {
                parameters += (i ? ", " : "") + names[i] + " As " + vb_type(function.parameters[i], true);
                types += lower(vb_type(function.parameters[i], true)) + ",";
                arguments += ", " + names[i];
            }
            if (!seen.insert(types).second) continue;
            const auto access = record.abstract || function.access != "public" ? "Protected " : "Public ";
            vb_ << "\n" << overload_doc(overload, arity).render("    ")
                << "    " << access << "Sub New(" << parameters << ")\n        MyBase.New(New NativeRaw())\n"
                << "        Rosetta.Construct(Me, GetType(" << plan.name << "), " << (plan.director ? "True" : "False") << ", "
                << plan.constructor_index << arguments << ")\n    End Sub\n";
        }
    }
    // Widening conversions to the secondary bases (.NET has single inheritance).
    for (const auto& other : class_order_) {
        if (other == owner || !derives(owner, other) || primary_derives(owner, other)) continue;
        if (!plan.bases.empty() && derives(plan.bases.front(), other)) continue; // the primary base converts
        const auto& target = classes_.at(other).name;
        vb_ << "\n    ''' <summary>This object as a <see cref=\"" << unbracket(target) << "\"/> (a C++ base class).</summary>\n"
            << "    Public Shared Widening Operator CType(self As " << plan.name << ") As " << target << "\n"
            << "        Return Rosetta.View(Of " << target << ")(self, Function() New " << target << "(New NativeRaw()))\n"
            << "    End Operator\n";
    }
    for (const auto& group : plan.methods) emit_overloads(&plan, owner, group, !group.is_static, "    ");
    for (const auto& group : plan.operators) emit_overloads(&plan, owner, group, true, "    ");
    // Fields: properties.
    auto& members = members_[owner];
    auto& names = names_[owner];
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto type = vb_type(f.type, false);
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        auto about = describe(f.documentation, "C++ field <c>" + xml(owner + "::" + f.name) + "</c>.");
        vb_ << "\n" << about.render("    ") << "    Public " << hiding(owner, field.name, "property") << (writable ? "" : "ReadOnly ")
            << "Property " << field.name << " As " << type << "\n"
            << "        Get\n            Return Rosetta.Cast(Of " << type << ")(Rosetta.Run(" << field.getter_index << ", Me))\n        End Get\n";
        if (writable)
            vb_ << "        Set(_value As " << type << ")\n            Rosetta.Run(" << field.setter_index << ", Me, _value)\n        End Set\n";
        vb_ << "    End Property\n";
        members["property " + fold(field.name)] = {field.name, false, type};
        names[fold(field.name)] = "property";
    }
    for (const auto& constant : plan.constants) {
        const auto type = vb_type(*constant.type, false);
        auto about = describe(*constant.documentation, "C++ constant <c>" + xml(constant.expression) + "</c>.");
        vb_ << "\n" << about.render("    ") << "    Public Shared " << hiding(owner, constant.name, "property") << "ReadOnly Property "
            << constant.name << " As " << type << "\n"
            << "        Get\n            Return Rosetta.Cast(Of " << type << ")(Rosetta.Run(" << constant.index << "))\n        End Get\n"
            << "    End Property\n";
        names[fold(constant.name)] = "property";
    }
    vb_ << "End Class\n";
}

GeneratedVisualBasicSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    namespace_ = options_.managed_namespace;
    if (namespace_.empty()) namespace_ = pascal(name_);
    for (std::size_t start = 0;;) {
        const auto dot = namespace_.find('.', start);
        const auto part = namespace_.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!identifier(part) || vb_keyword(part)) throw BindingError("Visual Basic namespace must be dotted identifiers: " + namespace_);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    const auto functions_class = options_.functions_class.empty() ? std::string("Functions") : options_.functions_class;
    if (!identifier(functions_class) || vb_keyword(functions_class)) throw BindingError("Visual Basic functions class must be an identifier: " + functions_class);
    GeneratedVisualBasicSource result;
    prepare();
    // Type names share the namespace, and VB compares them case-insensitively.
    std::map<std::string, std::string> taken;  // folded name -> what
    const auto reserve = [&](const std::string& name, const std::string& what) {
        const auto inserted = taken.emplace(fold(name), what);
        if (!inserted.second) throw BindingError("Visual Basic type name collision: " + unbracket(name) + " (" + inserted.first->second + " and " + what + ")");
    };
    const auto available = [&](const std::string& name) { return !taken.count(fold(name)); };
    for (const char* runtime : {"NativeObject", "NativeException", "NativeFunction", "NativeRaw", "Rosetta", "RosettaRuntime"})
        reserve(runtime, "Rosetta runtime");
    reserve(functions_class, "the module functions");
    {
        std::set<std::string> collided;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!available(plan.name)) {
                if (!flat_) throw BindingError("Visual Basic type name collision: " + unbracket(plan.name));
                reject(owner, "class name collides with another type (Visual Basic names are case-insensitive)");
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
        if (nested) name = escaped(unbracket(classes_.at(enumeration.owner).name) + "_" + unbracket(name));
        if (!available(name)) {
            if (!flat_) throw BindingError("Visual Basic type name collision: " + unbracket(name));
            reject(qualified, "enum name collides with another type (Visual Basic names are case-insensitive)");
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
        if (!identifier(submodule)) throw BindingError("Visual Basic submodule name must be an identifier: " + submodule);
        if (!available(submodule)) throw BindingError("Visual Basic submodule name collides with another type: " + submodule);
        reserve(submodule, "submodule " + submodule);
        types_.insert(fold(submodule));
    }
    // Members named like their class, or (case-insensitively) like a member of another kind.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::set<std::string> methods;
        for (auto& group : plan.methods) {
            if (fold(group.name) == fold(plan.name)) group.name = unbracket(group.name) + "_";
            methods.insert(fold(group.name));
        }
        for (auto& field : plan.fields) {
            if (fold(field.name) == fold(plan.name) || methods.count(fold(field.name))) field.name = unbracket(field.name) + "Field";
            if (!methods.insert(fold(field.name)).second) field.name = unbracket(field.name) + "_";
            methods.insert(fold(field.name));
        }
        for (auto& constant : plan.constants) {
            if (fold(constant.name) == fold(plan.name) || methods.count(fold(constant.name))) constant.name = unbracket(constant.name) + "Value";
            if (!methods.insert(fold(constant.name)).second) { reject(constant.expression, "constant name collides with another member"); constant.name.clear(); }
        }
        auto& list = plan.constants;
        list.erase(std::remove_if(list.begin(), list.end(), [](const ConstantPlan& c) { return c.name.empty(); }), list.end());
    }
    std::set<std::string> function_names;
    for (const auto& group : functions) if (group.module.empty()) function_names.insert(fold(group.name));
    std::vector<ConstantPlan> constants;
    for (auto& constant : plan_constants([&](const Variable& variable) {
             const auto qualified = variable.qualified_name();
             return renamed(qualified, method_name(flat(variable.namespaces) ? variable.name : exported(qualified)));
         })) {
        if (function_names.count(fold(constant.name)) || fold(constant.name) == fold(functions_class)) {
            reject(constant.expression.substr(2), "constant name collides with a function");
            continue;
        }
        function_names.insert(fold(constant.name));
        constants.push_back(constant);
    }
    check_diagnostics();
    emit_cpp(result.source, result.parts, functions, constants, "Visual Basic namespace " + namespace_ + " (" + name_ + ").");

    // --------------------------------------------------------------- VB source
    const auto library = options_.library.empty() ? name_ : options_.library;
    std::string environment;
    for (char c : name_) environment += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    environment += "_VB_LIBRARY";
    vb_ << "' <auto-generated>\n' Generated by Rosetta: Visual Basic interface to the native module `" << name_ << "`.\n"
        << "' It loads " << library << ".so (.dylib, .dll) from the application's directory (or lib/, ../lib),\n"
        << "' or the file " << environment << " names.\n' </auto-generated>\n"
        << "Option Strict On\nOption Explicit On\nOption Infer On\n"
        << "#Disable Warning BC40003, BC40004, BC40005, BC40007, BC42024, BC42025, BC42309\n"
        << "Imports System\nImports System.Collections.Generic\nImports System.Numerics\nImports " << namespace_ << ".RosettaRuntime\n\n"
        << "Namespace Global." << namespace_ << "\n";
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
    vb_ << runtime_text(detail::vb_full_runtime);

    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        bool big = false;
        for (const auto& value : enumeration.enumerator_values)
            if (!value.empty() && value[0] != '-' && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"))) big = true;
        vb_ << "\n" << describe(enumeration.documentation, "C++ enumeration <c>" + xml(qualified) + "</c>.").render("")
            << "Public Enum " << name << " As " << (big ? "ULong" : "Long") << "\n";
        std::set<std::string> labels;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            auto label = renamed(qualified + "::" + enumeration.enumerators[i], escaped(enumeration.enumerators[i]));
            while (!labels.insert(fold(label)).second) label = unbracket(label) + "_";
            const auto& value = enumeration.enumerator_values[i];
            const auto literal = value == "-9223372036854775808" ? std::string("Long.MinValue") : value + (big ? "UL" : "L");
            vb_ << "    ''' <summary>C++ <c>" << xml(enumeration.enumerators[i]) << "</c> (" << xml(value) << ").</summary>\n    "
                << label << " = " << literal << "\n";
        }
        vb_ << "End Enum\n";
    }
    for (const auto& owner : class_order_) emit_class(owner);

    const auto emit_functions = [&](const std::string& submodule, const std::string& cls) {
        bool any = false;
        for (const auto& group : functions) if (group.module == submodule) any = true;
        if (!any && (!submodule.empty() || constants.empty())) return;
        const auto about = submodule.empty() ? "The functions and constants of native module <c>" + xml(name_) + "</c>."
                                             : "The functions of the C++ namespace mapped to submodule <c>" + xml(submodule) + "</c>.";
        vb_ << "\n''' <summary>" << about << "</summary>\nPartial Public NotInheritable Class " << escaped(cls) << "\n"
            << "    Private Sub New()\n    End Sub\n";
        const auto owner = "module " + submodule;
        for (const auto& group : functions) if (group.module == submodule) emit_overloads(nullptr, owner, group, false, "    ");
        if (submodule.empty())
            for (const auto& constant : constants) {
                const auto type = vb_type(*constant.type, false);
                auto doc = describe(*constant.documentation, "C++ constant <c>" + xml(normalize(constant.expression)) + "</c>.");
                vb_ << "\n" << doc.render("    ") << "    Public Shared " << hiding(owner, constant.name, "property") << "ReadOnly Property "
                    << constant.name << " As " << type << "\n"
                    << "        Get\n            Return Rosetta.Cast(Of " << type << ")(Rosetta.Run(" << constant.index << "))\n"
                    << "        End Get\n    End Property\n";
            }
        vb_ << "End Class\n";
    };
    emit_functions("", functions_class);
    for (const auto& submodule : submodules) emit_functions(submodule, submodule);
    vb_ << "End Namespace\n\nNamespace Global." << namespace_ << ".RosettaRuntime\n";
    vb_ << runtime_text(detail::vb_full_runtime_internal);
    vb_ << "\nPartial Friend NotInheritable Class Rosetta\n    ' The class of each class index C++ sends.\n"
        << "    Friend Shared ReadOnly Classes As Global.System.Func(Of Global." << namespace_ << ".NativeObject)() = New Global.System.Func(Of Global."
        << namespace_ << ".NativeObject)() {";
    for (std::size_t i = 0; i < class_order_.size(); ++i)
        vb_ << (i ? "," : "") << "\n        Function() New Global." << namespace_ << "." << classes_.at(class_order_[i]).name
            << "(New Global." << namespace_ << ".NativeRaw())";
    vb_ << "\n    }\n    ' The Overridable methods C++ calls on VB subclass objects, by override index.\n"
        << "    Friend Shared ReadOnly Generics As String() = New String() {";
    for (std::size_t i = 0; i < generic_order_.size(); ++i) vb_ << (i ? ", " : "") << vb_string(unbracket(generic_order_[i]));
    vb_ << "}\nEnd Class\nEnd Namespace\n";
    result.interface = vb_.str();

    const auto& framework = options_.framework.empty() ? std::string("net10.0") : options_.framework;
    result.project = "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
                     "  <!-- Generated by Rosetta: the Visual Basic interface to native module " + name_ + ". -->\n"
                     "  <PropertyGroup>\n"
                     "    <TargetFramework>" + framework + "</TargetFramework>\n"
                     "    <RootNamespace></RootNamespace>\n"
                     "    <AssemblyName>" + namespace_ + "</AssemblyName>\n"
                     "    <OptionStrict>On</OptionStrict>\n"
                     "    <ImplicitUsings>disable</ImplicitUsings>\n"
                     "    <GenerateDocumentationFile>true</GenerateDocumentationFile>\n"
                     "  </PropertyGroup>\n"
                     "</Project>\n";
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedVisualBasicSource generate_visual_basic(const Module& module, const std::string& name, bool skip_unsupported,
                                                 const BindingPolicy& policy, const VisualBasicOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Visual Basic source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
