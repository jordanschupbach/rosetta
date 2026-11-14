// Full OCaml backend (--ocaml-api full): a library whose top module (NAME.ml + NAME.mli) holds a
// submodule per C++ class (type t, create, functions taking the object first), enum modules with
// variants, module functions and constants, and policy modules as submodules.
//
// Classes are phantom-typed native objects, `-'a obj`, whose row lists the class and its bases
// (type circle = [ `Circle | `Shape ] obj): functions take `[> `Shape ] obj`, so derived objects
// pass without coercion, and (c :> shape) or Shape.upcast upcast explicitly. Overloads become
// separate functions named after their parameter types (pick, pick_float), default arguments
// optional labelled arguments, mutable reference parameters `ref` cells. OCaml "subclasses" of
// classes with virtual methods (directors) are made by Class.extend with one optional closure per
// virtual method; Class.Base runs the C++ implementation. C++ exceptions become Invalid_argument,
// Failure, Out_of_memory or NAME.Error (kind, message); exceptions in OCaml callbacks propagate
// through C++ unchanged. Doxygen documentation becomes odoc comments in the .mli.
//
// It writes a C++ source (the library every full C ABI backend shares: cabi_full.hpp,
// lisp_full_runtime.hpp, plus the OCaml stubs of ocaml_full_runtime.hpp), NAME.ml (the runtime
// module Rosetta__ and the wrappers), NAME.mli (types and documentation) and a findlib META.
#include "rosetta/ocaml.hpp"
#include "cabi_full.hpp"
#include "ocaml_full_runtime.hpp"
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

bool ocaml_keyword(const std::string& name) {
    static const std::set<std::string> keywords = {"and", "as", "assert", "asr", "begin", "class", "constraint", "do", "done",
        "downto", "effect", "else", "end", "exception", "external", "false", "for", "fun", "function", "functor", "if", "in",
        "include", "inherit", "initializer", "land", "lazy", "let", "lor", "lsl", "lsr", "lxor", "match", "method", "mod",
        "module", "mutable", "new", "nonrec", "object", "of", "open", "or", "private", "rec", "sig", "struct", "then", "to",
        "true", "try", "type", "val", "virtual", "when", "while", "with"};
    return keywords.count(name) != 0;
}
// Type names a class alias must not take: predefined types, and the module's own.
bool reserved_type(const std::string& name) {
    static const std::set<std::string> names = {"int", "char", "string", "bytes", "float", "bool", "unit", "exn", "array", "list",
        "option", "int32", "int64", "nativeint", "format", "format4", "format6", "lazy_t", "extension_constructor",
        "floatarray", "result", "ref", "in_channel", "out_channel", "obj", "error_kind", "t", "iarray", "atomic_loc"};
    return names.count(name) != 0;
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
bool lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
bool upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
// camelCase, PascalCase or snake_case -> snake_case (runs of _ collapsed inside the name).
std::string snake(const std::string& name) {
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_' && !result.empty() && result.back() == '_') continue;
        if (upper(c) && i > 0 && name[i - 1] != '_' &&
            (lower(name[i - 1]) || digit(name[i - 1]) || (upper(name[i - 1]) && i + 1 < name.size() && lower(name[i + 1]))))
            if (!result.empty() && result.back() != '_') result += '_';
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}
// An OCaml value (or label) name for a C++ name.
// Names starting with a capital (PascalCase, HTTPServer) become snake_case; others keep their
// spelling (camelCase stays camelCase, so that camelCase and camel_case remain distinct), runs of
// underscores (a__b from qualified names) collapsed.
std::string value_name(const std::string& cpp) {
    std::string name, source;
    if (!cpp.empty() && upper(cpp[0])) source = snake(cpp);
    else
        for (const char c : cpp)
            if (!(c == '_' && !source.empty() && source.back() == '_')) source += c;
    for (const char c : source) name += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
    if (name.empty() || name.find_first_not_of('_') == std::string::npos) name = "x" + name;
    if (digit(name[0])) name = "n" + name;
    if (ocaml_keyword(name)) name += "_";
    return name;
}
// An OCaml module (or constructor) name: the C++ name's parts (a::b joined as a__b) capitalized.
std::string module_name(const std::string& cpp) {
    std::string result;
    std::size_t start = 0;
    while (start <= cpp.size()) {
        const auto split = cpp.find("__", start);
        auto part = cpp.substr(start, split == std::string::npos ? std::string::npos : split - start);
        while (!part.empty() && part[0] == '_') part.erase(0, 1);
        if (!part.empty()) {
            part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
            result += part;
        }
        if (split == std::string::npos) break;
        start = split + 2;
    }
    std::string clean;
    for (const char c : result) clean += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
    if (clean.empty() || !upper(clean[0])) clean = "M" + clean;
    return clean;
}
std::string type_alias(const std::string& module) {
    auto name = value_name(module);
    if (reserved_type(name)) name += "_";
    return name;
}
std::string ml_string(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += std::string("\\") + c;
        else if (c == '\n') result += "\\n";
        else if (c == '\t') result += "\\t";
        else if (c == '\r') result += "\\r";
        else if (static_cast<unsigned char>(c) < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof buffer, "\\%03d", c); result += buffer; }
        else result += c;
    }
    return result + '"';
}
std::string ml_char(char c) {
    if (c == '\\' || c == '\'') return std::string("'\\") + c + "'";
    if (c == '\n') return "'\\n'";
    if (c == '\t') return "'\\t'";
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7f) {
        char buffer[12];
        std::snprintf(buffer, sizeof buffer, "'\\%03d'", static_cast<unsigned char>(c));
        return buffer;
    }
    return std::string("'") + c + "'";
}
// The text of a C++ string literal ("..."), or false when it uses escapes we do not decode.
bool cpp_string(const std::string& literal, std::string& out) {
    if (literal.size() < 2 || literal.front() != '"' || literal.back() != '"') return false;
    out.clear();
    for (std::size_t i = 1; i + 1 < literal.size(); ++i) {
        char c = literal[i];
        if (c == '\\') {
            if (i + 2 >= literal.size()) return false;
            const char e = literal[++i];
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == '\\' || e == '"' || e == '\'' || e == '?') c = e;
            else if (e == '0' && (i + 1 >= literal.size() - 1 || !digit(literal[i + 1]))) c = '\0';
            else return false;
        }
        out += c;
    }
    return true;
}

// --- documentation (odoc) ---------------------------------------------------------------

