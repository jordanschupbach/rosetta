// Dart backend (--language dart): a Dart package over the shared C ABI library of the full C ABI
// backends (cabi_full.hpp, lisp_full_runtime.hpp), loaded through dart:ffi.
//
// Classes are Dart classes over NativeObject; the first C++ base class is the superclass, and the
// members of further bases are declared again (C++ upcasts the object), with an asBase getter for
// passing the object where a further base is expected. A Dart subclass overrides C++ virtual
// methods: constructing it creates a C++ subclass whose virtual methods call the Dart methods
// (super runs the C++ implementation). Dart has no overloading: the overloads of a C++ function
// (and its default arguments) are one Dart function whose parameters keep the Dart type all
// overloads agree on (else Object?), with trailing optional positional parameters; C++ resolves
// the call again at run time. A method merges with the base class's method of the same name, so
// overrides stay valid Dart. Calls throw NativeError; enums are extension types over int,
// containers List, Set, Map and records, std::function Dart function types, non-const reference
// parameters Ref, operators Dart operators, operator[] also [] and operator() call. Doxygen
// becomes dartdoc comments.
//
// The output is a package: pubspec.yaml, lib/PACKAGE.dart (the API) and lib/src/rosetta.dart (the
// runtime, a part of the API library).
#include "rosetta/dart.hpp"
#include "cabi_full.hpp"
#include "dart_full_runtime.hpp"
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

// Dart reserved words, and built-in identifiers that do not work as names here (get and set do).
bool dart_keyword(const std::string& name) {
    static const std::set<std::string> words = {"assert", "break", "case", "catch", "class", "const", "continue", "default", "do", "else",
        "enum", "extends", "false", "final", "finally", "for", "if", "in", "is", "new", "null", "rethrow", "return", "super", "switch",
        "this", "throw", "true", "try", "var", "void", "while", "with", "abstract", "as", "covariant", "deferred", "dynamic", "export",
        "extension", "external", "factory", "Function", "implements", "import", "interface", "late", "library", "mixin", "operator",
        "part", "required", "static", "typedef", "await", "yield"};
    return words.count(name) != 0;
}
// Names members, functions and parameters must not take: the core types the generated code
// spells (a member named int would hide the type in its class), Object's members and the
// import prefixes.
bool dart_reserved_value(const std::string& name) {
    static const std::set<std::string> words = {"int", "double", "bool", "num", "hashCode", "runtimeType", "noSuchMethod", "toString", "ffi",
                                                "io", "utf8", "print", "identical", "identityHashCode"};
    return words.count(name) != 0;
}
// Type names that would collide with dart:core (which user code imports too), the typed data
// types the API uses, and the runtime's public types.
bool dart_reserved_type(const std::string& name) {
    static const std::set<std::string> words = {"Object", "Null", "Never", "String", "List", "Map", "Set", "Iterable", "Iterator", "Function",
        "Type", "Symbol", "Record", "Enum", "Error", "Exception", "Duration", "DateTime", "BigInt", "Uri", "RegExp", "RegExpMatch", "Pattern",
        "Match", "Comparable", "Comparator", "Future", "Stream", "StackTrace", "Invocation", "Expando", "WeakReference", "Finalizer", "Sink",
        "StringBuffer", "StringSink", "MapEntry", "Runes", "RuneIterator", "Stopwatch", "Deprecated", "ArgumentError", "RangeError",
        "StateError", "TypeError", "UnsupportedError", "UnimplementedError", "FormatException", "AssertionError", "NoSuchMethodError",
        "ConcurrentModificationError", "OutOfMemoryError", "StackOverflowError", "IndexError", "BidirectionalIterator", "Sendable",
        "Uint8List", "ByteData", "Endian", "NativeObject", "NativeError", "NativeErrorKind", "NativeFunction", "NativeLibrary", "Complex",
        "Ref", "Pointer", "Float", "Double", "Int"};
    return words.count(name) != 0;
}

// snake_case words of a C++ name (camelCase, PascalCase, URLPath and snake_case alike).
std::string snake_words(const std::string& name) {
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!is_alnum(c)) {
            if (!result.empty() && result.back() != '_') result += '_';
            continue;
        }
        if (is_upper(c) && !result.empty() && result.back() != '_') {
            const char previous = name[i - 1];
            const bool next_lower = i + 1 < name.size() && is_lower(name[i + 1]);
            if (is_lower(previous) || is_digit(previous) || (is_upper(previous) && next_lower)) result += '_';
        }
        result += lower(c);
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    while (!result.empty() && result.front() == '_') result.erase(result.begin());
    if (result.empty()) return "x";
    if (is_digit(result[0])) result = "x" + result;
    return result;
}
std::string camel(const std::string& name) {
    const auto words = snake_words(name);
    std::string result;
    bool start = false;
    for (const char c : words) {
        if (c == '_') { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    return result;
}
std::string pascal(const std::string& name) {
    auto result = camel(name);
    result[0] = upper(result[0]);
    return result;
}
std::string dart_string(const std::string& text) {
    std::string result = "'";
    for (const char c : text) {
        if (c == '\\' || c == '\'' || c == '$') result += '\\';
        if (c == '\n') { result += "\\n"; continue; }
        result += c;
    }
    return result + "'";
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
bool starts(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }
bool ends(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}
std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) result += (i ? separator : "") + items[i];
    return result;
}

// Names in one Dart scope.
struct Names {
    std::set<std::string> taken;
    bool reserve(const std::string& name) { return taken.insert(name).second; }
    bool free(const std::string& name) const { return !taken.count(name); }
    // A member, function or parameter name: with an underscore if Dart does not allow it, else
    // with a number (getName2).
    std::string claim(std::string wanted) {
        if (dart_keyword(wanted) || dart_reserved_value(wanted)) wanted += "_";
        if (reserve(wanted)) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
    }
    // A type name: the wanted name, else with a trailing underscore (Point_), else a number.
    std::string claim_type(std::string wanted) {
        if (dart_keyword(wanted) || dart_reserved_type(wanted)) wanted += "_";
        if (reserve(wanted)) return wanted;
        return claim(wanted + "_");
    }
};

// ---------------------------------------------------------------- doc comments
struct Doc {
    std::vector<std::string> lines;
    void blank() { if (!lines.empty() && !lines.back().empty()) lines.push_back(""); }
    void paragraph(const std::string& text) {
        blank();
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const auto stripped = trim(line);
            if (stripped.empty()) blank();
            else if (starts(stripped, "* ")) lines.push_back("- " + trim(stripped.substr(2)));
            else lines.push_back(stripped);
        }
    }
    void heading(const std::string& text) {
        blank();
        lines.push_back("## " + one_line(trim(text)));
        lines.push_back("");
    }
    void code(const std::string& text) {
        blank();
        lines.push_back("```");
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        lines.insert(lines.end(), body.begin(), body.end());
        lines.push_back("```");
        lines.push_back("");
    }
    void list(const std::string& title, const std::vector<std::string>& items) {
        if (items.empty()) return;
        blank();
        if (!title.empty()) lines.push_back(title);
        if (!title.empty()) lines.push_back("");
        for (const auto& item : items) lines.push_back("- " + item);
        lines.push_back("");
    }
    std::string render(const std::string& indent = {}) const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        std::string out;
        bool previous_blank = true;
        bool fenced = false;
        for (const auto& line : copy) {
            if (line.empty() && previous_blank && !fenced) continue;
            if (starts(line, "```")) fenced = !fenced;
            previous_blank = line.empty();
            out += indent + "///" + (line.empty() ? "" : " " + line) + "\n";
        }
        return out;
    }
};

