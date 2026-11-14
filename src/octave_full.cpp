// Full Octave backend (--octave-api full): a native oct-file plus a package of .m files.
//
// C++ classes become classdef handle classes (+PACKAGE/Class.m) deriving from
// PACKAGE.NativeObject: methods, static methods, fields as properties, class constants as Constant
// properties, operators as Octave operator methods (plus, mtimes, eq, uminus, ...), operator[]
// and operator() through subsref, inheritance (also multiple), and Octave subclasses overriding
// C++ virtual methods (directors). Enums become classdef classes of Constant properties, free
// functions and constants .m functions, policy modules nested packages (+PACKAGE/+data/f.m).
// Overloads and default arguments are resolved at run time in C++; callbacks are function
// handles; C++ exceptions are Octave errors with identifiers (PACKAGE:invalid_argument).
//
// Every .m file carries Octave help text (plain text comment blocks after the function/classdef
// line) converted from the Doxygen comments: `help PACKAGE.f` and `doc` show it, and editors
// (mep's Octave server) read it from the files. +PACKAGE/Contents.m lists the package.
//
// The C++ side is the library every full C ABI backend shares (cabi_full.hpp,
// lisp_full_runtime.hpp) with octave_full_runtime.hpp appended: the main source is built as
// __MODULE__.oct, whose one DEFUN the .m stubs call.
#include "rosetta/octave.hpp"
#include "cabi_full.hpp"
#include "octave_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

const std::set<std::string>& octave_keywords() {
    static const std::set<std::string> keywords = {"__FILE__", "__LINE__", "arguments", "break", "case", "catch", "classdef",
        "continue", "do", "else", "elseif", "end", "end_try_catch", "end_unwind_protect", "endarguments", "endclassdef",
        "endenumeration", "endevents", "endfor", "endfunction", "endif", "endmethods", "endparfor", "endproperties",
        "endspmd", "endswitch", "endwhile", "enumeration", "events", "for", "function", "global", "if", "methods",
        "otherwise", "parfor", "persistent", "properties", "return", "spmd", "switch", "try", "until", "unwind_protect",
        "unwind_protect_cleanup", "while"};
    return keywords;
}
// Names an Octave classdef object answers itself (display, indexing, lifetime, operators):
// methods and properties named like them get a trailing underscore.
const std::set<std::string>& protocol_names() {
    static const std::set<std::string> names = {"delete", "disp", "display", "subsref", "subsasgn", "subsindex", "numel",
        "size", "ndims", "length", "isempty", "horzcat", "vertcat", "cat", "isa", "class", "isequal", "properties",
        "methods", "fieldnames", "loadobj", "saveobj", "copy", "notify", "addlistener", "listener", "findobj", "findprop",
        "isvalid", "eq", "ne", "lt", "le", "gt", "ge", "plus", "minus", "uminus", "uplus", "times", "mtimes", "rdivide",
        "ldivide", "mrdivide", "mldivide", "power", "mpower", "and", "or", "not", "ctranspose", "transpose", "colon",
        "mod", "char", "double", "logical", "struct", "cell", "end", "numArgumentsFromSubscript"};
    return names;
}
bool keyword(const std::string& name) { return octave_keywords().count(name) != 0; }
std::string escaped(const std::string& name) { return keyword(name) ? name + "_" : name; }
std::string member(const std::string& name) {
    if (keyword(name) || protocol_names().count(name) || name.rfind("rosetta_", 0) == 0) return name + "_";
    return name;
}
std::string m_string(const std::string& text) {
    std::string result = "'";
    for (char c : text) {
        if (c == '\'') result += "''";
        else if (c == '\n') result += "' newline '";
        else result += c;
    }
    return result + "'";
}

// --- documentation (plain text Octave help) ----------------------------------------

// Doxygen markup to plain text: **bold** loses its stars; `code` keeps its backquotes.
std::string plain(std::string text) {
    for (std::size_t i = 0; (i = text.find("**", i)) != std::string::npos;) text.erase(i, 2);
    return text;
}
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        lines.push_back(line);
    }
    return lines;
}
std::string error_kind_of(const std::string& cpp) {
    const auto bare = normalize(cpp);
    if (bare == "std::invalid_argument") return "invalid_argument";
    if (bare == "std::out_of_range") return "out_of_range";
    if (bare == "std::domain_error") return "domain_error";
    if (bare == "std::length_error") return "length_error";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "arithmetic_error";
    if (bare == "std::bad_alloc") return "allocation";
    return "native";
}
// A help block: comment lines under a function or classdef line.
struct Help {
    std::vector<std::string> lines;
    void line(const std::string& text = {}) { lines.push_back(text); }
    void blank() { if (!lines.empty() && !lines.back().empty()) lines.emplace_back(); }
    void text(const std::string& text, const std::string& indent) { for (const auto& l : split_lines(text)) lines.push_back(l.empty() ? l : indent + l); }
    std::string render(const std::string& indent) const {
        std::vector<std::string> trimmed = lines;
        while (!trimmed.empty() && trimmed.back().empty()) trimmed.pop_back();
        std::string out;
        for (const auto& l : trimmed) out += indent + "%" + (l.empty() ? "" : " " + l) + "\n";
        return out;
    }
};
struct Param { std::string name, type, default_value, text; };
// The description, notes and see-also of a declaration (after the H1 line and usage).
void describe(Help& help, const Documentation& doc, bool skip_first_paragraph) {
    bool first = skip_first_paragraph, heading = false;
    for (const auto& block : doc.details) {
        if (first && block.kind == "text") { first = false; continue; }
        first = false;
        if (!heading) help.blank(); // the block after a heading (@par) sits right under it
        if (block.kind == "code" || block.kind == "math") help.text(block.text, "    ");
        else if (block.kind == "heading") help.line("  " + plain(block.text) + ":");
        else help.text(plain(block.text), "  ");
        heading = block.kind == "heading";
    }
}
std::string summary(const Documentation& doc, const std::string& fallback) {
    if (!doc.brief.empty()) return one_line(plain(doc.brief));
    for (const auto& block : doc.details) if (block.kind == "text") return one_line(plain(block.text));
    return fallback;
}
void sections(Help& help, const Documentation& doc, const std::vector<Param>& inputs, const std::string& output_name,
              const std::string& output_type, const std::string& package, bool with_returns) {
    if (!inputs.empty()) {
        help.blank();
        help.line("  Inputs:");
        for (const auto& p : inputs) {
            std::string head = p.type;
            if (!p.default_value.empty()) head += (head.empty() ? "" : ", ") + std::string("default ") + p.default_value;
            help.line("    " + p.name + "  " + head + (p.text.empty() ? "" : ": " + one_line(plain(p.text))));
        }
    }
    if (with_returns) {
        help.blank();
        help.line("  Output:");
        help.line("    " + output_name + "  " + output_type + (doc.returns.empty() ? "" : ": " + one_line(plain(doc.returns))));
    }
    for (const auto& item : doc.retvals) help.line("    " + item.name + "  " + one_line(plain(item.text)));
    if (!doc.throws.empty()) {
        help.blank();
        help.line("  Errors:");
        for (const auto& item : doc.throws)
            help.line("    " + package + ":" + error_kind_of(item.name) + "  " + one_line(plain(item.text)) +
                      (item.name.empty() ? "" : " (C++ " + item.name + ")"));
    }
    bool notes = false;
    for (const auto& item : doc.notes) {
        if (!notes) help.blank();
        notes = true;
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        help.line("  " + label + ": " + one_line(plain(item.text)));
    }
}
void see_also(Help& help, const Documentation& doc, const std::string& package) {
    if (doc.see.empty()) return;
    std::string see;
    for (const auto& target : doc.see) {
        auto name = target;
        const auto colons = name.rfind("::");
        if (colons != std::string::npos) name = name.substr(colons + 2);
        see += (see.empty() ? "" : ", ") + package + "." + name;
    }
    help.blank();
    help.line("  See also: " + see);
}