// Plain text for odoc: markup characters escaped.
std::string odoc_escape(const std::string& text) {
    std::string result;
    for (const char c : text) {
        if (c == '{' || c == '}' || c == '[' || c == ']' || c == '@') result += '\\';
        result += c;
    }
    return result;
}
bool balanced_brackets(const std::string& text) {
    int depth = 0;
    for (const char c : text) {
        if (c == '[') ++depth;
        if (c == ']' && --depth < 0) return false;
    }
    return depth == 0;
}
// [code]: odoc code spans hold balanced brackets verbatim.
std::string code_span(const std::string& text) {
    if (trim(text).empty()) return "[\"\"]";
    if (!text.empty() && balanced_brackets(text) && text.find('\n') == std::string::npos) return "[" + text + "]";
    return "{e " + odoc_escape(text) + "}";
}
// A C++ default argument as C++ source.
std::string default_text(const DefaultValue& value) {
    if (value.kind.empty()) return "...";
    if (value.kind == "char") return "'" + value.value + "'";
    if (value.kind == "default" || value.value.empty()) return "{}";
    return value.value;
}
// Doxygen inline markup (`code`, **bold**, *emphasis*) as odoc.
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
            if (close != std::string::npos && close > i + 2) { result += "{b " + inline_doc(text.substr(i + 2, close - i - 2)) + "}"; i = close + 2; continue; }
        }
        if (c == '*' && i + 1 < text.size() && !std::isspace(static_cast<unsigned char>(text[i + 1])) && text[i + 1] != '/') {
            const auto close = text.find('*', i + 1);
            if (close != std::string::npos && close > i + 1 && !std::isspace(static_cast<unsigned char>(text[close - 1])) &&
                text.substr(i + 1, close - i - 1).find('\n') == std::string::npos) {
                result += "{i " + inline_doc(text.substr(i + 1, close - i - 1)) + "}";
                i = close + 1;
                continue;
            }
        }
        result += odoc_escape(std::string(1, c));
        ++i;
    }
    return result;
}
// One line of prose, its line breaks folded.
std::string one_paragraph(const std::string& text) {
    std::string result;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        line = trim(line);
        if (line.empty()) continue;
        result += (result.empty() ? "" : " ") + line;
    }
    return result;
}
// A text block: paragraphs, with "- item" lines (continued by indented lines) as {ul} lists.
std::vector<std::string> text_blocks(const std::string& text) {
    std::vector<std::string> blocks;
    std::string paragraph;
    std::vector<std::string> items;
    const auto flush_paragraph = [&] { if (!trim(paragraph).empty()) blocks.push_back(inline_doc(one_paragraph(paragraph))); paragraph.clear(); };
    const auto flush_list = [&] {
        if (items.empty()) return;
        std::string list = "{ul";
        for (const auto& item : items) list += "\n{- " + inline_doc(one_paragraph(item)) + "}";
        blocks.push_back(list + "}");
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
std::string code_block(const std::string& text, const std::string& language) {
    const auto tag = language.empty() || language == "cpp" || language == "c++" ? std::string("cpp") : value_name(language);
    if (text.find("]}") == std::string::npos) return "{@" + tag + "[\n" + text + "\n]}";
    return "{v\n" + replace_all(text, "v}", "v }") + "\n v}";
}
// Breaks a paragraph into lines of about `width` characters, never inside a code span or in front
// of a word odoc would read as a list or tag.
std::vector<std::string> wrap(const std::string& text, std::size_t width) {
    std::vector<std::string> words;
    std::string current;
    int depth = 0;
    for (const char c : text) {
        if (c == '[' || c == '{') ++depth;
        if ((c == ']' || c == '}') && depth > 0) --depth;
        if (c == ' ' && depth == 0) { if (!current.empty()) words.push_back(current); current.clear(); continue; }
        current += c;
    }
    if (!current.empty()) words.push_back(current);
    std::vector<std::string> lines;
    std::string line;
    for (const auto& word : words) {
        const bool risky = word[0] == '-' || word[0] == '+' || word[0] == '@' || word.rfind("{-", 0) == 0;
        if (!line.empty() && line.size() + 1 + word.size() > width && !risky) { lines.push_back(line); line.clear(); }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

struct ODoc {
    std::string summary;
    std::vector<std::string> body;                             // blocks (paragraphs, {ul ...}, code)
    std::vector<std::pair<std::string, std::string>> params;  // label, text
    std::string returns;
    std::vector<std::pair<std::string, std::string>> raises;  // exception, text
    std::vector<std::string> see;
    std::string since;
    std::string deprecated;
    bool is_deprecated = false;
    std::string render(const std::string& indent) const {
        std::vector<std::string> lines;
        const auto add_block = [&](const std::string& block) {
            if (block.rfind("{@", 0) == 0 || block.rfind("{v", 0) == 0 || block.rfind("{ul", 0) == 0) {
                std::istringstream split(block);
                for (std::string line; std::getline(split, line);) lines.push_back(line);
                return;
            }
            for (const auto& line : wrap(block, 92)) lines.push_back(line);
        };
        add_block(summary.empty() ? "Native binding." : summary);
        for (const auto& block : body) { lines.push_back(""); add_block(block); }
        bool tags = false;
        const auto tag = [&](const std::string& text) {
            if (!tags) { lines.push_back(""); tags = true; }
            const auto wrapped = wrap(text, 92);
            for (std::size_t i = 0; i < wrapped.size(); ++i) lines.push_back(i ? "  " + wrapped[i] : wrapped[i]);
        };
        for (const auto& [name, text] : params) tag("@param " + name + " " + (text.empty() ? "the C++ argument" : text));
        if (!returns.empty()) tag("@return " + returns);
        for (const auto& [exception, text] : raises) tag("@raise " + exception + " " + text);
        for (const auto& item : see) tag("@see " + item);
        if (!since.empty()) tag("@since " + since);
        if (is_deprecated) tag("@deprecated " + (deprecated.empty() ? std::string("Deprecated in C++.") : deprecated));
        std::string text;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i == 0) text += indent + "(** " + lines[i];
            else text += "\n" + (lines[i].empty() ? std::string() : indent + "    " + lines[i]);
        }
        return safe_comment(text) + " *)\n";
    }
    // The OCaml lexer reads strings, quoted strings and nested comments inside comments too.
    static std::string safe_comment(std::string text) {
        const auto head = text.find("(**");
        auto body = text.substr(head + 3);
        body = replace_all(body, "(*", "( *");
        body = replace_all(body, "*)", "* )");
        std::string fixed;
        for (std::size_t i = 0; i < body.size(); ++i) {
            fixed += body[i];
            if (body[i] != '{') continue;
            std::size_t j = i + 1;
            while (j < body.size() && (lower(body[j]) || body[j] == '_')) ++j;
            if (j < body.size() && body[j] == '|') fixed += ' ';
        }
        if (std::count(fixed.begin(), fixed.end(), '"') % 2) fixed = replace_all(fixed, "\"", "''");
        return text.substr(0, head + 3) + fixed;
    }
};

// C++ operators and their OCaml names ("" = not bound).
std::string ml_operator(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"pos", "add"}}, {"operator-", {"neg", "sub"}}, {"operator*", {nullptr, "mul"}}, {"operator/", {nullptr, "div"}},
        {"operator%", {nullptr, "rem"}}, {"operator==", {nullptr, "equal"}}, {"operator!=", {nullptr, "not_equal"}},
        {"operator<", {nullptr, "less"}}, {"operator<=", {nullptr, "less_equal"}}, {"operator>", {nullptr, "greater"}},
        {"operator>=", {nullptr, "greater_equal"}}, {"operator&", {nullptr, "logand"}}, {"operator|", {nullptr, "logor"}},
        {"operator^", {nullptr, "logxor"}}, {"operator~", {"lognot", nullptr}}, {"operator!", {"not_", nullptr}},
        {"operator<<", {nullptr, "shift_left"}}, {"operator>>", {nullptr, "shift_right"}}, {"operator[]", {nullptr, "get"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// An OCaml type for a C++ type, with its conversions: `enc` (ctx -> 'a -> node, OCaml to C++)
// and `dec` (node -> 'a, C++ to OCaml) are OCaml expressions.
struct MT {
    std::string type;
    std::string enc;
    std::string dec;
    std::string word;  // for overload names
    std::string why;   // non-empty: not representable
};
std::string paren(const std::string& type) {
    return type.find("->") == std::string::npos || (type.front() == '(' && type.back() == ')') ? type : "(" + type + ")";
}

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const OcamlFullOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedOcamlFullSource run();

private:
    OcamlFullOptions options_;
    std::string top_;                                   // the top module (NAME capitalized)
    std::map<std::string, std::string> aliases_;        // class -> type alias
    std::map<std::string, std::vector<std::string>> constructors_;  // enum -> OCaml constructors (parallel to enumerators)
    std::map<std::string, std::string> labels_;         // enum::enumerator -> C++ label (EnumTable)
    std::ostringstream ml_, mli_;

    std::string language() const override { return "OCaml"; }
    std::string target_name(const std::string& cpp) const override { return value_name(cpp); }
    std::string method_name(const std::string& cpp) const override { return value_name(cpp); }
    std::string type_name(const std::string& cpp) const override { return module_name(cpp); }
    std::string field_name(const std::string& cpp) const override { return value_name(cpp); }
    std::string rename_target(const std::string& name, const std::string& fallback) const override {
        return !fallback.empty() && upper(fallback[0]) && fallback.find('_') == std::string::npos ? module_name(name) : value_name(name);
    }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return ml_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return value_name(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken{"self"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : value_name(cpp);
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + cpp_signature(*overload.function) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + cpp_signature(*overload.function) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override { return plan.name + " is abstract: make an OCaml subclass with " + plan.name + ".extend"; }
    std::string protected_message(const ClassPlan& plan) const override { return plan.name + " constructor is protected: use " + plan.name + ".extend"; }
    std::string symbol_prefix() const override { return "rosetta_ml_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_OCAML"; }
    std::string module_prefix() const override { return "rosetta_ocaml_module_"; }

    // ------------------------------------------------------------ types
    MT ml(const Type& type, bool in) const;
    MT ml_spelled(std::string cpp, bool in) const;
    MT element_sequence(const MT& element, const std::string& canonical) const;
    MT tuple(const std::vector<MT>& items) const;
    MT function_type(const Type& signature, bool in) const;
    MT object_type(const std::string& owner, bool in) const;
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    MT parameter(const Type& type) const {
        if (!writeback(type)) return ml(type, true);
        auto inner = ml(referent(type), true);
        if (!inner.why.empty()) return inner;
        return {paren(inner.type) + " ref", "(Rosetta__.ref_e " + inner.enc + " " + inner.dec + ")", "", inner.word + "_ref", ""};
    }
    MT result(const Function& function) const {
        if (is_void(function.result)) return {"unit", "Rosetta__.unit_e", "Rosetta__.unit_d", "unit", ""};
        return ml(function.result, false);
    }
    std::string ml_literal(const DefaultValue& value, const Type& type, const MT& mt) const;
    std::string tag_row(const std::string& owner) const;
    std::string open_type(const std::string& owner) const { return "[> `" + classes_.at(owner).name + " ] obj"; }

    // ------------------------------------------------------------ documentation
    std::string cpp_signature(const Function& function) const;
    std::string exception_name(const std::string& cpp) const;
    ODoc describe(const Documentation& doc, const std::string& fallback) const;

    // ------------------------------------------------------------ emission
    // The problem of an overload in OCaml ("" = representable).
    std::string overload_problem(const Overload& overload) const;
    std::string words(const Overload& overload) const;
    std::vector<std::string> overload_names(const Group& group, std::set<std::string>& taken) const;
    struct Emit {
        std::string name;            // OCaml value name
        const Overload* overload = nullptr;
        std::size_t index = 0;       // native function
        std::string self_owner;      // non-empty: the object is the first positional argument
        long generic = -1;           // >= 0: dispatch to OCaml overrides first
        std::string note;            // extra documentation paragraph
    };
    void emit_function(const Emit& e, const std::string& indent);
    void emit_constructor(const ClassPlan& plan, const Overload& overload, const std::string& name, bool extend, const std::string& indent);
    void emit_class(const std::string& owner);
    void emit_enum(const std::string& qualified, const std::string& name);
    long dispatch_generic(const Overload& overload) const;
    const Group* find_group(const std::string& owner, const std::string& name, std::string* found_owner = nullptr) const;
    std::vector<std::string> ancestors(const std::string& owner) const;
    std::set<std::string> dispatchable_;  // virtual_key of methods some director overrides
    std::string word_of(const std::string& type) const;
};

// ------------------------------------------------------------------ types

std::string Generator::tag_row(const std::string& owner) const {
    std::vector<std::string> tags;
    const std::function<void(const std::string&)> walk = [&](const std::string& cls) {
        const auto& name = classes_.at(cls).name;
        if (std::find(tags.begin(), tags.end(), name) == tags.end()) tags.push_back(name);
        for (const auto& base : classes_.at(cls).bases) walk(base);
    };
    walk(owner);
    std::string row = "[ ";
    for (std::size_t i = 0; i < tags.size(); ++i) row += (i ? " | `" : "`") + tags[i];
    return row + " ] obj";
}

MT Generator::object_type(const std::string& owner, bool in) const {
    const auto& alias = aliases_.at(owner);
    return {in ? open_type(owner) : alias, "Rosetta__.obj_e", "Rosetta__.obj_d", alias, ""};
}

std::string Generator::word_of(const std::string& type) const {
    std::string word;
    for (const char c : type) {
        if (std::isalnum(static_cast<unsigned char>(c))) word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!word.empty() && word.back() != '_') word += '_';
    }
    while (!word.empty() && word.back() == '_') word.pop_back();
    return word.empty() ? "value" : word;
}

MT Generator::element_sequence(const MT& element, const std::string& canonical) const {
    if (!element.why.empty()) return element;
    const auto bare = strip_cv(canonical);
    if (element.type == "float") return {"float array", "Rosetta__.reals_e", "Rosetta__.reals_d", "float_array", ""};
    if (bare == "unsigned char") return {"bytes", "Rosetta__.bytes_e", "Rosetta__.bytes_d", "bytes", ""};
    return {paren(element.type) + " list", "(Rosetta__.list_e " + element.enc + ")", "(Rosetta__.list_d " + element.dec + ")", element.word + "_list", ""};
}

MT Generator::tuple(const std::vector<MT>& items) const {
    for (const auto& item : items) if (!item.why.empty()) return item;
    if (items.empty()) return {"unit", "(fun _ () -> Rosetta__.Tup [])", "Rosetta__.unit_d", "unit", ""};
    if (items.size() == 1)
        return {items[0].type, "(fun c x -> Rosetta__.Tup [" + items[0].enc + " c x])",
                "(fun n -> match Rosetta__.parts n 1 with [x] -> " + items[0].dec + " x | _ -> Rosetta__.malformed ())", items[0].word, ""};
    std::string type, vars, encs, pattern, decs, word;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto x = "x" + std::to_string(i + 1);
        type += (i ? " * " : "") + paren(items[i].type);
        vars += (i ? ", " : "") + x;
        encs += (i ? "; " : "") + items[i].enc + " c " + x;
        pattern += (i ? "; " : "") + x;
        decs += (i ? ", " : "") + items[i].dec + " " + x;
        word += (i ? "_" : "") + items[i].word;
    }
    return {"(" + type + ")", "(fun c (" + vars + ") -> Rosetta__.Tup [" + encs + "])",
            "(fun n -> match Rosetta__.parts n " + std::to_string(items.size()) + " with [" + pattern + "] -> (" + decs +
                ") | _ -> Rosetta__.malformed ())",
            word, ""};
}

// std::function: OCaml closures. In (OCaml passes a closure): C++ calls it with decoded arguments
// and the result is encoded back; out (C++ returns a native function): a closure calling it.
MT Generator::function_type(const Type& function, bool in) const {
    std::vector<MT> arguments;
    for (std::size_t i = 1; i < function.arguments.size(); ++i) {
        arguments.push_back(ml(function.arguments[i], !in));
        if (!arguments.back().why.empty()) return arguments.back();
    }
    MT result = function.arguments.empty() || is_void(function.arguments[0]) ? MT{"unit", "Rosetta__.unit_e", "Rosetta__.unit_d", "unit", ""}
                                                                              : ml(function.arguments[0], in);
    if (!result.why.empty()) return result;
    std::string type;
    for (const auto& argument : arguments) type += (argument.type.find("->") != std::string::npos ? "(" + argument.type + ")" : argument.type) + " -> ";
    if (arguments.empty()) type = "unit -> ";
    type = "(" + type + (result.type.find("->") != std::string::npos ? "(" + result.type + ")" : result.type) + ")";
    // In: the handler decodes the arguments C++ sends, applies the closure, encodes its result.
    std::string applied = "f";
    for (std::size_t i = 0; i < arguments.size(); ++i) applied += " (" + arguments[i].dec + " (Stdlib.Array.get args " + std::to_string(i) + "))";
    if (arguments.empty()) applied += " ()";
    const auto enc = "(fun c f -> Rosetta__.fn_e (fun c args -> " + result.enc + " c (" + applied + ")) c)";
    // Out: a closure encoding its arguments for the native function.
    std::string params, encs;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto x = "x" + std::to_string(i + 1);
        params += " " + x;
        encs += (i ? "; " : "") + arguments[i].enc + " c " + x;
    }
    if (arguments.empty()) params = " ()";
    const auto dec = "(fun n -> let h = Rosetta__.fn_d n in fun" + params + " -> Rosetta__.apply h (fun c -> [" + encs + "]) " + result.dec + ")";
    return {type, enc, dec, "fn", ""};
}

MT Generator::ml(const Type& type, bool in) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return {"string", "Rosetta__.string_e", "Rosetta__.string_d", "string", ""};
        return ml(pointee, in);
    }
    if (is_void(target)) return {"unit", "Rosetta__.unit_e", "Rosetta__.unit_d", "unit", ""};
    if (!conversion(target).empty()) return ml_spelled(policy_.conversions.at(conversion(target)), in);
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return {"bool", "Rosetta__.bool_e", "Rosetta__.bool_d", "bool", ""};
        if (bare == "char") return {"char", "Rosetta__.char_e", "Rosetta__.char_d", "char", ""};
        if (bare == "float" || bare == "double" || bare == "long double") return {"float", "Rosetta__.float_e", "Rosetta__.float_d", "float", ""};
        static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int",
            "long", "unsigned long", "long long", "unsigned long long"};
        if (integers.count(bare)) return {"int", "Rosetta__.int_e", "Rosetta__.int_d", "int", ""};
        return {"", "", "", "", "unsupported OCaml type " + target.canonical};
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return {"int", "Rosetta__.int_e", "Rosetta__.int_d", "int", ""};
        return {found->second + ".t", found->second + ".enc__", found->second + ".dec__", value_name(found->second), ""};
    }
    if (bound(target)) return object_type(target.declaration, in);
    if (bare == "std::filesystem::path") return {"string", "Rosetta__.string_e", "Rosetta__.string_d", "string", ""};
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {"unit", "Rosetta__.unit_e", "Rosetta__.unit_d", "unit", ""};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i, bool direction) {
            return i < target.arguments.size() ? ml(target.arguments[i], direction) : MT{"", "", "", "", "incomplete template " + target.canonical};
        };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view")
            return {"string", "Rosetta__.string_e", "Rosetta__.string_d", "string", ""};
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set") {
            if (target.arguments.empty()) return {"", "", "", "", "incomplete template " + target.canonical};
            const auto element = arg(0, in);
            if (strip_cv(target.arguments[0].canonical) == "bool")
                return {"bool list", "(Rosetta__.list_e Rosetta__.bool_e)", "(Rosetta__.list_d Rosetta__.bool_d)", "bool_list", ""};
            return element_sequence(element, target.arguments[0].canonical);
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") {
            const auto key = arg(0, in), value = arg(1, in);
            if (!key.why.empty()) return key;
            if (!value.why.empty()) return value;
            return {"(" + paren(key.type) + " * " + paren(value.type) + ") list", "(Rosetta__.map_e " + key.enc + " " + value.enc + ")",
                    "(Rosetta__.map_d " + key.dec + " " + value.dec + ")", "map", ""};
        }
        if (declaration == "std::optional") {
            const auto element = arg(0, in);
            if (!element.why.empty()) return element;
            return {paren(element.type) + " option", "(Rosetta__.option_e " + element.enc + ")", "(Rosetta__.option_d " + element.dec + ")",
                    element.word + "_opt", ""};
        }
        if (declaration == "std::complex") return {"Stdlib.Complex.t", "Rosetta__.complex_e", "Rosetta__.complex_d", "complex", ""};
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0, in);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::vector<MT> items;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) items.push_back(arg(i, in));
            return tuple(items);
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function)
            return function_type(target.arguments[0], in);
        if (declaration == "std::chrono::duration") return {"float", "Rosetta__.float_e", "Rosetta__.float_d", "seconds", ""};
        if (declaration == "std::variant") return {"", "", "", "", "std::variant has no OCaml mapping"};
    }
    return {"", "", "", "", "unsupported OCaml type " + target.canonical};
}

