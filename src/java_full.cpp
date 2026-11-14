// Full Java backend (--java-api full): native classes as Java classes extending NativeObject
// (AutoCloseable, with a Cleaner as a safety net), C++ virtual methods as methods Java subclasses
// override (directors), methods in camelCase, fields as getX/setX, overloads typed for Java (merged
// where Java's erasure cannot tell them apart) and resolved again at run time in C++, operators as
// named methods (plus, times, equals, compareTo, get), enums as Java enums, containers as
// primitive arrays, List/Set/Map/Optional and generated records, callbacks as functional
// interfaces, C++ exceptions as standard Java exceptions, and Javadoc carrying the C++
// documentation.
//
// It writes a C++ source (a shared library with JNI glue around the single entry point every full
// C ABI backend shares: cabi_full.hpp, lisp_full_runtime.hpp) and one .java file per class under
// the package directory; java_full_runtime_java.hpp holds the Java runtime and the JNI glue.
#include "rosetta/java.hpp"
#include "cabi_full.hpp"
#include "java_full_runtime_java.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

bool java_keyword(const std::string& name) {
    static const std::set<std::string> keywords = {"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class",
        "const", "continue", "default", "do", "double", "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if",
        "implements", "import", "instanceof", "int", "interface", "long", "native", "new", "package", "private", "protected", "public",
        "return", "short", "static", "strictfp", "super", "switch", "synchronized", "this", "throw", "throws", "transient", "try",
        "void", "volatile", "while", "true", "false", "null", "_"};
    return keywords.count(name) != 0;
}
// Contextual names a type may not take (they may still name methods and variables).
bool restricted_type(const std::string& name) {
    return name == "var" || name == "yield" || name == "record" || name == "sealed" || name == "permits";
}
std::string escaped(const std::string& name) { return java_keyword(name) ? name + "_" : name; }
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
    return result;
}
// sides_unknown, sidesUnknown -> SIDES_UNKNOWN.
std::string upper_snake(const std::string& name) {
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (std::isupper(static_cast<unsigned char>(c)) && i > 0 && name[i - 1] != '_' &&
            (std::islower(static_cast<unsigned char>(name[i - 1])) || std::isdigit(static_cast<unsigned char>(name[i - 1])) ||
             (i + 1 < name.size() && std::islower(static_cast<unsigned char>(name[i + 1])))))
            result += '_';
        result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (result.empty()) return "UNNAMED";
    if (std::isdigit(static_cast<unsigned char>(result[0]))) result = "_" + result;
    return result;
}
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\n");
    const auto last = text.find_last_not_of(" \t\n");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
std::string java_string(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += std::string("\\") + c;
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (static_cast<unsigned char>(c) < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof buffer, "\\u%04x", c); result += buffer; }
        else result += c;
    }
    return result + '"';
}
// The JNI spelling of a Java name (package dots become _, _ becomes _1).
std::string jni_mangle(const std::string& name) {
    std::string result;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) result += c;
        else if (c == '.') result += '_';
        else if (c == '_') result += "_1";
        else { char buffer[8]; std::snprintf(buffer, sizeof buffer, "_0%04x", static_cast<unsigned char>(c)); result += buffer; }
    }
    return result;
}

// --- documentation (Javadoc) ----------------------------------------------------------

// Text for Javadoc HTML: markup characters escaped, and @ (a block tag at a line start, an inline
// tag after {) and */ (the end of the comment) neutralized.
std::string html(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (c == '@') result += "&#64;";
        else if (c == '/' && i > 0 && text[i - 1] == '*') result += "&#47;";
        else result += c;
    }
    return result;
}
bool balanced(const std::string& text) {
    int depth = 0;
    for (const char c : text) {
        if (c == '{') ++depth;
        if (c == '}' && --depth < 0) return false;
    }
    return depth == 0;
}
// {@code text}, or <code> with escaped text where {@code} cannot hold it.
std::string code_span(const std::string& text) {
    if (!text.empty() && balanced(text) && text.find("*/") == std::string::npos && text.find('\n') == std::string::npos &&
        text.front() != ' ' && text.back() != ' ')
        return "{@code " + text + "}";
    return "<code>" + html(text) + "</code>";
}
// Doxygen inline markup (`code`, **bold**, *emphasis*) as Javadoc HTML.
std::string inline_doc(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '`') {
            const auto close = text.find('`', i + 1);
            if (close != std::string::npos) { result += code_span(text.substr(i + 1, close - i - 1)); i = close + 1; continue; }
        }
        if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            const auto close = text.find("**", i + 2);
            if (close != std::string::npos && close > i + 2) { result += "<b>" + inline_doc(text.substr(i + 2, close - i - 2)) + "</b>"; i = close + 2; continue; }
        }
        if (c == '*' && i + 1 < text.size() && !std::isspace(static_cast<unsigned char>(text[i + 1])) && text[i + 1] != '/') {
            const auto close = text.find('*', i + 1);
            if (close != std::string::npos && close > i + 1 && !std::isspace(static_cast<unsigned char>(text[close - 1])) &&
                text.substr(i + 1, close - i - 1).find('\n') == std::string::npos) {
                result += "<i>" + inline_doc(text.substr(i + 1, close - i - 1)) + "</i>";
                i = close + 1;
                continue;
            }
        }
        result += html(std::string(1, c));
        ++i;
    }
    return result;
}
// A text block: paragraphs, with "- item" lines (continued by indented lines) as <ul> lists.
std::vector<std::string> text_blocks(const std::string& text) {
    std::vector<std::string> blocks;
    std::string paragraph;
    std::vector<std::string> items;
    const auto flush_paragraph = [&] { if (!trim(paragraph).empty()) blocks.push_back("<p>" + inline_doc(trim(paragraph))); paragraph.clear(); };
    const auto flush_list = [&] {
        if (items.empty()) return;
        std::string list = "<ul>";
        for (const auto& item : items) list += "\n<li>" + inline_doc(trim(item)) + "</li>";
        blocks.push_back(list + "\n</ul>");
        items.clear();
    };
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("- ", 0) == 0) { flush_paragraph(); items.push_back(line.substr(2)); continue; }
        if (!items.empty() && line.rfind("  ", 0) == 0) { items.back() += "\n" + trim(line); continue; }
        flush_list();
        if (trim(line).empty()) { flush_paragraph(); continue; }
        paragraph += (paragraph.empty() ? "" : "\n") + line;
    }
    flush_paragraph();
    flush_list();
    return blocks;
}
std::string code_block(const std::string& text) {
    bool plain = balanced(text) && text.find("*/") == std::string::npos;
    std::istringstream check(text);
    for (std::string line; std::getline(check, line);) if (!trim(line).empty() && trim(line)[0] == '@') plain = false;
    if (plain) return "<pre>{@code\n" + text + "\n}</pre>";
    return "<pre>\n" + replace_all(replace_all(html(text), "{", "&#123;"), "}", "&#125;") + "\n</pre>";
}
// Javadoc's summary is its first sentence: the brief, or a fallback.
struct JDoc {
    std::string summary;
    std::vector<std::string> body;                                 // blocks: <p>..., <pre>..., <ul>...
    std::vector<std::pair<std::string, std::string>> params;      // name (or <T>), text
    std::string returns;
    std::vector<std::pair<std::string, std::string>> throws_;     // class, text
    std::vector<std::string> see;
    std::string since;
    std::string deprecated;
    bool is_deprecated = false;
    std::string render(const std::string& indent) const {
        std::vector<std::string> lines;
        const auto add = [&](const std::string& block) {
            std::istringstream split(block);
            for (std::string line; std::getline(split, line);) lines.push_back(line);
        };
        add(summary.empty() ? "Native binding." : summary);
        for (const auto& block : body) { lines.push_back(""); add(block); }
        bool tags = false;
        const auto tag = [&](const std::string& text) {
            if (!tags) { lines.push_back(""); tags = true; }
            add(text);
        };
        for (const auto& [name, text] : params) tag("@param " + name + " " + (text.empty() ? "the C++ argument" : text));
        if (!returns.empty()) tag("@return " + returns);
        for (const auto& [cls, text] : throws_) tag("@throws " + cls + (text.empty() ? "" : " " + text));
        for (const auto& item : see) tag("@see " + item);
        if (!since.empty()) tag("@since " + since);
        if (is_deprecated) tag("@deprecated " + (deprecated.empty() ? std::string("Deprecated in C++.") : deprecated));
        std::string out = indent + "/**\n";
        for (const auto& line : lines) out += indent + " *" + (line.empty() ? "" : " " + line) + "\n";
        return out + indent + " */\n";
    }
};

// The Java helper types the generated code may need: name -> generated.
struct Helpers {
    std::set<std::size_t> tuples;     // sizes of TupleN records (2 is Pair)
    std::set<std::size_t> functions;  // arities of generated FunctionN interfaces
    std::set<std::size_t> consumers;  // arities of generated ConsumerN interfaces
};

// A Java type for a C++ type, and how to convert a decoded value (an Object expression in place
// of \x01) to it.
struct JV {
    std::string type;    // as declared
    std::string boxed;   // as a type argument
    std::string conv;    // template: \x01 stands for the decoded value
    std::string prim;    // primitive type name, or ""
    bool is_class = false;
    // std::function: the interface's method, its arguments and result.
    bool function = false;
    std::string method;
    std::vector<JV> arguments;
    std::vector<JV> result;  // one item; its type is "void" for void functions
};
std::string apply_conv(const std::string& conv, const std::string& expression) { return replace_all(conv, "\x01", expression); }

