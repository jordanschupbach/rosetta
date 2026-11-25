// Haskell backend (--language haskell): a GHC module (MODULE.hs) over the shared C ABI library of
// the full C ABI backends (cabi_full.hpp, lisp_full_runtime.hpp), which it calls through the FFI;
// its runtime is the module MODULE.Rosetta (haskell_full_runtime.hpp), generated beside it.
//
// Every call is an IO action. C++ classes are newtypes over a handle with a type class per class
// (IsShape) that every derived class instantiates, so functions on a base class take derived
// objects (shapeArea :: IsShape a => a -> IO Double), and toShape upcasts; downcast returns the
// most-derived class. Names are flat and prefixed with the class (newCircle, circleRadius,
// vec2X, vec2SetX). Overloads and default arguments become distinct functions (pick, pickDouble;
// add, addWithB), resolved again at run time in C++. Enumerations are data types (KindCircle)
// crossing by C++ name, containers lists, Maps and Sets, std::optional Maybe, std::function a
// function returning IO, and a non-const reference parameter an IORef written back. Classes with
// virtual methods have extendShape: a C++ subclass whose virtual methods run the functions of a
// record of overrides (ShapeOverrides), also when C++ calls them; shapeAreaBase runs the C++
// implementation. C++ exceptions are CppError; Haskell exceptions in callbacks cross C++
// unchanged. Doxygen becomes Haddock.
#include "rosetta/haskell.hpp"
#include "cabi_full.hpp"
#include "haskell_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
bool starts(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }
std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) result += (i ? separator : "") + items[i];
    return result;
}
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}

// The words of a C++ name (camelCase, PascalCase, URLPath, snake_case and a__b alike), lower case.
std::vector<std::string> words_of(const std::string& name) {
    std::vector<std::string> words;
    std::string current;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!is_alnum(c)) {
            if (!current.empty()) words.push_back(current);
            current.clear();
            continue;
        }
        if (is_upper(c) && !current.empty()) {
            const char previous = name[i - 1];
            const bool next_lower = i + 1 < name.size() && is_lower(name[i + 1]);
            if (is_lower(previous) || is_digit(previous) || (is_upper(previous) && next_lower)) {
                words.push_back(current);
                current.clear();
            }
        }
        current += lower(c);
    }
    if (!current.empty()) words.push_back(current);
    return words;
}
std::string capitalized(std::string word) {
    if (!word.empty()) word[0] = upper(word[0]);
    return word;
}
// PascalCase: a Haskell type or constructor name (a capital first).
std::string pascal(const std::string& name) {
    std::string result;
    for (const auto& word : words_of(name)) result += capitalized(word);
    if (result.empty()) return "X";
    if (is_digit(result[0])) result = "X" + result;
    return result;
}
bool haskell_keyword(const std::string& name) {
    static const std::set<std::string> words = {"case", "class", "data", "default", "deriving", "do", "else", "foreign", "if", "import",
        "in", "infix", "infixl", "infixr", "instance", "let", "module", "newtype", "of", "then", "type", "where", "forall", "mdo",
        "rec", "proc", "pattern", "family", "role"};
    return words.count(name) != 0;
}
// camelCase: a Haskell value name (a lowercase letter first).
std::string camel(const std::string& name) {
    const auto words = words_of(name);
    std::string result;
    for (std::size_t i = 0; i < words.size(); ++i) result += i ? capitalized(words[i]) : words[i];
    if (result.empty()) return "x";
    if (is_digit(result[0])) result = "x" + result;
    if (haskell_keyword(result)) result += "'";
    return result;
}

// A camelCase name after a prefix: capitalized, without the prime that kept it from a keyword.
std::string member(const std::string& cpp) {
    auto name = camel(cpp);
    while (!name.empty() && name.back() == '\'') name.pop_back();
    return capitalized(name);
}

// A Haskell string literal.
std::string hs_string(const std::string& text) {
    std::string result = "\"";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\t') result += "\\t";
        else if (c < 0x20 || c == 0x7f) result += "\\" + std::to_string(c) + "\\&";
        else result += static_cast<char>(c);
    }
    return result + "\"";
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
            else if (e == '\\' || e == '"' || e == '\'') c = e;
            else return false;
        }
        out += c;
    }
    return true;
}

// ---------------------------------------------------------------- Haddock
// Plain text for Haddock: its markup characters escaped.
std::string escape(const std::string& text) {
    std::string result;
    for (const char c : text) {
        if (c == '\\' || c == '/' || c == '\'' || c == '`' || c == '"' || c == '@' || c == '<' || c == '>' || c == '#' || c == '*' ||
            c == '_' || c == '$' || c == '[' || c == ']')
            result += '\\';
        result += c;
    }
    return result;
}
// Monospaced text (@...@).
std::string code(const std::string& text) { return "@" + escape(text) + "@"; }
// Doxygen inline markup (`code`, **bold**, *emphasis*) as Haddock.
std::string inline_doc(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '`') {
            const auto close = text.find('`', i + 1);
            if (close != std::string::npos && close > i + 1) { result += code(text.substr(i + 1, close - i - 1)); i = close + 1; continue; }
        }
        if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            const auto close = text.find("**", i + 2);
            if (close != std::string::npos && close > i + 2) { result += "__" + escape(text.substr(i + 2, close - i - 2)) + "__"; i = close + 2; continue; }
        }
        if (c == '*' && i + 1 < text.size() && !std::isspace(static_cast<unsigned char>(text[i + 1]))) {
            const auto close = text.find('*', i + 1);
            if (close != std::string::npos && close > i + 1 && !std::isspace(static_cast<unsigned char>(text[close - 1])) &&
                text.substr(i + 1, close - i - 1).find('\n') == std::string::npos) {
                result += "/" + escape(text.substr(i + 1, close - i - 1)) + "/";
                i = close + 1;
                continue;
            }
        }
        result += escape(std::string(1, c));
        ++i;
    }
    // A line starting with a number and a dot or parenthesis would be a list item.
    if (!result.empty() && is_digit(result[0])) {
        std::size_t j = 0;
        while (j < result.size() && is_digit(result[j])) ++j;
        if (j < result.size() && (result[j] == '.' || result[j] == ')')) result.insert(j, "\\");
    }
    if (!result.empty() && result[0] == '-') result = "\\" + result;
    return result;
}
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

// A Haddock comment: blocks of lines (paragraphs, lists, code), rendered with "-- " prefixes.
struct HDoc {
    std::vector<std::vector<std::string>> blocks;
    void add(std::vector<std::string> lines) { if (!lines.empty()) blocks.push_back(std::move(lines)); }
    void line(const std::string& text) { if (!text.empty()) blocks.push_back({text}); }
    // Prose with "- item" lists (continued by indented lines).
    void prose(const std::string& text) {
        std::istringstream in(text);
        std::string paragraph;
        std::vector<std::string> items;
        const auto flush_paragraph = [&] { if (!trim(paragraph).empty()) line(inline_doc(one_paragraph(paragraph))); paragraph.clear(); };
        const auto flush_items = [&] {
            if (items.empty()) return;
            std::vector<std::string> block;
            for (const auto& item : items) block.push_back("* " + inline_doc(one_paragraph(item)));
            add(block);
            items.clear();
        };
        for (std::string raw; std::getline(in, raw);) {
            const auto stripped = trim(raw);
            if (starts(stripped, "- ") || starts(stripped, "* ")) { flush_paragraph(); items.push_back(stripped.substr(2)); continue; }
            if (!items.empty() && !stripped.empty() && starts(raw, "  ")) { items.back() += "\n" + stripped; continue; }
            flush_items();
            if (stripped.empty()) { flush_paragraph(); continue; }
            paragraph += (paragraph.empty() ? "" : "\n") + stripped;
        }
        flush_paragraph();
        flush_items();
    }
    // Verbatim code (bird tracks).
    void verbatim(const std::string& text) {
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string raw; std::getline(in, raw);) body.push_back(raw);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        if (body.empty()) return;
        std::vector<std::string> block;
        for (const auto& item : body) block.push_back("> " + item);
        add(block);
    }
    // `indent` precedes every line; the first line starts with "-- |".
    std::string render(const std::string& indent) const {
        std::string out;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (i) out += indent + "--\n";
            for (std::size_t j = 0; j < blocks[i].size(); ++j)
                out += indent + (i == 0 && j == 0 ? "-- | " : "-- ") + blocks[i][j] + "\n";
        }
        return out.empty() ? indent + "-- | Native binding.\n" : out;
    }
};