// The OCaml type of a policy conversion's target, spelled as C++ text ("std::vector<double>").
MT Generator::ml_spelled(std::string cpp, bool in) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (cpp.rfind("const ", 0) == 0) cpp = trim(cpp.substr(6));
    if (cpp.rfind("::", 0) == 0) cpp = cpp.substr(2);
    static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int", "long",
        "unsigned long", "long long", "unsigned long long", "std::size_t", "size_t", "std::ptrdiff_t", "std::int64_t", "std::uint64_t",
        "std::int32_t", "std::uint32_t", "std::int16_t", "std::uint16_t", "std::int8_t", "std::uint8_t", "int64_t", "uint64_t", "int32_t", "uint32_t"};
    if (integers.count(cpp)) return {"int", "Rosetta__.int_e", "Rosetta__.int_d", "int", ""};
    if (cpp == "bool") return {"bool", "Rosetta__.bool_e", "Rosetta__.bool_d", "bool", ""};
    if (cpp == "char") return {"char", "Rosetta__.char_e", "Rosetta__.char_d", "char", ""};
    if (cpp == "float" || cpp == "double" || cpp == "long double") return {"float", "Rosetta__.float_e", "Rosetta__.float_d", "float", ""};
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path")
        return {"string", "Rosetta__.string_e", "Rosetta__.string_d", "string", ""};
    for (const auto& owner : class_order_)
        if (normalize(owner) == cpp) return object_type(owner, in);
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') return {"", "", "", "", "unsupported OCaml conversion target " + cpp};
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
    const auto arg = [&](std::size_t i) { return i < arguments.size() ? ml_spelled(arguments[i], in) : MT{"", "", "", "", "incomplete " + cpp}; };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::valarray" ||
        name == "std::set" || name == "std::unordered_set") {
        if (!arguments.empty() && arguments[0] == "bool")
            return {"bool list", "(Rosetta__.list_e Rosetta__.bool_e)", "(Rosetta__.list_d Rosetta__.bool_d)", "bool_list", ""};
        return element_sequence(arg(0), arguments.empty() ? std::string() : arguments[0]);
    }
    if (name == "std::map" || name == "std::unordered_map") {
        const auto key = arg(0), value = arg(1);
        if (!key.why.empty()) return key;
        if (!value.why.empty()) return value;
        return {"(" + paren(key.type) + " * " + paren(value.type) + ") list", "(Rosetta__.map_e " + key.enc + " " + value.enc + ")",
                "(Rosetta__.map_d " + key.dec + " " + value.dec + ")", "map", ""};
    }
    if (name == "std::optional") {
        const auto element = arg(0);
        if (!element.why.empty()) return element;
        return {paren(element.type) + " option", "(Rosetta__.option_e " + element.enc + ")", "(Rosetta__.option_d " + element.dec + ")", element.word + "_opt", ""};
    }
    if (name == "std::complex") return {"Stdlib.Complex.t", "Rosetta__.complex_e", "Rosetta__.complex_d", "complex", ""};
    if (name == "std::pair" || name == "std::tuple") {
        std::vector<MT> items;
        for (std::size_t i = 0; i < arguments.size(); ++i) items.push_back(arg(i));
        return tuple(items);
    }
    return {"", "", "", "", "unsupported OCaml conversion target " + cpp};
}