struct Candidate {
    std::string name;
    bool is_static = false;
    bool is_constructor = false;
    std::string access = "public";
    std::string result = "void";        // Java result type
    std::string result_conv;            // template converting the call's result
    std::vector<std::pair<std::string, std::string>> params;  // type, name
    std::vector<std::string> arguments; // expressions passed to the call (after `this` when instance)
    std::vector<std::pair<std::size_t, std::string>> writebacks; // param index, statement
    std::string body;                   // replaces the generated call body when set
    std::string doc;
    std::vector<std::string> annotations;
    std::size_t index = 0;              // native function
    std::string suffix = "_";           // appended on a name collision
    bool instance_call = false;         // passes `this` first
    bool merged = false;
    std::string full_key() const {
        std::string key = name + "(";
        for (const auto& [type, n] : params) key += type + ",";
        return key + ")";
    }
};
// A type without its type arguments (what the JVM sees).
std::string erasure(const std::string& type) {
    std::string result;
    int depth = 0;
    for (const char c : type) {
        if (c == '<') { ++depth; continue; }
        if (c == '>') { --depth; continue; }
        if (depth == 0) result += c;
    }
    return result;
}
std::string erasure_key(const Candidate& c) {
    std::string key = c.name + "(";
    for (const auto& [type, n] : c.params) key += erasure(type) + ",";
    return key + ")";
}

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const JavaFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedJavaFullSource run();

private:
    JavaFullOptions options_;
    std::string package_;
    std::string functions_class_;
    std::set<std::string> types_;             // every type name of the package
    // Context of the file being written: the prefix of package types, and the names hiding JDK ones.
    std::string prefix_;
    const std::set<std::string>* shadow_ = nullptr;
    bool qualify_all_ = false;
    mutable Helpers helpers_;
    std::map<std::string, std::string> helper_names_;  // Pair, Tuple3, Function3, Ref, Complex ... -> spelling
    std::map<const Function*, std::string> java_names_; // full-arity Java name of each bound C++ method
    std::map<std::string, std::string> class_files_;    // owner -> source text
    std::set<std::string> comparable_, iterable_;        // classes implementing Comparable / Iterable

    std::string language() const override { return "Java"; }
    std::string target_name(const std::string& cpp) const override { return escaped(cpp); }
    std::string method_name(const std::string& cpp) const override {
        auto name = camel(cpp);
        static const std::set<std::string> reserved = {"getClass", "wait", "notify", "notifyAll", "finalize", "close", "isClosed"};
        if (reserved.count(name) || java_keyword(name)) name += "_";
        return name;
    }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override {
        const auto name = cpp.find("__") == std::string::npos ? cpp : pascal(cpp);
        return java_keyword(name) || restricted_type(name) ? name + "_" : name;
    }
    std::string rename_target(const std::string& name, const std::string&) const override { return escaped(name); }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override {
        static const std::map<std::string, std::pair<const char*, const char*>> names = {
            {"operator+", {"plus", "plus"}}, {"operator-", {"negate", "minus"}}, {"operator*", {nullptr, "times"}},
            {"operator/", {nullptr, "div"}}, {"operator%", {nullptr, "rem"}}, {"operator==", {nullptr, "equals"}},
            {"operator!=", {nullptr, "notEquals"}}, {"operator<", {nullptr, "lessThan"}}, {"operator<=", {nullptr, "lessEqual"}},
            {"operator>", {nullptr, "greaterThan"}}, {"operator>=", {nullptr, "greaterEqual"}}, {"operator&", {nullptr, "and"}},
            {"operator|", {nullptr, "or"}}, {"operator^", {nullptr, "xor"}}, {"operator~", {"not", nullptr}},
            {"operator!", {"logicalNot", nullptr}}, {"operator[]", {nullptr, "get"}}};
        if (cpp == "operator()") return "call";
        const auto found = names.find(cpp);
        if (found == names.end() || arity > 1) return {};
        const char* result = arity == 0 ? found->second.first : found->second.second;
        return result ? result : "";
    }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return upper_snake(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken{"rosettaResult", "rosettaArgs", "yield"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : escaped(camel(cpp));
            if (name.rfind("rosetta", 0) == 0) name += "_";
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + plain_signature(overload, group.name) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    new " + plain_signature(overload, plan.name) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override { return plan.name + " is abstract: extend it with a Java class"; }
    std::string protected_message(const ClassPlan& plan) const override { return plan.name + " constructor is protected: call it from a Java subclass"; }
    std::string symbol_prefix() const override { return "rosetta_java_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_JAVA"; }
    std::string module_prefix() const override { return "rosetta_java_module_"; }

    // ------------------------------------------------------------ names in the current file
    std::string jl(const std::string& name) const { return qualify_all_ || (shadow_ && shadow_->count(name)) ? "java.lang." + name : name; }
    std::string ju(const std::string& name) const { return qualify_all_ || (shadow_ && shadow_->count(name)) ? "java.util." + name : name; }
    std::string juf(const std::string& name) const { return qualify_all_ || (shadow_ && shadow_->count(name)) ? "java.util.function." + name : name; }
    std::string pt(const std::string& name) const { return prefix_ + name; }  // a type of the package
    std::string helper(const std::string& name) const;

    // ------------------------------------------------------------ types
    JV java(const Type& type, bool parameter, int depth = 0) const;
    JV prim(const std::string& name) const;
    JV function_type(const Type& function, int depth) const;
    Type spelled(std::string cpp) const;
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    std::string parameter_type(const Type& type) const {
        if (writeback(type)) return pt(helper("Ref")) + "<" + java(type, false).boxed + ">";
        return java(type, true).type;
    }
    // The expression passing a parameter to C++ (a Java function becomes a Rosetta.Callback).
    std::string argument(const Type& type, const std::string& name) const;
    std::string callback(const JV& f, const std::string& name) const;
    std::string plain_signature(const Overload& overload, const std::string& name) const;
    std::string cpp_signature(const Function& function) const;
    std::string java_default(const DefaultValue& value, const Type& type) const;

    // ------------------------------------------------------------ documentation
    std::string exception_class(const std::string& cpp) const;
    JDoc describe(const Documentation& doc, const std::string& fallback) const;
    JDoc overload_doc(const Overload& overload, std::size_t arity, const std::vector<std::string>& names) const;
    std::string see_target(const std::string& target) const;
    std::set<std::string> function_names_;  // Java names of the main module's functions

    // ------------------------------------------------------------ members
    struct Inherited { std::string full, result; bool is_static, is_final; };
    std::map<std::string, std::map<std::string, Inherited>> members_;  // class -> erasure key -> member
    std::map<std::string, std::vector<Candidate>> class_members_;      // class -> members (emission order)
    const Inherited* inherited(const std::string& owner, const std::string& key) const;
    bool result_compatible(const std::string& derived, const std::string& base) const;
    bool add_member(const std::string& owner, std::vector<Candidate>& list, Candidate candidate);
    void merge(Candidate& into, const Candidate& other) const;
    void add_overloads(const std::string& owner, std::vector<Candidate>& list, const ClassPlan* plan, const Group& group, bool is_method);
    std::string emit_candidate(const Candidate& c, const std::string& class_name) const;
    std::string emit_class(const std::string& owner);
    std::string emit_enum(const std::string& qualified, const std::string& name);
    std::string file_head(const std::string& package) const;
};

std::string Generator::helper(const std::string& name) const {
    const auto found = helper_names_.find(name);
    if (found != helper_names_.end()) return found->second;
    if (name == "Pair") helpers_.tuples.insert(2);
    else if (name.rfind("Tuple", 0) == 0) helpers_.tuples.insert(std::stoul(name.substr(5)));
    else if (name.rfind("Function", 0) == 0) helpers_.functions.insert(std::stoul(name.substr(8)));
    else if (name.rfind("Consumer", 0) == 0) helpers_.consumers.insert(std::stoul(name.substr(8)));
    auto& names = const_cast<std::map<std::string, std::string>&>(helper_names_);
    auto& types = const_cast<std::set<std::string>&>(types_);
    auto spelling = types.count(name) ? "Native" + name : name;
    while (types.count(spelling)) spelling += "_";
    types.insert(spelling);
    names[name] = spelling;
    return spelling;
}

JV Generator::prim(const std::string& name) const {
    static const std::map<std::string, std::pair<std::string, std::string>> table = {
        {"boolean", {"Boolean", "asBoolean"}}, {"byte", {"Byte", "asByte"}}, {"short", {"Short", "asShort"}},
        {"int", {"Integer", "asInt"}}, {"long", {"Long", "asLong"}}, {"float", {"Float", "asFloat"}},
        {"double", {"Double", "asDouble"}}, {"char", {"Character", "asChar"}}};
    const auto& entry = table.at(name);
    JV v;
    v.type = name;
    v.boxed = jl(entry.first);
    v.conv = pt("Rosetta") + "." + entry.second + "(\x01)";
    v.prim = name;
    return v;
}

Type Generator::spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (cpp.rfind("const ", 0) == 0) cpp = trim(cpp.substr(6));
    if (cpp.rfind("::", 0) == 0) cpp = cpp.substr(2);
    Type type;
    type.spelling = type.canonical = cpp;
    static const std::map<std::string, std::string> aliases = {{"std::size_t", "unsigned long"}, {"size_t", "unsigned long"},
        {"std::int64_t", "long"}, {"std::uint64_t", "unsigned long"}, {"std::int32_t", "int"}, {"std::uint32_t", "unsigned int"},
        {"std::int16_t", "short"}, {"std::uint16_t", "unsigned short"}, {"std::int8_t", "signed char"}, {"std::uint8_t", "unsigned char"}};
    if (const auto alias = aliases.find(cpp); alias != aliases.end()) cpp = alias->second;
    static const std::set<std::string> builtins = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short", "int",
        "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double", "long double"};
    if (builtins.count(cpp)) { type.kind = TypeKind::builtin; type.canonical = cpp; return type; }
    if (cpp == "std::string" || cpp == "std::string_view") { type.kind = TypeKind::template_instance; type.declaration = "std::basic_string"; return type; }
    if (cpp == "std::filesystem::path") { type.kind = TypeKind::record; type.declaration = cpp; return type; }
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') { type.kind = TypeKind::unresolved; return type; }
    type.kind = TypeKind::template_instance;
    type.declaration = cpp.substr(0, open);
    int depth = 0;
    std::string current;
    for (std::size_t i = open + 1; i + 1 < cpp.size(); ++i) {
        const char c = cpp[i];
        if (c == '<' || c == '(') ++depth;
        if (c == '>' || c == ')') --depth;
        if (c == ',' && depth == 0) { type.arguments.push_back(spelled(current)); current.clear(); continue; }
        current += c;
    }
    type.arguments.push_back(spelled(current));
    return type;
}