// C++ operators and their Haskell names ("" = not bound).
std::string hs_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "call";
    static const std::map<std::string, std::pair<const char*, const char*>> table = {
        {"operator+", {"plus", "add"}}, {"operator-", {"negate", "subtract"}}, {"operator*", {nullptr, "multiply"}},
        {"operator/", {nullptr, "divide"}}, {"operator%", {nullptr, "modulo"}}, {"operator==", {nullptr, "equals"}},
        {"operator!=", {nullptr, "notEquals"}}, {"operator<", {nullptr, "less"}}, {"operator<=", {nullptr, "lessEqual"}},
        {"operator>", {nullptr, "greater"}}, {"operator>=", {nullptr, "greaterEqual"}}, {"operator&", {nullptr, "bitAnd"}},
        {"operator|", {nullptr, "bitOr"}}, {"operator^", {nullptr, "bitXor"}}, {"operator~", {"complement", nullptr}},
        {"operator!", {"not", nullptr}}, {"operator<<", {nullptr, "shiftLeft"}}, {"operator>>", {nullptr, "shiftRight"}},
        {"operator[]", {nullptr, "at"}}, {"operator+=", {nullptr, "addAssign"}}, {"operator-=", {nullptr, "subtractAssign"}},
        {"operator*=", {nullptr, "multiplyAssign"}}, {"operator/=", {nullptr, "divideAssign"}}};
    const auto found = table.find(name);
    if (found == table.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

std::string exception_kind(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "InvalidArgument";
    if (bare == "std::out_of_range") return "OutOfRange";
    if (bare == "std::domain_error") return "DomainError";
    if (bare == "std::length_error") return "LengthError";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "ArithmeticError";
    if (bare == "std::bad_alloc") return "AllocationError";
    return "NativeError";
}

// Whether a Haskell type needs parentheses as an argument of a type constructor or function.
bool atomic(const std::string& type) {
    int depth = 0;
    for (const char c : type) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (c == ' ' && depth == 0) return false;
    }
    return true;
}
std::string paren(const std::string& type) { return atomic(type) ? type : "(" + type + ")"; }

// A Haskell type for a C++ type.
struct HT {
    std::string type;
    std::string word;         // for overload names (PascalCase)
    std::string why;          // non-empty: no Haskell type
    bool ord = false;         // an Ord instance (Map keys, Set elements)
    std::string object;       // a C++ class taken as a parameter (a type variable constrained by its Is class)
};

// Names in one Haskell namespace (values, types and classes, or data constructors).
struct Names {
    std::set<std::string> taken;
    bool free(const std::string& name) const { return !taken.count(name); }
    void reserve(const std::string& name) { taken.insert(name); }
    std::string claim(const std::string& wanted) {
        auto name = wanted;
        while (!free(name)) name += "'";
        reserve(name);
        return name;
    }
};

// One Haskell function of a group of overloads: an overload taking its first `arity` C++ parameters.
struct Variant {
    const Overload* overload = nullptr;
    std::size_t arity = 0;
    std::string name;
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const HaskellOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedHaskellSource run();

private:
    HaskellOptions options_;
    std::string module_name_;
    Names values_, types_, constructors_;
    std::map<std::string, std::string> prefixes_;                  // class -> value prefix (circle)
    std::map<std::string, std::string> is_classes_, overrides_types_, overrides_ctors_, overrides_values_, upcasts_, wrappers_;
    std::map<std::string, std::vector<std::string>> enum_constructors_;
    std::map<std::string, std::vector<Variant>> constructor_variants_, extend_variants_;
    std::map<std::string, std::vector<Overload>> public_constructors_;
    std::map<const Group*, std::vector<Variant>> variants_;
    std::map<std::string, std::pair<std::string, std::string>> field_names_;  // class::field -> getter, setter
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::string> submodule_prefixes_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> override_fields_;  // class -> (generic name, field)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> base_names_;       // class -> (generic name, function)
    std::set<std::string> dispatchable_;
    std::map<std::string, std::string> links_;                      // C++ name -> Haskell name
    std::vector<std::string> exports_;
    std::ostringstream body_;

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Haskell"; }
    // C++ names group the overloads: camelCase and camel_case stay separate groups (the generator
    // camel-cases them, and one of the two gets a prime).
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string& fallback) const override {
        return !fallback.empty() && is_upper(fallback[0]) ? pascal(name) : camel(name);
    }
    std::string field_name(const std::string& cpp) const override { return camel(cpp); }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return hs_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return camel(cpp); }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : camel(cpp);
            while (!taken.insert(name).second) name += "'";
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
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: create an extended object (extend" + plan.name + ") whose overrides implement its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: create an extended object (extend" + plan.name + ")";
    }
    std::string symbol_prefix() const override { return "rosetta_hs_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_HASKELL"; }
    std::string module_prefix() const override { return "rosetta_haskell_module_"; }

    // ------------------------------------------------------------ types
    HT ht(const Type& type, bool in) const;
    HT ht_spelled(std::string cpp) const;
    HT function_type(const Type& signature, bool in) const;
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    HT parameter(const Type& type) const {
        if (!writeback(type)) return ht(type, true);
        auto inner = ht(referent(type), false);
        if (!inner.why.empty()) return inner;
        return {"IORef.IORef " + paren(inner.type), inner.word + "Ref", "", false, ""};
    }
    HT result_type(const Function& function) const {
        if (is_void(function.result)) return {"()", "Unit", "", true, ""};
        return ht(function.result, false);
    }
    std::string overload_problem(const Overload& overload) const;
    std::string literal(const DefaultValue& value, const Type& type) const;
    std::string cpp_signature(const Function& function) const;
    std::vector<std::string> ancestors(const std::string& owner) const;
    const Group* find_group(const std::string& owner, const std::string& name, std::string* found_owner = nullptr) const;
    long dispatch_generic(const Overload& overload) const;

    // ------------------------------------------------------------ naming and emission
    std::vector<Variant> plan_variants(const std::vector<Overload>& overloads, const std::string& stem) const;
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    std::string link(const std::string& cpp) const;
    HDoc describe(const Documentation& documentation, const std::string& fallback) const;
    void add_tags(HDoc& doc, const Documentation& documentation) const;
    struct Emit {
        const Variant* variant = nullptr;
        std::size_t index = 0;
        std::string self_owner;  // a method's class (the object comes first), "" otherwise
        long generic = -1;       // a virtual method: extended objects run their override
        std::string note;
        bool direct = false;     // a Base function: no Haskell dispatch
    };
    void emit_function(const Emit& e);
    void emit_constructor(const ClassPlan& plan, const Variant& variant, bool extend);
    void emit_class(const std::string& owner);
    void emit_enum(const std::string& qualified);
    void emit_constant(const ConstantPlan& constant, const std::string& name);
    void export_item(const std::string& item) { exports_.push_back(item); }
    void heading(const std::string& text) { exports_.push_back("-- " + text); }
    std::string file_doc() const;
};

// ---------------------------------------------------------------- types
HT Generator::function_type(const Type& function, bool in) const {
    std::vector<HT> arguments;
    for (std::size_t i = 1; i < function.arguments.size(); ++i) {
        arguments.push_back(ht(function.arguments[i], false));
        if (!arguments.back().why.empty()) return arguments.back();
    }
    if (arguments.size() > 4) return {"", "", "std::function with more than 4 parameters has no Haskell mapping", false, ""};
    const HT result = function.arguments.empty() || is_void(function.arguments[0]) ? HT{"()", "Unit", "", true, ""} : ht(function.arguments[0], false);
    if (!result.why.empty()) return result;
    (void)in;
    std::string type;
    for (const auto& argument : arguments) type += (argument.type.find("->") != std::string::npos ? "(" + argument.type + ")" : argument.type) + " -> ";
    type += "P.IO " + paren(result.type);
    return {type, "Function", "", false, ""};
}