// An OCaml literal for a C++ default argument ("" = none).
std::string Generator::ml_literal(const DefaultValue& value, const Type& type, const MT& mt) const {
    auto text = value.value;
    if (value.kind == "int") {
        while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        const bool negative = !text.empty() && text[0] == '-';
        const auto digits = negative ? text.substr(1) : text;
        if (digits.empty() || digits.size() > 17 || digits.find_first_not_of("0123456789") != std::string::npos) return {};
        if (mt.type == "float") return negative ? "(" + text + ".)" : text + ".";
        if (mt.type != "int") return {};
        return negative ? "(" + text + ")" : text;
    }
    if (value.kind == "float") {
        if (mt.type != "float") return {};
        text = shortest_double(text);
        if (text.empty() || text.find_first_of("ni") != std::string::npos) return {};
        if (text.find_first_of(".e") == std::string::npos) text += ".";
        return text[0] == '-' ? "(" + text + ")" : text;
    }
    if (value.kind == "bool") return mt.type == "bool" ? (value.value == "true" ? "true" : "false") : std::string();
    if (value.kind == "string") {
        std::string decoded;
        if (mt.type != "string" || !cpp_string(value.value, decoded)) return {};
        return ml_string(decoded);
    }
    if (value.kind == "char") return mt.type == "char" && value.value.size() == 1 ? ml_char(value.value[0]) : std::string();
    if (value.kind == "null") return mt.type.size() > 7 && mt.type.compare(mt.type.size() - 7, 7, " option") == 0 ? "None" : std::string();
    if (value.kind == "enum") {
        const auto& target = referent(type);
        const auto found = constructors_.find(target.declaration);
        if (found == constructors_.end() || !enums_.count(target.declaration)) return {};
        const auto& enumeration = *enums_.at(target.declaration);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return enum_names_.at(target.declaration) + "." + found->second[i];
    }
    return {};
}

// ------------------------------------------------------------------ documentation

std::string Generator::cpp_signature(const Function& function) const {
    std::string text = function.is_constructor ? std::string() : function.result.spelling + " ";
    text += function.qualified_name() + "(";
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        text += (i ? ", " : "") + function.parameters[i].spelling;
        const auto& cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (!cpp.empty()) text += " " + cpp;
        if (i < function.default_values.size() && !function.default_values[i].kind.empty()) {
            const auto& value = function.default_values[i];
            text += " = " + default_text(value);
        } else if (i + function.default_arguments >= function.parameters.size()) text += " = ...";
    }
    text += ")";
    if (function.is_const) text += " const";
    return text;
}

std::string Generator::exception_name(const std::string& cpp) const {
    const auto name = normalize(cpp);
    if (name == "std::invalid_argument") return "Invalid_argument";
    if (name == "std::out_of_range") return "Error [(`Out_of_range, _)]";
    if (name == "std::length_error") return "Error [(`Length_error, _)]";
    if (name == "std::domain_error") return "Error [(`Domain_error, _)]";
    if (name == "std::overflow_error" || name == "std::underflow_error" || name == "std::range_error") return "Error [(`Arithmetic, _)]";
    if (name == "std::bad_alloc") return "Out_of_memory";
    return "Failure";
}

ODoc Generator::describe(const Documentation& doc, const std::string& fallback) const {
    ODoc result;
    bool first_text = true;
    for (const auto& block : doc.details) {
        if (block.kind == "code" || block.kind == "math") { result.body.push_back(code_block(block.text, block.language)); continue; }
        if (block.kind == "heading") { result.body.push_back("{b " + inline_doc(one_paragraph(block.text)) + ":}"); continue; }
        auto blocks = text_blocks(block.text);
        if (first_text && doc.brief.empty() && !blocks.empty() && blocks.front().rfind("{ul", 0) != 0) {
            result.summary = blocks.front();
            blocks.erase(blocks.begin());
        }
        first_text = false;
        result.body.insert(result.body.end(), blocks.begin(), blocks.end());
    }
    if (!doc.brief.empty()) result.summary = inline_doc(one_paragraph(doc.brief));
    if (result.summary.empty()) result.summary = fallback;
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { result.is_deprecated = true; result.deprecated = inline_doc(one_paragraph(item.text)); continue; }
        if (item.name == "since") { result.since = inline_doc(one_paragraph(item.text)); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        result.body.push_back("{b " + odoc_escape(label) + ":} " + inline_doc(one_paragraph(item.text)));
    }
    for (const auto& item : doc.see) {
        auto target = trim(item);
        while (!target.empty() && (target.back() == '.' || target.back() == ',')) target.pop_back();
        if (target.empty()) continue;
        result.see.push_back("\"" + replace_all(target, "\"", "'") + "\" C++ " + code_span(target));
    }
    for (const auto& item : doc.throws)
        result.raises.emplace_back(exception_name(item.name), inline_doc(one_paragraph(item.text)) +
            (item.name.empty() ? "" : std::string(item.text.empty() ? "" : " ") + "(C++ " + code_span(item.name) + ")"));
    return result;
}

// ------------------------------------------------------------------ overloads and names