// C++ operators and their Octave method ("" = not bound); "(index)" and "(call)" go through subsref.
std::string octave_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "(call)";
    if (name == "operator[]") return arity == 1 ? "(index)" : "";
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"uplus", "plus"}}, {"operator-", {"uminus", "minus"}}, {"operator*", {nullptr, "mtimes"}},
        {"operator/", {nullptr, "mrdivide"}}, {"operator%", {nullptr, "mod"}}, {"operator==", {nullptr, "eq"}},
        {"operator!=", {nullptr, "ne"}}, {"operator<", {nullptr, "lt"}}, {"operator<=", {nullptr, "le"}},
        {"operator>", {nullptr, "gt"}}, {"operator>=", {nullptr, "ge"}}, {"operator!", {"not", nullptr}},
        {"operator&", {nullptr, "and"}}, {"operator|", {nullptr, "or"}}};
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
std::string operator_symbol(const std::string& name) {
    static const std::map<std::string, std::string> symbols = {{"plus", "+"}, {"minus", "-"}, {"uplus", "+ (unary)"},
        {"uminus", "- (unary)"}, {"mtimes", "*"}, {"mrdivide", "/"}, {"mod", "mod"}, {"eq", "=="}, {"ne", "~="}, {"lt", "<"},
        {"le", "<="}, {"gt", ">"}, {"ge", ">="}, {"not", "~ (unary)"}, {"and", "&"}, {"or", "|"}};
    const auto found = symbols.find(name);
    return found == symbols.end() ? name : found->second;
}

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const OctaveFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedOctaveFullSource run();