// A std::function<R(A...)> as a java.util.function interface (or a generated one).
JV Generator::function_type(const Type& function, int depth) const {
    JV v;
    v.function = true;
    const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
    JV result;
    if (returns) result = java(function.arguments[0], false, depth + 1);
    else result.type = result.boxed = "void";
    for (std::size_t i = 1; i < function.arguments.size(); ++i) v.arguments.push_back(java(function.arguments[i], false, depth + 1));
    v.result.push_back(result);
    const auto n = v.arguments.size();
    const auto a = [&](std::size_t i) { return v.arguments[i].prim; };
    const auto& r = result.prim;
    std::string boxes;
    for (const auto& argument : v.arguments) boxes += (boxes.empty() ? "" : ", ") + argument.boxed;
    const auto generic = [&](const std::string& name, const std::string& extra) {
        return juf(name) + "<" + boxes + (extra.empty() ? "" : (boxes.empty() ? "" : ", ") + extra) + ">";
    };
    if (n == 0) {
        if (!returns) { v.type = jl("Runnable"); v.method = "run"; }
        else if (r == "double") { v.type = juf("DoubleSupplier"); v.method = "getAsDouble"; }
        else if (r == "int") { v.type = juf("IntSupplier"); v.method = "getAsInt"; }
        else if (r == "long") { v.type = juf("LongSupplier"); v.method = "getAsLong"; }
        else if (r == "boolean") { v.type = juf("BooleanSupplier"); v.method = "getAsBoolean"; }
        else { v.type = juf("Supplier") + "<" + result.boxed + ">"; v.method = "get"; }
    } else if (n == 1) {
        if (!returns) {
            if (a(0) == "double") { v.type = juf("DoubleConsumer"); v.method = "accept"; }
            else if (a(0) == "int") { v.type = juf("IntConsumer"); v.method = "accept"; }
            else if (a(0) == "long") { v.type = juf("LongConsumer"); v.method = "accept"; }
            else { v.type = generic("Consumer", ""); v.method = "accept"; }
        } else if (a(0) == "double" && r == "double") { v.type = juf("DoubleUnaryOperator"); v.method = "applyAsDouble"; }
        else if (a(0) == "int" && r == "int") { v.type = juf("IntUnaryOperator"); v.method = "applyAsInt"; }
        else if (a(0) == "long" && r == "long") { v.type = juf("LongUnaryOperator"); v.method = "applyAsLong"; }
        else if (r == "boolean") {
            if (a(0) == "double") { v.type = juf("DoublePredicate"); }
            else if (a(0) == "int") { v.type = juf("IntPredicate"); }
            else if (a(0) == "long") { v.type = juf("LongPredicate"); }
            else v.type = generic("Predicate", "");
            v.method = "test";
        } else if (a(0) == "double" && r == "int") { v.type = juf("DoubleToIntFunction"); v.method = "applyAsInt"; }
        else if (a(0) == "double" && r == "long") { v.type = juf("DoubleToLongFunction"); v.method = "applyAsLong"; }
        else if (a(0) == "int" && r == "double") { v.type = juf("IntToDoubleFunction"); v.method = "applyAsDouble"; }
        else if (a(0) == "int" && r == "long") { v.type = juf("IntToLongFunction"); v.method = "applyAsLong"; }
        else if (a(0) == "long" && r == "double") { v.type = juf("LongToDoubleFunction"); v.method = "applyAsDouble"; }
        else if (a(0) == "long" && r == "int") { v.type = juf("LongToIntFunction"); v.method = "applyAsInt"; }
        else if (r == "double") { v.type = generic("ToDoubleFunction", ""); v.method = "applyAsDouble"; }
        else if (r == "int") { v.type = generic("ToIntFunction", ""); v.method = "applyAsInt"; }
        else if (r == "long") { v.type = generic("ToLongFunction", ""); v.method = "applyAsLong"; }
        else if (a(0) == "double") { v.type = juf("DoubleFunction") + "<" + result.boxed + ">"; v.method = "apply"; }
        else if (a(0) == "int") { v.type = juf("IntFunction") + "<" + result.boxed + ">"; v.method = "apply"; }
        else if (a(0) == "long") { v.type = juf("LongFunction") + "<" + result.boxed + ">"; v.method = "apply"; }
        else { v.type = generic("Function", result.boxed); v.method = "apply"; }
    } else if (n == 2) {
        if (!returns) {
            if (a(1) == "double" && a(0) != "double" && a(0) != "int" && a(0) != "long" && a(0).empty()) { v.type = juf("ObjDoubleConsumer") + "<" + v.arguments[0].boxed + ">"; v.method = "accept"; }
            else { v.type = generic("BiConsumer", ""); v.method = "accept"; }
        } else if (a(0) == "double" && a(1) == "double" && r == "double") { v.type = juf("DoubleBinaryOperator"); v.method = "applyAsDouble"; }
        else if (a(0) == "int" && a(1) == "int" && r == "int") { v.type = juf("IntBinaryOperator"); v.method = "applyAsInt"; }
        else if (a(0) == "long" && a(1) == "long" && r == "long") { v.type = juf("LongBinaryOperator"); v.method = "applyAsLong"; }
        else if (r == "boolean") { v.type = generic("BiPredicate", ""); v.method = "test"; }
        else if (r == "double") { v.type = generic("ToDoubleBiFunction", ""); v.method = "applyAsDouble"; }
        else if (r == "int") { v.type = generic("ToIntBiFunction", ""); v.method = "applyAsInt"; }
        else if (r == "long") { v.type = generic("ToLongBiFunction", ""); v.method = "applyAsLong"; }
        else { v.type = generic("BiFunction", result.boxed); v.method = "apply"; }
    } else {
        if (!returns) { v.type = pt(helper("Consumer" + std::to_string(n))) + "<" + boxes + ">"; v.method = "accept"; }
        else { v.type = pt(helper("Function" + std::to_string(n))) + "<" + boxes + ", " + result.boxed + ">"; v.method = "apply"; }
    }
    v.boxed = v.type;
    // A returned C++ function, wrapped in the interface.
    const auto f = "rosettaF" + std::to_string(depth);
    std::string parameters, passed;
    for (std::size_t i = 0; i < n; ++i) {
        const auto x = "rosettaX" + std::to_string(depth) + "_" + std::to_string(i);
        parameters += (i ? ", " : "") + x;
        passed += (i ? ", " : "") + x;
    }
    const auto invoke = f + ".invoke(" + passed + ")";
    const auto body = returns ? apply_conv(result.conv, invoke) : "{ " + invoke + "; }";
    v.conv = pt("Rosetta") + ".function(\x01, " + f + " -> (" + v.type + ") (" + parameters + ") -> " + body + ")";
    return v;
}