std::string Generator::overload_problem(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto mt = parameter(function.parameters[i]);
        if (!mt.why.empty()) return mt.why;
    }
    if (!function.is_constructor) {
        const auto mt = result(function);
        if (!mt.why.empty()) return mt.why;
    }
    return {};
}

std::string Generator::words(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    std::string text;
    for (std::size_t i = first; i < function.parameters.size(); ++i) text += (text.empty() ? "" : "_") + word_of(parameter(function.parameters[i]).word);
    return text.empty() ? "unit" : text;
}

// One OCaml name per overload: the first keeps the group's name, the others add their parameter
// types (pick, pick_float, pick_string); a name already taken gets a trailing _.
std::vector<std::string> Generator::overload_names(const Group& group, std::set<std::string>& taken) const {
    std::vector<std::string> names;
    for (std::size_t i = 0; i < group.overloads.size(); ++i) {
        auto name = i == 0 ? group.name : group.name + "_" + words(group.overloads[i]);
        if (ocaml_keyword(name)) name += "_";
        while (!taken.insert(name).second) name += "_";
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> Generator::ancestors(const std::string& owner) const {
    std::vector<std::string> result;
    std::vector<std::string> queue = classes_.at(owner).bases;
    for (std::size_t i = 0; i < queue.size(); ++i) {
        if (std::find(result.begin(), result.end(), queue[i]) != result.end()) continue;
        result.push_back(queue[i]);
        for (const auto& base : classes_.at(queue[i]).bases) queue.push_back(base);
    }
    return result;
}

const Group* Generator::find_group(const std::string& owner, const std::string& name, std::string* found_owner) const {
    std::vector<std::string> order{owner};
    for (const auto& base : ancestors(owner)) order.push_back(base);
    for (const auto& cls : order)
        for (const auto& group : classes_.at(cls).methods)
            if (group.name == name && !group.is_static) {
                if (found_owner) *found_owner = cls;
                return &group;
            }
    return nullptr;
}

long Generator::dispatch_generic(const Overload& overload) const {
    const auto& function = *overload.function;
    if (!function.is_virtual || overload.extension || function.is_static) return -1;
    if (!dispatchable_.count(virtual_key(function))) return -1;
    const auto found = generic_index_.find(renamed(function.qualified_name(), method_name(function.name)));
    return found == generic_index_.end() ? -1 : static_cast<long>(found->second);
}

// ------------------------------------------------------------------ emission

void Generator::emit_function(const Emit& e, const std::string& indent) {
    const auto& overload = *e.overload;
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size();
    const std::size_t required = count - function.default_arguments;
    const auto names = parameter_names(overload);
    const bool self = !e.self_owner.empty();
    const auto res = result(function);

    auto doc = describe(function.documentation, "C++ " + code_span(function.qualified_name()) + ".");
    std::string signature, params, required_encs, optional_entries;
    std::vector<std::string> optional_vars, plain_args;
    std::vector<std::pair<std::string, std::string>> dispatch_optional;  // var, literal ("" = none)
    std::string omitted;
    for (std::size_t i = first; i < count; ++i) {
        const auto& type = function.parameters[i];
        const auto mt = parameter(type);
        const auto label = names[i - first];
        const auto var = "x" + std::to_string(i - first);
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : function.documentation.params) if (!cpp.empty() && item.name == cpp) text = inline_doc(one_paragraph(item.text));
        if (text.empty()) text = "the C++ argument " + code_span(cpp.empty() ? label : cpp);
        const auto& default_value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
        if (i >= required) {
            const auto literal = ml_literal(default_value, type, mt);
            signature += "?" + label + ":" + paren(mt.type) + " -> ";
            params += " ?" + label + ":" + var;
            optional_entries += std::string(optional_entries.empty() ? "" : "; ") + "(Stdlib.Option.map (" + mt.enc + " c) " + var + ", " +
                (literal.empty() ? std::string("None") : "Some (" + mt.enc + " c " + literal + ")") + ", " + ml_string(label) + ")";
            text += default_value.kind.empty() ? " (optional: C++ default)"
                                               : " (optional; C++ default " + code_span(default_text(default_value)) + ")";
            dispatch_optional.emplace_back(var, literal);
            optional_vars.push_back(var);
            plain_args.push_back("@OPT" + std::to_string(dispatch_optional.size() - 1) + "@");
        } else {
            required_encs += "; " + mt.enc + " c " + var;
            plain_args.push_back(var);
        }
        doc.params.emplace_back(label, text);
    }
    // Positional parameters: the object, then the required ones (optional ones come first).
    std::string positional_signature, positional_params;
    if (self) { positional_signature += open_type(e.self_owner) + " -> "; positional_params += " self"; }
    for (std::size_t i = first; i < required; ++i) {
        positional_signature += paren(parameter(function.parameters[i]).type).find("->") != std::string::npos && parameter(function.parameters[i]).type.front() != '('
            ? "(" + parameter(function.parameters[i]).type + ") -> " : parameter(function.parameters[i]).type + " -> ";
        positional_params += " x" + std::to_string(i - first);
    }
    if (positional_params.empty()) { positional_signature += "unit -> "; positional_params += " ()"; }
    signature += positional_signature + res.type;

    std::string required_list = self ? "Rosetta__.obj_e c self" : "";
    if (!required_encs.empty()) required_list += self ? required_encs : required_encs.substr(2);
    std::string arguments;
    if (optional_entries.empty()) arguments = required_list.empty() ? "(fun _ -> [])" : "(fun c -> [" + required_list + "])";
    else arguments = "(fun c -> Rosetta__.trailing [" + required_list + "] [" + optional_entries + "])";
    std::string body = "Rosetta__.call " + std::to_string(e.index) + " " + arguments + " " + res.dec;
    if (e.generic >= 0 && self) {
        // An OCaml subclass instance runs its override first (Base.name runs the C++ method).
        std::string applied = "(Stdlib.Obj.obj f) self";
        std::string guards;
        for (std::size_t i = 0; i < plain_args.size(); ++i) {
            auto arg = plain_args[i];
            if (arg.rfind("@OPT", 0) == 0) {
                const auto k = std::stoul(arg.substr(4));
                const auto& [var, literal] = dispatch_optional[k];
                if (literal.empty()) { guards += ", " + var; arg = var + "_"; }
                else arg = "(match " + var + " with Some v -> v | None -> " + literal + ")";
            }
            applied += " " + arg;
        }
        if (guards.empty())
            body = "match Rosetta__.dispatch self " + std::to_string(e.generic) + " with\n" + indent + "  | Some f -> " + applied + "\n" + indent +
                   "  | None -> " + body;
        else {
            std::string pattern = "Some f";
            std::string tuple_guard = "Rosetta__.dispatch self " + std::to_string(e.generic);
            for (std::size_t start = 2; start < guards.size();) {
                const auto comma = guards.find(", ", start);
                const auto var = guards.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                tuple_guard += ", " + var;
                pattern += ", Some " + var + "_";
                if (comma == std::string::npos) break;
                start = comma + 2;
            }
            body = "match " + tuple_guard + " with\n" + indent + "  | " + pattern + " -> " + applied + "\n" + indent + "  | _ -> " + body;
        }
        doc.body.push_back("Virtual: on an OCaml subclass instance (made with [extend]) it runs the OCaml override; [Base." + e.name +
                           "] runs the C++ implementation.");
    }
    if (!e.note.empty()) doc.body.push_back(e.note);
    doc.body.push_back("C++: " + code_span(cpp_signature(function)));
    if (!function.is_constructor && !is_void(function.result))
        doc.returns = function.documentation.returns.empty() ? std::string() : inline_doc(one_paragraph(function.documentation.returns));
    mli_ << "\n" << doc.render(indent) << indent << "val " << e.name << " : " << signature << "\n";
    ml_ << indent << "let " << e.name << params << positional_params << " =\n" << indent << "  " << body << "\n";
}

void Generator::emit_constructor(const ClassPlan& plan, const Overload& overload, const std::string& name, bool extend, const std::string& indent) {
    const auto& function = *overload.function;
    const std::size_t count = function.parameters.size();
    const std::size_t required = count - function.default_arguments;
    auto names = parameter_names(overload);
    const auto owner = plan.record->qualified_name();
    auto doc = describe(function.documentation, std::string(extend ? "An OCaml subclass instance" : "A new") + " [" + plan.name + "] (C++ constructor).");
    if (!function.documentation.brief.empty() || !function.documentation.details.empty()) {}
    else if (!extend) doc.summary = "A new [" + plan.name + "] (C++ constructor).";
    std::string signature, params, overrides;
    std::set<std::string> labels;
    if (extend) {
        std::vector<std::string> items;
        std::set<std::string> seen;
        for (const auto& item : overridable(plan)) {
            if (!seen.insert(item.name).second) continue;
            const auto& method = *item.method;
            std::vector<MT> arguments;
            bool ok = true;
            for (const auto& parameter : method.parameters) {
                arguments.push_back(ml(parameter, false));
                if (!arguments.back().why.empty()) ok = false;
            }
            const auto res = is_void(method.result) ? MT{"unit", "Rosetta__.unit_e", "Rosetta__.unit_d", "unit", ""} : ml(method.result, true);
            if (!ok || !res.why.empty()) continue;
            std::string type = "t -> ";
            for (const auto& argument : arguments) type += (argument.type.find("->") != std::string::npos ? "(" + argument.type + ")" : argument.type) + " -> ";
            type += res.type.find("->") != std::string::npos ? "(" + res.type + ")" : res.type;
            std::string applied = "f (Rosetta__.obj_d (Stdlib.Array.get args 0))";
            for (std::size_t i = 0; i < arguments.size(); ++i) applied += " (" + arguments[i].dec + " (Stdlib.Array.get args " + std::to_string(i + 1) + "))";
            const auto* group = find_group(owner, item.name);
            if (!group) continue;
            const auto generic_index = generic_index_.at(item.name);
            labels.insert(item.name);
            signature += "?" + item.name + ":(" + type + ") -> ";
            params += " ?" + item.name;
            items.push_back("Rosetta__.override " + std::to_string(generic_index) + " " + item.name + " (fun f c args -> " + res.enc + " c (" + applied +
                            ")) " + std::to_string(group->index));
            doc.params.emplace_back(item.name, std::string("overrides ") + (item.pure ? "pure virtual " : "virtual ") +
                                                   code_span(cpp_signature(method)) + (item.pure ? "" : " (default: the C++ implementation)"));
        }
        for (std::size_t i = 0; i < items.size(); ++i) overrides += (i ? "; " : "") + items[i];
        overrides = "(Some [" + overrides + "])";
    }
    std::string required_encs, optional_entries, positional_signature, positional_params;
    for (std::size_t i = 0; i < count; ++i) {
        auto& label = names[i];
        if (labels.count(label)) label += "_arg";
        const auto& type = function.parameters[i];
        const auto mt = parameter(type);
        const auto var = "x" + std::to_string(i);
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : function.documentation.params) if (!cpp.empty() && item.name == cpp) text = inline_doc(one_paragraph(item.text));
        if (text.empty()) text = "the C++ argument " + code_span(cpp.empty() ? label : cpp);
        if (i >= required) {
            const auto& default_value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
            const auto literal = ml_literal(default_value, type, mt);
            signature += "?" + label + ":" + paren(mt.type) + " -> ";
            params += " ?" + label + ":" + var;
            optional_entries += std::string(optional_entries.empty() ? "" : "; ") + "(Stdlib.Option.map (" + mt.enc + " c) " + var + ", " +
                (literal.empty() ? std::string("None") : "Some (" + mt.enc + " c " + literal + ")") + ", " + ml_string(label) + ")";
            text += " (optional; C++ default " + code_span(default_text(default_value)) + ")";
        } else {
            required_encs += std::string(required_encs.empty() ? "" : "; ") + mt.enc + " c " + var;
            positional_signature += (mt.type.find("->") != std::string::npos && mt.type.front() != '(' ? "(" + mt.type + ")" : mt.type) + " -> ";
            positional_params += " " + var;
        }
        doc.params.emplace_back(label, text);
    }
    if (positional_params.empty()) { positional_signature = "unit -> "; positional_params = " ()"; }
    signature += positional_signature + "t";
    std::string arguments;
    if (optional_entries.empty()) arguments = required_encs.empty() ? "(fun _ -> [])" : "(fun c -> [" + required_encs + "])";
    else arguments = "(fun c -> Rosetta__.trailing [" + required_encs + "] [" + optional_entries + "])";
    if (extend)
        doc.body.push_back("Each optional closure overrides a C++ virtual method, also when C++ calls it; its first argument is the "
                           "object itself. Without one the C++ implementation runs (a pure virtual method then raises "
                           "[Error (`Binding, _)]).");
    doc.body.push_back("C++: " + code_span(cpp_signature(function)));
    mli_ << "\n" << doc.render(indent) << indent << "val " << name << " : " << signature << "\n";
    ml_ << indent << "let " << name << params << positional_params << " =\n" << indent << "  Rosetta__.construct "
        << plan.constructor_index << " " << (extend ? overrides : "None") << " " << arguments << "\n";
}