private:
    OctaveFullOptions options_;
    std::string package_;     // the Octave package (+package)
    std::string entry_;       // the native function: __module__
    GeneratedOctaveFullSource result_;
    mutable std::map<std::string, std::string> protocol_escapes_; // name_ -> name

    std::string language() const override { return "Octave"; }
    std::string target_name(const std::string& cpp) const override { return escaped(cpp); }
    std::string method_name(const std::string& cpp) const override {
        const auto name = member(cpp);
        if (name != cpp && !keyword(cpp)) protocol_escapes_[name] = cpp;
        return name;
    }
    std::string field_name(const std::string& cpp) const override { return member(cpp); }
    std::string type_name(const std::string& cpp) const override { return escaped(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return escaped(name); }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return octave_operator(cpp, arity); }
    bool free_operator(const std::string& name) const override { return name != "(index)" && name != "(call)"; }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return member(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken{"obj", "r", "varargin", "varargout", "nargin", "nargout", "ans", "self", "value"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) || cpp[0] == '_' ? "arg" + std::to_string(i - first + 1) : escaped(cpp);
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool is_method) const override {
        std::string text;
        for (const auto& line : usages(group, is_method ? "obj." + group.name : group_path(group), false)) text += "    " + line + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& line : constructor_usages(plan)) text += "    " + line + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return package_ + "." + plan.name + " is abstract: derive an Octave classdef from it";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return package_ + "." + plan.name + " constructor is protected: call it from an Octave subclass";
    }
    std::string symbol_prefix() const override { return "rosetta_oct_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_OCTAVE"; }
    std::string module_prefix() const override { return "rosetta_octave_module_"; }

    // ------------------------------------------------------------ naming and types
    std::string group_path(const Group& group) const {
        std::string path = package_;
        if (!group.module.empty()) path += "." + group.module;
        return path + "." + group.name;
    }
    std::string class_path(const std::string& owner) const { return package_ + "." + classes_.at(owner).name; }
    std::string octave_type(const Type& type) const;
    std::string default_text(const DefaultValue& value, const Type& type) const;
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    static bool returns_void(const Group& group) {
        return std::all_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) { return is_void(o.function->result); });
    }
    bool has_writebacks(const Group& group) const {
        for (const auto& overload : group.overloads) {
            const std::size_t first = overload.receives_self ? 1 : 0;
            for (std::size_t i = first; i < overload.function->parameters.size(); ++i) if (writeback(overload.function->parameters[i])) return true;
        }
        return false;
    }
    std::string output_name(const Group& group) const { return returns_void(group) ? std::string() : "r"; }
    // The call forms of a group (one per overload and arity).
    std::vector<std::string> usages(const Group& group, const std::string& callee, bool) const {
        std::vector<std::string> lines;
        std::set<std::string> seen;
        for (const auto& overload : group.overloads) {
            const auto& function = *overload.function;
            const std::size_t first = overload.receives_self ? 1 : 0;
            const auto names = parameter_names(overload);
            const std::size_t required = function.parameters.size() - function.default_arguments;
            for (std::size_t arity = std::max(required, first); arity <= function.parameters.size(); ++arity) {
                std::string arguments, outputs;
                std::vector<std::string> updated;
                for (std::size_t i = first; i < arity; ++i) {
                    arguments += (i > first ? ", " : "") + names[i - first];
                    if (writeback(function.parameters[i])) updated.push_back(names[i - first]);
                }
                if (!is_void(function.result)) updated.insert(updated.begin(), "r");
                if (updated.size() == 1) outputs = updated[0] + " = ";
                else if (updated.size() > 1) {
                    outputs = "[";
                    for (std::size_t i = 0; i < updated.size(); ++i) outputs += (i ? ", " : "") + updated[i];
                    outputs += "] = ";
                }
                const auto line = outputs + callee + " (" + arguments + ")";
                if (seen.insert(line).second) lines.push_back(line);
            }
        }
        return lines;
    }
    std::vector<std::string> constructor_usages(const ClassPlan& plan) const {
        std::vector<std::string> lines;
        std::set<std::string> seen;
        for (const auto& overload : plan.constructors) {
            const auto& function = *overload.function;
            const auto names = parameter_names(overload);
            const std::size_t required = function.parameters.size() - function.default_arguments;
            for (std::size_t arity = required; arity <= function.parameters.size(); ++arity) {
                std::string arguments;
                for (std::size_t i = 0; i < arity; ++i) arguments += (i ? ", " : "") + names[i];
                const auto line = "obj = " + class_path(plan.record->qualified_name()) + " (" + arguments + ")";
                if (seen.insert(line).second) lines.push_back(line);
            }
        }
        return lines;
    }
    std::string cpp_signature(const Function& function) const {
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
    Help group_help(const Group& group, const std::string& h1, const std::string& callee, bool is_method) const;
    std::string call_body(const Group& group, const std::string& prefix, const std::string& indent, bool named, const std::vector<std::string>& names) const;
    std::string callable(const Group& group, const std::string& function_name, bool is_method, const std::string& indent) const;
    void emit_class(const std::string& owner);
    void emit_enum(const std::string& qualified, const std::string& name);
    void emit_runtime_classes();
    void add_file(const std::string& path, const std::string& text) { result_.files.emplace_back(path, text); }
    std::string package_dir(const std::string& submodule) const {
        std::string dir = "+" + package_;
        for (std::size_t start = 0; !submodule.empty();) {
            const auto dot = submodule.find('.', start);
            dir += "/+" + submodule.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return dir;
    }
    std::string header(const std::string& what) const {
        // At the end: Octave takes a leading comment block for the help text.
        return "% Generated by Rosetta from " + module_.header + ": " + what + ". Do not edit.\n";
    }
};

std::string Generator::octave_type(const Type& original) const {
    const Type& type = referent(original);
    if (type.kind == TypeKind::pointer && !type.arguments.empty()) {
        const auto& pointee = type.arguments[0];
        if (bound(pointee)) return class_path(pointee.declaration) + " or []";
        return "char row";
    }
    const auto bare = normalize(strip_cv(type.canonical));
    if (type.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(type.declaration);
        return found != enum_names_.end() ? package_ + "." + found->second + " value" : "integer";
    }
    if (bound(type)) return class_path(type.declaration);
    if (bare == "bool") return "logical";
    if (bare == "char") return "char";
    if (bare == "float" || bare == "double" || bare == "long double") return "double";
    if (type.kind == TypeKind::builtin) return "integer";
    if (bare == "std::filesystem::path") return "char row (path)";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "[]";
    if (!conversion(type).empty()) return "value convertible to " + policy_.conversions.at(conversion(type));
    if (type.kind != TypeKind::template_instance) return strip_cv(type.spelling);
    const auto declaration = normalize(type.declaration);
    const auto argument = [&](std::size_t i) { return i < type.arguments.size() ? octave_type(type.arguments[i]) : std::string("value"); };
    if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "char row";
    if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::valarray" ||
        declaration == "std::array" || declaration == "std::set" || declaration == "std::unordered_set") {
        const auto element = argument(0);
        if (element == "double") return "double vector";
        if (element == "integer") return "integer vector";
        if (element == "logical") return "logical vector";
        if (element == "double vector") return "double matrix (rows)";
        if (element == "char row") return "cell array of char";
        return "cell array of " + element;
    }
    if (declaration == "std::optional") return argument(0) + " or []";
    if (declaration == "std::map" || declaration == "std::unordered_map") return "containers.Map (" + argument(0) + " -> " + argument(1) + ")";
    if (declaration == "std::pair" || declaration == "std::tuple") {
        std::string text = "cell {";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? ", " : "") + argument(i);
        return text + "}";
    }
    if (declaration == "std::variant") {
        std::string text;
        for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? " or " : "") + argument(i);
        return text;
    }
    if (declaration == "std::complex") return "complex";
    if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return argument(0) + " or []";
    if (declaration == "std::function") return "function handle";
    if (declaration == "std::chrono::duration") return "double (seconds)";
    return strip_cv(type.spelling);
}

std::string Generator::default_text(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "constant") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "[]";
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "string") {
        auto text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
        return m_string(text);
    }
    if (value.kind == "enum") {
        const auto found = enum_names_.find(referent(type).declaration);
        return found != enum_names_.end() ? package_ + "." + found->second + "." + escaped(value.value) : value.value;
    }
    if (value.kind == "default") return "the C++ default";
    return {};
}

// Help text of a function, method or static method: H1 line, call forms, the description of the
// first overload with documentation, inputs/output of the widest form, and the C++ signatures.
Help Generator::group_help(const Group& group, const std::string& h1, const std::string& callee, bool) const {
    const Overload* documented = &group.overloads.front();
    for (const auto& overload : group.overloads) if (!overload.function->documentation.empty()) { documented = &overload; break; }
    const auto& function = *documented->function;
    const auto& doc = function.documentation;
    Help help;
    help.line(h1 + "  " + summary(doc, "C++ " + function.qualified_name() + "."));
    help.blank();
    for (const auto& line : usages(group, callee, false)) help.line("  " + line);
    describe(help, doc, doc.brief.empty());
    std::vector<Param> inputs;
    if (group.overloads.size() == 1) {
        const std::size_t first = documented->receives_self ? 1 : 0;
        const auto names = parameter_names(*documented);
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            std::string text;
            for (const auto& item : doc.params) if (!cpp.empty() && item.name == cpp) text = item.text;
            std::string fallback = i + function.default_arguments >= function.parameters.size() ? "the C++ default" : "";
            const auto shown = i < function.default_values.size() ? default_text(function.default_values[i], function.parameters[i]) : std::string();
            inputs.push_back({names[i - first], octave_type(function.parameters[i]) + (writeback(function.parameters[i]) ? " (updated: also an output)" : ""),
                              shown.empty() ? fallback : shown, text});
        }
    }
    const bool with_returns = group.overloads.size() == 1 && !is_void(function.result);
    sections(help, doc, inputs, "r", with_returns ? octave_type(function.result) : std::string(), package_, with_returns);
    help.blank();
    if (group.overloads.size() > 1) help.line("  Overloads are chosen at run time from the argument types (C++):");
    for (const auto& overload : group.overloads) help.line("  C++: " + cpp_signature(*overload.function));
    see_also(help, doc, package_);
    return help;
}