// C++ documentation text as Markdown dartdoc reads literally: brackets are no references, < no
// HTML tag, and a line starting with > or # no quote or heading; URLs become autolinks.
std::string md(const std::string& text) {
    std::string out;
    bool line_start = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (text.compare(i, 7, "http://") == 0 || text.compare(i, 8, "https://") == 0) {
            std::size_t end = i;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != ')' && text[end] != '>') ++end;
            while (end > i && (text[end - 1] == '.' || text[end - 1] == ',')) --end;
            out += "<" + text.substr(i, end - i) + ">";
            i = end - 1;
            line_start = false;
            continue;
        }
        if (c == '[' || c == ']' || c == '<' || ((c == '>' || c == '#') && line_start)) out += '\\';
        out += c;
        if (c == '\n') line_start = true;
        else if (c != ' ' && c != '\t') line_start = false;
    }
    return out;
}

std::string error_kind(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "invalidArgument";
    if (bare == "std::out_of_range") return "outOfRange";
    if (bare == "std::domain_error") return "domainError";
    if (bare == "std::length_error") return "lengthError";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "arithmeticError";
    if (bare == "std::bad_alloc") return "allocation";
    return "native";
}

// C++ operators: their group names ("" = not bound) and Dart operators ("" = a named method).
struct OperatorName { const char* unary; const char* binary; const char* unary_symbol; const char* binary_symbol; };
const std::map<std::string, OperatorName>& operator_table() {
    static const std::map<std::string, OperatorName> table = {
        {"operator+", {"pos", "add", "", "+"}}, {"operator-", {"neg", "subtract", "-", "-"}}, {"operator*", {nullptr, "multiply", "", "*"}},
        {"operator/", {nullptr, "divide", "", "/"}}, {"operator%", {nullptr, "modulo", "", "%"}}, {"operator==", {nullptr, "equals", "", ""}},
        {"operator!=", {nullptr, "notEquals", "", ""}}, {"operator<", {nullptr, "less", "", "<"}}, {"operator<=", {nullptr, "lessEqual", "", "<="}},
        {"operator>", {nullptr, "greater", "", ">"}}, {"operator>=", {nullptr, "greaterEqual", "", ">="}}, {"operator&", {nullptr, "bitAnd", "", "&"}},
        {"operator|", {nullptr, "bitOr", "", "|"}}, {"operator^", {nullptr, "bitXor", "", "^"}}, {"operator~", {"complement", nullptr, "~", ""}},
        {"operator!", {"not", nullptr, "", ""}}, {"operator<<", {nullptr, "shiftLeft", "", "<<"}}, {"operator>>", {nullptr, "shiftRight", "", ">>"}},
        {"operator[]", {nullptr, "at", "", "[]"}}, {"operator+=", {nullptr, "addAssign", "", ""}}, {"operator-=", {nullptr, "subtractAssign", "", ""}},
        {"operator*=", {nullptr, "multiplyAssign", "", ""}}, {"operator/=", {nullptr, "divideAssign", "", ""}}};
    return table;
}
std::string dart_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "call";
    const auto found = operator_table().find(name);
    if (found == operator_table().end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.unary : found->second.binary;
    return result ? result : "";
}
// The Dart operator symbol of a C++ operator with `arity` operands besides the object.
std::string dart_symbol(const std::string& cpp, std::size_t arity) {
    const auto found = operator_table().find(cpp);
    if (found == operator_table().end()) return {};
    return arity == 0 ? found->second.unary_symbol : found->second.binary_symbol;
}

// A C++ type in Dart: its spelling, the runtime codec converting it, and the C++ class it is.
struct DT {
    std::string type = "Object?";
    std::string codec = "_Codecs.anything";
    std::string cls;          // a class type (or a nullable one): its qualified C++ name
    bool nullable = true;
    bool function = false;
};
DT scalar(const std::string& type, const std::string& codec) { return {type, codec, "", false, false}; }

// One overload a Dart function calls: the C++ function and its native function number.
struct Entry {
    const Overload* overload;
    const std::size_t* at;   // the group's native function number (assigned when the C++ is emitted)
    std::size_t index() const { return *at; }
};
struct Param {
    DT type;
    std::string name;
    bool ref = false;        // a Ref<T> (C++ writes it back)
    bool any = false;        // overloads disagree: Object?
    bool any_ref = false;    // an Object? some overload writes back
};
// The Dart signature of overloads: parameters (optional from `min` on), result, and the native
// function for each argument count.
struct Sig {
    std::vector<Param> params;
    std::size_t min = 0;
    DT result;
    bool returns = false;
    std::vector<std::size_t> indexes;   // per argument count, min..params.size()
};

// A member of a Dart class (inherited ones included), for overriding, merging and copying.
struct Member {
    char kind = 'm';                    // m: method, s: static method, f: field, o: operator
    std::vector<Entry> entries;
    DT result;                          // a method's result, a field's type
    std::string from;                   // the class declaring it
    const FieldPlan* field = nullptr;
    bool is_virtual = false;
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const DartOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedDartSource run();

private:
    DartOptions options_;
    std::string package_, library_;
    Names types_;                                            // the library's top-level names
    std::map<std::string, Names> members_;                   // class -> member names (inherited ones included)
    std::map<std::string, std::map<std::string, std::string>> mapped_;   // class -> C++ member key -> Dart name
    std::map<const Group*, std::string> names_;
    std::map<const Group*, std::vector<Entry>> merged_;      // a method's overloads with the base method's
    std::map<std::string, std::string> field_names_;
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::map<std::string, std::string> submodules_;          // policy module -> Dart class
    std::map<std::string, std::string> links_;
    std::map<std::string, std::string> secondary_names_;
    std::map<std::string, std::vector<Overload>> constructor_overloads_;
    std::map<std::string, std::map<std::string, Member>> table_;   // class -> Dart member -> what it is
    std::map<std::string, std::vector<std::string>> order_;        // class -> its own and copied members, in order
    std::map<std::string, Names> modules_;                         // policy module -> function names
    mutable std::map<std::string, std::string> codecs_;      // codec expression -> cached name
    mutable std::vector<std::string> codec_order_;

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Dart"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return dart_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        Names taken;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            names.push_back(taken.claim(cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : camel(cpp)));
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + overload_signature(overload, group.name) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, plan.name) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: subclass it in Dart and override its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: subclass it in Dart";
    }
    std::string symbol_prefix() const override { return "rosetta_dart_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_DART"; }
    std::string module_prefix() const override { return "rosetta_dart_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    bool derives(const std::string& from, const std::string& to) const {
        if (from == to) return true;
        const auto found = classes_.find(from);
        if (found == classes_.end()) return false;
        for (const auto& base : found->second.bases) if (derives(base, to)) return true;
        return false;
    }
    DT class_type(const std::string& qualified) const {
        const auto& name = classes_.at(qualified).name;
        return {name, "_Codecs.object<" + name + ">()", qualified, false, false};
    }
    static DT optional(const DT& inner) {
        if (inner.nullable) return inner;
        DT result = inner;
        result.type = inner.type + "?"; // Dart: `int Function(int)?` (a parenthesized type would be a record)
        result.codec = "_Codecs.optional(" + inner.codec + ")";
        result.nullable = true;
        return result;
    }
    DT dart_type(const Type& type) const;
    DT dart_spelled(std::string cpp) const;
    std::string dart_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    Sig make_sig(const std::vector<Entry>& entries) const;
    DT union_result(const std::vector<DT>& types) const;
    bool compatible(const DT& result, const DT& inherited) const;
    std::string codec(const std::string& expression) const;

    // ------------------------------------------------------------ naming and emission
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    std::string link(const std::string& cpp) const;
    Doc doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Doc& comment, const Documentation& doc) const;
    Doc entries_doc(const std::vector<Entry>& entries, const Sig& sig, const std::string& what) const;
    std::string parameter_list(const Sig& sig) const;
    void emit_arguments(std::ostream& out, const std::string& indent, const Sig& sig, std::vector<std::string>& after) const;
    void emit_body(std::ostream& out, const std::string& indent, const Sig& sig, const std::string& receiver) const;
    void emit_function(std::ostream& out, const Group& group, const std::string& indent, const std::string& modifiers) const;
    void emit_method(std::ostream& out, const std::string& owner, const std::string& name, const Member& member, bool overrides);
    void emit_field(std::ostream& out, const std::string& name, const Member& member, bool overrides) const;
    void emit_class(std::ostream& out, const std::string& owner);
    void emit_enum(std::ostream& out, const std::string& qualified) const;
    std::string dispatch();
};