void Generator::emit_enum(const std::string& qualified, const std::string& name) {
    const auto& enumeration = *enums_.at(qualified);
    const auto& ctors = constructors_.at(qualified);
    auto doc = describe(enumeration.documentation, "C++ enumeration " + code_span(qualified) + ".");
    doc.body.push_back("Values cross to C++ by enumerator name; [to_int] and [of_int] give the C++ values.");
    mli_ << "\n" << doc.render("") << "module " << name << " : sig\n  type t =\n";
    ml_ << "\nmodule " << name << " = struct\n  type t =\n";
    for (std::size_t i = 0; i < ctors.size(); ++i) {
        mli_ << "    | " << ctors[i] << "  (** C++ " << code_span(enumeration.enumerators[i]) << " ("
             << odoc_escape(enumeration.enumerator_values[i]) << "). *)\n";
        ml_ << "    | " << ctors[i] << "\n";
    }
    mli_ << "\n  (** The C++ value of an enumerator. *)\n  val to_int : t -> int\n"
         << "\n  (** The enumerator with C++ value [v] (the first, for aliases).\n      @raise Invalid_argument if no enumerator has it. *)\n"
         << "  val of_int : int -> t\n"
         << "\n  (** The C++ name of an enumerator. *)\n  val to_string : t -> string\n"
         << "\n  (** The enumerator with C++ name [s].\n      @raise Invalid_argument if there is none. *)\n  val of_string : string -> t\n"
         << "\n  (** Every enumerator, in declaration order. *)\n  val all : t list\nend\n";
    ml_ << "  let to_int = function";
    for (std::size_t i = 0; i < ctors.size(); ++i) {
        auto value = enumeration.enumerator_values[i];
        const bool negative = !value.empty() && value[0] == '-';
        const auto digits = negative ? value.substr(1) : value;
        if (digits.size() > 18) value = negative ? "Stdlib.min_int" : "Stdlib.max_int";
        else if (negative) value = "(" + value + ")";
        ml_ << " | " << ctors[i] << " -> " << value;
    }
    ml_ << "\n  let to_string = function";
    for (std::size_t i = 0; i < ctors.size(); ++i) ml_ << " | " << ctors[i] << " -> " << ml_string(labels_.at(qualified + "::" + enumeration.enumerators[i]));
    ml_ << "\n  let all = [";
    for (std::size_t i = 0; i < ctors.size(); ++i) ml_ << (i ? "; " : " ") << ctors[i];
    ml_ << " ]\n"
        << "  let of_int v = match Stdlib.List.filter (fun e -> to_int e = v) all with e :: _ -> e | [] -> Stdlib.invalid_arg (\""
        << name << ".of_int: \" ^ Stdlib.string_of_int v)\n"
        << "  let of_string s = match Stdlib.List.filter (fun e -> to_string e = s) all with e :: _ -> e | [] -> Stdlib.invalid_arg (\""
        << name << ".of_string: \" ^ s)\n"
        << "  let enc__ _ v = Rosetta__.Sym (to_string v)\n"
        << "  let dec__ n = match Rosetta__.symbol_d n with `Name s -> of_string s | `Value v -> of_int v\n"
        << "end\n";
}