// The statement(s) calling the native function for a group.
std::string Generator::call_body(const Group& group, const std::string& prefix, const std::string& indent, bool named,
                                 const std::vector<std::string>& names) const {
    std::string arguments = prefix;
    if (named) for (const auto& name : names) arguments += ", " + name;
    else arguments += ", varargin{:}";
    const auto call = entry_ + "(" + std::to_string(group.index) + arguments + ")";
    if (has_writebacks(group)) return indent + "[varargout{1:max(nargout, 1)}] = " + call + ";\n";
    if (returns_void(group)) return indent + call + ";\n";
    return indent + "r = " + call + ";\n";
}

// A function (module function, method or static method) with its help text.
std::string Generator::callable(const Group& group, const std::string& function_name, bool is_method, const std::string& indent) const {
    const bool named = group.overloads.size() == 1 && group.overloads.front().function->default_arguments == 0 && !has_writebacks(group);
    const auto names = named ? parameter_names(group.overloads.front()) : std::vector<std::string>{};
    std::string inputs = is_method ? "obj" : "";
    if (named) for (const auto& name : names) inputs += (inputs.empty() ? "" : ", ") + name;
    else inputs += std::string(inputs.empty() ? "" : ", ") + "varargin";
    std::string outputs = has_writebacks(group) ? "varargout = " : returns_void(group) ? "" : "r = ";
    const auto callee = is_method ? "obj." + group.name : function_name;
    const auto help = group_help(group, group.name, callee, is_method);
    return indent + "function " + outputs + group.name + " (" + inputs + ")\n" + help.render(indent + "  ") +
           call_body(group, is_method ? ", obj" : "", indent + "  ", named, names) + indent + "end\n";
}