HT Generator::ht(const Type& type, bool in) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return {"P.String", "String", "", true, ""};
        if (is_void(pointee)) return {"", "", "void pointer has no Haskell mapping", false, ""};
        return ht(pointee, in);
    }
    if (is_void(target)) return {"()", "Unit", "", true, ""};
    if (!conversion(target).empty()) return ht_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return {"P.Bool", "Bool", "", true, ""};
        if (bare == "char") return {"P.Char", "Char", "", true, ""};
        if (bare == "float" || bare == "double" || bare == "long double") return {"P.Double", "Double", "", true, ""};
        static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int",
            "long", "long long"};
        const auto spelled = normalize(strip_cv(target.spelling));
        if (bare == "unsigned long long" || (bare == "unsigned long" && spelled.find("int64") != std::string::npos))
            return {"Word.Word64", "Word64", "", true, ""};
        if (integers.count(bare) || bare == "unsigned long") return {"P.Int", "Int", "", true, ""};
        return {"", "", "unsupported Haskell type " + target.canonical, false, ""};
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return {"P.Int", "Int", "", true, ""};
        return {found->second, found->second, "", true, ""};
    }
    if (bound(target)) {
        const auto& name = classes_.at(target.declaration).name;
        return {name, name, "", false, in ? target.declaration : std::string()};
    }
    if (bare == "std::filesystem::path") return {"P.String", "String", "", true, ""};
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {"()", "Unit", "", true, ""};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) {
            return i < target.arguments.size() ? ht(target.arguments[i], false) : HT{"", "", "incomplete template " + target.canonical, false, ""};
        };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return {"P.String", "String", "", true, ""};
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span") {
            const auto element = arg(0);
            if (!element.why.empty()) return element;
            if (!target.arguments.empty() && strip_cv(target.arguments[0].canonical) == "unsigned char")
                return {"B.ByteString", "Bytes", "", true, ""};
            return {"[" + element.type + "]", "List" + element.word, "", element.ord, ""};
        }
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset" ||
            declaration == "std::unordered_multiset") {
            const auto element = arg(0);
            if (!element.why.empty()) return element;
            const bool unique = declaration == "std::set" || declaration == "std::unordered_set";
            if (element.ord && unique) return {"S.Set " + paren(element.type), "Set" + element.word, "", true, ""};
            return {"[" + element.type + "]", "List" + element.word, "", element.ord, ""};
        }
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" ||
            declaration == "std::unordered_multimap") {
            const auto key = arg(0), value = arg(1);
            if (!key.why.empty()) return key;
            if (!value.why.empty()) return value;
            const bool unique = declaration == "std::map" || declaration == "std::unordered_map";
            if (key.ord && unique) return {"M.Map " + paren(key.type) + " " + paren(value.type), "Map", "", key.ord && value.ord, ""};
            return {"[(" + key.type + ", " + value.type + ")]", "Pairs", "", key.ord && value.ord, ""};
        }
        if (declaration == "std::optional") {
            const auto element = arg(0);
            if (!element.why.empty()) return element;
            return {"P.Maybe " + paren(element.type), "Maybe" + element.word, "", element.ord, ""};
        }
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            if (target.arguments.empty()) return {"", "", "incomplete template " + target.canonical, false, ""};
            return ht(target.arguments[0], in);
        }
        if (declaration == "std::complex") return {"C.Complex P.Double", "Complex", "", false, ""};
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::vector<HT> elements;
            bool ord = true;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) {
                elements.push_back(arg(i));
                if (!elements.back().why.empty()) return elements.back();
                ord = ord && elements.back().ord;
            }
            if (elements.empty()) return {"()", "Unit", "", true, ""};
            if (elements.size() == 1) return elements[0];
            if (elements.size() > 4) return {"", "", "std::tuple of more than 4 elements has no Haskell mapping", false, ""};
            std::vector<std::string> types;
            std::string word = "Tuple";
            for (const auto& element : elements) { types.push_back(element.type); word += element.word; }
            return {"(" + join(types, ", ") + ")", word, "", ord, ""};
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function)
            return function_type(target.arguments[0], in);
        if (declaration == "std::chrono::duration") return {"P.Double", "Seconds", "", true, ""};
        if (declaration == "std::variant") return {"", "", "std::variant has no Haskell mapping", false, ""};
    }
    return {"", "", "unsupported Haskell type " + target.canonical, false, ""};
}

// The Haskell type of a policy conversion's target, spelled as C++ text ("std::vector<double>").
HT Generator::ht_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int", "long",
        "unsigned long", "long long", "std::size_t", "size_t", "std::ptrdiff_t", "std::int64_t", "std::int32_t", "std::uint32_t",
        "std::int16_t", "std::uint16_t", "std::int8_t", "std::uint8_t", "int64_t", "int32_t", "uint32_t"};
    if (integers.count(cpp)) return {"P.Int", "Int", "", true, ""};
    if (cpp == "unsigned long long" || cpp == "std::uint64_t" || cpp == "uint64_t") return {"Word.Word64", "Word64", "", true, ""};
    if (cpp == "bool") return {"P.Bool", "Bool", "", true, ""};
    if (cpp == "char") return {"P.Char", "Char", "", true, ""};
    if (cpp == "float" || cpp == "double" || cpp == "long double") return {"P.Double", "Double", "", true, ""};
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return {"P.String", "String", "", true, ""};
    for (const auto& [qualified, name] : enum_names_) if (normalize(qualified) == cpp) return {name, name, "", true, ""};
    for (const auto& owner : class_order_)
        if (normalize(owner) == cpp) return {classes_.at(owner).name, classes_.at(owner).name, "", false, ""};
    const auto open = cpp.find('<');
    if (open == std::string::npos || cpp.back() != '>') return {"", "", "unsupported Haskell conversion target " + cpp, false, ""};
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
    const auto arg = [&](std::size_t i) { return i < arguments.size() ? ht_spelled(arguments[i]) : HT{"", "", "incomplete " + cpp, false, ""}; };
    if (name == "std::vector" || name == "std::deque" || name == "std::list" || name == "std::array" || name == "std::set" ||
        name == "std::unordered_set") {
        const auto element = arg(0);
        if (!element.why.empty()) return element;
        return {"[" + element.type + "]", "List" + element.word, "", element.ord, ""};
    }
    if (name == "std::map" || name == "std::unordered_map") {
        const auto key = arg(0), value = arg(1);
        if (!key.why.empty()) return key;
        if (!value.why.empty()) return value;
        if (key.ord) return {"M.Map " + paren(key.type) + " " + paren(value.type), "Map", "", value.ord, ""};
        return {"[(" + key.type + ", " + value.type + ")]", "Pairs", "", false, ""};
    }
    if (name == "std::optional") {
        const auto element = arg(0);
        if (!element.why.empty()) return element;
        return {"P.Maybe " + paren(element.type), "Maybe" + element.word, "", element.ord, ""};
    }
    if (name == "std::complex") return {"C.Complex P.Double", "Complex", "", false, ""};
    if ((name == "std::pair" || name == "std::tuple") && arguments.size() >= 2 && arguments.size() <= 4) {
        std::vector<std::string> types;
        std::string word = "Tuple";
        bool ord = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto element = arg(i);
            if (!element.why.empty()) return element;
            types.push_back(element.type);
            word += element.word;
            ord = ord && element.ord;
        }
        return {"(" + join(types, ", ") + ")", word, "", ord, ""};
    }
    return {"", "", "unsupported Haskell conversion target " + cpp, false, ""};
}

std::string Generator::overload_problem(const Overload& overload) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto problem = parameter(function.parameters[i]).why;
        if (!problem.empty()) return problem;
    }
    if (!function.is_constructor) return result_type(function).why;
    return {};
}