JV Generator::java(const Type& type, bool parameter, int depth) const {
    const auto& target = referent(type);
    const auto object = [&] {
        JV v;
        v.type = v.boxed = jl("Object");
        v.conv = pt("Rosetta") + ".plain(\x01)";
        return v;
    };
    const auto named = [&](const std::string& name, const std::string& conv) {
        JV v;
        v.type = v.boxed = name;
        v.conv = conv;
        return v;
    };
    const auto var = "rosettaV" + std::to_string(depth);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return named(jl("String"), pt("Rosetta") + ".asString(\x01)");
        auto v = java(pointee, parameter, depth);
        if (!v.prim.empty()) { v.type = v.boxed; v.prim.clear(); }
        return v;
    }
    if (is_void(target)) return named("void", "\x01");
    if (!conversion(target).empty()) return java(spelled(policy_.conversions.at(conversion(target))), parameter, depth);
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        static const std::map<std::string, std::string> builtins = {{"bool", "boolean"}, {"char", "char"}, {"signed char", "byte"},
            {"unsigned char", "short"}, {"short", "short"}, {"unsigned short", "int"}, {"int", "int"}, {"unsigned int", "long"},
            {"long", "long"}, {"unsigned long", "long"}, {"long long", "long"}, {"unsigned long long", "long"}, {"float", "float"},
            {"double", "double"}, {"long double", "double"}, {"char16_t", "char"}, {"char32_t", "int"}, {"wchar_t", "int"}};
        const auto found = builtins.find(bare);
        return found != builtins.end() ? prim(found->second) : object();
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return prim("long");
        return named(pt(found->second), pt("Rosetta") + ".asEnum(\x01, " + pt(found->second) + ".class)");
    }
    if (bound(target)) {
        auto v = named(pt(classes_.at(target.declaration).name), pt("Rosetta") + ".asObject(\x01, " + pt(classes_.at(target.declaration).name) + ".class)");
        v.is_class = true;
        return v;
    }
    if (bare == "std::filesystem::path" || bare == "std::filesystem::__cxx11::path") return named(jl("String"), pt("Rosetta") + ".asString(\x01)");
    if (bare == "std::nullptr_t" || bare == "std::monostate") return object();
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? java(target.arguments[i], false, depth + 1) : object(); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return named(jl("String"), pt("Rosetta") + ".asString(\x01)");
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray") {
            const auto element = arg(0);
            if (element.prim == "double") return named("double[]", pt("Rosetta") + ".asDoubles(\x01)");
            if (element.prim == "float") return named("float[]", pt("Rosetta") + ".asFloats(\x01)");
            if (element.prim == "int") return named("int[]", pt("Rosetta") + ".asInts(\x01)");
            if (element.prim == "long") return named("long[]", pt("Rosetta") + ".asLongs(\x01)");
            if (!target.arguments.empty() && normalize(strip_cv(target.arguments[0].canonical)) == "unsigned char")
                return named("byte[]", pt("Rosetta") + ".asBytes(\x01)");
            const auto shown = parameter && element.is_class ? "? extends " + element.boxed : element.boxed;
            return named(ju("List") + "<" + shown + ">", pt("Rosetta") + ".asList(\x01, " + var + " -> " + apply_conv(element.conv, var) + ")");
        }
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset") {
            const auto element = arg(0);
            const auto shown = parameter && element.is_class ? "? extends " + element.boxed : element.boxed;
            return named(ju("Set") + "<" + shown + ">", pt("Rosetta") + ".asSet(\x01, " + var + " -> " + apply_conv(element.conv, var) + ")");
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") {
            const auto key = arg(0), value = arg(1);
            const auto other = "rosettaW" + std::to_string(depth);
            return named(ju("Map") + "<" + key.boxed + ", " + value.boxed + ">",
                         pt("Rosetta") + ".asMap(\x01, " + var + " -> " + apply_conv(key.conv, var) + ", " + other + " -> " + apply_conv(value.conv, other) + ")");
        }
        if (declaration == "std::optional") {
            const auto element = arg(0);
            if (element.prim == "double") return named(ju("OptionalDouble"), pt("Rosetta") + ".asOptionalDouble(\x01)");
            if (element.prim == "int") return named(ju("OptionalInt"), pt("Rosetta") + ".asOptionalInt(\x01)");
            if (element.prim == "long") return named(ju("OptionalLong"), pt("Rosetta") + ".asOptionalLong(\x01)");
            return named(ju("Optional") + "<" + element.boxed + ">", pt("Rosetta") + ".asOptional(\x01, " + var + " -> " + apply_conv(element.conv, var) + ")");
        }
        if (declaration == "std::complex") return named(pt(helper("Complex")), pt("Rosetta") + ".asComplex(\x01)");
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            auto v = arg(0);
            if (!v.prim.empty()) { v.type = v.boxed; v.prim.clear(); }
            return v;
        }
        if (declaration == "std::pair" || declaration == "std::tuple") {
            const auto count = target.arguments.size();
            if (count == 0 || count > 8) return object();
            const auto record = pt(helper(count == 2 ? std::string("Pair") : "Tuple" + std::to_string(count)));
            std::string arguments, items;
            const auto t = "rosettaT" + std::to_string(depth);
            for (std::size_t i = 0; i < count; ++i) {
                const auto item = java(target.arguments[i], false, depth + 1);
                arguments += (i ? ", " : "") + item.boxed;
                items += (i ? ", " : "") + apply_conv(item.conv, t + ".get(" + std::to_string(i) + ")");
            }
            const auto full = record + "<" + arguments + ">";
            return named(full, pt("Rosetta") + ".tuple(\x01, " + std::to_string(count) + ", " + t + " -> new " + full + "(" + items + "))");
        }
        if (declaration == "std::variant") return object();
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function)
            return function_type(target.arguments[0], depth);
        if (declaration == "std::chrono::duration") return prim("double");
    }
    return object();
}

std::string Generator::callback(const JV& f, const std::string& name) const {
    std::string arguments;
    for (std::size_t i = 0; i < f.arguments.size(); ++i)
        arguments += (i ? ", " : "") + apply_conv(f.arguments[i].conv, "rosettaArgs[" + std::to_string(i) + "]");
    const auto call = name + "." + f.method + "(" + arguments + ")";
    const bool returns = f.result.front().type != "void";
    return "(" + name + " == null ? null : (" + pt("Rosetta") + ".Callback) rosettaArgs -> { " + pt("Rosetta") + ".arity(rosettaArgs, " +
           std::to_string(f.arguments.size()) + "); " + (returns ? "return " + call + ";" : call + "; return null;") + " })";
}

std::string Generator::argument(const Type& type, const std::string& name) const {
    if (writeback(type)) return name;
    const auto v = java(type, true);
    if (v.function) return callback(v, name);
    return name;
}

std::string Generator::plain_signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::string result = name + "(";
    for (std::size_t i = first; i < function.parameters.size(); ++i)
        result += (i > first ? ", " : "") + parameter_type(function.parameters[i]) + " " + names[i - first];
    return result + ")";
}

std::string Generator::cpp_signature(const Function& function) const {
    std::string text = function.is_constructor ? std::string() : function.result.spelling + " ";
    text += function.qualified_name() + "(";
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        text += (i ? ", " : "") + function.parameters[i].spelling;
        const auto& cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (!cpp.empty()) text += " " + cpp;
        if (i < function.default_values.size() && !function.default_values[i].kind.empty()) {
            const auto& value = function.default_values[i];
            text += " = " + (value.kind == "char" ? "'" + value.value + "'" : value.kind == "default" ? std::string("{}") : value.value);
        } else if (i + function.default_arguments >= function.parameters.size()) text += " = ...";
    }
    text += ")";
    if (function.is_const) text += " const";
    return text;
}

std::string Generator::java_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant" || value.kind == "string") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "null";
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "enum") {
        const auto found = enum_names_.find(referent(type).declaration);
        return found != enum_names_.end() ? found->second + "." + escaped(value.value) : value.value;
    }
    if (value.kind == "default") return "default";
    return {};
}

std::string Generator::exception_class(const std::string& cpp) const {
    const auto name = normalize(cpp);
    if (name == "std::invalid_argument") return pt("NativeError.InvalidArgument");
    if (name == "std::out_of_range") return pt("NativeError.OutOfRange");
    if (name == "std::domain_error" || name == "std::overflow_error" || name == "std::underflow_error" || name == "std::range_error")
        return pt("NativeError.Arithmetic");
    return pt("NativeException");
}

std::string Generator::see_target(const std::string& target) const {
    auto name = trim(target);
    while (!name.empty() && (name.back() == '.' || name.back() == ',')) name.pop_back();
    if (name.size() > 2 && name.compare(name.size() - 2, 2, "()") == 0) name = name.substr(0, name.size() - 2);
    for (const auto& owner : class_order_)
        if (classes_.at(owner).name == name || owner == name || normalize(owner) == normalize(name)) return pt(classes_.at(owner).name);
    for (const auto& [qualified, java_name] : enum_names_)
        if (java_name == name || qualified == name) return pt(java_name);
    const auto method = method_name(name);
    if (prefix_.empty() && function_names_.count(method)) return functions_class_ + "#" + method;
    return "\"" + replace_all(html(name), "\"", "&quot;") + "\"";
}

JDoc Generator::describe(const Documentation& doc, const std::string& fallback) const {
    JDoc result;
    std::vector<std::string> body;
    bool first_text = true;
    for (const auto& block : doc.details) {
        if (block.kind == "code" || block.kind == "math") { body.push_back(code_block(block.text)); continue; }
        if (block.kind == "heading") { body.push_back("<p><b>" + inline_doc(block.text) + "</b>"); continue; }
        auto blocks = text_blocks(block.text);
        if (first_text && doc.brief.empty() && !blocks.empty() && blocks.front().rfind("<p>", 0) == 0) {
            result.summary = blocks.front().substr(3);
            blocks.erase(blocks.begin());
        }
        first_text = false;
        body.insert(body.end(), blocks.begin(), blocks.end());
    }
    if (!doc.brief.empty()) result.summary = inline_doc(trim(doc.brief));
    if (result.summary.empty()) result.summary = fallback;
    result.body = body;
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { result.is_deprecated = true; result.deprecated = inline_doc(one_line(item.text)); continue; }
        if (item.name == "since") { result.since = inline_doc(one_line(item.text)); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        result.body.push_back("<p><b>" + html(label) + ":</b> " + inline_doc(trim(item.text)));
    }
    for (const auto& item : doc.see) result.see.push_back(see_target(item));
    for (const auto& item : doc.throws)
        result.throws_.emplace_back(exception_class(item.name), inline_doc(one_line(item.text)) +
            (item.name.empty() ? "" : std::string(item.text.empty() ? "" : " ") + "(C++ " + code_span(item.name) + ")"));
    return result;
}

// The documentation of one Java overload: the C++ function's, with its first `arity` parameters.
JDoc Generator::overload_doc(const Overload& overload, std::size_t arity, const std::vector<std::string>& names) const {
    const auto& function = *overload.function;
    auto doc = describe(function.documentation, "C++ " + code_span(function.qualified_name()) + ".");
    const std::size_t first = overload.receives_self ? 1 : 0;
    std::string omitted;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : function.documentation.params) if (item.name == cpp && !cpp.empty()) text = inline_doc(one_line(item.text));
        const auto name = i - first < names.size() ? names[i - first] : "arg" + std::to_string(i);
        if (i < arity) {
            if (text.empty()) text = "the C++ argument " + code_span(cpp.empty() ? name : cpp);
            doc.params.emplace_back(name, text);
            continue;
        }
        const auto shown = java_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        omitted += (omitted.empty() ? "" : ", ") + code_span(name + (shown.empty() ? "" : " = " + shown));
    }
    if (!omitted.empty()) doc.body.push_back("<p>Uses the C++ default for " + omitted + ".");
    doc.body.push_back("<p>C++: " + code_span(cpp_signature(function)));
    if (!function.is_constructor && !is_void(function.result))
        doc.returns = function.documentation.returns.empty() ? "the result" : inline_doc(one_line(function.documentation.returns));
    return doc;
}