void Generator::emit_class(const std::string& owner) {
    auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto path = class_path(owner);
    std::ostringstream m;
    m << "classdef " << plan.name << " < ";
    if (plan.bases.empty()) m << package_ << ".NativeObject";
    for (std::size_t i = 0; i < plan.bases.size(); ++i) m << (i ? " & " : "") << class_path(plan.bases[i]);
    m << "\n";

    // Class help.
    Help help;
    help.line(plan.name + "  " + summary(record.documentation, "C++ class " + owner + "."));
    help.blank();
    for (const auto& line : constructor_usages(plan)) help.line("  " + line);
    describe(help, record.documentation, record.documentation.brief.empty());
    if (!plan.bases.empty()) {
        help.blank();
        std::string bases;
        for (const auto& base : plan.bases) bases += (bases.empty() ? "" : ", ") + class_path(base);
        help.line("  Inherits from " + bases + ".");
    }
    if (record.abstract) {
        help.blank();
        help.line(std::string("  Abstract in C++: ") + (plan.director ? "derive an Octave classdef from it and implement its abstract methods." : "C++ returns objects of derived classes."));
    }
    if (plan.director) {
        std::string names;
        for (const auto& item : overridable(plan)) names += (names.empty() ? "" : ", ") + item.name + (item.pure ? " (abstract)" : "");
        help.blank();
        help.line("  Octave subclasses (classdef MyClass < " + path + ") override its virtual");
        help.line("  methods, also when C++ calls them: " + names + ".");
    }
    if (!plan.fields.empty()) {
        help.blank();
        help.line("  Properties:");
        for (const auto& field : plan.fields) {
            const auto& f = *field.field;
            const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
            help.line("    " + field.name + "  " + octave_type(f.type) + (writable ? "" : ", read-only") +
                      (f.documentation.empty() ? "" : ": " + summary(f.documentation, "")));
        }
    }
    const auto list = [&](const std::string& title, bool statics) {
        bool any = false;
        for (const auto& group : plan.methods) {
            if (group.is_static != statics) continue;
            if (!any) { help.blank(); help.line("  " + title + ":"); }
            any = true;
            const auto* function = group.overloads.front().function;
            for (const auto& overload : group.overloads) if (!overload.function->documentation.empty()) { function = overload.function; break; }
            help.line("    " + group.name + "  " + summary(function->documentation, "C++ " + function->qualified_name() + "."));
        }
    };
    list("Methods", false);
    list("Static methods", true);
    if (!plan.constants.empty()) {
        help.blank();
        help.line("  Constants:");
        for (const auto& constant : plan.constants)
            help.line("    " + constant.name + "  " + summary(*constant.documentation, "C++ " + constant.expression + "."));
    }
    if (!plan.operators.empty()) {
        std::string ops;
        std::set<std::string> seen;
        for (const auto& group : plan.operators) {
            const auto shown = group.name == "(index)" ? "obj(i) (1-based) and obj{i} (C++ operator[])" : group.name == "(call)" ? "obj(...) (C++ operator())" : operator_symbol(group.name);
            if (seen.insert(shown).second) ops += (ops.empty() ? "" : ", ") + shown;
        }
        help.blank();
        help.line("  Operators: " + ops + ".");
    }
    help.blank();
    help.line(std::string("  C++: ") + (record.abstract ? "abstract " : "") + "class " + owner);
    see_also(help, record.documentation, package_);
    m << help.render("  ");

    // Properties: fields (dependent: read and written in C++) and constants.
    const auto writable_field = [&](const FieldPlan& field) {
        const auto& f = *field.field;
        return !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
    };
    for (const bool writable : {true, false}) {
        bool any = false;
        for (const auto& field : plan.fields) {
            if (writable_field(field) != writable) continue;
            if (!any) m << "  properties (Dependent" << (writable ? "" : ", SetAccess = private") << ")\n";
            any = true;
            m << "    " << field.name << "  % " << summary(field.field->documentation, octave_type(field.field->type) + " (C++ " + field.field->name + ")") << "\n";
        }
        if (any) m << "  end\n";
    }
    if (!plan.constants.empty()) {
        m << "  properties (Constant)\n";
        for (const auto& constant : plan.constants)
            m << "    " << constant.name << " = " << entry_ << "(" << constant.index << ");  % " << summary(*constant.documentation, "C++ " + constant.expression) << "\n";
        m << "  end\n";
    }

    // Constructor.
    m << "  methods\n    function obj = " << plan.name << " (varargin)\n";
    {
        Help ctor;
        const Function* function = plan.constructors.empty() ? nullptr : plan.constructors.front().function;
        for (const auto& overload : plan.constructors) if (!overload.function->documentation.empty()) { function = overload.function; break; }
        ctor.line(plan.name + "  Construct a C++ " + owner + ".");
        ctor.blank();
        for (const auto& line : constructor_usages(plan)) ctor.line("  " + line);
        if (function) {
            describe(ctor, function->documentation, false);
            if (!function->documentation.brief.empty()) { ctor.blank(); ctor.line("  " + one_line(plain(function->documentation.brief))); }
            std::vector<Param> inputs;
            if (plan.constructors.size() == 1) {
                const auto names = parameter_names(plan.constructors.front());
                for (std::size_t i = 0; i < function->parameters.size(); ++i) {
                    const auto cpp = i < function->parameter_names.size() ? function->parameter_names[i] : std::string();
                    std::string text;
                    for (const auto& item : function->documentation.params) if (!cpp.empty() && item.name == cpp) text = item.text;
                    const auto shown = i < function->default_values.size() ? default_text(function->default_values[i], function->parameters[i]) : std::string();
                    inputs.push_back({names[i], octave_type(function->parameters[i]), shown, text});
                }
            }
            sections(ctor, function->documentation, inputs, "obj", "", package_, false);
            ctor.blank();
            for (const auto& overload : plan.constructors) ctor.line("  C++: " + cpp_signature(*overload.function));
        } else {
            ctor.line("  It has no public constructor: C++ functions return its objects.");
        }
        m << ctor.render("      ");
    }
    if (plan.bases.empty()) m << "      obj@" << package_ << ".NativeObject(struct('rosetta_skip__', true));\n";
    for (const auto& base : plan.bases) m << "      obj@" << class_path(base) << "(struct('rosetta_skip__', true));\n";
    m << "      if nargin == 1 && isstruct (varargin{1}) && isfield (varargin{1}, 'rosetta_skip__')\n        return;\n      end\n";
    if (plan.constructor.empty())
        m << "      error ('" << package_ << ":binding', '%s', " << m_string(record.abstract ? abstract_message(plan) : path + " has no public constructor") << ");\n";
    else
        m << "      " << entry_ << "(-2, obj, " << plan.constructor_index << ", "
          << (plan.director ? "~strcmp (class (obj), '" + path + "')" : std::string("false")) << ", varargin{:});\n";
    m << "    end\n";

    // Field accessors.
    for (const auto& field : plan.fields) {
        m << "    function v = get." << field.name << " (obj)\n      v = " << entry_ << "(" << field.getter_index << ", obj);\n    end\n";
        if (writable_field(field))
            m << "    function set." << field.name << " (obj, v)\n      " << entry_ << "(" << field.setter_index << ", obj, v);\n    end\n";
    }
    // Instance methods.
    std::set<std::string> void_methods;
    for (const auto& group : plan.methods) {
        if (group.is_static) continue;
        if (returns_void(group) && !has_writebacks(group)) void_methods.insert(group.name);
        m << callable(group, group.name, true, "    ");
    }
    // Operators: an Octave operator method per name; the operand of this class decides which
    // group (this class first: member operators and free operators taking it first).
    std::map<std::string, std::vector<const Group*>> operators;
    std::vector<std::string> operator_order;
    const Group* index_group = nullptr;
    const Group* call_group = nullptr;
    for (const auto& group : plan.operators) {
        if (group.name == "(index)") { index_group = &group; continue; }
        if (group.name == "(call)") { call_group = &group; continue; }
        if (!operators.count(group.name)) operator_order.push_back(group.name);
        operators[group.name].push_back(&group);
    }
    for (const auto& name : operator_order) {
        const auto& groups = operators[name];
        const bool unary = name == "uminus" || name == "uplus" || name == "not";
        const Group* first = nullptr;
        const Group* second = nullptr;
        for (const auto* group : groups) (group->operand == 0 ? first : second) = group;
        m << "    function r = " << name << (unary ? " (a)\n" : " (a, b)\n");
        m << "      % " << name << "  C++ operator" << (unary ? "" : " ") << operator_symbol(name) << " on " << path << ".\n";
        if (unary) m << "      r = " << entry_ << "(" << first->index << ", a);\n";
        else {
            const auto failure = "error ('" + package_ + ":binding', 'operator " + operator_symbol(name) + " is not defined for %s and %s', class (a), class (b));";
            m << "      if isa (a, '" << path << "')\n        "
              << (first ? "r = " + entry_ + "(" + std::to_string(first->index) + ", a, b);" : failure) << "\n      else\n        "
              << (second ? "r = " + entry_ + "(" + std::to_string(second->index) + ", a, b);" : failure) << "\n      end\n";
        }
        m << "    end\n";
    }
    const auto alias = [&](const std::string& name, const std::string& target, const std::string& body) {
        if (!operators.count(target) || operators.count(name)) return;
        m << "    function r = " << name << " (a, b)\n      % " << name << "  The same as " << target << ".\n      r = " << body << ";\n    end\n";
    };
    alias("times", "mtimes", "mtimes (a, b)");
    alias("rdivide", "mrdivide", "mrdivide (a, b)");
    alias("ne", "eq", "~eq (a, b)");
    alias("gt", "lt", "lt (b, a)");
    alias("ge", "le", "le (b, a)");
    if (index_group || call_group) {
        bool shift = false;
        if (index_group) {
            shift = true;
            for (const auto& overload : index_group->overloads) {
                const auto& function = *overload.function;
                const auto& parameter = referent(function.parameters.back());
                const auto bare = normalize(strip_cv(parameter.canonical));
                if (parameter.kind != TypeKind::builtin || bare == "bool" || bare == "char" || bare == "float" || bare == "double") shift = false;
            }
        }
        std::string voids;
        for (const auto& name : void_methods) voids += (voids.empty() ? "" : ", ") + m_string(name);
        m << "    function varargout = subsref (obj, s)\n"
          << "      % subsref  obj(...) and obj{...} call C++ operators; obj.name is a method or property.\n"
          << "      switch s(1).type\n";
        if (call_group || index_group) {
            m << "        case '()'\n          k = s(1).subs;\n";
            if (call_group) m << "          r = " << entry_ << "(" << call_group->index << ", obj, k{:});\n";
            else {
                if (shift) m << "          for i = 1:numel (k)\n            if isnumeric (k{i})\n              k{i} = k{i} - 1;\n            end\n          end\n";
                m << "          r = " << entry_ << "(" << index_group->index << ", obj, k{:});\n";
            }
        }
        if (index_group)
            m << "        case '{}'\n          r = " << entry_ << "(" << index_group->index << ", obj, s(1).subs{:});\n";
        m << "        otherwise\n"
          << "          if nargout == 0 && numel (s) <= 2 && strcmp (s(1).type, '.') && any (strcmp (s(1).subs, {" << voids << "}))\n"
          << "            varargout = cell (1, 0);\n"
          << "          else\n"
          << "            varargout = cell (1, max (nargout, 1));\n"
          << "          end\n"
          << "          [varargout{:}] = builtin ('subsref', obj, s);\n"
          << "          return;\n"
          << "      end\n"
          << "      if numel (s) > 1\n        [varargout{1:max (nargout, 1)}] = subsref (r, s(2:end));\n"
          << "      else\n        varargout = {r};\n      end\n    end\n";
    }
    m << "  end\n";
    bool statics = false;
    for (const auto& group : plan.methods) {
        if (!group.is_static) continue;
        if (!statics) m << "  methods (Static)\n";
        statics = true;
        m << callable(group, path + "." + group.name, false, "    ");
    }
    if (statics) m << "  end\n";
    m << "end\n";
    add_file("+" + package_ + "/" + plan.name + ".m", m.str() + header("class " + owner));
}