// A Haskell literal for a C++ default argument ("" = none), for the documentation.
std::string Generator::literal(const DefaultValue& value, const Type& type) const {
    auto text = value.value;
    if (value.kind == "int") {
        while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
        return text;
    }
    if (value.kind == "float") return shortest_double(text);
    if (value.kind == "bool") return value.value == "true" ? "True" : "False";
    if (value.kind == "null") return "Nothing";
    if (value.kind == "char") return value.value.size() == 1 ? "'" + value.value + "'" : std::string();
    if (value.kind == "string") {
        std::string decoded;
        return cpp_string(value.value, decoded) ? hs_string(decoded) : std::string();
    }
    if (value.kind == "enum") {
        const auto& target = referent(type);
        const auto found = enum_constructors_.find(target.declaration);
        if (found == enum_constructors_.end() || !enums_.count(target.declaration)) return {};
        const auto& enumeration = *enums_.at(target.declaration);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return found->second[i];
    }
    return {};
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
            text += " = " + (value.kind == "char" ? "'" + value.value + "'" : value.kind == "default" || value.value.empty() ? std::string("{}") : value.value);
        } else if (i + function.default_arguments >= function.parameters.size()) text += " = ...";
    }
    text += ")";
    if (function.is_const) text += " const";
    return text;
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

// ---------------------------------------------------------------- naming
// The Haskell functions of a group of overloads: one per overload and arity (default arguments),
// with duplicates of an earlier Haskell signature dropped (C++ resolves the call at run time). The
// overload taking the fewest arguments keeps `stem`; the others add their parameter names
// (newVec2XY), else their parameter types (pickDouble), else a number. Longer arities of one
// overload add "With" and the last parameter's name (addWithB).
std::vector<Variant> Generator::plan_variants(const std::vector<Overload>& overloads, const std::string& stem) const {
    struct Candidate { const Overload* overload; std::vector<std::size_t> arities; std::string by_name, by_type; std::size_t minimum; };
    std::vector<Candidate> candidates;
    std::set<std::string> seen;
    for (const auto& overload : overloads) {
        if (!overload_problem(overload).empty()) continue;
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const std::size_t required = function.parameters.size() - function.default_arguments;
        Candidate candidate{&overload, {}, {}, {}, 0};
        for (std::size_t arity = std::max(required, first); arity <= function.parameters.size(); ++arity) {
            std::string key;
            for (std::size_t i = first; i < arity; ++i) key += parameter(function.parameters[i]).type + ",";
            if (seen.insert(key).second) candidate.arities.push_back(arity);
        }
        if (candidate.arities.empty()) continue;
        candidate.minimum = candidate.arities.front() - first;
        bool named = true;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            if (cpp.empty() || !identifier(cpp)) named = false;
            else candidate.by_name += pascal(cpp);
            candidate.by_type += parameter(function.parameters[i]).word;
        }
        if (!named) candidate.by_name.clear();
        if (candidate.by_type.empty()) candidate.by_type = "Unit";
        candidates.push_back(candidate);
    }
    std::size_t primary = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) if (candidates[i].minimum < candidates[primary].minimum) primary = i;
    std::vector<Variant> result;
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        const auto& candidate = candidates[c];
        std::string name = stem;
        if (c != primary) {
            const auto unique = [&](const std::string Candidate::*field) {
                const auto& value = candidate.*field;
                if (value.empty()) return false;
                for (std::size_t o = 0; o < candidates.size(); ++o)
                    if (o != c && candidates[o].*field == value) return false;
                return true;
            };
            if (unique(&Candidate::by_name)) name += candidate.by_name;
            else if (unique(&Candidate::by_type)) name += candidate.by_type;
            else name += std::to_string(c + 1);
        }
        const auto& function = *candidate.overload->function;
        const std::size_t first = candidate.overload->receives_self ? 1 : 0;
        for (std::size_t a = 0; a < candidate.arities.size(); ++a) {
            const auto arity = candidate.arities[a];
            auto variant = name;
            if (a > 0) {
                const auto cpp = arity - 1 < function.parameter_names.size() ? function.parameter_names[arity - 1] : std::string();
                variant += "With" + (cpp.empty() || !identifier(cpp) ? "Arg" + std::to_string(arity - first) : pascal(cpp));
            }
            result.push_back({candidate.overload, arity, variant});
        }
    }
    return result;
}

void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    // The runtime's names, re-exported.
    for (const auto* name : {"CppError", "ErrorKind", "CppObject", "CppEnum", "IORef"}) types_.reserve(name);
    for (const auto* name : {"CppError", "BindingError", "InvalidArgument", "OutOfRange", "DomainError", "LengthError", "ArithmeticError",
                             "AllocationError", "NativeError"})
        constructors_.reserve(name);
    for (const auto* name : {"errorKind", "errorMessage", "closeObject", "isClosed", "downcast", "sameObject", "enumValue", "enumName",
                             "enumAll", "enumFromValue", "enumFromName"})
        values_.reserve(name);
    // Types: classes, enumerations, then the Is classes and the override records.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = types_.claim(pascal(plan.name));
        wrappers_[owner] = constructors_.claim(plan.name + "'");
    }
    for (auto& [qualified, name] : enum_names_) name = types_.claim(name);
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        is_classes_[owner] = types_.claim("Is" + plan.name);
        if (plan.director) {
            overrides_types_[owner] = types_.claim(plan.name + "Overrides");
            overrides_ctors_[owner] = constructors_.claim(overrides_types_[owner]);
        }
    }
    // Enumerators: the enumeration's name, then the enumerator's (an enumerator starting with the
    // enumeration's name keeps its own).
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        for (const auto& enumerator : enumeration.enumerators) {
            const auto own = pascal(renamed(qualified + "::" + enumerator, enumerator));
            const auto wanted = starts(own, name) && own.size() > name.size() ? own : name + own;
            enum_constructors_[qualified].push_back(constructors_.claim(wanted));
        }
    }
    // Values: every planned function's wanted name, claimed in two passes (plain names first, so
    // a variant never takes another group's plain name).
    struct Planned { std::vector<Variant>* variants; std::string stem; };
    std::vector<Planned> planned;
    const auto plan = [&](std::vector<Variant>& into, const std::vector<Overload>& overloads, const std::string& stem) {
        into = plan_variants(overloads, stem);
        planned.push_back({&into, stem});
    };
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto prefix = cls.name;
        while (!prefix.empty() && prefix.back() == '\'') prefix.pop_back();
        prefix[0] = lower(prefix[0]);
        prefixes_[owner] = prefix;
        upcasts_[owner] = values_.claim("to" + cls.name);
        auto& visible = public_constructors_[owner];
        for (const auto& overload : cls.constructors)
            if (!cls.record->abstract && overload.function->access == "public") visible.push_back(overload);
        if (!visible.empty()) plan(constructor_variants_[owner], visible, "new" + cls.name);
        if (cls.director) plan(extend_variants_[owner], cls.constructors, "extend" + cls.name);
        for (auto& group : cls.methods) plan(variants_[&group], group.overloads, prefix + member(group.name));
        for (auto& group : cls.operators) {
            const auto& front = group.overloads.front();
            if (front.extension && !front.receives_self) continue;   // a free operator with the object on the right
            plan(variants_[&group], group.overloads, prefix + member(group.name));
        }
    }
    const auto class_items = planned.size();
    for (auto& group : functions) {
        const auto prefix = group.module.empty() ? std::string() : submodule_prefixes_.at(group.module);
        plan(variants_[&group], group.overloads, prefix.empty() ? camel(group.name) : prefix + member(group.name));
    }
    // Plain names in order: methods, then fields, then free functions (class members keep their
    // names when a free function is spelled like one); variants after all of them.
    std::set<const Variant*> claimed;
    const auto claim_plain = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i)
            for (auto& variant : *planned[i].variants)
                if (variant.name == planned[i].stem && values_.free(variant.name)) {
                    values_.reserve(variant.name);
                    claimed.insert(&variant);
                    break;
                }
    };
    claim_plain(0, class_items);
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        const auto& prefix = prefixes_.at(owner);
        for (const auto& field : cls.fields) {
            const auto& f = *field.field;
            const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
            const auto name = member(field.name);
            const auto getter = values_.claim(prefix + name);
            field_names_[owner + "::" + f.name] = {getter, writable ? values_.claim(prefix + "Set" + name) : std::string()};
        }
    }
    claim_plain(class_items, planned.size());
    for (auto& item : planned)
        for (auto& variant : *item.variants)
            if (!claimed.count(&variant)) variant.name = values_.claim(variant.name);
    // Class constants, Base functions and override records.
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        const auto& prefix = prefixes_.at(owner);
        for (auto& constant : cls.constants) constant_names_[&constant] = values_.claim(prefix + member(constant.name));
        if (cls.director) {
            std::set<std::string> seen;
            for (const auto& item : overridable(cls)) {
                if (!seen.insert(item.name).second || !find_group(owner, item.name)) continue;
                override_fields_[owner].emplace_back(item.name, values_.claim(prefix + member(item.name) + "Override"));
                base_names_[owner].emplace_back(item.name, values_.claim(prefix + member(item.name) + "Base"));
            }
            overrides_values_[owner] = values_.claim(prefix + "Overrides");
        }
    }
    for (auto& constant : constants) constant_names_[&constant] = values_.claim(camel(constant.name));
    // Haddock references.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        for (const auto& group : cls.methods) {
            const auto& variants = variants_.at(&group);
            if (variants.empty()) continue;
            links_[owner + "::" + group.overloads.front().function->name] = variants.front().name;
            links_.emplace(cls.record->name + "::" + group.overloads.front().function->name, variants.front().name);
        }
        links_[owner] = cls.name;
        links_.emplace(cls.record->name, cls.name);
    }
    for (const auto& group : functions) {
        const auto& variants = variants_.at(&group);
        if (variants.empty()) continue;
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = variants.front().name;
            if (group.module.empty()) links_.emplace(overload.function->name, variants.front().name);
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = name;
        links_.emplace(enums_.at(qualified)->name, name);
    }
}