// ---------------------------------------------------------------- types
std::string Generator::codec(const std::string& expression) const {
    if (expression == "_Codecs.anything" || expression == "_Codecs.void_") return expression;
    const auto found = codecs_.find(expression);
    if (found != codecs_.end()) return found->second;
    const auto name = "_c" + std::to_string(codecs_.size());
    codecs_[expression] = name;
    codec_order_.push_back(expression);
    return name;
}

DT Generator::dart_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return scalar("String", "_Codecs.string");
        if (is_void(pointee)) return {};
        return optional(dart_type(pointee));
    }
    if (is_void(target)) return {"void", "_Codecs.void_", "", false, false};
    if (!conversion(target).empty()) return dart_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        const auto spelled = normalize(strip_cv(target.spelling));
        if (spelled == "size_t" || spelled == "std::size_t" || spelled == "ssize_t" || spelled == "ptrdiff_t" || spelled == "std::ptrdiff_t")
            return scalar("int", "_Codecs.int64");
        static const std::map<std::string, std::string> fixed = {{"int8_t", "int8"}, {"int16_t", "int16"}, {"int32_t", "int32"},
            {"int64_t", "int64"}, {"uint8_t", "uint8"}, {"uint16_t", "uint16"}, {"uint32_t", "uint32"}, {"uint64_t", "uint64"}};
        const auto width = fixed.find(starts(spelled, "std::") ? spelled.substr(5) : spelled);
        if (width != fixed.end()) return scalar("int", "_Codecs." + width->second);
        static const std::map<std::string, std::pair<std::string, std::string>> builtins = {{"bool", {"bool", "boolean"}},
            {"char", {"String", "character"}}, {"signed char", {"int", "int8"}}, {"unsigned char", {"int", "uint8"}},
            {"short", {"int", "int16"}}, {"unsigned short", {"int", "uint16"}}, {"int", {"int", "int32"}}, {"unsigned int", {"int", "uint32"}},
            {"long", {"int", "int64"}}, {"unsigned long", {"int", "uint64"}}, {"long long", {"int", "int64"}},
            {"unsigned long long", {"int", "uint64"}}, {"float", {"double", "real"}}, {"double", {"double", "real"}},
            {"long double", {"double", "real"}}, {"char16_t", {"int", "uint16"}}, {"char32_t", {"int", "uint32"}}, {"wchar_t", {"int", "int32"}}};
        const auto found = builtins.find(bare);
        if (found == builtins.end()) return {};
        return scalar(found->second.first, "_Codecs." + found->second.second);
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return scalar("int", "_Codecs.int64");
        return scalar(found->second, "_Codecs.enumeration<" + found->second + ">(" + found->second + ".new, (e) => e.value)");
    }
    if (bound(target)) return class_type(target.declaration);
    if (bare == "std::filesystem::path") return scalar("String", "_Codecs.string");
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {"void", "_Codecs.void_", "", false, false};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? dart_type(target.arguments[i]) : DT{}; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return scalar("String", "_Codecs.string");
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span") {
            const auto item = argument(0);
            if (item.codec == "_Codecs.uint8") return scalar("Uint8List", "_Codecs.bytes");
            return scalar("List<" + item.type + ">", "_Codecs.list(" + item.codec + ")");
        }
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset" || declaration == "std::unordered_multiset") {
            const auto item = argument(0);
            if (declaration.find("multi") == std::string::npos) return scalar("Set<" + item.type + ">", "_Codecs.set(" + item.codec + ")");
            return scalar("List<" + item.type + ">", "_Codecs.list(" + item.codec + ")");
        }
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" || declaration == "std::unordered_multimap") {
            const auto key = argument(0), item = argument(1);
            if (declaration.find("multi") == std::string::npos && key.cls.empty())
                return scalar("Map<" + key.type + ", " + item.type + ">", "_Codecs.map(" + key.codec + ", " + item.codec + ")");
            return scalar("List<(" + key.type + ", " + item.type + ")>", "_Codecs.pairs(" + key.codec + ", " + item.codec + ")");
        }
        if (declaration == "std::optional") return target.arguments.empty() ? DT{} : optional(argument(0));
        if (declaration == "std::complex") return scalar("Complex", "_Codecs.complex");
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return target.arguments.empty() ? DT{} : argument(0);
        if ((declaration == "std::pair" || declaration == "std::tuple") && target.arguments.size() >= 2 && target.arguments.size() <= 6) {
            std::vector<std::string> types, codecs;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) {
                const auto item = argument(i);
                types.push_back(item.type);
                codecs.push_back(item.codec);
            }
            return scalar("(" + join(types, ", ") + ")", "_Codecs.tuple" + std::to_string(types.size()) + "(" + join(codecs, ", ") + ")");
        }
        if (declaration == "std::tuple" && target.arguments.empty()) return {"void", "_Codecs.void_", "", false, false};
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            if (function.arguments.size() > 5) return {};
            std::vector<std::string> types, codecs;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) {
                const auto item = dart_type(function.arguments[i]);
                types.push_back(item.type);
                codecs.push_back(item.codec);
            }
            const auto result = function.arguments.empty() ? DT{"void", "_Codecs.void_", "", false, false} : dart_type(function.arguments[0]);
            codecs.push_back(result.codec);
            DT closure = scalar(result.type + " Function(" + join(types, ", ") + ")",
                                "_Codecs.function" + std::to_string(types.size()) + "(" + join(codecs, ", ") + ")");
            closure.function = true;
            return closure;
        }
        if (declaration == "std::chrono::duration") return scalar("double", "_Codecs.real");
    }
    return {};
}

DT Generator::dart_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::map<std::string, std::pair<std::string, std::string>> scalars = {{"bool", {"bool", "boolean"}},
        {"char", {"String", "character"}}, {"signed char", {"int", "int8"}}, {"unsigned char", {"int", "uint8"}}, {"short", {"int", "int16"}},
        {"unsigned short", {"int", "uint16"}}, {"int", {"int", "int32"}}, {"unsigned int", {"int", "uint32"}}, {"long", {"int", "int64"}},
        {"unsigned long", {"int", "uint64"}}, {"long long", {"int", "int64"}}, {"unsigned long long", {"int", "uint64"}}, {"float", {"double", "real"}},
        {"double", {"double", "real"}}, {"long double", {"double", "real"}}, {"std::size_t", {"int", "int64"}}, {"size_t", {"int", "int64"}},
        {"std::int64_t", {"int", "int64"}}, {"std::uint64_t", {"int", "uint64"}}, {"std::int32_t", {"int", "int32"}}, {"std::uint32_t", {"int", "uint32"}},
        {"std::int16_t", {"int", "int16"}}, {"std::uint16_t", {"int", "uint16"}}, {"std::int8_t", {"int", "int8"}}, {"std::uint8_t", {"int", "uint8"}}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return scalar(found->second.first, "_Codecs." + found->second.second);
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return scalar("String", "_Codecs.string");
    for (const auto& [qualified, plan] : classes_)
        if (normalize(qualified) == cpp) return class_type(qualified);
    for (const auto& [qualified, name] : enum_names_)
        if (normalize(qualified) == cpp) return scalar(name, "_Codecs.enumeration<" + name + ">(" + name + ".new, (e) => e.value)");
    return {};
}