void Generator::emit_enum(const std::string& qualified, const std::string& name) {
    const auto& enumeration = *enums_.at(qualified);
    std::ostringstream m;
    Help help;
    help.line(name + "  " + summary(enumeration.documentation, "C++ enumeration " + qualified + "."));
    describe(help, enumeration.documentation, enumeration.documentation.brief.empty());
    help.blank();
    help.line("  Constants (numbers; functions also accept the names as char rows):");
    std::vector<std::pair<std::string, std::string>> entries;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        const auto label = renamed(qualified + "::" + enumeration.enumerators[i], escaped(enumeration.enumerators[i]));
        entries.emplace_back(label, enumeration.enumerator_values[i]);
        help.line("    " + package_ + "." + name + "." + label + " = " + enumeration.enumerator_values[i]);
    }
    help.blank();
    help.line(std::string("  C++: enum ") + (enumeration.scoped ? "class " : "") + qualified);
    m << "classdef " << name << "\n" << help.render("  ") << "  properties (Constant)\n";
    for (std::size_t i = 0; i < entries.size(); ++i)
        m << "    " << entries[i].first << " = " << entries[i].second << ";  % C++ " << qualified << "::" << enumeration.enumerators[i] << "\n";
    m << "  end\nend\n";
    add_file("+" + package_ + "/" + name + ".m", m.str() + header("enumeration " + qualified));
}

void Generator::emit_runtime_classes() {
    const auto p = package_;
    std::ostringstream m;
    m << "classdef NativeObject < handle\n"
      << "  % NativeObject  Base class of every " << p << " class: an Octave handle to a C++ object.\n"
      << "  %\n"
      << "  %   Objects of " << p << " classes own (or borrow) a C++ object, which is destroyed\n"
      << "  %   with the last Octave reference or by delete (obj); a closed object raises\n"
      << "  %   " << p << ":binding errors. == compares identity unless the class defines\n"
      << "  %   operator==. C++ errors are Octave errors with identifiers such as\n"
      << "  %   " << p << ":invalid_argument, " << p << ":out_of_range or " << p << ":binding.\n"
      << "  properties (Hidden)\n"
      << "    rosetta_h__ = uint64 (0)  % address of the native holder\n"
      << "    rosetta_id__ = 0          % id of this object in the native module\n"
      << "    rosetta_keep__ = []       % the object a borrowed C++ object lives in\n"
      << "  end\n"
      << "  methods\n"
      << "    function obj = NativeObject (varargin)\n"
      << "      % NativeObject  Not constructed directly: base of the generated classes.\n"
      << "    end\n"
      << "    function delete (obj)\n"
      << "      % delete  Release the C++ object now (later calls raise " << p << ":binding).\n"
      << "      if obj.rosetta_h__ ~= 0 || obj.rosetta_id__ ~= 0\n"
      << "        " << entry_ << "(-1, obj.rosetta_h__, obj.rosetta_id__);\n"
      << "        obj.rosetta_h__ = uint64 (0);\n"
      << "        obj.rosetta_id__ = 0;\n"
      << "      end\n"
      << "    end\n"
      << "    function tf = isclosed (obj)\n"
      << "      % isclosed  True once the object no longer refers to a C++ object.\n"
      << "      tf = obj.rosetta_h__ == 0;\n"
      << "    end\n"
      << "    function r = eq (a, b)\n"
      << "      % eq  Identity: the same Octave object.\n"
      << "      r = isa (a, '" << p << ".NativeObject') && isa (b, '" << p << ".NativeObject') && a.rosetta_id__ == b.rosetta_id__ && a.rosetta_id__ ~= 0;\n"
      << "    end\n"
      << "    function r = ne (a, b)\n"
      << "      % ne  Not the same Octave object.\n"
      << "      r = ~eq (a, b);\n"
      << "    end\n"
      << "    function disp (obj)\n"
      << "      % disp  The class and, for classes with C++ fields, their values.\n"
      << "      if obj.rosetta_h__ == 0\n"
      << "        printf ('  <%s (closed)>\\n', class (obj));\n"
      << "        return;\n"
      << "      end\n"
      << "      printf ('  <%s>\\n', class (obj));\n"
      << "      names = properties (obj);\n"
      << "      for i = 1:numel (names)\n"
      << "        try\n"
      << "          v = obj.(names{i});\n"
      << "          if ischar (v)\n"
      << "            text = ['''' v ''''];\n"
      << "          elseif (isnumeric (v) || islogical (v)) && numel (v) <= 16\n"
      << "            text = mat2str (v);\n"
      << "          else\n"
      << "            text = sprintf ('[%s %s]', strjoin (arrayfun (@num2str, size (v), 'UniformOutput', false), 'x'), class (v));\n"
      << "          end\n"
      << "        catch\n"
      << "          text = '?';\n"
      << "        end\n"
      << "        printf ('    %s: %s\\n', names{i}, text);\n"
      << "      end\n"
      << "    end\n"
      << "  end\n"
      << "end\n";
    add_file("+" + p + "/NativeObject.m", m.str() + header("runtime class"));
    std::ostringstream f;
    f << "classdef NativeFunction < " << p << ".NativeObject\n"
      << "  % NativeFunction  A C++ std::function returned to Octave.\n"
      << "  %\n"
      << "  %   C++ functions returning std::function return a function handle that calls\n"
      << "  %   it; the handle keeps this object (and the C++ function) alive.\n"
      << "  methods\n"
      << "    function obj = NativeFunction (varargin)\n"
      << "      obj@" << p << ".NativeObject (varargin{:});\n"
      << "    end\n"
      << "    function varargout = call (obj, varargin)\n"
      << "      % call  Call the C++ function.\n"
      << "      [varargout{1:max (nargout, 1)}] = " << entry_ << "(0, obj, varargin{:});\n"
      << "    end\n"
      << "  end\n"
      << "  methods (Static, Hidden)\n"
      << "    function f = handle__ (obj)\n"
      << "      f = @(varargin) " << entry_ << "(0, obj, varargin{:});\n"
      << "    end\n"
      << "  end\n"
      << "end\n";
    add_file("+" + p + "/NativeFunction.m", f.str() + header("runtime class"));
}