// ---------------------------------------------------------------- documentation
std::string Generator::link(const std::string& cpp) const {
    auto target = trim(cpp);
    while (!target.empty() && (target.back() == '.' || target.back() == ',' || target.back() == ')' || target.back() == '(')) target.pop_back();
    const auto found = links_.find(normalize(target));
    return found != links_.end() ? "'" + found->second + "'" : code(target);
}

HDoc Generator::describe(const Documentation& documentation, const std::string& fallback) const {
    HDoc doc;
    auto details = documentation.details;
    auto brief = documentation.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    // The fallback is already Haddock markup.
    if (!brief.empty()) doc.prose(brief);
    if (doc.blocks.empty()) doc.line(fallback);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") doc.verbatim(block.text);
        else if (block.kind == "heading") doc.line("__" + escape(one_paragraph(block.text)) + "__");
        else doc.prose(block.text);
    }
    for (const auto& item : documentation.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        doc.line("__" + escape(label) + ":__ " + inline_doc(one_paragraph(item.text)));
    }
    return doc;
}

void Generator::add_tags(HDoc& doc, const Documentation& documentation) const {
    for (const auto& item : documentation.throws) {
        auto text = item.text.empty() ? std::string() : ": " + inline_doc(one_paragraph(item.text));
        if (text.empty() || std::string(".!?").find(text.back()) == std::string::npos) text += ".";
        doc.line("Throws t'R.CppError' ('R." + exception_kind(item.name) + "')" + (item.name.empty() ? "" : " for C++ " + code(item.name)) + text);
    }
    std::vector<std::string> see;
    for (const auto& item : documentation.see) if (!trim(item).empty()) see.push_back(link(item));
    if (!see.empty()) doc.line("See also: " + join(see, ", ") + ".");
}

// ---------------------------------------------------------------- emission
// A function of one variant: its signature (with Haddock per argument), then its equation.
void Generator::emit_function(const Emit& e) {
    const auto& variant = *e.variant;
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    const bool self = !e.self_owner.empty();
    const auto res = result_type(function);

    auto doc = describe(function.documentation, "Calls C++ " + code(function.qualified_name()) + ".");
    struct Arg { std::string type, doc, variable, value; };
    std::vector<Arg> args;
    std::vector<std::string> constraints, reads, writebacks;
    char next_var = 'a';
    const auto type_variable = [&](const std::string& owner) {
        const std::string var(1, next_var++);
        constraints.push_back(is_classes_.at(owner) + " " + var);
        return var;
    };
    if (self) {
        const auto var = type_variable(e.self_owner);
        args.push_back({var, "the object", "_self", "R.object _self"});
    } else if (overload.receives_self) {
        // An extension attached by the policy: its first C++ parameter is the object.
        const auto owner = ht(function.parameters[0], true).object;
        const auto var = owner.empty() ? std::string("a") : type_variable(owner);
        args.push_back({var, "the object", "_self", "R.object _self"});
    }
    std::size_t slots = 0;
    std::vector<std::string> omitted;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& type = function.parameters[i];
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (i >= variant.arity) {
            const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
            const auto shown = literal(value, type);
            omitted.push_back(code(cpp.empty() ? names[i - first] : cpp) + " = " +
                              (!shown.empty() ? code(shown) : value.value.empty() ? std::string("its C++ default") : code(value.value)));
            continue;
        }
        const auto mt = parameter(type);
        std::string text;
        for (const auto& item : function.documentation.params) if (!cpp.empty() && item.name == cpp && !item.text.empty()) text = inline_doc(one_paragraph(item.text));
        const auto variable = "_x" + std::to_string(i - first + 1);
        if (writeback(type)) {
            const auto slot = std::to_string(slots++);
            reads.push_back("_v" + std::to_string(i - first + 1) + " <- IORef.readIORef " + variable);
            writebacks.push_back("R.writeBack _reply " + slot + " " + variable);
            text += std::string(text.empty() ? "" : "; ") + "C++ writes it back";
            args.push_back({mt.type, text, variable, "R.slot " + slot + " _v" + std::to_string(i - first + 1)});
        } else if (!mt.object.empty()) {
            args.push_back({type_variable(mt.object), text, variable, "R.object " + variable});
        } else {
            args.push_back({mt.type, text, variable, "R.toCpp " + variable});
        }
    }
    if (!omitted.empty()) doc.line("Arguments left out take the C++ defaults: " + join(omitted, ", ") + ".");
    if (e.generic >= 0 && self) {
        doc.line("Virtual: on an extended object (made by an @extend@ function) it runs the Haskell override.");
    }
    if (e.direct) doc.line("Runs the C++ implementation even on an extended object: what an override calls for the behavior it replaces.");
    if (!e.note.empty()) doc.line(e.note);
    doc.line("C++: " + code(cpp_signature(function)));
    add_tags(doc, function.documentation);
    const auto returns = one_paragraph(function.documentation.returns);
    // The signature: one line, or one line per argument when some argument has documentation.
    bool documented = !returns.empty();
    for (const auto& arg : args) documented = documented || !arg.doc.empty();
    const auto arg_type = [](const std::string& type) { return type.find("->") != std::string::npos ? "(" + type + ")" : type; };
    const auto result_text = "P.IO " + paren(res.type);
    std::string signature;
    const auto context = constraints.empty() ? std::string() : constraints.size() == 1 ? constraints[0] : "(" + join(constraints, ", ") + ")";
    if (!documented) {
        signature = variant.name + " :: " + (context.empty() ? "" : context + " => ");
        for (const auto& arg : args) signature += arg_type(arg.type) + " -> ";
        signature += result_text + "\n";
    } else {
        signature = variant.name + "\n";
        std::string lead = "  :: ";
        if (!context.empty()) { signature += lead + context + "\n"; lead = "  => "; }
        for (const auto& arg : args) {
            signature += lead + arg_type(arg.type) + (arg.doc.empty() ? "" : "  -- ^ " + arg.doc) + "\n";
            lead = "  -> ";
        }
        signature += lead + result_text + (returns.empty() ? "" : "  -- ^ " + inline_doc(returns)) + "\n";
    }
    std::vector<std::string> variables, values;
    for (const auto& arg : args) { variables.push_back(arg.variable); values.push_back(arg.value); }
    const auto callee = e.generic >= 0 && self && !e.direct ? "R.callVirtual " + std::to_string(e.generic) + " " + std::to_string(e.index)
                                                              : "R.call " + std::to_string(e.index);
    const auto call = callee + " [" + join(values, ", ") + "]";
    body_ << "\n" << doc.render("") << signature << variant.name << (variables.empty() ? "" : " " + join(variables, " ")) << " =";
    if (writebacks.empty()) {
        body_ << " " << call << " P.>>= R.result\n";
    } else {
        body_ << " do\n";
        for (const auto& line : reads) body_ << "  " << line << "\n";
        body_ << "  _reply <- " << call << "\n";
        for (const auto& line : writebacks) body_ << "  " << line << "\n";
        body_ << "  R.result _reply\n";
    }
    export_item(variant.name);
}