std::string Generator::dart_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "null";
    if (value.kind == "char") return value.value.size() == 1 ? dart_string(value.value) : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos)
            return dart_string(text.substr(1, text.size() - 2));
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        const auto found = enum_names_.find(qualified);
        if (found == enum_names_.end() || !enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return found->second + "." + enum_cases_.at(qualified)[i];
    }
    return {};
}

std::string Generator::overload_signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    const std::size_t defaults_from = function.parameters.size() - std::min(function.default_arguments, function.parameters.size() - first);
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& type = function.parameters[i];
        const auto dt = dart_type(type);
        auto text = (writeback(type) && !function.is_constructor ? "Ref<" + dt.type + ">" : dt.type) + " " + names[i - first];
        if (i >= defaults_from) {
            const auto shown = dart_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, type);
            text += " = " + (shown.empty() ? std::string("<C++ default>") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + "(" + join(parameters, ", ") + ")";
    if (!function.is_constructor && !is_void(function.result)) result += " → " + dart_type(function.result).type;
    return result;
}

// The result type of overloads: the type they agree on, a common base class, else Object?.
DT Generator::union_result(const std::vector<DT>& types) const {
    if (types.empty()) return {"void", "_Codecs.void_", "", false, false};
    bool same = true;
    for (const auto& type : types) same = same && type.type == types.front().type;
    if (same) {
        // Integers of different C++ widths: the widest.
        if (types.front().type == "int") {
            for (const auto& type : types)
                if (type.codec != types.front().codec) return scalar("int", "_Codecs.int64");
        }
        return types.front();
    }
    bool classes = true, nullable = false;
    for (const auto& type : types) {
        classes = classes && !type.cls.empty();
        nullable = nullable || type.nullable;
    }
    if (classes) {
        // The first class along the first type's bases that every type derives from.
        std::vector<std::string> queue = {types.front().cls};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            bool all = true;
            for (const auto& type : types) all = all && derives(type.cls, queue[i]);
            if (all) {
                auto result = class_type(queue[i]);
                return nullable ? optional(result) : result;
            }
            for (const auto& base : classes_.at(queue[i]).bases) queue.push_back(base);
        }
    }
    return {};
}

// Whether a method result type may override one returning `inherited`.
bool Generator::compatible(const DT& result, const DT& inherited) const {
    if (inherited.type == "void" || inherited.type == "Object?" || result.type == inherited.type) return true;
    if (inherited.nullable && ends(inherited.type, "?") && result.type + "?" == inherited.type) return true;
    if (!result.cls.empty() && !inherited.cls.empty() && derives(result.cls, inherited.cls) && (inherited.nullable || !result.nullable)) return true;
    return false;
}

Sig Generator::make_sig(const std::vector<Entry>& entries) const {
    Sig sig;
    struct Shape { std::size_t required, total, first; };
    std::vector<Shape> shapes;
    std::size_t max = 0;
    sig.min = static_cast<std::size_t>(-1);
    for (const auto& entry : entries) {
        const auto& function = *entry.overload->function;
        const std::size_t first = entry.overload->receives_self ? 1 : 0;
        const std::size_t total = function.parameters.size() - first;
        const std::size_t required = total - std::min(function.default_arguments, total);
        shapes.push_back({required, total, first});
        max = std::max(max, total);
        sig.min = std::min(sig.min, required);
    }
    if (entries.empty()) sig.min = 0;
    Names taken;
    for (const char* local : {"$c", "$n", "$r"}) taken.reserve(local);
    for (std::size_t p = 0; p < max; ++p) {
        Param param;
        bool first_seen = true;
        std::string spelled;
        for (std::size_t e = 0; e < entries.size(); ++e) {
            if (shapes[e].total <= p) continue;
            const auto& function = *entries[e].overload->function;
            const auto& type = function.parameters[shapes[e].first + p];
            const auto dt = dart_type(type);
            const bool ref = writeback(type) && !function.is_constructor;
            const auto text = ref ? "Ref<" + dt.type + ">" : dt.type;
            if (first_seen) {
                param.type = dt;
                param.ref = ref;
                param.name = parameter_names(*entries[e].overload)[p];
                spelled = text;
                first_seen = false;
            } else if (text != spelled) {
                param.any = true;
            }
            if (ref) param.any_ref = true;
        }
        if (param.any) {
            param.type = DT{};
            param.ref = false;
        } else {
            param.any_ref = false;
        }
        param.name = taken.claim(param.name);
        sig.params.push_back(param);
    }
    for (std::size_t n = sig.min; n <= max; ++n) {
        std::size_t index = entries.empty() ? 0 : entries.front().index();
        for (std::size_t e = 0; e < entries.size(); ++e)
            if (shapes[e].required <= n && n <= shapes[e].total) { index = entries[e].index(); break; }
        sig.indexes.push_back(index);
    }
    std::vector<DT> results;
    bool constructor = false;
    for (const auto& entry : entries) {
        const auto& function = *entry.overload->function;
        if (function.is_constructor) { constructor = true; continue; }
        results.push_back(is_void(function.result) ? DT{"void", "_Codecs.void_", "", false, false} : dart_type(function.result));
    }
    sig.result = constructor ? DT{"void", "_Codecs.void_", "", false, false} : union_result(results);
    sig.returns = sig.result.type != "void";
    return sig;
}