GeneratedOctaveFullSource Generator::run() {
    if (!identifier(name_) || name_[0] == '_') throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = options_.package.empty() ? name_ : options_.package;
    if (!identifier(package_) || package_[0] == '_' || keyword(package_)) throw BindingError("Octave package name must be an identifier: " + package_);
    entry_ = "__" + name_ + "__";
    prepare();

    // Package members share one namespace (classes, enums, functions, constants, subpackages).
    std::map<std::string, std::string> taken;
    const auto reserve = [&](const std::string& name, const std::string& what) {
        const auto inserted = taken.emplace(name, what);
        if (!inserted.second) throw BindingError("Octave name collision in package " + package_ + ": " + name + " (" + inserted.first->second + " and " + what + ")");
    };
    const auto available = [&](const std::string& name) { return !taken.count(name); };
    for (const char* runtime : {"NativeObject", "NativeFunction", "Contents"}) reserve(runtime, "Rosetta runtime");
    {
        std::set<std::string> collided;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!available(plan.name)) {
                if (!flat_) throw BindingError("Octave name collision: " + plan.name);
                reject(owner, "class name collides with another name in the package");
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
            if (!flat_) throw BindingError("Octave name collision: " + name);
            reject(qualified, "enum name collides with another name in the package");
            continue;
        }
        reserve(name, "enum " + qualified);
        enum_names_[qualified] = name;
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    // Free functions need no protection from object protocol names.
    for (auto& group : functions) {
        const auto found = protocol_escapes_.find(group.name);
        if (found != protocol_escapes_.end()) group.name = found->second;
    }
    std::set<std::string> submodules;
    for (const auto& group : functions) if (!group.module.empty()) submodules.insert(group.module);
    for (const auto& submodule : submodules) {
        for (std::size_t start = 0;;) {
            const auto dot = submodule.find('.', start);
            const auto part = submodule.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!identifier(part) || keyword(part)) throw BindingError("Octave subpackage name must be an identifier: " + submodule);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        const auto top = submodule.substr(0, submodule.find('.'));
        if (taken.count(top) && taken.at(top) != "subpackage") throw BindingError("Octave subpackage name collides with another name: " + top);
        taken.emplace(top, "subpackage");
    }
    std::set<std::pair<std::string, std::string>> function_names; // (submodule, name)
    for (const auto& group : functions) {
        if (group.module.empty()) reserve(group.name, "function " + group.overloads.front().function->qualified_name());
        else if (!function_names.emplace(group.module, group.name).second) throw BindingError("Octave name collision in " + group.module + ": " + group.name);
    }
    // Class members: methods, properties and constants share the classdef; a member named like its
    // class would be the constructor, and properties may not redefine inherited ones.
    std::map<std::string, std::set<std::string>> properties_of;
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::set<std::string> inherited;
        const std::function<void(const std::string&)> walk = [&](const std::string& cls) {
            for (const auto& base : classes_.at(cls).bases) {
                for (const auto& name : properties_of[base]) inherited.insert(name);
                walk(base);
            }
        };
        walk(owner);
        std::set<std::string> names{"isclosed"};
        for (auto& group : plan.methods) {
            if (group.name == plan.name || group.name == "isclosed") group.name += "_";
            names.insert(group.name);
        }
        for (auto& field : plan.fields) {
            while (field.name == plan.name || names.count(field.name) || inherited.count(field.name)) field.name += "_";
            names.insert(field.name);
            properties_of[owner].insert(field.name);
        }
        for (auto& constant : plan.constants) {
            while (constant.name == plan.name || names.count(constant.name) || inherited.count(constant.name)) constant.name += "_";
            names.insert(constant.name);
            properties_of[owner].insert(constant.name);
        }
    }
    std::vector<ConstantPlan> constants;
    for (auto& constant : plan_constants([&](const Variable& variable) {
             const auto qualified = variable.qualified_name();
             return renamed(qualified, escaped(flat(variable.namespaces) ? variable.name : exported(qualified)));
         })) {
        if (!available(constant.name)) { reject(constant.expression.substr(2), "constant name collides with another name in the package"); continue; }
        reserve(constant.name, "constant " + constant.expression);
        constants.push_back(constant);
    }
    check_diagnostics();
    emit_cpp(result_.source, result_.parts, functions, constants, "Octave package " + package_ + " (" + name_ + ", full API).");

    // ------------------------------------------------------------------ the oct-file glue
    std::vector<unsigned long long> masks(symbols_.size() + 1, 0);
    std::vector<int> voids(symbols_.size() + 1, 0);
    const auto plan_group = [&](const Group& group, bool is_method) {
        unsigned long long mask = 0;
        for (const auto& overload : group.overloads) {
            const std::size_t skip = overload.receives_self ? 1 : 0;
            const std::size_t base = (is_method && !overload.extension) || overload.receives_self ? 1 : 0;
            for (std::size_t i = skip; i < overload.function->parameters.size(); ++i)
                if (writeback(overload.function->parameters[i]) && base + i - skip < 64) mask |= 1ULL << (base + i - skip);
        }
        masks.at(group.index) = mask;
        voids.at(group.index) = returns_void(group) ? 1 : 0;
    };
    for (const auto& group : functions) plan_group(group, false);
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto& group : plan.methods) plan_group(group, !group.is_static);
        for (const auto& group : plan.operators) plan_group(group, true);
        for (const auto& field : plan.fields) voids.at(field.setter_index) = 1;
    }
    std::ostringstream glue;
    glue << "\n// ---------------------------------------------------------------- Octave\n"
         << "#include <octave/oct.h>\n#include <octave/interpreter.h>\n#include <octave/ov-classdef.h>\n"
         << "#include <octave/cdef-object.h>\n#include <octave/cdef-utils.h>\n#include <octave/oct-map.h>\n#include <octave/version.h>\n"
         << "#include <unordered_map>\n"
         << "#if OCTAVE_MAJOR_VERSION < 9\n#error Rosetta's full Octave API requires Octave 9 or newer\n#endif\n"
         << "namespace ROSETTA_LISP_MODULE { namespace rosetta_oct {\n"
         << "inline const char* const package = " << quote(package_) << ";\n"
         << "inline const char* const classes[] = {";
    for (const auto& owner : class_order_) glue << quote(class_path(owner)) << ", ";
    glue << "nullptr};\ninline constexpr std::size_t class_count = " << class_order_.size() << ";\n"
         << "inline const char* const generics[] = {";
    for (const auto& name : generic_order_) glue << quote(name) << ", ";
    glue << "nullptr};\ninline constexpr std::size_t generic_count = " << generic_order_.size() << ";\n"
         << "inline constexpr std::size_t function_count = sizeof(ROSETTA_LISP_MODULE::table) / sizeof(ROSETTA_LISP_MODULE::table[0]);\n"
         << "inline const unsigned long long out_masks[] = {";
    for (std::size_t i = 0; i < masks.size(); ++i) glue << (i ? ", " : "") << masks[i] << "ULL";
    glue << "};\ninline const unsigned char void_results[] = {";
    for (std::size_t i = 0; i < voids.size(); ++i) glue << (i ? ", " : "") << voids[i];
    glue << "};\n"
         << "inline int init(int version, ROSETTA_LISP_MODULE::rosetta_lisp::InvokeFn invoke, ROSETTA_LISP_MODULE::rosetta_lisp::RetainFn retain) {\n"
         << "    return rosetta_" << name_ << "_init(version, invoke, retain);\n}\n"
         << "} }\n"
         << detail::octave_full_runtime
         << "DEFMETHOD_DLD(" << entry_ << ", interp, args, nargout,\n"
         << "    " << quote("-*- texinfo -*-\n@deftypefn {} {} " + entry_ + " (@var{index}, @dots{})\nNative entry point of the Octave package "
                          + package_ + " (generated by Rosetta); call the functions of +" + package_ + " instead.\n@end deftypefn") << ") {\n"
         << "    return ROSETTA_LISP_MODULE::rosetta_oct::entry(interp, args, nargout);\n}\n";
    result_.source += glue.str();

    // ------------------------------------------------------------------ the package
    emit_runtime_classes();
    for (const auto& [qualified, name] : enum_names_) emit_enum(qualified, name);
    for (const auto& owner : class_order_) emit_class(owner);
    for (const auto& group : functions) {
        const auto file = package_dir(group.module) + "/" + group.name + ".m";
        add_file(file, callable(group, group_path(group), false, "") +
                           header("function " + group.overloads.front().function->qualified_name()));
    }
    for (const auto& constant : constants) {
        Help help;
        help.line(constant.name + "  " + summary(*constant.documentation, "C++ constant " + normalize(constant.expression) + "."));
        help.blank();
        help.line("  r = " + package_ + "." + constant.name + " ()");
        describe(help, *constant.documentation, constant.documentation->brief.empty());
        help.blank();
        help.line("  Output:");
        help.line("    r  " + octave_type(*constant.type));
        help.blank();
        help.line("  C++: " + normalize(constant.expression));
        add_file("+" + package_ + "/" + constant.name + ".m", "function r = " + constant.name + " ()\n" + help.render("  ") + "  r = " +
                 entry_ + "(" + std::to_string(constant.index) + ");\nend\n" + header("constant " + normalize(constant.expression)));
    }

    // Contents.m: the package overview.
    Help contents;
    contents.line(package_ + "  Octave interface to the C++ library " + name_ + " (generated by Rosetta).");
    contents.blank();
    contents.line("  addpath (DIR) on the directory holding +" + package_ + " and " + entry_ + ".oct.");
    const auto section = [&](const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows) {
        if (rows.empty()) return;
        contents.blank();
        contents.line("  " + title + ":");
        for (const auto& [name, text] : rows) contents.line("    " + name + "  " + text);
    };
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& owner : class_order_) rows.emplace_back(package_ + "." + classes_.at(owner).name, summary(classes_.at(owner).record->documentation, "C++ class " + owner + "."));
    section("Classes", rows);
    rows.clear();
    for (const auto& [qualified, name] : enum_names_) rows.emplace_back(package_ + "." + name, summary(enums_.at(qualified)->documentation, "C++ enumeration " + qualified + "."));
    section("Enumerations", rows);
    rows.clear();
    for (const auto& group : functions) {
        const auto* function = group.overloads.front().function;
        for (const auto& overload : group.overloads) if (!overload.function->documentation.empty()) { function = overload.function; break; }
        rows.emplace_back(group_path(group), summary(function->documentation, "C++ " + function->qualified_name() + "."));
    }
    section("Functions", rows);
    rows.clear();
    for (const auto& constant : constants) rows.emplace_back(package_ + "." + constant.name, summary(*constant.documentation, "C++ " + normalize(constant.expression) + "."));
    section("Constants", rows);
    contents.blank();
    contents.line("  Runtime: " + package_ + ".NativeObject (base class), " + package_ + ".NativeFunction (C++ functions).");
    result_.contents = contents.render("") + "\n" + header("package overview");
    result_.contents_path = "+" + package_ + "/Contents.m";
    result_.diagnostics = diagnostics_;
    return std::move(result_);
}
} // namespace

GeneratedOctaveFullSource generate_octave_full(const Module& module, const std::string& name, bool skip_unsupported,
                                               const BindingPolicy& policy, const OctaveFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Octave source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