void Generator::emit_constructor(const ClassPlan& plan, const Variant& variant, bool extend) {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const auto owner = plan.record->qualified_name();
    const auto names = parameter_names(overload);
    auto doc = describe(function.documentation, extend ? "Creates an extended " + plan.name + ": a C++ subclass object whose virtual methods run the given overrides."
                                                       : "Creates a " + plan.name + " (C++ constructor).");
    if (extend && (!function.documentation.brief.empty() || !function.documentation.details.empty()))
        doc.line("Creates an extended object: a C++ subclass whose virtual methods run the given overrides.");
    std::vector<std::string> types, docs, variables, values, constraints, omitted;
    char next_var = 'a';
    if (extend) {
        types.push_back(overrides_types_.at(owner));
        docs.push_back("the Haskell overrides (fields left 'P.Nothing' run the C++ implementation)");
        variables.push_back("_overrides");
    }
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        const auto& type = function.parameters[i];
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        if (i >= variant.arity) {
            const auto& value = i < function.default_values.size() ? function.default_values[i] : DefaultValue{};
            const auto shown = literal(value, type);
            omitted.push_back(code(cpp.empty() ? names[i] : cpp) + " = " + (!shown.empty() ? code(shown) : value.value.empty() ? std::string("its C++ default") : code(value.value)));
            continue;
        }
        const auto mt = parameter(type);
        std::string text;
        for (const auto& item : function.documentation.params) if (!cpp.empty() && item.name == cpp && !item.text.empty()) text = inline_doc(one_paragraph(item.text));
        const auto variable = "_x" + std::to_string(i + 1);
        variables.push_back(variable);
        docs.push_back(text);
        if (!mt.object.empty()) {
            const std::string var(1, next_var++);
            constraints.push_back(is_classes_.at(mt.object) + " " + var);
            types.push_back(var);
            values.push_back("R.object " + variable);
        } else {
            types.push_back(mt.type);
            values.push_back("R.toCpp " + variable);
        }
    }
    if (!omitted.empty()) doc.line("Arguments left out take the C++ defaults: " + join(omitted, ", ") + ".");
    if (extend)
        doc.line("C++ calls the overrides too, and an override's first argument is the object itself; the @...Base@ functions run "
                 "the C++ implementation an override replaces. A pure virtual method without override throws a 'R.BindingError'.");
    doc.line("C++: " + code(cpp_signature(function)));
    add_tags(doc, function.documentation);
    bool documented = false;
    for (const auto& text : docs) documented = documented || !text.empty();
    const auto arg_type = [](const std::string& type) { return type.find("->") != std::string::npos ? "(" + type + ")" : type; };
    const auto context = constraints.empty() ? std::string() : constraints.size() == 1 ? constraints[0] : "(" + join(constraints, ", ") + ")";
    std::string signature;
    if (!documented) {
        signature = variant.name + " :: " + (context.empty() ? "" : context + " => ");
        for (const auto& type : types) signature += arg_type(type) + " -> ";
        signature += "P.IO " + plan.name + "\n";
    } else {
        signature = variant.name + "\n";
        std::string lead = "  :: ";
        if (!context.empty()) { signature += lead + context + "\n"; lead = "  => "; }
        for (std::size_t i = 0; i < types.size(); ++i) {
            signature += lead + arg_type(types[i]) + (docs[i].empty() ? "" : "  -- ^ " + docs[i]) + "\n";
            lead = "  -> ";
        }
        signature += lead + "P.IO " + plan.name + "\n";
    }
    std::string director = "P.Nothing";
    if (extend) {
        std::vector<std::string> entries;
        const auto& wrapper = wrappers_.at(owner);
        for (const auto& item : overridable(plan)) {
            const auto field = std::find_if(override_fields_[owner].begin(), override_fields_[owner].end(), [&](const auto& f) { return f.first == item.name; });
            if (field == override_fields_[owner].end()) continue;
            const auto* group = find_group(owner, item.name);
            const auto key = "(" + std::to_string(generic_index_.at(item.name)) + ", " + std::to_string(item.method->parameters.size()) + ")";
            if (std::any_of(entries.begin(), entries.end(), [&](const std::string& entry) { return starts(entry, "(" + key); })) continue;
            entries.push_back("(" + key + ", P.maybe (R.base " + std::to_string(group->index) + ") (\\_f -> R.overrideWith (\\_h -> _f (" + wrapper +
                              " _h))) (" + field->second + " _overrides))");
        }
        director = "(P.Just (R.director\n    [ " + join(entries, "\n    , ") + "\n    ]))";
    }
    body_ << "\n" << doc.render("") << signature << variant.name << (variables.empty() ? "" : " " + join(variables, " ")) << " =\n  P.fmap "
          << wrappers_.at(owner) << " (R.construct " << plan.constructor_index << " " << director << " [" << join(values, ", ") << "])\n";
    export_item(variant.name);
}

void Generator::emit_enum(const std::string& qualified) {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& ctors = enum_constructors_.at(qualified);
    auto doc = describe(enumeration.documentation, "The C++ enumeration " + code(qualified) + ".");
    doc.line("Its constructors cross to C++ by name; 'R.enumValue' and 'R.enumFromValue' give the C++ values.");
    body_ << "\n" << doc.render("") << "data " << name << "\n";
    for (std::size_t i = 0; i < ctors.size(); ++i)
        body_ << "  " << (i ? "| " : "= ") << ctors[i] << "  -- ^ C++ " << code(enumeration.enumerators[i]) << " (" << escape(enumeration.enumerator_values[i]) << ").\n";
    if (ctors.empty()) body_ << "  = " << name << "Empty'\n";
    body_ << "  deriving (P.Eq, P.Ord, P.Show, P.Read, P.Bounded, P.Enum)\n\n"
          << "instance R.CppEnum " << name << " where\n  enumValue _x = case _x of\n";
    for (std::size_t i = 0; i < ctors.size(); ++i) {
        auto value = enumeration.enumerator_values[i];
        body_ << "    " << ctors[i] << " -> " << (!value.empty() && value[0] == '-' ? "(" + value + ")" : value) << "\n";
    }
    body_ << "  enumName _x = case _x of\n";
    for (std::size_t i = 0; i < ctors.size(); ++i)
        body_ << "    " << ctors[i] << " -> " << hs_string(renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i])) << "\n";
    body_ << "  enumAll = [" << join(ctors, ", ") << "]\n"
          << "instance R.ToCpp " << name << " where toCpp = R.enumToCpp\n"
          << "instance R.FromCpp " << name << " where fromCpp = R.enumFromCpp\n";
    export_item(name + "(..)");
}

void Generator::emit_constant(const ConstantPlan& constant, const std::string& name) {
    const auto mt = ht(*constant.type, false);
    if (!mt.why.empty()) return;
    auto doc = describe(*constant.documentation, "The C++ constant " + code(normalize(constant.expression)) + ".");
    body_ << "\n" << doc.render("") << name << " :: " << mt.type << "\n" << name << " = R.constant " << constant.index << "\n{-# NOINLINE "
          << name << " #-}\n";
    export_item(name);
}