// ------------------------------------------------------------ members and collisions

const Generator::Inherited* Generator::inherited(const std::string& owner, const std::string& key) const {
    for (auto cls = owner; !classes_.at(cls).bases.empty();) {
        cls = classes_.at(cls).bases.front();
        const auto found = members_.find(cls);
        if (found == members_.end()) continue;
        const auto member = found->second.find(key);
        if (member != found->second.end()) return &member->second;
    }
    // java.lang.Object and NativeObject.
    static const std::map<std::string, Inherited> object = {
        {"toString()", {"toString()", "String", false, false}}, {"hashCode()", {"hashCode()", "int", false, false}},
        {"equals(Object,)", {"equals(Object,)", "boolean", false, false}}, {"clone()", {"clone()", "Object", false, false}},
        {"finalize()", {"finalize()", "void", false, true}}, {"getClass()", {"getClass()", "Class", false, true}},
        {"notify()", {"notify()", "void", false, true}}, {"notifyAll()", {"notifyAll()", "void", false, true}},
        {"wait()", {"wait()", "void", false, true}}, {"wait(long,)", {"wait(long,)", "void", false, true}},
        {"wait(long,int,)", {"wait(long,int,)", "void", false, true}}, {"close()", {"close()", "void", false, true}},
        {"isClosed()", {"isClosed()", "boolean", false, true}}};
    const auto found = object.find(replace_all(key, "java.lang.", ""));
    return found == object.end() ? nullptr : &found->second;
}

bool Generator::result_compatible(const std::string& derived, const std::string& base) const {
    if (derived == base) return true;
    const auto plain = [](const std::string& text) { return replace_all(text, "java.lang.", ""); };
    if (plain(derived) == plain(base)) return true;
    static const std::set<std::string> primitives = {"void", "boolean", "byte", "short", "int", "long", "float", "double", "char"};
    if (plain(base) == "Object") return !primitives.count(derived);
    const auto class_named = [&](const std::string& name) {
        for (const auto& cls : class_order_) if (classes_.at(cls).name == name) return cls;
        return std::string();
    };
    const auto d = class_named(derived), b = class_named(base);
    return !d.empty() && !b.empty() && primary_derives(d, b);
}

void Generator::merge(Candidate& into, const Candidate& other) const {
    for (std::size_t i = 0; i < into.params.size(); ++i)
        if (into.params[i].first != other.params[i].first) into.params[i].first = jl("Object");
    if (into.result != other.result) {
        into.result = jl("Object");
        into.result_conv = pt("Rosetta") + ".plain(\x01)";
    }
    std::vector<std::pair<std::size_t, std::string>> kept;
    for (const auto& writeback : into.writebacks)
        if (into.params[writeback.first].first != jl("Object")) kept.push_back(writeback);
    into.writebacks = kept;
    // An argument of a merged parameter goes as it is (C++ resolves the overload by its value).
    for (std::size_t i = 0; i < into.params.size(); ++i)
        if (into.params[i].first == jl("Object")) into.arguments[i] = into.params[i].second;
    into.merged = true;
}

// Adds a member unless Java would reject it: duplicates are dropped, overloads Java's erasure
// cannot tell apart (of one native function) are merged into one taking Object, and a name taken
// by another member, an inherited one or a final method of Object/NativeObject gets a suffix.
bool Generator::add_member(const std::string& owner, std::vector<Candidate>& list, Candidate candidate) {
    auto& own = members_[owner];
    for (int guard = 0; guard < 64; ++guard) {
        const auto key = erasure_key(candidate);
        const auto* base = owner.empty() ? nullptr : inherited(owner, key);
        if (owner.empty()) {
            // Module functions: only Object's methods are inherited.
            static const std::set<std::string> object = {"toString()", "hashCode()", "equals(Object,)", "clone()", "finalize()",
                "getClass()", "notify()", "notifyAll()", "wait()", "wait(long,)", "wait(long,int,)"};
            if (object.count(replace_all(key, "java.lang.", ""))) { candidate.name += candidate.suffix; continue; }
        }
        if (base && !candidate.is_constructor) {
            const bool same = base->full == replace_all(candidate.full_key(), "java.lang.", "") || base->full == candidate.full_key();
            if (base->is_final || base->is_static != candidate.is_static || !same || !result_compatible(candidate.result, base->result)) {
                candidate.name += candidate.suffix;
                continue;
            }
            if (!candidate.is_static && std::find(candidate.annotations.begin(), candidate.annotations.end(), "@" + jl("Override")) == candidate.annotations.end())
                candidate.annotations.push_back("@" + jl("Override"));
        }
        const auto found = std::find_if(list.begin(), list.end(), [&](const Candidate& c) { return c.is_constructor == candidate.is_constructor && erasure_key(c) == key; });
        if (found != list.end()) {
            if (found->index == candidate.index && found->is_static == candidate.is_static && found->body.empty() && candidate.body.empty()) {
                if (found->full_key() == candidate.full_key()) return false;
                Candidate merged = *found;
                merge(merged, candidate);
                own.erase(key);
                list.erase(found);
                candidate = merged;
                continue;
            }
            if (candidate.is_constructor) return false;
            candidate.name += candidate.suffix;
            continue;
        }
        if (!candidate.is_constructor) own[key] = {candidate.full_key(), candidate.result, candidate.is_static, false};
        list.push_back(candidate);
        return true;
    }
    return false;
}

// The Java overloads of a group: one per C++ overload and arity (default arguments).
void Generator::add_overloads(const std::string& owner, std::vector<Candidate>& list, const ClassPlan* plan, const Group& group, bool is_method) {
    const bool op = plan && std::find_if(plan->operators.begin(), plan->operators.end(), [&](const Group& g) { return &g == &group; }) != plan->operators.end();
    for (const auto& overload : group.overloads) {
        const auto& function = *overload.function;
        bool instance = plan && is_method && (!overload.extension || overload.receives_self);
        std::size_t first = overload.receives_self ? 1 : 0;
        if (op && overload.extension) {
            // A free operator: an instance method when the class's operand comes first, else static.
            instance = group.operand == 0;
            first = instance ? 1 : 0;
        }
        const std::size_t required = function.parameters.size() - function.default_arguments;
        std::vector<std::string> names;
        if (first == (overload.receives_self ? 1u : 0u)) names = parameter_names(overload);
        else {
            Overload shifted = overload;
            shifted.receives_self = first == 1;
            names = parameter_names(shifted);
        }
        const auto result = java(function.result, false);
        for (std::size_t arity = std::max(required, first); arity <= function.parameters.size(); ++arity) {
            Candidate c;
            c.name = group.name;
            c.is_static = !instance;
            c.instance_call = instance;
            c.index = group.index;
            c.result = is_void(function.result) ? "void" : result.type;
            c.result_conv = result.conv;
            for (std::size_t i = first; i < arity; ++i) {
                const auto& type = function.parameters[i];
                const auto& name = names[i - first];
                c.params.emplace_back(parameter_type(type), name);
                c.arguments.push_back(argument(type, name));
                if (writeback(type)) {
                    const auto value = java(type, false, 1);
                    c.writebacks.emplace_back(i - first, pt("Rosetta") + ".writeback(" + name + ", rosettaV0 -> " + apply_conv(value.conv, "rosettaV0") + ");");
                }
            }
            auto doc = overload_doc(overload, arity, names);
            if (doc.is_deprecated) c.annotations.push_back("@" + jl("Deprecated"));
            // C++ to_string() is Java's toString().
            if (c.name == "toString" && c.params.empty() && c.result != jl("String") && c.result != "String") c.name = "toString_";
            if (c.name == "hashCode" && c.params.empty() && c.result != "int") c.name = "hashCode_";
            if (c.name == "clone" && c.params.empty() && !result.prim.empty()) c.name = "clone_";
            if (c.name == "equals" && !op) c.name = "equals_";
            c.doc = doc.render("    ");
            if (add_member(owner, list, c) && arity == function.parameters.size() && !list.empty() && !op) java_names_[&function] = list.back().name;
        }
    }
}

std::string Generator::emit_candidate(const Candidate& c, const std::string& class_name) const {
    std::ostringstream out;
    out << "\n" << c.doc;
    for (const auto& annotation : c.annotations) out << "    " << annotation << "\n";
    std::string parameters;
    for (std::size_t i = 0; i < c.params.size(); ++i) parameters += (i ? ", " : "") + c.params[i].first + " " + c.params[i].second;
    if (c.is_constructor) {
        out << "    @" << jl("SuppressWarnings") << "(\"this-escape\")\n"
            << "    " << c.access << " " << class_name << "(" << parameters << ") {\n"
            << "        super(RAW);\n";
        std::string arguments;
        for (const auto& argument : c.arguments) arguments += ", " + argument;
        out << "        " << pt("Rosetta") << ".construct(this, " << class_name << ".class, " << c.body << ", " << c.index << arguments << ");\n";
        for (const auto& writeback : c.writebacks) out << "        " << writeback.second << "\n";
        out << "    }\n";
        return out.str();
    }
    out << "    " << c.access << " " << (c.is_static ? "static " : "") << c.result << " " << c.name << "(" << parameters << ") {\n";
    if (!c.body.empty()) out << c.body;
    else {
        std::string arguments = c.instance_call ? "this" : "";
        for (const auto& argument : c.arguments) arguments += (arguments.empty() ? "" : ", ") + argument;
        const auto call = pt("Rosetta") + ".call(" + std::to_string(c.index) + (arguments.empty() ? "" : ", " + arguments) + ")";
        if (c.writebacks.empty()) {
            if (c.result == "void") out << "        " << call << ";\n";
            else out << "        return " << apply_conv(c.result_conv, call) << ";\n";
        } else {
            out << "        " << (c.result == "void" ? "" : "final " + jl("Object") + " rosettaResult = ") << call << ";\n";
            for (const auto& writeback : c.writebacks) out << "        " << writeback.second << "\n";
            if (c.result != "void") out << "        return " << apply_conv(c.result_conv, "rosettaResult") << ";\n";
        }
    }
    out << "    }\n";
    return out.str();
}