// ---------------------------------------------------------------- naming
void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        auto& mapped = mapped_[owner];
        auto& table = table_[owner];
        auto& order = order_[owner];
        const auto add = [&](const std::string& name, const Member& member) {
            table[name] = member;
            if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
        };
        // A C++ name a base class binds keeps its Dart name (an override, a merged method, or a
        // further base's member declared again).
        for (const auto& base : cls.bases) {
            if (!members_.count(base)) continue;
            for (const auto& name : members_.at(base).taken) members.reserve(name);
            for (const auto& [key, name] : mapped_.at(base)) mapped.emplace(key, name);
        }
        if (!cls.bases.empty()) table = table_.at(cls.bases.front());
        for (const char* reserved : {"close", "isClosed", "cast", "toString", "hashCode", "runtimeType", "noSuchMethod", "int", "double", "bool",
                                     "num"})
            members.reserve(reserved);
        const bool constructible = !cls.record->abstract;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) constructor_overloads_[owner].push_back(overload);
        const auto name_member = [&](const std::string& key, const std::string& wanted) {
            const auto found = mapped.find(key);
            if (found != mapped.end()) return found->second;
            const auto name = members.claim(wanted);
            mapped[key] = name;
            return name;
        };
        // A base method's overloads, unless an own overload has their parameter types (an override).
        const auto merge = [&](std::vector<Entry>& entries, const std::vector<Entry>& inherited) {
            const auto key = [&](const Entry& entry) {
                const auto& function = *entry.overload->function;
                std::string text;
                for (std::size_t i = entry.overload->receives_self ? 1 : 0; i < function.parameters.size(); ++i)
                    text += dart_type(function.parameters[i]).type + (writeback(function.parameters[i]) ? "&" : "") + ",";
                return text + std::to_string(function.default_arguments);
            };
            std::set<std::string> own;
            for (const auto& entry : entries) own.insert(key(entry));
            for (const auto& entry : inherited)
                if (own.insert(key(entry)).second) entries.push_back(entry);
        };
        const auto fresh = [&](const std::string& key, const std::string& wanted) {
            const auto name = members.claim(wanted);
            mapped[key] = name;
            return name;
        };
        for (auto& group : cls.methods) {
            const auto key = "m:" + group.name;
            std::vector<Entry> own;
            for (const auto& overload : group.overloads) own.push_back({&overload, &group.index});
            // to_string() is Object.toString; one whose parameters all have defaults overrides it with
            // optional parameters (`toString([int? width])`).
            if (group.name == "to_string" && !group.is_static) {
                const auto sig = make_sig(own);
                if (sig.min == 0 && sig.result.type == "String") {
                    names_[&group] = "toString";
                    merged_[&group] = own;
                    Member member;
                    member.entries = own;
                    member.result = sig.result;
                    member.from = owner;
                    add("toString", member);
                    continue;
                }
            }
            auto name = name_member(key, camel(group.name));
            auto inherited = table.find(name);
            std::vector<Entry> entries = own;
            if (inherited != table.end()) {
                const auto& member = inherited->second;
                if (member.kind == 's' && group.is_static) {
                    // Static methods do not inherit: a static method of the same name is another one.
                } else if (member.kind != 'm' || group.is_static) {
                    name = fresh(key, camel(group.name));
                } else {
                    merge(entries, member.entries);
                    if (!compatible(make_sig(entries).result, member.result)) {
                        // Another result: Dart could not override it, so it takes its own name.
                        name = fresh(key, camel(group.name));
                        entries = own;
                    }
                }
            }
            names_[&group] = name;
            merged_[&group] = entries;
            Member member;
            member.kind = group.is_static ? 's' : 'm';
            member.entries = entries;
            member.result = make_sig(entries).result;
            member.from = owner;
            for (const auto& overload : group.overloads)
                member.is_virtual = member.is_virtual || (overload.function->is_virtual && !overload.extension && !group.is_static);
            const auto previous = table.find(name);
            if (previous != table.end() && previous->second.kind == 'm' && previous->second.is_virtual && !group.is_static) member.is_virtual = true;
            add(name, member);
        }
        for (auto& group : cls.operators) {
            const auto& cpp = group.overloads.front().function->name;
            std::vector<Entry> own;
            for (const auto& overload : group.overloads) own.push_back({&overload, &group.index});
            const auto sig = make_sig(own);
            const auto symbol = group.operand == 0 ? dart_symbol(cpp, sig.params.size()) : std::string();
            const bool fits = !symbol.empty() && sig.min == sig.params.size() && (sig.params.empty() || !sig.params[0].ref);
            if (fits) {
                const auto key = std::string("operator ") + (sig.params.empty() && symbol == "-" ? "unary-" : symbol);
                auto inherited = table.find(key);
                std::vector<Entry> entries = own;
                if (inherited != table.end()) {
                    merge(entries, inherited->second.entries);
                    const auto merged = make_sig(entries);
                    if (merged.min != merged.params.size() || merged.params.size() != sig.params.size() ||
                        !compatible(merged.result, inherited->second.result))
                        entries = own;
                }
                names_[&group] = key;
                merged_[&group] = entries;
                Member member;
                member.kind = 'o';
                member.entries = entries;
                member.result = make_sig(entries).result;
                member.from = owner;
                add(key, member);
                if (group.name != "at") continue;
            }
            if (group.operand != 0) continue;
            // A named method: operator[] (at, besides []), operator() (call) and operators Dart
            // has no symbol for.
            auto name = name_member("o:" + group.name, group.name);
            auto inherited = table.find(name);
            std::vector<Entry> entries = own;
            if (inherited != table.end()) {
                if (inherited->second.kind != 'm') name = fresh("o:" + group.name, group.name);
                else {
                    merge(entries, inherited->second.entries);
                    if (!compatible(make_sig(entries).result, inherited->second.result)) {
                        name = fresh("o:" + group.name, group.name);
                        entries = own;
                    }
                }
            }
            if (!fits) {
                names_[&group] = name;
                merged_[&group] = entries;
            }
            Member member;
            member.kind = 'm';
            member.entries = entries;
            member.result = make_sig(entries).result;
            member.from = owner;
            add(name, member);
        }
        for (const auto& field : cls.fields) {
            const auto name = name_member("f:" + field.field->name, camel(field.name));
            field_names_[owner + "::" + field.field->name] = name;
            if (table.count(name) && table[name].kind == 'f') continue;
            Member member;
            member.kind = 'f';
            member.field = &field;
            member.from = owner;
            member.result = dart_type(field.field->type);
            add(name, member);
        }
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(camel(constant.name));
        // Further bases are reached through asBase, and their members are declared again.
        for (std::size_t i = 1; i < cls.bases.size(); ++i) {
            secondary_names_[owner + "::" + cls.bases[i]] = name_member("a:" + cls.bases[i], "as" + classes_.at(cls.bases[i]).name);
            for (const auto& [name, member] : table_.at(cls.bases[i]))
                if (member.kind != 's' && member.kind != 'o' && !table.count(name)) add(name, member);
        }
    }
    for (auto& group : functions) {
        if (group.module.empty()) names_[&group] = types_.claim(camel(group.name));
        else {
            names_[&group] = modules_[group.module].claim(camel(group.name));
        }
        std::vector<Entry> entries;
        for (const auto& overload : group.overloads) entries.push_back({&overload, &group.index});
        merged_[&group] = entries;
    }
    for (auto& constant : constants) constant_names_[&constant] = types_.claim(camel(constant.name));
    // dartdoc references.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        links_[owner] = "[" + cls.name + "]";
        links_[cls.record->name] = links_[owner];
        for (const auto& group : cls.methods) {
            links_[owner + "::" + group.name] = "[" + cls.name + "." + names_.at(&group) + "]";
            links_[cls.record->name + "::" + group.name] = links_[owner + "::" + group.name];
        }
    }
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = "[" + names_.at(&group) + "]";
            links_.emplace(overload.function->name, "[" + names_.at(&group) + "]");
        }
    }
    for (const auto& [qualified, name] : enum_names_) {
        links_[qualified] = "[" + name + "]";
        links_.emplace(enums_.at(qualified)->name, links_[qualified]);
    }
}

// ---------------------------------------------------------------- documentation
std::string Generator::link(const std::string& cpp) const {
    const auto found = links_.find(normalize(cpp));
    return found != links_.end() ? found->second : "`" + cpp + "`";
}
void Generator::add_notes(Doc& comment, const Documentation& doc) const {
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { comment.paragraph("**Deprecated:** " + md(one_line(item.text))); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        comment.paragraph("**" + label + ":** " + md(one_line(item.text)));
    }
    if (!doc.see.empty()) {
        std::string see = "See also ";
        for (std::size_t i = 0; i < doc.see.size(); ++i)
            see += (i == 0 ? "" : i + 1 == doc.see.size() ? " and " : ", ") + link(doc.see[i]);
        comment.paragraph(see + ".");
    }
}
Doc Generator::doc_of(const Documentation& doc, const std::string& lead) const {
    Doc comment;
    auto details = doc.details;
    auto brief = doc.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    comment.paragraph(brief.empty() ? lead : md(brief));
    if (!brief.empty() && !lead.empty()) comment.paragraph(lead);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") comment.code(block.text);
        else if (block.kind == "heading") comment.heading(md(block.text));
        else comment.paragraph(md(block.text));
    }
    return comment;
}