void Generator::emit_class(const std::string& owner) {
    auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& name = plan.name;
    const auto& wrapper = wrappers_.at(owner);
    const auto& is_class = is_classes_.at(owner);
    const auto& prefix = prefixes_.at(owner);
    heading("* " + name);
    auto doc = describe(record.documentation, "The C++ class " + code(owner) + ".");
    if (!record.documentation.brief.empty() || !record.documentation.details.empty()) doc.line("The C++ class " + code(owner) + ".");
    std::vector<std::string> bases;
    for (const auto& base : plan.bases) bases.push_back("'" + classes_.at(base).name + "'");
    if (!bases.empty())
        doc.line("Derives from " + join(bases, ", ") + ": the functions of " + (bases.size() == 1 ? "that class" : "those classes") +
                 " take its objects too.");
    if (plan.director && !extend_variants_[owner].empty())
        doc.line(std::string(record.abstract ? "Abstract in C++: create" : "Create") + " an extended object ('" + extend_variants_[owner].front().name +
                 "') to implement its virtual methods in Haskell.");
    else if (record.abstract)
        doc.line("Abstract in C++: C++ functions return objects of derived classes.");
    doc.line("A value holds the C++ object; it is released when garbage collection reclaims the last value holding it, or by 'R.closeObject'.");
    body_ << "\n-- ---------------------------------------------------------------- " << name << "\n\n" << doc.render("") << "newtype " << name
          << " = " << wrapper << " R.Handle\n\n"
          << "instance R.CppObject " << name << " where\n  objectHandle (" << wrapper << " _h) = _h\n  wrapHandle = " << wrapper
          << "\n  classIndex _ = " << (std::find(class_order_.begin(), class_order_.end(), owner) - class_order_.begin()) << "\n"
          << "instance R.ToCpp " << name << " where toCpp = R.object\n"
          << "instance R.FromCpp " << name << " where fromCpp = R.fromObject\n"
          << "\n-- | Values of class '" << name << "', or of a class deriving from it.\nclass R.CppObject a => " << is_class << " a\n";
    export_item(name);
    export_item(is_class);
    for (std::size_t i = 0; i < class_order_.size(); ++i)
        if (derives(class_order_[i], owner)) body_ << "instance " << is_class << " " << classes_.at(class_order_[i]).name << "\n";
    const auto& upcast = upcasts_.at(owner);
    body_ << "\n-- | The object as a '" << name << "' (an upcast).\n" << upcast << " :: " << is_class << " a => a -> " << name << "\n" << upcast
          << " = R.wrapHandle P.. R.objectHandle\n";
    export_item(upcast);
    // Constructors.
    for (const auto& variant : constructor_variants_[owner]) emit_constructor(plan, variant, false);
    if (plan.director) {
        // The record of overrides and its empty value.
        const auto& type = overrides_types_.at(owner);
        body_ << "\n-- | Haskell overrides of the virtual methods of '" << name << "', for the @extend" << name
              << "@ functions: each field left 'P.Nothing' keeps the C++ implementation.\ndata " << type << " = " << overrides_ctors_.at(owner);
        std::vector<std::string> fields, nothing;
        for (const auto& [generic_name, field] : override_fields_[owner]) {
            const Overridable* found = nullptr;
            const auto items = overridable(plan);
            for (const auto& item : items) if (item.name == generic_name) { found = &item; break; }
            if (!found) continue;
            const auto& method = *found->method;
            std::string signature = name + " -> ";
            bool ok = true;
            for (const auto& parameter : method.parameters) {
                const auto mt = ht(parameter, false);
                if (!mt.why.empty()) { ok = false; break; }
                signature += (mt.type.find("->") != std::string::npos ? "(" + mt.type + ")" : mt.type) + " -> ";
            }
            const auto res = is_void(method.result) ? HT{"()", "Unit", "", true, ""} : ht(method.result, false);
            if (!ok || !res.why.empty()) continue;
            signature += "P.IO " + paren(res.type);
            fields.push_back(field + " :: P.Maybe (" + signature + ")\n    -- ^ Overrides " + (found->pure ? "pure virtual " : "virtual ") +
                             code(cpp_signature(method)) + ".");
            nothing.push_back(field + " = P.Nothing");
        }
        if (fields.empty()) body_ << " {}\n";
        else body_ << "\n  { " << join(fields, "\n  , ") << "\n  }\n";
        export_item(type + "(..)");
        const auto& value = overrides_values_.at(owner);
        body_ << "\n-- | No overrides: every virtual method runs its C++ implementation. Set the fields of the overrides to\n-- implement: @"
              << value << " { ... }@.\n" << value << " :: " << type << "\n" << value << " = " << overrides_ctors_.at(owner)
              << (nothing.empty() ? " {}" : " { " + join(nothing, ", ") + " }") << "\n";
        export_item(value);
        for (const auto& variant : extend_variants_[owner]) emit_constructor(plan, variant, true);
    }
    // Methods and operators.
    for (const auto* groups : {&plan.methods, &plan.operators})
        for (const auto& group : *groups) {
            if (!variants_.count(&group)) continue;
            for (const auto& variant : variants_.at(&group)) {
                const auto& overload = *variant.overload;
                const auto& function = *overload.function;
                const bool self = (!group.is_static && !overload.extension) || false;
                Emit e{&variant, group.index, self ? owner : std::string(), self ? dispatch_generic(overload) : -1, "", false};
                if (overload.extension) e.note = "Attached by the binding policy: C++ " + code(function.qualified_name()) + ".";
                emit_function(e);
            }
        }
    // Base functions: the C++ implementations of the methods extended objects override.
    for (const auto& [generic_name, function_name] : base_names_[owner]) {
        std::string group_owner;
        const auto* group = find_group(owner, generic_name, &group_owner);
        if (!group) continue;
        const Overload* chosen = nullptr;
        for (const auto& item : overridable(plan))
            if (item.name == generic_name)
                for (const auto& overload : group->overloads)
                    if (!chosen && !overload.extension && virtual_key(*overload.function) == virtual_key(*item.method) && overload_problem(overload).empty())
                        chosen = &overload;
        if (!chosen) continue;
        Variant variant{chosen, chosen->function->parameters.size(), function_name};
        Emit e{&variant, group->index, group_owner, -1, "", true};
        emit_function(e);
    }
    // Fields and constants.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto& [getter, setter] = field_names_.at(owner + "::" + f.name);
        const auto mt = ht(f.type, false);
        if (!mt.why.empty()) continue;
        const auto about = one_paragraph(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        const auto text = about.empty() ? "The C++ field " + code(owner + "::" + f.name) + "." : inline_doc(about);
        body_ << "\n-- | " << text << (setter.empty() ? " (Read-only.)" : "") << "\n" << getter << " :: " << is_class << " a => a -> P.IO "
              << paren(mt.type) << "\n" << getter << " _self = R.call " << field.getter_index << " [R.object _self] P.>>= R.result\n";
        export_item(getter);
        if (setter.empty()) continue;
        const auto in = ht(f.type, true);
        if (!in.object.empty())
            body_ << "\n-- | Sets the C++ field " << code(owner + "::" + f.name) << ".\n" << setter << " :: (" << is_class << " a, "
                  << is_classes_.at(in.object) << " b) => a -> b -> P.IO ()\n" << setter << " _self _x = R.call " << field.setter_index
                  << " [R.object _self, R.object _x] P.>>= R.result\n";
        else
            body_ << "\n-- | Sets the C++ field " << code(owner + "::" + f.name) << ".\n" << setter << " :: " << is_class << " a => a -> "
                  << (in.type.find("->") != std::string::npos ? "(" + in.type + ")" : in.type) << " -> P.IO ()\n" << setter << " _self _x = R.call "
                  << field.setter_index << " [R.object _self, R.toCpp _x] P.>>= R.result\n";
        export_item(setter);
    }
    for (const auto& constant : plan.constants) emit_constant(constant, constant_names_.at(&constant));
    (void)prefix;
}