std::string Generator::file_head(const std::string& package) const {
    return "// Generated by Rosetta: Java interface to native module " + name_ + " (full API).\npackage " + package +
           ";\n\nimport java.util.*;\nimport java.util.function.*;\n";
}

std::string Generator::emit_class(const std::string& owner) {
    auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    auto& list = class_members_[owner];
    const auto base = plan.bases.empty() ? std::string("NativeObject") : classes_.at(plan.bases.front()).name;
    // Constructors: one per C++ constructor and arity.
    std::vector<Candidate> constructors;
    for (const auto& overload : plan.constructors) {
        const auto& function = *overload.function;
        const auto names = parameter_names(overload);
        const std::size_t required = function.parameters.size() - function.default_arguments;
        for (std::size_t arity = required; arity <= function.parameters.size(); ++arity) {
            Candidate c;
            c.name = plan.name;
            c.is_constructor = true;
            c.access = record.abstract || function.access != "public" ? "protected" : "public";
            c.index = plan.constructor_index;
            c.body = plan.director ? "true" : "false";
            for (std::size_t i = 0; i < arity; ++i) {
                const auto& type = function.parameters[i];
                c.params.emplace_back(parameter_type(type), names[i]);
                c.arguments.push_back(argument(type, names[i]));
                if (writeback(type)) {
                    const auto value = java(type, false, 1);
                    c.writebacks.emplace_back(i, pt("Rosetta") + ".writeback(" + names[i] + ", rosettaV0 -> " + apply_conv(value.conv, "rosettaV0") + ");");
                }
            }
            c.doc = overload_doc(overload, arity, names).render("    ");
            // Erasure collisions merge (the constructor resolves its overload at run time).
            const auto key = erasure_key(c);
            const auto found = std::find_if(constructors.begin(), constructors.end(), [&](const Candidate& o) { return erasure_key(o) == key; });
            if (found == constructors.end()) { constructors.push_back(c); continue; }
            if (found->full_key() == c.full_key()) continue;
            const auto body = found->body;
            found->body.clear();
            merge(*found, c);
            found->body = body;
            if (c.access == "public") found->access = "public";
        }
    }
    // Views of the secondary bases (Java has single inheritance).
    for (const auto& other : class_order_) {
        if (other == owner || !derives(owner, other) || primary_derives(owner, other)) continue;
        if (!plan.bases.empty() && derives(plan.bases.front(), other)) continue;
        const auto& target = classes_.at(other).name;
        Candidate c;
        c.name = "as" + pascal(target);
        c.result = target;
        c.body = "        return " + pt("Rosetta") + ".view(this, " + target + "::new);\n";
        JDoc doc;
        doc.summary = "This object as a {@link " + target + "} (a C++ base class): the same C++ object.";
        doc.returns = "the view";
        c.doc = doc.render("    ");
        c.index = static_cast<std::size_t>(-1);
        add_member(owner, list, c);
    }
    for (const auto& group : plan.methods) add_overloads(owner, list, &plan, group, !group.is_static);
    // Operators; == is equals(Object) with hashCode().
    for (const auto& group : plan.operators) {
        if (group.name != "equals") { add_overloads(owner, list, &plan, group, true); continue; }
        bool boolean = group.operand == 0;
        for (const auto& overload : group.overloads) if (normalize(strip_cv(overload.function->result.canonical)) != "bool") boolean = false;
        if (!boolean) continue;
        Candidate equals;
        equals.name = "equals";
        equals.result = "boolean";
        equals.params.emplace_back(jl("Object"), "other");
        equals.index = group.index;
        equals.body = "        if (other == this) return true;\n        if (other == null || isClosed()) return false;\n        return " + pt("Rosetta") +
                      ".equal(" + std::to_string(group.index) + ", this, other);\n";
        JDoc doc;
        doc.summary = "Compares with the C++ {@code operator==}.";
        doc.params.emplace_back("other", "the object to compare with");
        doc.returns = "whether C++ finds them equal (false when it cannot compare them)";
        doc.body.push_back("<p>C++: " + code_span(cpp_signature(*group.overloads.front().function)));
        equals.doc = doc.render("    ");
        if (!add_member(owner, list, equals) || list.back().name != "equals") continue;
        Candidate hash;
        hash.name = "hashCode";
        hash.result = "int";
        hash.index = group.index;
        hash.body = "        return getClass().getName().hashCode();\n";
        JDoc hash_doc;
        hash_doc.summary = "A hash code consistent with {@link #equals(Object)}: the same for every object of the class.";
        hash_doc.returns = "the class's hash code";
        hash.doc = hash_doc.render("    ");
        add_member(owner, list, hash);
    }
    // Comparable, from operator< on this class.
    bool base_comparable = false, base_iterable = false;
    for (auto cls = owner; !classes_.at(cls).bases.empty();) {
        cls = classes_.at(cls).bases.front();
        base_comparable = base_comparable || comparable_.count(cls);
        base_iterable = base_iterable || iterable_.count(cls);
    }
    std::vector<std::string> interfaces;
    if (!base_comparable) {
        for (const auto& member : list) {
            if (member.name != "lessThan" || member.is_static || member.params.size() != 1 || member.params[0].first != plan.name || member.result != "boolean") continue;
            Candidate compare;
            compare.name = "compareTo";
            compare.result = "int";
            compare.params.emplace_back(plan.name, "other");
            compare.index = member.index;
            compare.body = "        return lessThan(other) ? -1 : other.lessThan(this) ? 1 : 0;\n";
            JDoc doc;
            doc.summary = "Orders with the C++ {@code operator<}.";
            doc.params.emplace_back("other", "the object to compare with");
            doc.returns = "-1, 0 or 1";
            compare.doc = doc.render("    ");
            compare.annotations.push_back("@" + jl("Override"));
            if (add_member(owner, list, compare) && list.back().name == "compareTo") {
                interfaces.push_back(jl("Comparable") + "<" + plan.name + ">");
                comparable_.insert(owner);
            }
            break;
        }
    }
    // Iterable, from size() and operator[] (or at()) taking an index.
    if (!base_iterable) {
        const Candidate* size = nullptr;
        const Candidate* get = nullptr;
        bool clash = false;
        for (const auto& member : list) {
            if (member.is_static) continue;
            if (member.name == "size" && member.params.empty() && (member.result == "int" || member.result == "long")) size = &member;
            if ((member.name == "get" || (member.name == "at" && !get)) && member.params.size() == 1 &&
                (member.params[0].first == "int" || member.params[0].first == "long") && member.result != "void")
                get = &member;
            if (member.name == "iterator" || member.name == "forEach" || member.name == "spliterator") clash = true;
        }
        if (size && get && !clash) {
            const auto element = [&] {
                static const std::map<std::string, std::string> boxes = {{"boolean", "Boolean"}, {"byte", "Byte"}, {"short", "Short"},
                    {"int", "Integer"}, {"long", "Long"}, {"float", "Float"}, {"double", "Double"}, {"char", "Character"}};
                const auto found = boxes.find(get->result);
                return found == boxes.end() ? get->result : jl(found->second);
            }();
            Candidate iterator;
            iterator.name = "iterator";
            iterator.result = ju("Iterator") + "<" + element + ">";
            iterator.index = get->index;
            iterator.body = "        return " + pt("Rosetta") + ".iterator(" + size->name + "(), rosettaI -> " + get->name + "(rosettaI));\n";
            iterator.annotations.push_back("@" + jl("Override"));
            JDoc doc;
            doc.summary = "Iterates over the items, by {@code " + get->name + "} from 0 to {@code " + size->name + "()}.";
            doc.returns = "the iterator";
            iterator.doc = doc.render("    ");
            if (add_member(owner, list, iterator) && list.back().name == "iterator") {
                interfaces.push_back(jl("Iterable") + "<" + element + ">");
                iterable_.insert(owner);
            }
        }
    }
    // Fields: getX/setX.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto value = java(f.type, false);
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        auto about = describe(f.documentation, "C++ field " + code_span(owner + "::" + f.name) + ".");
        Candidate getter;
        getter.name = "get" + pascal(field.name);
        getter.result = value.type;
        getter.result_conv = value.conv;
        getter.index = field.getter_index;
        getter.instance_call = true;
        getter.suffix = "Field";
        auto getter_doc = about;
        getter_doc.returns = "the value of the C++ field " + code_span(f.name);
        getter.doc = getter_doc.render("    ");
        add_member(owner, list, getter);
        if (!writable) continue;
        Candidate setter;
        setter.name = "set" + pascal(field.name);
        setter.index = field.setter_index;
        setter.instance_call = true;
        setter.suffix = "Field";
        setter.params.emplace_back(java(f.type, true).type, "value");
        setter.arguments.push_back(argument(f.type, "value"));
        auto setter_doc = about;
        setter_doc.summary = "Sets: " + about.summary;
        setter_doc.params.emplace_back("value", "the new value");
        setter.doc = setter_doc.render("    ");
        add_member(owner, list, setter);
    }

    // ------------------------------------------------------------ the file
    std::ostringstream out;
    out << file_head(package_);
    auto doc = describe(record.documentation, "C++ class " + code_span(owner) + ".");
    if (plan.bases.size() > 1) {
        std::string also;
        for (std::size_t i = 1; i < plan.bases.size(); ++i) also += (i > 1 ? ", " : "") + std::string("{@link ") + classes_.at(plan.bases[i]).name + "}";
        doc.body.push_back("<p>Also derives (in C++) from " + also + ": the {@code as...} methods view it as them.");
    }
    if (record.abstract) doc.body.push_back(std::string("<p>Abstract in C++: ") + (plan.director ? "extend it in Java and override its abstract methods." : "C++ returns objects of derived classes."));
    else if (plan.director) doc.body.push_back("<p>Java classes extending it override its virtual methods, also when C++ calls them.");
    doc.body.push_back("<p>C++: " + code_span(owner));
    out << "\n" << doc.render("") << "public class " << plan.name << " extends " << base;
    for (std::size_t i = 0; i < interfaces.size(); ++i) out << (i ? ", " : " implements ") << interfaces[i];
    out << " {\n"
        << "    /**\n     * Wraps an existing C++ object (used by the binding).\n     *\n     * @param raw the marker {@link NativeObject#RAW}\n     */\n"
        << "    protected " << plan.name << "(NativeObject.Raw raw) { super(raw); }\n";
    std::set<std::string> fields;
    for (const auto& constant : plan.constants) {
        const auto value = java(*constant.type, false);
        auto name = constant.name;
        while (!fields.insert(name).second) name += "_";
        auto about = describe(*constant.documentation, "C++ constant " + code_span(constant.expression) + ".");
        out << "\n" << about.render("    ") << "    public static final " << value.type << " " << name << " = "
            << apply_conv(value.conv, pt("Rosetta") + ".call(" + std::to_string(constant.index) + ")") << ";\n";
    }
    for (const auto& c : constructors) out << emit_candidate(c, plan.name);
    for (const auto& c : list) out << emit_candidate(c, plan.name);
    out << "}\n";
    return out.str();
}