Doc Generator::entries_doc(const std::vector<Entry>& entries, const Sig& sig, const std::string& what) const {
    // The documentation of the first documented overload.
    const Function* documented = entries.empty() ? nullptr : entries.front().overload->function;
    for (const auto& entry : entries) {
        const auto& doc = entry.overload->function->documentation;
        if (!doc.brief.empty() || !doc.details.empty()) { documented = entry.overload->function; break; }
    }
    const auto& doc = documented ? documented->documentation : Documentation{};
    auto comment = doc_of(doc, "Calls " + what + ".");
    if (entries.size() > 1) {
        std::vector<std::string> overloads;
        for (const auto& entry : entries) overloads.push_back("`" + overload_signature(*entry.overload, entry.overload->function->name) + "`");
        comment.list("C++ overloads (C++ chooses among them at run time):", overloads);
    }
    // Optional parameters take the C++ defaults.
    std::vector<std::string> defaults;
    for (std::size_t p = sig.min; p < sig.params.size(); ++p) {
        std::string shown;
        for (const auto& entry : entries) {
            const auto& function = *entry.overload->function;
            const std::size_t i = p + (entry.overload->receives_self ? 1 : 0);
            if (i >= function.parameters.size()) continue;
            const std::size_t defaults_from = function.parameters.size() - function.default_arguments;
            if (i < defaults_from) continue;
            shown = dart_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            if (shown.empty()) shown = i < function.default_values.size() && !function.default_values[i].value.empty() ? function.default_values[i].value : "C++ default";
            break;
        }
        defaults.push_back("`" + sig.params[p].name + (shown.empty() ? "" : " = " + shown) + "`");
    }
    if (!defaults.empty())
        comment.paragraph("Optional arguments left out (or `null`) are left out of the C++ call, which applies its defaults: " + join(defaults, ", ") + ".");
    std::vector<std::string> parameters;
    for (const auto& param : sig.params) {
        std::string text;
        if (documented) {
            for (const auto& entry : entries) {
                if (entry.overload->function != documented) continue;
                const auto names = parameter_names(*entry.overload);
                const std::size_t first = entry.overload->receives_self ? 1 : 0;
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (names[i] != param.name) continue;
                    const auto cpp = i + first < documented->parameter_names.size() ? documented->parameter_names[i + first] : std::string();
                    for (const auto& item : doc.params) if (item.name == cpp && !item.text.empty()) text = md(one_line(item.text));
                }
            }
        }
        if (param.ref) text += std::string(text.empty() ? "" : " ") + "(C++ writes it back)";
        if (!text.empty()) parameters.push_back("[" + param.name + "]: " + text);
    }
    comment.list("Parameters:", parameters);
    if (sig.returns && !doc.returns.empty()) comment.paragraph("Returns " + md(one_line(doc.returns)));
    std::vector<std::string> errors;
    for (const auto& item : doc.throws)
        errors.push_back("[NativeError] of kind [NativeErrorKind." + error_kind(item.name) + "]" + (item.name.empty() ? std::string() : " (C++ `" + item.name + "`)") +
                         (item.text.empty() ? std::string() : ": " + md(one_line(item.text))));
    if (!errors.empty()) comment.paragraph("Throws " + join(errors, "; ") + ".");
    add_notes(comment, doc);
    return comment;
}

// ---------------------------------------------------------------- emission
std::string Generator::parameter_list(const Sig& sig) const {
    std::vector<std::string> required, optional_items;
    for (std::size_t i = 0; i < sig.params.size(); ++i) {
        const auto& param = sig.params[i];
        std::string type = param.ref ? "Ref<" + param.type.type + ">" : param.type.type;
        if (i >= sig.min) {
            if (param.ref || !param.type.nullable) type += "?";
            optional_items.push_back(type + " " + param.name);
        } else {
            required.push_back(type + " " + param.name);
        }
    }
    auto text = join(required, ", ");
    if (!optional_items.empty()) text += (text.empty() ? "" : ", ") + std::string("[") + join(optional_items, ", ") + "]";
    return text;
}

// The argument lines of a call ($c and, with optional parameters, $n exist); `after` gets the
// write-back lines.
void Generator::emit_arguments(std::ostream& out, const std::string& indent, const Sig& sig, std::vector<std::string>& after) const {
    for (std::size_t i = 0; i < sig.params.size(); ++i) {
        const auto& param = sig.params[i];
        const bool optional_param = i >= sig.min;
        const auto& name = param.name;
        const auto slot = "$s" + std::to_string(i);
        std::string value = name;
        if (optional_param && (param.ref || !param.type.nullable)) value = "_Rosetta.given(" + name + ", " + dart_string(name) + ")";
        std::string line;
        if (param.any) {
            if (param.any_ref) {
                out << indent << "int? " << slot << ";\n";
                line = slot + " = $c.any(" + name + ");";
                after.push_back("if (" + slot + " != null) $r.writeBackAny(" + slot + ", " + name + ");");
            } else {
                line = "$c.any(" + name + ");";
            }
        } else if (param.ref) {
            const auto c = codec(param.type.codec);
            if (optional_param) {
                out << indent << "int? " << slot << ";\n";
                line = slot + " = $c.slot(" + value + ".value, " + c + ");";
                after.push_back("if (" + slot + " != null) $r.writeBack(" + slot + ", " + name + "!, " + c + ");");
            } else {
                line = "final " + slot + " = $c.slot(" + name + ".value, " + c + ");";
                after.push_back("$r.writeBack(" + slot + ", " + name + ", " + c + ");");
            }
        } else {
            line = "$c.arg(" + value + ", " + codec(param.type.codec) + ");";
        }
        if (optional_param) out << indent << "if ($n > " << i << ") " << line << "\n";
        else out << indent << line << "\n";
    }
}

// The body of a Dart function calling its native functions (with `receiver` first, unless empty).
void Generator::emit_body(std::ostream& out, const std::string& indent, const Sig& sig, const std::string& receiver) const {
    const std::size_t max = sig.params.size();
    if (max > sig.min) {
        out << indent << "final $n = ";
        for (std::size_t i = max; i-- > sig.min;) out << sig.params[i].name << " != null ? " << i + 1 << " : ";
        out << sig.min << ";\n";
    }
    bool same = true;
    for (const auto index : sig.indexes) same = same && index == sig.indexes.front();
    std::string index;
    if (same) index = std::to_string(sig.indexes.empty() ? 0 : sig.indexes.front());
    else {
        std::vector<std::string> items;
        for (const auto i : sig.indexes) items.push_back(std::to_string(i));
        index = "const [" + join(items, ", ") + "][$n - " + std::to_string(sig.min) + "]";
    }
    out << indent << "final $c = _Call(" << index << ")" << (receiver.empty() ? "" : "..object(" + receiver + ")") << ";\n";
    std::vector<std::string> after;
    emit_arguments(out, indent, sig, after);
    const auto result = sig.result.type == "Object?" ? std::string(".value") : ".as(" + codec(sig.result.codec) + ")";
    if (after.empty()) {
        if (sig.returns) out << indent << "return $c.run()" << result << ";\n";
        else out << indent << "$c.run();\n";
    } else {
        out << indent << "final $r = $c.run();\n";
        for (const auto& line : after) out << indent << line << "\n";
        if (sig.returns) out << indent << "return $r" << result << ";\n";
    }
}

// A module function (or a function of a policy module).
void Generator::emit_function(std::ostream& out, const Group& group, const std::string& indent, const std::string& modifiers) const {
    const auto& entries = merged_.at(&group);
    const auto sig = make_sig(entries);
    const auto& name = names_.at(&group);
    const auto& function = *entries.front().overload->function;
    out << "\n" << entries_doc(entries, sig, "C++ `" + function.qualified_name() + "`").render(indent) << indent << modifiers << sig.result.type << " "
        << name << "(" << parameter_list(sig) << ") {\n";
    emit_body(out, indent + "  ", sig, "");
    out << indent << "}\n";
}