std::string Generator::file_doc() const {
    std::string first = class_order_.empty() ? std::string("Shape") : classes_.at(class_order_.front()).name;
    std::ostringstream doc;
    const auto header = [&] {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }();
    doc << "-- | Haskell bindings to the C++ library " << escape(name_) << " (" << code(header) << "), generated by Rosetta.\n--\n"
        << "-- Every function is an 'P.IO' action calling C++. A C++ class is a type whose values hold a C++ object, with a type\n"
        << "-- class (@Is" << escape(first) << "@) that the classes deriving from it instantiate: a function on a base class takes\n"
        << "-- the objects of derived classes, @to...@ functions upcast and 'R.downcast' returns the most-derived class. Functions are\n"
        << "-- named after their class (@new" << escape(first) << "@, @" << escape(camel(first)) << "...@); C++ overloads and default arguments are\n"
        << "-- separate functions (@pick@, @pickDouble@; @add@, @addWithB@), which C++ resolves again at run time.\n--\n"
        << "-- Values: C++ integers are 'P.Int' (C++ checks the range), floating point 'P.Double', @std::string@ 'P.String',\n"
        << "-- sequences lists (@std::vector\\<unsigned char\\>@ a 'B.ByteString'), maps 'M.Map', sets 'S.Set', @std::optional@\n"
        << "-- 'P.Maybe', @std::pair@ and @std::tuple@ tuples, @std::complex@ 'C.Complex', enumerations data types, @std::function@\n"
        << "-- a function returning 'P.IO', and a non-const reference parameter an 'IORef.IORef' C++ writes back.\n--\n"
        << "-- Classes with virtual methods have @extend...@ functions: a C++ subclass object whose virtual methods run the\n"
        << "-- Haskell functions of a record of overrides (@...Overrides@), also when C++ calls them; @...Base@ functions run the\n"
        << "-- C++ implementation. C++ exceptions are t'R.CppError'; an exception a Haskell callback or override throws crosses C++\n"
        << "-- unchanged. Objects are released by garbage collection or 'R.closeObject'.\n--\n"
        << "-- Link the native library of the module (@-l" << escape(name_) << "@) and use the threaded runtime when C++ calls back\n"
        << "-- from its own threads. Names may meet the Prelude's: import the module qualified, or hide them.\n";
    return doc.str();
}

GeneratedHaskellSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    module_name_ = options_.module.empty() ? haskell_module_name(name_) : options_.module;
    {
        bool ok = !module_name_.empty();
        bool start = true;
        for (const char c : module_name_) {
            if (c == '.') { ok = ok && !start; start = true; continue; }
            ok = ok && (start ? is_upper(c) : (is_alnum(c) || c == '_' || c == '\''));
            start = false;
        }
        ok = ok && !start;
        if (!ok) throw BindingError("Haskell module name must be a module identifier (Capitalized parts separated by dots): " + module_name_);
        static const std::set<std::string> taken = {"Prelude", "Main", "Data.Map", "Data.Set", "Data.IORef", "Data.Word", "Data.Complex"};
        if (taken.count(module_name_) || starts(module_name_, "GHC.")) throw BindingError("Haskell module name names a standard module: " + module_name_);
    }
    GeneratedHaskellSource result;
    prepare();
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = pascal(renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified)));
        if (nested) name = pascal(classes_.at(enumeration.owner).name) + name;
        enum_names_[qualified] = name;
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, camel(flat(variable.namespaces) ? variable.name : exported(qualified)));
    });
    // Methods some extended object can override dispatch to it.
    for (const auto& owner : class_order_)
        if (classes_.at(owner).director)
            for (const auto& item : overridable(classes_.at(owner))) dispatchable_.insert(virtual_key(*item.method));
    // Overloads Haskell cannot express are reported (C++ keeps them).
    const auto report = [&](const Overload& overload, const std::string& what) {
        const auto problem = overload_problem(overload);
        if (!problem.empty()) diagnostics_.push_back(what + ": " + problem);
    };
    for (auto& group : functions) for (const auto& overload : group.overloads) report(overload, overload.function->qualified_name());
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        for (const auto* groups : {&plan.methods, &plan.operators})
            for (const auto& group : *groups) for (const auto& overload : group.overloads) report(overload, overload.function->qualified_name());
        for (const auto& overload : plan.constructors) report(overload, owner + "::" + plan.record->name);
        for (const auto& field : plan.fields) {
            const auto problem = ht(field.field->type, false).why;
            if (!problem.empty()) diagnostics_.push_back(owner + "::" + field.field->name + ": " + problem);
        }
    }
    for (const auto& constant : constants) {
        const auto problem = ht(*constant.type, false).why;
        if (!problem.empty()) diagnostics_.push_back(normalize(constant.expression) + ": " + problem);
    }
    check_diagnostics();
    for (const auto& group : functions) {
        if (group.module.empty() || submodule_prefixes_.count(group.module)) continue;
        auto prefix = camel(replace_all(group.module, ".", "_"));
        while (!prefix.empty() && prefix.back() == '\'') prefix.pop_back();
        submodule_prefixes_[group.module] = prefix;
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    emit_cpp(result.source, result.parts, functions, constants, "Haskell module " + module_name_ + " (" + name_ + ").");
    // Generic numbers exist once the C++ library is planned.
    name_everything(functions, constants);

    // ------------------------------------------------------------ the Haskell module
    heading("* Errors");
    for (const auto* item : {"R.CppError(..)", "R.ErrorKind(..)"}) export_item(item);
    heading("* Objects");
    for (const auto* item : {"R.CppObject", "R.closeObject", "R.isClosed", "R.downcast", "R.sameObject"}) export_item(item);
    heading("* Enumerations");
    for (const auto* item : {"R.CppEnum(..)", "R.enumFromValue", "R.enumFromName"}) export_item(item);
    for (const auto& [qualified, name] : enum_names_) emit_enum(qualified);
    for (const auto& owner : class_order_) emit_class(owner);
    if (!functions.empty() || !constants.empty()) {
        heading("* Functions and constants");
        body_ << "\n-- ---------------------------------------------------------------- functions\n";
    }
    std::string previous_module;
    bool constants_done = false;
    const auto emit_constants = [&] {
        if (constants_done) return;
        constants_done = true;
        for (const auto& constant : constants) emit_constant(constant, constant_names_.at(&constant));
    };
    for (const auto& group : functions) {
        if (group.module != previous_module) {
            emit_constants();
            previous_module = group.module;
            std::string from;
            for (const auto& [ns, mapped] : policy_.modules) if (mapped == group.module) from = ns;
            heading("** " + group.module + ": the C++ namespace " + escape(from));
        }
        for (const auto& variant : variants_.at(&group)) emit_function({&variant, group.index, "", -1, "", false});
    }
    emit_constants();

    std::ostringstream hs;
    hs << "-- Code generated by Rosetta from " << module_.header.substr(module_.header.find_last_of("/\\") + 1) << "; DO NOT EDIT.\n"
       << "{-# LANGUAGE NoImplicitPrelude #-}\n{-# OPTIONS_GHC -Wno-unused-imports -Wno-name-shadowing #-}\n" << file_doc()
       << "module " << module_name_ << "\n  (\n";
    bool first = true;
    for (const auto& item : exports_) {
        if (starts(item, "-- ")) { hs << "    " << item << "\n"; continue; }
        hs << (first ? "    " : "  , ") << item << "\n";
        first = false;
    }
    hs << "  ) where\n\n"
       << "import qualified Prelude as P\nimport qualified Data.ByteString as B\nimport qualified Data.Complex as C\n"
       << "import qualified Data.IORef as IORef\nimport qualified Data.Map.Strict as M\nimport qualified Data.Set as S\n"
       << "import qualified Data.Word as Word\nimport qualified " << module_name_ << ".Rosetta as R\n" << body_.str();
    result.haskell = hs.str();
    // The runtime module.
    std::string runtime;
    for (std::size_t i = 0; i < detail::haskell_full_runtime_count; ++i) runtime += detail::haskell_full_runtime_parts[i];
    std::vector<std::string> table;
    for (const auto& owner : class_order_) {
        std::vector<std::string> bases;
        for (const auto& base : classes_.at(owner).bases)
            bases.push_back(std::to_string(std::find(class_order_.begin(), class_order_.end(), base) - class_order_.begin()));
        table.push_back("(" + hs_string(classes_.at(owner).name) + ", [" + join(bases, ", ") + "])");
    }
    runtime = replace_all(runtime, "@CLASSES@", "[" + join(table, ", ") + "]");
    runtime = replace_all(runtime, "@C@", "rosetta_" + name_);
    runtime = replace_all(runtime, "@M@", module_name_);
    result.runtime = runtime;
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

std::string haskell_module_name(const std::string& name) { return pascal(name); }

GeneratedHaskellSource generate_haskell(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                        const HaskellOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Haskell C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