void Generator::emit_class(const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& name = plan.name;
    const auto& alias = aliases_.at(owner);
    auto doc = describe(record.documentation, "C++ class " + code_span(owner) + ".");
    if (!record.documentation.brief.empty() || !record.documentation.details.empty()) doc.body.push_back("C++ class " + code_span(owner) + ".");
    if (!plan.bases.empty()) {
        std::string bases;
        for (std::size_t i = 0; i < plan.bases.size(); ++i) bases += (i ? ", " : "") + std::string("{!") + classes_.at(plan.bases[i]).name + "}";
        doc.body.push_back("Derives from " + bases + ": it passes wherever those are expected, and [(x :> " + aliases_.at(plan.bases.front()) +
                           ")] or [" + classes_.at(plan.bases.front()).name + ".upcast x] upcasts it explicitly. Inherited functions are "
                           "repeated here.");
    }
    if (record.abstract) doc.body.push_back(std::string("Abstract in C++: ") + (plan.director ? "make instances with [extend], overriding its pure virtual methods." : "C++ returns objects of derived classes.") );
    else if (plan.director) doc.body.push_back("[extend] makes OCaml subclass instances whose closures override its virtual methods, also when C++ calls them.");
    mli_ << "\n" << doc.render("") << "module " << name << " : sig\n"
         << "  (** A native [" << odoc_escape(owner) << "]: freed when collected, or by {!close}. *)\n"
         << "  type t = " << alias << "\n";
    ml_ << "\nmodule " << name << " = struct\n  type t = " << alias << "\n";
    std::set<std::string> taken{"cast", "upcast", "close", "is_closed"};
    // Constructors: create (and extend for OCaml subclasses), one per C++ overload.
    if (!plan.constructors.empty()) {
        Group constructors{"create", plan.constructors, true, {}, 0, {}, 0};
        std::set<std::string> local;
        const auto names = overload_names(constructors, local);
        for (std::size_t i = 0; i < plan.constructors.size(); ++i) {
            const auto& overload = plan.constructors[i];
            const auto problem = overload_problem(overload);
            if (!problem.empty()) { diagnostics_.push_back(owner + "::" + record.name + ": " + problem); continue; }
            const bool plain = !record.abstract && overload.function->access == "public";
            if (plain) { taken.insert(names[i]); emit_constructor(plan, overload, names[i], false, "  "); }
            if (plan.director) {
                const auto extended = "extend" + names[i].substr(6);
                taken.insert(extended);
                emit_constructor(plan, overload, extended, true, "  ");
            }
        }
    }
    // Base: the C++ implementations of the methods OCaml subclasses override.
    if (plan.director) {
        std::ostringstream saved_ml, saved_mli;
        std::set<std::string> base_names;
        std::vector<Emit> base;
        for (const auto& item : overridable(plan)) {
            std::string group_owner;
            const auto* group = find_group(owner, item.name, &group_owner);
            if (!group) continue;
            for (const auto& overload : group->overloads) {
                if (overload.extension || virtual_key(*overload.function) != virtual_key(*item.method)) continue;
                if (!overload_problem(overload).empty() || !base_names.insert(item.name).second) continue;
                base.push_back({item.name, &overload, group->index, group_owner, -1, ""});
            }
        }
        if (!base.empty()) {
            mli_ << "\n  (** The C++ implementations of the virtual methods, for overrides to call (a pure virtual\n"
                 << "      method raises [Error (`Binding, _)]). *)\n  module Base : sig";
            ml_ << "  module Base = struct\n";
            for (const auto& e : base) emit_function(e, "    ");
            mli_ << "  end\n";
            ml_ << "  end\n";
        }
    }
    // Methods, operators, fields and constants of the class, then the inherited ones.
    const auto emit_members = [&](const std::string& cls, bool inherited) {
        const auto& source = classes_.at(cls);
        const std::string note = inherited ? "Inherited from {!" + source.name + "}." : std::string();
        for (const auto* groups : {&source.methods, &source.operators}) {
            for (const auto& group : *groups) {
                if (inherited && group.is_static) continue;
                if (inherited && taken.count(group.name)) continue;
                std::vector<const Overload*> usable;
                Group kept = group;
                kept.overloads.clear();
                for (const auto& overload : group.overloads) {
                    const auto problem = overload_problem(overload);
                    if (!problem.empty()) { if (!inherited) diagnostics_.push_back(overload.function->qualified_name() + ": " + problem); continue; }
                    kept.overloads.push_back(overload);
                    usable.push_back(&overload);
                }
                if (usable.empty()) continue;
                const auto names = overload_names(kept, taken);
                for (std::size_t i = 0; i < usable.size(); ++i) {
                    const auto& overload = *usable[i];
                    const bool self = (!group.is_static && !overload.extension) || overload.receives_self;
                    Emit e{names[i], &overload, group.index, self ? cls : std::string(), self ? dispatch_generic(overload) : -1, note};
                    emit_function(e, "  ");
                }
            }
        }
        for (const auto& field : source.fields) {
            const auto& f = *field.field;
            auto getter = field.name;
            if (inherited && taken.count(getter)) continue;
            const auto mt = ml(f.type, false);
            if (!mt.why.empty()) { if (!inherited) diagnostics_.push_back(cls + "::" + f.name + ": " + mt.why); continue; }
            while (taken.count(getter)) getter += "_field";
            taken.insert(getter);
            auto fdoc = describe(f.documentation, "The C++ field " + code_span(cls + "::" + f.name) + ".");
            if (!note.empty()) fdoc.body.push_back(note);
            mli_ << "\n" << fdoc.render("  ") << "  val " << getter << " : " << open_type(cls) << " -> " << mt.type << "\n";
            ml_ << "  let " << getter << " self = Rosetta__.call " << field.getter_index << " (fun c -> [Rosetta__.obj_e c self]) " << mt.dec << "\n";
            const bool writable = !f.type.is_const && !readonly(cls + "::" + f.name, cls) && f.type.kind != TypeKind::lvalue_reference;
            if (!writable) continue;
            const auto in = ml(f.type, true);
            auto setter = "set_" + getter;
            while (taken.count(setter)) setter += "_field";
            taken.insert(setter);
            mli_ << "\n  (** Sets the C++ field " << code_span(cls + "::" + f.name) << ". *)\n  val " << setter << " : " << open_type(cls)
                 << " -> " << (in.type.find("->") != std::string::npos ? "(" + in.type + ")" : in.type) << " -> unit\n";
            ml_ << "  let " << setter << " self v = Rosetta__.call " << field.setter_index << " (fun c -> [Rosetta__.obj_e c self; " << in.enc
                << " c v]) Rosetta__.unit_d\n";
        }
        if (inherited) return;
        for (const auto& constant : source.constants) {
            const auto mt = ml(*constant.type, false);
            if (!mt.why.empty()) { diagnostics_.push_back(constant.expression + ": " + mt.why); continue; }
            auto value = constant.name;
            while (taken.count(value)) value += "_value";
            taken.insert(value);
            auto cdoc = describe(*constant.documentation, "The C++ constant " + code_span(constant.expression) + ".");
            mli_ << "\n" << cdoc.render("  ") << "  val " << value << " : " << mt.type << "\n";
            ml_ << "  let " << value << " = Rosetta__.call " << constant.index << " (fun _ -> []) " << mt.dec << "\n";
        }
    };
    emit_members(owner, false);
    for (const auto& base : ancestors(owner)) emit_members(base, true);
    // Casts and lifetime.
    std::string descendants;
    for (std::size_t i = 0; i < class_order_.size(); ++i)
        if (derives(class_order_[i], owner)) descendants += (descendants.empty() ? "" : "; ") + std::to_string(i);
    mli_ << "\n  (** [Some x] when the object is (in C++) a [" << odoc_escape(owner) << "] or derives from it, [None] otherwise. *)\n"
         << "  val cast : 'a obj -> t option\n"
         << "\n  (** The object as a [" << name << "] (an upcast: no check is needed). *)\n"
         << "  val upcast : " << open_type(owner) << " -> t\n"
         << "\n  (** Frees the native object now (when OCaml owns it). Later calls on it raise\n      [Error (`Binding, _)]; closing twice does nothing. *)\n"
         << "  val close : " << open_type(owner) << " -> unit\n"
         << "\n  (** Whether {!close} was called on the object. *)\n"
         << "  val is_closed : " << open_type(owner) << " -> bool\nend\n";
    ml_ << "  let cast (x : 'a obj) : t option = if Rosetta__.is_a x [|" << descendants << "|] then Some x else None\n"
        << "  let upcast (x : " << open_type(owner) << ") : t = x\n"
        << "  let close (x : " << open_type(owner) << ") = Rosetta__.close x\n"
        << "  let is_closed (x : " << open_type(owner) << ") = Rosetta__.is_closed x\nend\n";
}

GeneratedOcamlFullSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    top_ = name_;
    top_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(top_[0])));
    GeneratedOcamlFullSource result;
    prepare();
    // Module names (classes, enums, policy modules) share one namespace; type aliases another.
    std::map<std::string, std::string> modules;
    const auto reserve_module = [&](const std::string& name, const std::string& what) { return modules.emplace(name, what).second; };
    reserve_module("Rosetta__", "the runtime");
    reserve_module("Stdlib", "the standard library");
    std::set<std::string> types{"obj", "error_kind"};
    {
        std::set<std::string> collided;
        for (const auto& owner : class_order_) {
            auto& plan = classes_.at(owner);
            if (!reserve_module(plan.name, "class " + owner)) {
                if (!flat_) throw BindingError("OCaml module name collision: " + plan.name + " (" + modules.at(plan.name) + " and class " + owner + ")");
                reject(owner, "class name collides with another OCaml module");
                collided.insert(owner);
            }
        }
        drop_classes(collided);
        for (auto it = modules.begin(); it != modules.end();) {
            const auto owner = it->second.rfind("class ", 0) == 0 ? it->second.substr(6) : std::string();
            if (!owner.empty() && !classes_.count(owner)) it = modules.erase(it);
            else ++it;
        }
    }
    for (const auto& owner : class_order_) {
        auto alias = type_alias(classes_.at(owner).name);
        while (!types.insert(alias).second) alias += "_";
        aliases_[owner] = alias;
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, type_name(nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        if (!reserve_module(name, "enum " + qualified)) {
            if (!flat_) throw BindingError("OCaml module name collision: " + name + " (" + modules.at(name) + " and enum " + qualified + ")");
            reject(qualified, "enum name collides with another OCaml module");
            continue;
        }
        enum_names_[qualified] = name;
        std::set<std::string> seen;
        std::vector<std::string> ctors;
        for (const auto& enumerator : enumeration.enumerators) {
            const auto label = renamed(qualified + "::" + enumerator, enumerator);
            labels_[qualified + "::" + enumerator] = label;
            auto ctor = module_name(label);
            if (ctor == "True" || ctor == "False") ctor += "_";
            while (!seen.insert(ctor).second) ctor += "_";
            ctors.push_back(ctor);
        }
        constructors_[qualified] = ctors;
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::set<std::string> submodules;
    for (const auto& group : functions) if (!group.module.empty()) submodules.insert(group.module);
    std::map<std::string, std::string> submodule_names;
    for (const auto& submodule : submodules) {
        const auto name = module_name(submodule);
        if (!reserve_module(name, "policy module " + submodule)) throw BindingError("OCaml policy module name collides with another module: " + name);
        submodule_names[submodule] = name;
    }
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, value_name(flat(variable.namespaces) ? variable.name : exported(qualified)));
    });
    // Methods some OCaml subclass can override dispatch to it.
    for (const auto& owner : class_order_)
        if (classes_.at(owner).director)
            for (const auto& item : overridable(classes_.at(owner))) {
                dispatchable_.insert(virtual_key(*item.method));
                generic(item.name);
            }
    // Overloads OCaml cannot express are reported (the C++ side keeps them).
    for (auto& group : functions)
        for (const auto& overload : group.overloads) {
            const auto problem = overload_problem(overload);
            if (!problem.empty()) diagnostics_.push_back(overload.function->qualified_name() + ": " + problem);
        }
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto* groups : {&plan.methods, &plan.operators})
            for (const auto& group : *groups)
                for (const auto& overload : group.overloads) {
                    const auto problem = overload_problem(overload);
                    if (!problem.empty()) diagnostics_.push_back(overload.function->qualified_name() + ": " + problem);
                }
        for (const auto& overload : plan.constructors) {
            const auto problem = overload_problem(overload);
            if (!problem.empty()) diagnostics_.push_back(owner + "::" + plan.record->name + ": " + problem);
        }
        for (const auto& field : plan.fields) {
            const auto problem = ml(field.field->type, false).why;
            if (!problem.empty()) diagnostics_.push_back(owner + "::" + field.field->name + ": " + problem);
        }
    }
    for (const auto& constant : constants) {
        const auto problem = ml(*constant.type, false).why;
        if (!problem.empty()) diagnostics_.push_back(constant.expression.substr(2) + ": " + problem);
    }
    check_diagnostics();
    const auto reported = diagnostics_;
    emit_cpp(result.source, result.parts, functions, constants, "OCaml module " + top_ + " (" + name_ + ", full API).");
    {
        std::string stubs = detail::ocaml_full_stubs_cpp;
        stubs = replace_all(stubs, "@C@", "rosetta_" + name_);
        stubs = replace_all(stubs, "@M@", name_);
        result.source += stubs;
    }

    // --------------------------------------------------------------- NAME.ml and NAME.mli
    ml_ << "(* Generated by Rosetta: OCaml bindings to the C++ module " << name_ << " (full API). Do not edit;\n"
        << "   the interface and its documentation are in " << name_ << ".mli. *)\n"
        << "[@@@warning \"-a\"]\n";
    ml_ << replace_all(replace_all(detail::ocaml_full_runtime_ml, "@M@", name_), "@TOP@", top_);
    ml_ << "\ntype 'a obj = Rosetta__.repr\ntype error_kind = Rosetta__.error_kind\nexception Error = Rosetta__.Error\n";
    mli_ << "(** OCaml bindings to the C++ module [" << odoc_escape(name_) << "] (generated by Rosetta).\n\n"
         << "    Each C++ class is a submodule ({!" << (class_order_.empty() ? std::string("obj") : classes_.at(class_order_.front()).name)
         << "}) whose [type t] is a native object, created by [create] and freed when collected (or by [close]); its "
            "functions take the object as their first positional argument ([M.f x a]; [x |> M.f] for functions of the object alone). Object types are "
            "phantom types listing the class and its C++ bases: a function on [[> `Shape ] obj] accepts every class "
            "deriving from [Shape], and [(x :> shape)] upcasts.\n\n"
         << "    {ul\n"
         << "    {- Overloads are separate functions named after their parameter types ([pick], [pick_float]); C++ "
            "default arguments are optional labelled arguments ([?b]), given before the positional ones.}\n"
         << "    {- Mutable C++ reference parameters ([T&]) are ['a ref] cells, updated after the call.}\n"
         << "    {- Integers are [int] (range-checked both ways), [std::vector<double>] is [float array], "
            "[std::vector<unsigned char>] is [bytes], other sequences are lists, maps are association lists, "
            "[std::optional] is [option], [std::complex] is [Complex.t], [std::function] a closure.}\n"
         << "    {- A null object result raises [Not_found]. C++ exceptions raise [Invalid_argument] "
            "([std::invalid_argument]), [Failure] (other [std::exception]s), [Out_of_memory], or {!Error} with a kind; "
            "an exception raised in an OCaml callback or override comes back unchanged.}\n"
         << "    {- Classes with virtual methods have [extend]: an OCaml subclass instance whose optional closures "
            "override them, also when C++ calls them; [Base] runs the C++ implementation.}} *)\n\n"
         << "(** A native object; the type parameter lists its class and C++ bases (as polymorphic variant tags). *)\n"
         << "type -'a obj\n\n"
         << "(** The kinds of {!Error}. *)\n"
         << "type error_kind = [ `Binding | `Out_of_range | `Domain_error | `Length_error | `Arithmetic | `Native ]\n\n"
         << "(** A C++ exception ([`Out_of_range]: [std::out_of_range], [`Domain_error], [`Length_error], [`Arithmetic]: overflow, "
            "underflow and range errors, [`Native]: others), or an error of the binding itself ([`Binding]: a closed object, "
            "a pure virtual method without override, a value out of range). *)\n"
         << "exception Error of error_kind * string\n";
    for (const auto& [qualified, name] : enum_names_) emit_enum(qualified, name);
    if (!class_order_.empty()) {
        mli_ << "\n(** {1 Classes} *)\n";
        ml_ << "\n";
        for (const auto& owner : class_order_) {
            const auto& plan = classes_.at(owner);
            mli_ << "\n(** Objects of class {!" << plan.name << "} (or derived). *)\ntype " << aliases_.at(owner) << " = " << tag_row(owner) << "\n";
            ml_ << "type " << aliases_.at(owner) << " = " << tag_row(owner) << "\n";
        }
        for (const auto& owner : class_order_) emit_class(owner);
    }
    // Module functions and constants, then the policy modules.
    const auto emit_functions = [&](const std::string& submodule, const std::string& indent) {
        std::set<std::string> taken;
        for (const auto& group : functions) {
            if (group.module != submodule) continue;
            Group kept = group;
            kept.overloads.clear();
            std::vector<const Overload*> usable;
            for (const auto& overload : group.overloads) {
                if (!overload_problem(overload).empty()) continue;
                kept.overloads.push_back(overload);
                usable.push_back(&overload);
            }
            if (usable.empty()) continue;
            const auto names = overload_names(kept, taken);
            for (std::size_t i = 0; i < usable.size(); ++i) emit_function({names[i], usable[i], group.index, {}, -1, ""}, indent);
        }
        if (!submodule.empty()) return;
        for (const auto& constant : constants) {
            const auto mt = ml(*constant.type, false);
            if (!mt.why.empty()) continue;
            auto value = constant.name;
            while (taken.count(value)) value += "_value";
            taken.insert(value);
            auto doc = describe(*constant.documentation, "The C++ constant " + code_span(normalize(constant.expression)) + ".");
            mli_ << "\n" << doc.render(indent) << indent << "val " << value << " : " << mt.type << "\n";
            ml_ << indent << "let " << value << " = Rosetta__.call " << constant.index << " (fun _ -> []) " << mt.dec << "\n";
        }
    };
    mli_ << "\n(** {1 Functions and constants} *)\n";
    ml_ << "\n";
    emit_functions("", "");
    for (const auto& submodule : submodules) {
        const auto& name = submodule_names.at(submodule);
        mli_ << "\n(** The functions of the C++ namespace mapped to policy module [" << odoc_escape(submodule) << "]. *)\nmodule " << name << " : sig";
        ml_ << "\nmodule " << name << " = struct\n";
        emit_functions(submodule, "  ");
        mli_ << "end\n";
        ml_ << "end\n";
    }
    result.ml = ml_.str();
    result.mli = mli_.str();
    result.meta = "# Generated by Rosetta: findlib package of the OCaml bindings to the C++ module " + name_ + ".\n"
                  "description = \"OCaml bindings to the C++ module " + name_ + " (Rosetta)\"\n"
                  "version = \"" + (options_.version.empty() ? std::string("1.0") : options_.version) + "\"\n"
                  "requires = \"\"\n"
                  "archive(byte) = \"" + name_ + ".cma\"\n"
                  "archive(native) = \"" + name_ + ".cmxa\"\n";
    // Problems found while emitting (after check_diagnostics) are already in the list.
    result.diagnostics = diagnostics_;
    (void)reported;
    return result;
}
} // namespace

GeneratedOcamlFullSource generate_ocaml_full(const Module& module, const std::string& name, bool skip_unsupported,
                                             const BindingPolicy& policy, const OcamlFullOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("OCaml source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