void Generator::emit_method(std::ostream& out, const std::string& owner, const std::string& name, const Member& member, bool overrides) {
    const auto& plan = classes_.at(owner);
    const bool open = plan.director && !plan.record->is_final;
    const auto sig = make_sig(member.entries);
    const auto& function = *member.entries.front().overload->function;
    const auto& source = classes_.at(member.from);
    std::string what;
    if (member.entries.front().overload->extension) what = "C++ `" + function.qualified_name() + "`";
    else what = "the C++ method `" + source.record->qualified_name() + "::" + function.name + "`";
    auto comment = entries_doc(member.entries, sig, what);
    if (member.is_virtual && open && member.from == owner)
        comment.paragraph(std::string(function.is_pure ? "Pure virtual in C++: a" : "A") + " Dart subclass overrides it, and C++ calls the override.");
    out << "\n" << comment.render("  ");
    if (member.kind == 'o') {
        const auto symbol = name.substr(9) == "unary-" ? std::string("-") : name.substr(9);
        if (overrides) out << "  @override\n";
        out << "  " << sig.result.type << " operator " << symbol << "(" << (sig.params.empty() ? std::string() : sig.params[0].type.type + " other")
            << ") {\n";
        Sig renamed = sig;
        if (!renamed.params.empty()) renamed.params[0].name = "other";
        emit_body(out, "    ", renamed, "this");
        out << "  }\n";
        return;
    }
    if (overrides || name == "toString") out << "  @override\n";
    out << "  " << (member.kind == 's' ? "static " : "") << sig.result.type << " " << name << "(" << parameter_list(sig) << ") {\n";
    if (name == "toString") out << "    if (isClosed) return super.toString();\n";
    emit_body(out, "    ", sig, member.kind == 's' ? "" : "this");
    out << "  }\n";
}

void Generator::emit_field(std::ostream& out, const std::string& name, const Member& member, bool overrides) const {
    const auto& field = *member.field;
    const auto& f = *field.field;
    const bool writable = !f.type.is_const && !readonly(member.from + "::" + f.name, member.from) && f.type.kind != TypeKind::lvalue_reference;
    const auto type = dart_type(f.type);
    const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
    out << "\n  /// " << (about.empty() ? "The C++ field `" + member.from + "::" + f.name + "`" : md(about)) << (writable ? "" : " (read-only)") << ".\n";
    if (overrides) out << "  @override\n";
    out << "  " << type.type << " get " << name << " => (_Call(" << field.getter_index << ")..object(this)).run()."
        << (type.type == "Object?" ? std::string("value") : "as(" + codec(type.codec) + ")") << ";\n";
    if (writable) {
        if (overrides) out << "\n  @override\n";
        out << "  set " << name << "(" << type.type << " value) => (_Call(" << field.setter_index << ")..object(this)..arg(value, "
            << codec(type.codec) << ")).run();\n";
    }
}

void Generator::emit_class(std::ostream& out, const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto primary = plan.bases.empty() ? std::string() : plan.bases.front();
    const auto superclass = primary.empty() ? std::string("NativeObject") : classes_.at(primary).name;
    const bool director = plan.director && !record.is_final;
    auto comment = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    if (plan.bases.size() > 1) {
        std::vector<std::string> further;
        for (std::size_t i = 1; i < plan.bases.size(); ++i)
            further.push_back("[" + classes_.at(plan.bases[i]).name + "] ([" + secondary_names_.at(owner + "::" + plan.bases[i]) + "])");
        comment.paragraph("Its further C++ base classes' members are declared again here; pass it where one of them is expected through "
                          "its `as` getter: " + join(further, ", ") + ".");
    }
    if (director)
        comment.paragraph(std::string(record.abstract ? "Abstract in C++: subclass" : "Subclass") +
                          " it in Dart to override its virtual methods: the object of a Dart subclass is a C++ subclass whose virtual methods "
                          "call the Dart overrides, and `super` runs the C++ implementation.");
    else if (record.abstract) comment.paragraph("Abstract in C++: C++ functions return objects of derived classes.");
    if (!constructor_overloads_.count(owner)) comment.paragraph("C++ functions return its objects; Dart does not construct them.");
    add_notes(comment, record.documentation);
    out << "\n" << comment.render() << (record.is_final ? "final " : "") << "class " << plan.name << " extends " << superclass << " {\n"
        << "  " << plan.name << "._wrap(super._handle) : super._wrap();\n";
    // The constructor: all C++ constructors, which C++ chooses among.
    if (constructor_overloads_.count(owner)) {
        std::vector<Entry> entries;
        for (const auto& overload : constructor_overloads_.at(owner)) entries.push_back({&overload, &plan.constructor_index});
        const auto sig = make_sig(entries);
        auto about = entries_doc(entries, sig, "the C++ constructor of `" + owner + "`");
        if (director) about.paragraph("The object of a Dart subclass is a C++ subclass whose virtual methods call the Dart overrides.");
        out << "\n" << about.render("  ") << "  " << plan.name << "(" << parameter_list(sig) << ") : super._wrap(_Handle.empty()) {\n";
        const std::size_t max = sig.params.size();
        if (max > sig.min) {
            out << "    final $n = ";
            for (std::size_t i = max; i-- > sig.min;) out << sig.params[i].name << " != null ? " << i + 1 << " : ";
            out << sig.min << ";\n";
        }
        out << "    _Rosetta.construct(this, " << plan.constructor_index << ", " << (director ? "runtimeType != " + plan.name : std::string("false"))
            << ", ($c) {\n";
        std::vector<std::string> after;
        emit_arguments(out, "      ", sig, after);
        out << "    });\n  }\n";
    }
    // Members: own ones, then further bases' ones declared again.
    const auto& table = table_.at(owner);
    const std::map<std::string, Member> empty;
    const auto& inherited = primary.empty() ? empty : table_.at(primary);
    for (const auto& name : order_[owner]) {
        const auto& member = table.at(name);
        if (member.from != owner && inherited.count(name)) continue;   // inherited as it is
        const bool overrides = member.kind != 's' && inherited.count(name) && inherited.at(name).kind != 's';
        if (member.kind == 'f') emit_field(out, name, member, overrides);
        else emit_method(out, owner, name, member, overrides);
    }
    for (std::size_t i = 1; i < plan.bases.size(); ++i) {
        const auto& base = plan.bases[i];
        const auto& name = classes_.at(base).name;
        out << "\n  /// This object as its further C++ base class [" << name << "].\n  " << name << " get "
            << secondary_names_.at(owner + "::" + base) << " => cast<" << name << ">()!;\n";
    }
    for (const auto& constant : plan.constants) {
        const auto type = dart_type(*constant.type);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        out << "\n" << about.render("  ") << "  static " << type.type << " get " << constant_names_.at(&constant) << " => _Call(" << constant.index
            << ").run().as(" << codec(type.codec) << ");\n";
    }
    out << "}\n";
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto comment = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    comment.paragraph("An extension type over the value: its enumerators are constants (`" + name + "." + (cases.empty() ? std::string("x") : cases.front()) +
                      "`), and other values of the C++ type are `" + name + "(42)`.");
    add_notes(comment, enumeration.documentation);
    out << "\n" << comment.render() << "extension type const " << name << "(int value) {\n";
    std::vector<std::string> values;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        auto value = enumeration.enumerator_values[i];
        if (!value.empty() && value[0] != '-' && value.size() >= 19) {
            const auto number = std::stoull(value);
            if (number > 9223372036854775807ull) value = std::to_string(static_cast<long long>(number));
        }
        if (value == "-9223372036854775808") value = "-9223372036854775807 - 1";
        values.push_back(value);
        out << "  /// C++ `" << qualified << "::" << enumeration.enumerators[i] << "`.\n  static const " << cases[i] << " = " << name << "(" << value << ");\n\n";
    }
    out << "  /// The enumerators.\n  static const values = <" << name << ">[" << join(cases, ", ") << "];\n\n"
        << "  /// The enumerator's name, else the value.\n  String get name => switch (value) {\n";
    std::set<std::string> seen;
    for (std::size_t i = 0; i < cases.size(); ++i)
        if (seen.insert(values[i]).second) out << "        " << values[i] << " => " << dart_string(cases[i]) << ",\n";
    out << "        _ => '$value',\n      };\n}\n";
}