std::string Generator::emit_enum(const std::string& qualified, const std::string& name) {
    const auto& enumeration = *enums_.at(qualified);
    std::ostringstream out;
    out << file_head(package_) << "\n"
        << describe(enumeration.documentation, "C++ enumeration " + code_span(qualified) + ".").render("")
        << "public enum " << name << " implements NativeEnum {";
    std::set<std::string> labels{"rosettaValue", "rosettaName"};
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        const auto cpp = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
        auto label = escaped(cpp);
        while (!labels.insert(label).second) label += "_";
        const auto& value = enumeration.enumerator_values[i];
        const bool big = !value.empty() && value[0] != '-' && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
        const auto literal = value == "-9223372036854775808" ? jl("Long") + ".MIN_VALUE"
                           : big ? jl("Long") + ".parseUnsignedLong(\"" + value + "\")" : value + "L";
        out << (i ? "," : "") << "\n    /** C++ " << code_span(enumeration.enumerators[i]) << " (" << html(value) << "). */\n    "
            << label << "(" << literal << ", " << java_string(cpp) << ")";
    }
    out << ";\n\n    private final long rosettaValue;\n    private final " << jl("String") << " rosettaName;\n\n"
        << "    " << name << "(long value, " << jl("String") << " nativeName) {\n        rosettaValue = value;\n        rosettaName = nativeName;\n    }\n\n"
        << "    /**\n     * The C++ value of the enumerator.\n     *\n     * @return the value\n     */\n"
        << "    @" << jl("Override") << "\n    public long value() { return rosettaValue; }\n\n"
        << "    /**\n     * The C++ name of the enumerator.\n     *\n     * @return the name\n     */\n"
        << "    @" << jl("Override") << "\n    public " << jl("String") << " nativeName() { return rosettaName; }\n}\n";
    return out.str();
}

GeneratedJavaFullSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = options_.package.empty() ? name_ : options_.package;
    for (std::size_t start = 0;;) {
        const auto dot = package_.find('.', start);
        const auto part = package_.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!identifier(part) || java_keyword(part)) throw BindingError("Java package must be dotted identifiers: " + package_);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    functions_class_ = options_.functions_class.empty() ? pascal(name_) : options_.functions_class;
    if (!identifier(functions_class_) || java_keyword(functions_class_) || restricted_type(functions_class_))
        throw BindingError("Java functions class must be an identifier: " + functions_class_);
    GeneratedJavaFullSource result;
    prepare();
    // Type names share the package.
    std::map<std::string, std::string> taken;
    const auto reserve = [&](const std::string& name, const std::string& what) {
        const auto inserted = taken.emplace(name, what);
        if (!inserted.second) throw BindingError("Java type name collision: " + name + " (" + inserted.first->second + " and " + what + ")");
    };
    const auto available = [&](const std::string& name) { return !taken.count(name); };
    for (const char* runtime : {"NativeObject", "NativeError", "NativeException", "NativeFunction", "NativeEnum", "Rosetta"})
        reserve(runtime, "Rosetta runtime");
    {
        std::set<std::string> collided;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!available(plan.name)) {
                if (!flat_) throw BindingError("Java type name collision: " + plan.name);
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
            if (!flat_) throw BindingError("Java type name collision: " + name);
            reject(qualified, "enum name collides with another type");
            continue;
        }
        reserve(name, "enum " + qualified);
        enum_names_[qualified] = name;
    }
    if (!available(functions_class_)) functions_class_ += "Functions";
    if (!available(functions_class_)) throw BindingError("Java functions class collides with another type: " + functions_class_);
    reserve(functions_class_, "the module functions");
    for (const auto& [name, what] : taken) types_.insert(name);
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::set<std::string> submodules;
    for (const auto& group : functions) if (!group.module.empty()) submodules.insert(group.module);
    for (const auto& submodule : submodules) {
        for (std::size_t start = 0;;) {
            const auto dot = submodule.find('.', start);
            const auto part = submodule.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!identifier(part) || java_keyword(part)) throw BindingError("Java submodule name must be dotted identifiers: " + submodule);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }
    for (const auto& group : functions) if (group.module.empty()) function_names_.insert(group.name);
    std::vector<ConstantPlan> constants;
    std::set<std::string> constant_names;
    for (auto& constant : plan_constants([&](const Variable& variable) {
             const auto qualified = variable.qualified_name();
             return renamed(qualified, upper_snake(flat(variable.namespaces) ? variable.name : exported(qualified)));
         })) {
        if (!constant_names.insert(constant.name).second) { reject(constant.expression.substr(2), "constant name collides with another constant"); continue; }
        constants.push_back(constant);
    }
    check_diagnostics();
    emit_cpp(result.source, result.parts, functions, constants, "Java package " + package_ + " (" + name_ + ", full API).");
    {
        std::string glue = detail::java_full_jni_cpp;
        glue = replace_all(glue, "@C@", "rosetta_" + name_);
        glue = replace_all(glue, "@JNI@", "Java_" + jni_mangle(package_ + ".Rosetta"));
        result.source += glue;
    }

    // --------------------------------------------------------------- Java sources
    const auto library = options_.library.empty() ? name_ : options_.library;
    std::string environment;
    for (char c : name_) environment += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    environment += "_JAVA_LIBRARY";
    const auto property = name_ + ".library";
    std::string directory = replace_all(package_, ".", "/") + "/";
    prefix_.clear();
    shadow_ = &types_;
    qualify_all_ = false;
    // Helper types the runtime always uses.
    helper("Ref");
    helper("Complex");
    const auto runtime_text = [&](std::string runtime) {
        runtime = replace_all(runtime, "@P@", package_);
        runtime = replace_all(runtime, "@MOD@", name_);
        runtime = replace_all(runtime, "@LIB@", library);
        runtime = replace_all(runtime, "@ENV@", environment);
        runtime = replace_all(runtime, "@PROP@", property);
        runtime = replace_all(runtime, "@REF@", helper_names_.at("Ref"));
        runtime = replace_all(runtime, "@COMPLEX@", helper_names_.at("Complex"));
        return runtime;
    };
    std::vector<std::pair<std::string, std::string>> files;
    for (const auto& [qualified, name] : enum_names_) files.emplace_back(directory + name + ".java", emit_enum(qualified, name));
    for (const auto& owner : class_order_) files.emplace_back(directory + classes_.at(owner).name + ".java", emit_class(owner));

    const auto emit_functions = [&](const std::string& submodule) {
        bool any = false;
        for (const auto& group : functions) if (group.module == submodule) any = true;
        if (!any && (!submodule.empty() || constants.empty())) return;
        std::string package = package_, cls = functions_class_;
        std::set<std::string> local;
        if (!submodule.empty()) {
            package += "." + submodule;
            const auto dot = submodule.rfind('.');
            cls = pascal(dot == std::string::npos ? submodule : submodule.substr(dot + 1));
            if (java_keyword(cls) || restricted_type(cls)) cls += "_";
            local.insert(cls);
            prefix_ = package_ + ".";
            shadow_ = &local;
        }
        std::vector<Candidate> list;
        members_.erase("");
        for (const auto& group : functions) if (group.module == submodule) add_overloads("", list, nullptr, group, false);
        std::ostringstream out;
        out << file_head(package) << "\n";
        const auto about = submodule.empty() ? "The functions and constants of native module {@code " + name_ + "}."
                                             : "The functions of the C++ namespace mapped to submodule {@code " + submodule + "} of native module {@code " + name_ + "}.";
        out << "/**\n * " << about << "\n */\npublic final class " << cls << " {\n    private " << cls << "() {}\n";
        if (submodule.empty())
            for (const auto& constant : constants) {
                const auto value = java(*constant.type, false);
                auto doc = describe(*constant.documentation, "C++ constant " + code_span(normalize(constant.expression)) + ".");
                out << "\n" << doc.render("    ") << "    public static final " << value.type << " " << constant.name << " = "
                    << apply_conv(value.conv, pt("Rosetta") + ".call(" + std::to_string(constant.index) + ")") << ";\n";
            }
        for (const auto& c : list) out << emit_candidate(c, cls);
        out << "}\n";
        files.emplace_back(replace_all(package, ".", "/") + "/" + cls + ".java", out.str());
        prefix_.clear();
        shadow_ = &types_;
    };
    emit_functions("");
    for (const auto& submodule : submodules) emit_functions(submodule);

    // The runtime class, with the class table and the dispatch of overrides.
    std::ostringstream rosetta;
    prefix_ = package_ + ".";
    qualify_all_ = true;
    rosetta << runtime_text(detail::java_full_rosetta);
    rosetta << "\n    // ------------------------------------------------------------ generated tables\n"
            << "    // The class of each class index C++ sends.\n"
            << "    static final List<Function<NativeObject.Raw, NativeObject>> CLASSES = classes();\n"
            << "    private static List<Function<NativeObject.Raw, NativeObject>> classes() {\n"
            << "        final ArrayList<Function<NativeObject.Raw, NativeObject>> c = new ArrayList<>();\n";
    for (const auto& owner : class_order_) rosetta << "        c.add(" << pt(classes_.at(owner).name) << "::new);\n";
    rosetta << "        return c;\n    }\n"
            << "    // The virtual methods C++ calls on Java subclass objects, by override index.\n"
            << "    @SuppressWarnings(\"deprecation\")\n"
            << "    private static Object dispatch(int generic, Object[] a) throws Throwable {\n"
            << "        final Object self = a.length > 0 ? a[0] : null;\n"
            << "        switch (generic) {\n";
    {
        std::map<std::size_t, std::vector<std::string>> cases;
        std::set<std::string> seen;
        for (const auto& owner : class_order_) {
            const auto& plan = classes_.at(owner);
            if (!plan.director) continue;
            for (const auto& item : overridable(plan)) {
                const auto found = generic_index_.find(item.name);
                if (found == generic_index_.end()) continue;
                const auto* method = item.method;
                const auto java_name = java_names_.count(method) ? java_names_.at(method) : std::string();
                const auto cls = pt(plan.name);
                const auto arity = method->parameters.size() + 1;
                std::string line;
                if (java_name.empty()) {
                    line = "            if (self instanceof " + cls + " && a.length == " + std::to_string(arity) + ") return reflect(self, " +
                           java_string(item.name) + ", a);\n";
                } else {
                    std::string arguments;
                    for (std::size_t i = 0; i < method->parameters.size(); ++i) {
                        const auto& type = method->parameters[i];
                        const auto value = java(type, writeback(type) ? false : true, 1);
                        auto converted = apply_conv(value.conv, "a[" + std::to_string(i + 1) + "]");
                        if (writeback(type)) converted = "new " + pt(helper("Ref")) + "<>(" + converted + ")";
                        arguments += (i ? ", " : "") + converted;
                    }
                    const auto call = "((" + cls + ") self)." + java_name + "(" + arguments + ")";
                    line = "            if (self instanceof " + cls + " && a.length == " + std::to_string(arity) + ") " +
                           (is_void(method->result) ? "{ " + call + "; return null; }" : "return " + call + ";") + "\n";
                }
                if (seen.insert(std::to_string(found->second) + line).second) cases[found->second].push_back(line);
            }
        }
        for (const auto& [index, lines] : cases) {
            rosetta << "            case " << index << ":\n";
            for (const auto& line : lines) rosetta << line;
            rosetta << "                break;\n";
        }
    }
    rosetta << "            default:\n                break;\n        }\n        throw noOverride(self, generic);\n    }\n"
            << "    // An override the binding has no Java method for: found by name.\n"
            << "    private static Object reflect(Object self, String name, Object[] a) throws Throwable {\n"
            << "        for (final java.lang.reflect.Method method : self.getClass().getMethods()) {\n"
            << "            if (!method.getName().equals(name) || method.getParameterCount() != a.length - 1) continue;\n"
            << "            try { return method.invoke(self, Arrays.copyOfRange(a, 1, a.length)); }\n"
            << "            catch (java.lang.reflect.InvocationTargetException error) { throw error.getCause(); }\n"
            << "        }\n"
            << "        throw noOverride(self, -1);\n    }\n}\n";
    qualify_all_ = false;
    prefix_.clear();
    shadow_ = &types_;

    // Runtime classes and helpers.
    files.emplace_back(directory + "NativeObject.java", runtime_text(detail::java_full_native_object));
    files.emplace_back(directory + "NativeError.java", runtime_text(detail::java_full_native_error));
    files.emplace_back(directory + "NativeException.java", runtime_text(detail::java_full_native_exception));
    files.emplace_back(directory + "NativeFunction.java", runtime_text(detail::java_full_native_function));
    files.emplace_back(directory + "NativeEnum.java", runtime_text(detail::java_full_native_enum));
    files.emplace_back(directory + helper_names_.at("Ref") + ".java", runtime_text(detail::java_full_ref));
    files.emplace_back(directory + helper_names_.at("Complex") + ".java", runtime_text(detail::java_full_complex));
    files.emplace_back(directory + "Rosetta.java", rosetta.str());
    static const char* ordinals[] = {"first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth"};
    for (const auto size : helpers_.tuples) {
        const auto name = helper_names_.at(size == 2 ? std::string("Pair") : "Tuple" + std::to_string(size));
        std::ostringstream out;
        out << file_head(package_) << "\n/**\n * A C++ " << (size == 2 ? "{@code std::pair}" : "{@code std::tuple}") << " of " << size << " values.\n *\n";
        std::string types, components;
        for (std::size_t i = 0; i < size; ++i) {
            const std::string type(1, static_cast<char>('A' + i));
            out << " * @param <" << type << "> the type of the " << ordinals[i] << " value\n";
            types += (i ? ", " : "") + type;
            components += (i ? ", " : "") + type + " " + ordinals[i];
        }
        for (std::size_t i = 0; i < size; ++i) out << " * @param " << ordinals[i] << " the " << ordinals[i] << " value\n";
        out << " */\npublic record " << name << "<" << types << ">(" << components << ") {}\n";
        files.emplace_back(directory + name + ".java", out.str());
    }
    for (const auto arity : helpers_.functions) {
        const auto name = helper_names_.at("Function" + std::to_string(arity));
        std::ostringstream out;
        out << file_head(package_) << "\n/**\n * A function of " << arity << " arguments (a C++ {@code std::function}).\n *\n";
        std::string types, parameters;
        for (std::size_t i = 0; i < arity; ++i) {
            const auto type = "A" + std::to_string(i + 1);
            out << " * @param <" << type << "> the type of argument " << i + 1 << "\n";
            types += type + ", ";
            parameters += (i ? ", " : "") + type + " a" + std::to_string(i + 1);
        }
        out << " * @param <R> the type of the result\n */\n@" << jl("FunctionalInterface") << "\npublic interface " << name << "<" << types << "R> {\n"
            << "    /**\n     * Calls the function.\n     *\n";
        for (std::size_t i = 0; i < arity; ++i) out << "     * @param a" << i + 1 << " argument " << i + 1 << "\n";
        out << "     * @return the result\n     */\n    R apply(" << parameters << ");\n}\n";
        files.emplace_back(directory + name + ".java", out.str());
    }
    for (const auto arity : helpers_.consumers) {
        const auto name = helper_names_.at("Consumer" + std::to_string(arity));
        std::ostringstream out;
        out << file_head(package_) << "\n/**\n * An action of " << arity << " arguments (a C++ {@code std::function} returning void).\n *\n";
        std::string types, parameters;
        for (std::size_t i = 0; i < arity; ++i) {
            const auto type = "A" + std::to_string(i + 1);
            out << " * @param <" << type << "> the type of argument " << i + 1 << "\n";
            types += (i ? ", " : "") + type;
            parameters += (i ? ", " : "") + type + " a" + std::to_string(i + 1);
        }
        out << " */\n@" << jl("FunctionalInterface") << "\npublic interface " << name << "<" << types << "> {\n"
            << "    /**\n     * Calls the action.\n     *\n";
        for (std::size_t i = 0; i < arity; ++i) out << "     * @param a" << i + 1 << " argument " << i + 1 << "\n";
        out << "     */\n    void accept(" << parameters << ");\n}\n";
        files.emplace_back(directory + name + ".java", out.str());
    }
    result.files = files;

    std::ostringstream info;
    info << "// Generated by Rosetta: Java interface to native module " << name_ << " (full API).\n"
         << "/**\n * The Java interface to the C++ native module {@code " << name_ << "} (generated by Rosetta).\n *\n"
         << " * <p>Classes are {@link " << package_ << ".NativeObject}s: {@code close()} (or try-with-resources) releases the C++ object,\n"
         << " * otherwise a {@link java.lang.ref.Cleaner} does once the Java object is unreachable. Module functions are static\n"
         << " * methods of {@link " << package_ << "." << functions_class_ << "}. C++ exceptions become standard Java exceptions that\n"
         << " * implement {@link " << package_ << ".NativeError}.\n *\n"
         << " * <p>The JNI library is the file named by the system property {@code " << property << "} or the environment\n"
         << " * variable {@code " << environment << "}; otherwise {@code System.loadLibrary(\"" << library << "\")} loads\n"
         << " * {@code lib" << library << ".so} ({@code .dylib}, {@code " << library << ".dll}) from {@code java.library.path}.\n */\n"
         << "package " << package_ << ";\n";
    result.package_info = info.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedJavaFullSource generate_java_full(const Module& module, const std::string& name, bool skip_unsupported,
                                           const BindingPolicy& policy, const JavaFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Java source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