// The function C++ calls a director's virtual methods through: the Dart method of the object.
std::string Generator::dispatch() {
    std::ostringstream out;
    out << "\n// The Dart methods C++ calls for the virtual methods of Dart subclasses.\n"
        << "Object? _rosettaDispatch(NativeObject object, int generic, List<Object?> arguments) {\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director || plan.record->is_final) continue;
        for (const auto& item : overridable(plan)) {
            const auto generic = generic_index_.find(item.name);
            const auto name = mapped_.at(owner).find("m:" + item.name);
            if (generic == generic_index_.end() || name == mapped_.at(owner).end()) continue;
            const auto found = table_.at(owner).find(name->second);
            if (found == table_.at(owner).end() || found->second.kind != 'm') continue;
            const auto sig = make_sig(found->second.entries);
            const auto& method = *item.method;
            std::vector<std::string> arguments;
            std::ostringstream conversions;
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                const auto& type = method.parameters[i];
                const auto dt = dart_type(type);
                const auto value = dt.codec == "_Codecs.anything" ? "arguments[" + std::to_string(i) + "]"
                                                                  : codec(dt.codec) + ".from(arguments[" + std::to_string(i) + "])";
                arguments.push_back(writeback(type) ? "Ref(" + value + ")" : value);
            }
            const bool returns = !is_void(method.result);
            out << "  if (generic == " << generic->second << " && arguments.length == " << method.parameters.size() << " && object is " << plan.name
                << ") {\n";
            const auto call = "object." + name->second + "(" + join(arguments, ", ") + ")";
            if (returns) {
                const auto result = dart_type(method.result);
                const auto cast = result.type == sig.result.type || result.type == "Object?" ? call : "(" + call + " as " + result.type + ")";
                out << "    return " << (result.codec == "_Codecs.anything" ? cast : codec(result.codec) + ".to(" + cast + ")") << ";\n";
            } else {
                out << "    " << call << ";\n    return null;\n";
            }
            out << "  }\n";
        }
    }
    out << "  throw NativeError(NativeErrorKind.binding, 'no Dart method for C++ virtual method $generic of $object');\n}\n";
    return out.str();
}

GeneratedDartSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = options_.package.empty() ? dart_package_name(name_) : options_.package;
    bool valid = !package_.empty() && is_lower(package_[0]);
    for (const char c : package_) valid = valid && (is_lower(c) || is_digit(c) || c == '_');
    if (!valid || dart_keyword(package_)) throw BindingError("Dart package name must be lowercase_with_underscores and not a Dart keyword: " + package_);
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Dart native library name must be a plain file name: " + library_);
    GeneratedDartSource result;
    prepare();
    const auto type_name = [&](const std::string& wanted) { return types_.claim_type(pascal(wanted)); };
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = type_name(plan.name);
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = type_name(name);
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        Names scope;
        for (const char* reserved : {"value", "name", "values", "hashCode", "runtimeType", "toString", "noSuchMethod", "int", "String"}) scope.reserve(reserved);
        for (const auto& enumerator : enumeration.enumerators)
            enum_cases_[qualified].push_back(scope.claim(camel(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Policy modules are abstract final classes of static methods (a.b is AB).
    for (const auto& group : functions) {
        if (group.module.empty() || submodules_.count(group.module)) continue;
        std::string joined = group.module;
        std::replace(joined.begin(), joined.end(), '.', '_');
        submodules_[group.module] = type_name(joined);
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    // Top-level functions share the library's namespace with its types.
    for (const char* reserved : {"ffi", "io", "utf8", "print", "identical", "identityHashCode", "int", "double", "bool", "num"}) types_.reserve(reserved);
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Dart package " + package_ + " (" + name_ + ").");

    // ------------------------------------------------------------ the API
    std::ostringstream body;
    for (const auto& [qualified, name] : enum_names_) emit_enum(body, qualified);
    for (const auto& owner : class_order_) emit_class(body, owner);
    for (const auto& group : functions)
        if (group.module.empty()) emit_function(body, group, "", "");
    for (const auto& constant : constants) {
        const auto type = dart_type(*constant.type);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
        body << "\n" << about.render() << type.type << " get " << constant_names_.at(&constant) << " => _Call(" << constant.index << ").run().as("
             << codec(type.codec) << ");\n";
    }
    for (const auto& [module, name] : submodules_) {
        std::string from;
        for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) from = ns;
        body << "\n/// The functions of the C++ namespace `" << from << "` (policy module `" << module << "`).\nabstract final class " << name << " {";
        for (const auto& group : functions)
            if (group.module == module) emit_function(body, group, "  ", "static ");
        body << "}\n";
    }
    body << "\n// The C++ classes: name, base classes (indexes), Dart class, and how to wrap an object.\nfinal _classes = <_ClassInfo>[\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        std::vector<std::string> bases;
        for (const auto& base : plan.bases) {
            std::size_t at = 0;
            while (class_order_[at] != base) ++at;
            bases.push_back(std::to_string(at));
        }
        body << "  _ClassInfo(" << dart_string(plan.name) << ", const [" << join(bases, ", ") << "], " << plan.name << ", " << plan.name << "._wrap),\n";
    }
    body << "];\n" << dispatch();
    // The codecs, built once.
    std::ostringstream codecs;
    if (!codec_order_.empty()) codecs << "\n// The codecs of the API's types.\n";
    for (std::size_t i = 0; i < codec_order_.size(); ++i) codecs << "final " << codecs_.at(codec_order_[i]) << " = " << codec_order_[i] << ";\n";

    std::ostringstream dart;
    dart << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\n"
         << "/// The Dart interface to the C++ library " << name_ << ".\n///\n"
         << "/// Each C++ class is a class over [NativeObject] (`Circle(2)`), each module function a function, and each C++\n"
         << "/// constant a getter. Dart has no overloading: the overloads and default arguments of a C++ function are one\n"
         << "/// Dart function with optional parameters, and C++ chooses the overload at run time. Calls throw\n"
         << "/// [NativeError] for C++ exceptions and values C++ does not accept. The native library " << library_ << " is\n"
         << "/// loaded at the first call (see [NativeLibrary]).\nlibrary;\n\n"
         << "import 'dart:convert' show utf8;\nimport 'dart:ffi' as ffi;\nimport 'dart:io' as io;\nimport 'dart:typed_data' show ByteData, Endian, Uint8List;\n\n"
         << "part 'src/rosetta.dart';\n" << body.str() << codecs.str();
    result.dart = dart.str();

    // ------------------------------------------------------------ the package
    std::string runtime;
    for (const auto* part : detail::dart_full_runtime_parts) runtime += part;
    const auto replace = [&](std::string& text, const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    };
    auto env = name_;
    for (auto& c : env) c = is_alnum(c) ? upper(c) : '_';
    replace(runtime, "@LIBFILE@", package_ + ".dart");
    replace(runtime, "@C@", "rosetta_" + name_);
    replace(runtime, "@LIB@", library_);
    replace(runtime, "@ENV@", env + "_DART_LIBRARY");
    std::ostringstream pubspec;
    pubspec << "# Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\nname: " << package_
            << "\ndescription: Dart bindings to the C++ library " << name_ << " (generated by Rosetta).\nversion: 0.0.0\npublish_to: none\n"
            << "environment:\n  sdk: ^3.3.0\n";
    result.files.emplace_back("pubspec.yaml", pubspec.str());
    result.files.emplace_back("lib/src/rosetta.dart", runtime);
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

std::string dart_package_name(const std::string& name) { return snake_words(name); }

GeneratedDartSource generate_dart(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                  const DartOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Dart C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
