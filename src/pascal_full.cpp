// Pascal backend (--language pascal): a Free Pascal unit (objfpc mode) over the shared C ABI library
// of the full C ABI backends (cabi_full.hpp, lisp_full_runtime.hpp).
//
// Classes are Pascal classes over TNativeObject (TCircle); the first C++ base class is the parent
// class, and the members of further bases are declared again (C++ upcasts the object), with an
// AsBase function for passing the object where a further base is expected. A Pascal subclass
// overrides C++ virtual methods: constructing it creates a C++ subclass whose virtual methods call
// the Pascal methods (inherited runs the C++ implementation). Overloads and default arguments become
// Pascal overloads (one per overload and argument count, so C++ applies its default arguments),
// resolved again at run time in C++. C++ exceptions raise ENativeError; enums are scoped enums,
// containers dynamic arrays and records, std::function nested procedural types (and TNative
// objects with an Invoke method as results), non-const reference parameters var parameters,
// operators methods (operator[] also the default array property) and to_string ToString. Doxygen
// becomes XML documentation comments.
//
// Names are case-insensitive in Pascal: every scope claims them case-insensitively; keywords are
// escaped (&Repeat).
#include "rosetta/pascal.hpp"
#include "cabi_full.hpp"
#include "pascal_full_runtime.hpp"
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
std::string fold(std::string text) {
    for (auto& c : text) c = lower(c);
    return text;
}

// Free Pascal's reserved words in objfpc mode: identifiers spell them &Name.
bool pascal_keyword(const std::string& name) {
    static const std::set<std::string> words = {"and", "array", "as", "asm", "begin", "case", "class", "const", "constructor", "destructor",
        "dispinterface", "div", "do", "downto", "else", "end", "except", "exports", "file", "finalization", "finally", "for", "function",
        "generic", "goto", "if", "implementation", "in", "inherited", "initialization", "inline", "interface", "is", "label", "library",
        "mod", "nil", "not", "object", "of", "on", "operator", "or", "out", "packed", "procedure", "program", "property", "raise", "record",
        "repeat", "resourcestring", "set", "shl", "shr", "specialize", "string", "then", "threadvar", "to", "try", "type", "unit", "until",
        "uses", "var", "while", "with", "xor", "self", "false", "true"};
    return words.count(fold(name)) != 0;
}
// Identifiers that do not work even escaped (Result in a function, Self in a method).
bool pascal_unusable(const std::string& name) {
    static const std::set<std::string> words = {"result", "self"};
    return words.count(fold(name)) != 0;
}
// Identifiers the runtime and the generated code use unqualified: a module name like one would
// take over the runtime's calls inside the unit.
const std::set<std::string>& runtime_names() {
    static const std::set<std::string> words = [] {
        std::set<std::string> folded;
        for (const char* name : {"Abs", "Frac", "Trunc", "IntToStr", "TryStrToInt64", "TryStrToQWord", "StrToFloat", "Copy", "Delete", "Move",
                                 "SetLength", "Length", "Inc", "Dec", "Default", "Exit", "High", "Low", "Ord", "Chr", "Assigned", "Pointer",
                                 "InitCriticalSection", "EnterCriticalSection", "LeaveCriticalSection", "GetExceptionMask", "SetExceptionMask",
                                 "AcquireExceptionObject", "Exception", "TBytes", "TObject", "TClass", "Boolean", "Byte", "Integer", "LongInt",
                                 "LongWord", "QWord", "Int8", "Int16", "Int32", "Int64", "UInt8", "UInt16", "UInt32", "UInt64", "Double", "Single",
                                 "AnsiChar", "Char", "RawByteString", "AnsiString", "PByte", "PPByte", "PtrUInt", "PPtrUInt", "TRTLCriticalSection",
                                 "TFPUExceptionMask", "SysUtils", "Math", "Generics", "Collections", "TDictionary", "System", "TNativeErrorKind",
                                 "ENativeError", "TNativeComplex", "TNativeValue", "TNativeObject", "TNativeObjectClass", "TNativeFunction",
                                 "TRosettaInvokeFn", "TRosettaRetainFn", "TRosettaValue", "TRosettaValues", "TRosettaObjects", "TRosettaCallback",
                                 "TRosettaClassInfo", "TRosettaWriter", "TRosettaReply", "TRosettaCall", "TRosettaReader", "RosettaNativeCall",
                                 "RosettaNativeFree", "RosettaNativeAlloc", "RosettaNativeRelease", "RosettaNativeInit", "RosettaLock",
                                 "RosettaNext", "RosettaTable", "RosettaCauses", "RosettaClasses", "RosettaCauseMarker", "RosettaFunctionClass",
                                 "RosettaDispatch", "RosettaFail", "RosettaKindName", "RosettaMismatch", "RosettaNewId", "RosettaIdOf",
                                 "RosettaLookup", "RosettaRegister", "RosettaAdjust", "RosettaSaveCause", "RosettaRaiseCause", "RosettaDerives",
                                 "RosettaIndexOf", "RosettaToBool", "RosettaToInt", "RosettaToUInt64", "RosettaToInt8", "RosettaToInt16",
                                 "RosettaToInt32", "RosettaToInt64", "RosettaToUInt8", "RosettaToUInt16", "RosettaToUInt32", "RosettaToDouble",
                                 "RosettaToSingle", "RosettaToComplex", "RosettaToString", "RosettaToChar", "RosettaToValue", "RosettaItems",
                                 "RosettaPairs", "RosettaTuple", "RosettaArity", "RosettaToObject", "RosettaToFunction", "RosettaDropCallbacks",
                                 "RosettaConstruct", "RosettaFinish", "RosettaInvoke", "RosettaRetain", "RosettaStart", "RosettaRegisterClasses",
                                 "exInvalidOp", "exDenormalized", "exZeroDivide", "exOverflow", "exUnderflow", "exPrecision"})
            folded.insert(fold(name));
        return folded;
    }();
    return words;
}
// An identifier as Pascal code spells it.
std::string ident(const std::string& name) { return pascal_keyword(name) ? "&" + name : name; }

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
// PascalCase (types without the T, functions, methods, properties, enumerators).
std::string pascal(const std::string& name) {
    const auto words = snake_words(name);
    std::string result;
    bool start = true;
    for (const char c : words) {
        if (c == '_') { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    return result;
}
// A Pascal string literal.
std::string pascal_string(const std::string& text) {
    std::string result;
    bool open = false;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32) {
            if (open) { result += "'"; open = false; }
            result += "#" + std::to_string(byte);
            continue;
        }
        if (!open) { result += "'"; open = true; }
        result += c == '\'' ? std::string("''") : std::string(1, c);
    }
    if (open) result += "'";
    return result.empty() ? "''" : result;
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
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

// Names in one Pascal scope (case-insensitive).
struct Names {
    std::set<std::string> taken;   // folded
    bool reserve(const std::string& name) { return taken.insert(fold(name)).second; }
    bool free(const std::string& name) const { return !taken.count(fold(name)); }
    // The wanted name (with an underscore if Pascal does not allow it), else with a number (GetName2).
    std::string claim(std::string wanted) {
        if (pascal_unusable(wanted)) wanted += "_";
        if (reserve(wanted)) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
    }
    // A type name: the wanted name, else with a trailing underscore (TPoint_), else a number.
    std::string claim_type(const std::string& wanted) {
        if (reserve(wanted)) return wanted;
        return claim(wanted + "_");
    }
};

// ---------------------------------------------------------------- doc comments (XML)
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
    std::string out;
    std::istringstream in(block.text);
    std::vector<std::string> items;
    std::string paragraph;
    const auto flush = [&] {
        if (!items.empty()) {
            out += "<list type=\"bullet\">\n";
            for (const auto& item : items) out += "<item><description>" + inline_xml(item) + "</description></item>\n";
            out += "</list>\n";
            items.clear();
        }
    };
    for (std::string line; std::getline(in, line);) {
        const auto stripped = trim(line);
        if (starts(stripped, "* ") || starts(stripped, "- ")) {
            if (!paragraph.empty()) { out += "<para>" + inline_xml(paragraph) + "</para>\n"; paragraph.clear(); }
            items.push_back(trim(stripped.substr(2)));
        } else if (!items.empty() && !stripped.empty()) {
            items.back() += " " + stripped;
        } else {
            flush();
            if (!stripped.empty()) paragraph += (paragraph.empty() ? "" : " ") + stripped;
        }
    }
    flush();
    if (!paragraph.empty()) out += "<para>" + inline_xml(paragraph) + "</para>\n";
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
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
    std::vector<std::pair<std::string, std::string>> params;   // name, text
    std::string returns;
    std::vector<std::string> exceptions;
    std::string render(const std::string& indent) const {
        std::string out = "<summary>" + summary + "</summary>\n";
        if (!remarks.empty()) {
            out += "<remarks>\n";
            for (const auto& remark : remarks) out += remark + "\n";
            out += "</remarks>\n";
        }
        for (const auto& [name, text] : params) out += "<param name=\"" + name + "\">" + text + "</param>\n";
        if (!returns.empty()) out += "<returns>" + returns + "</returns>\n";
        for (const auto& text : exceptions) out += "<exception cref=\"ENativeError\">" + text + "</exception>\n";
        return comment(out, indent);
    }
};

std::string error_kind(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "InvalidArgument";
    if (bare == "std::out_of_range") return "OutOfRange";
    if (bare == "std::domain_error") return "DomainError";
    if (bare == "std::length_error") return "LengthError";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "ArithmeticError";
    if (bare == "std::bad_alloc") return "Allocation";
    return "Native";
}

// C++ operators as Pascal methods ("" = not bound).
std::string pascal_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "Invoke";
    static const std::map<std::string, std::pair<const char*, const char*>> table = {
        {"operator+", {"Plus", "Add"}}, {"operator-", {"Negate", "Subtract"}}, {"operator*", {nullptr, "Multiply"}},
        {"operator/", {nullptr, "Divide"}}, {"operator%", {nullptr, "Modulo"}}, {"operator==", {nullptr, "IsEqual"}},
        {"operator!=", {nullptr, "IsNotEqual"}}, {"operator<", {nullptr, "IsLess"}}, {"operator<=", {nullptr, "IsLessOrEqual"}},
        {"operator>", {nullptr, "IsGreater"}}, {"operator>=", {nullptr, "IsGreaterOrEqual"}}, {"operator&", {nullptr, "BitAnd"}},
        {"operator|", {nullptr, "BitOr"}}, {"operator^", {nullptr, "BitXor"}}, {"operator~", {"Complement", nullptr}},
        {"operator!", {"LogicalNot", nullptr}}, {"operator<<", {nullptr, "ShiftLeft"}}, {"operator>>", {nullptr, "ShiftRight"}},
        {"operator[]", {nullptr, "At"}}, {"operator+=", {nullptr, "AddAssign"}}, {"operator-=", {nullptr, "SubtractAssign"}},
        {"operator*=", {nullptr, "MultiplyAssign"}}, {"operator/=", {nullptr, "DivideAssign"}}};
    const auto found = table.find(name);
    if (found == table.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// A C++ type in Pascal: its spelling, how a value goes to C++ ($W the writer, $V the value) and
// comes back ($V a TRosettaValue), and the part of generated type names it gives.
struct PT {
    std::string type = "TNativeValue";
    std::string put = "$W.Value($V)";
    std::string to = "RosettaToValue($V)";
    std::string mangle = "Value";
    bool object = false;     // a class: passed with ObjArg
    bool callback = false;   // a procedural type
};
std::string apply(const std::string& pattern, const std::string& writer, const std::string& value) {
    return replace_all(replace_all(pattern, "$W", writer), "$V", value);
}

// One Pascal routine of a group: an overload called with its first `arity` C++ parameters.
struct Variant {
    const Overload* overload;
    std::size_t arity;
    std::vector<PT> params;     // the Pascal parameters (receiver excluded)
    std::vector<std::string> names;
    std::vector<bool> var;
    PT result;
    bool returns = false;
    std::string key;            // the Pascal parameter types (folded)
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const PascalOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedPascalSource run();

private:
    PascalOptions options_;
    std::string unit_, library_;
    Names globals_;                                          // the unit's types, functions and constants
    std::map<std::string, Names> modules_;                   // policy module -> function names
    std::map<std::string, Names> members_;                   // class -> member names (inherited ones included)
    std::map<std::string, std::map<std::string, std::string>> mapped_;   // class -> C++ member key -> Pascal name
    std::map<const Group*, std::string> names_;
    std::map<std::string, std::string> field_names_;         // class::field -> property
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::map<std::string, std::string> submodules_;          // policy module -> class path (Data.Inner)
    std::map<std::string, std::string> secondary_names_;     // class::base -> AsBase
    std::map<std::string, std::vector<Overload>> constructor_overloads_;
    std::map<std::string, std::string> items_names_;         // class -> its default array property ("" = none)
    struct Emitted { std::string result; bool is_virtual; };
    std::map<std::string, std::map<std::string, Emitted>> emitted_;   // class -> member key -> what it is (inherited ones included)
    std::set<std::string> enum_int64_;                        // enums Pascal cannot declare: Int64

    // Generated helper types, in dependency order: declarations (interface) and codecs (implementation).
    mutable std::vector<std::string> type_decls_, codec_decls_, box_decls_, native_decls_, native_bodies_;
    mutable std::map<std::string, std::string> helper_types_;   // mangle -> type name
    mutable Names* helper_scope_ = nullptr;

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Pascal"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return pascal_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        Names taken;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            const auto wanted = cpp.empty() || !identifier(cpp) ? "A" + std::to_string(i - first + 1) : "A" + pascal(cpp);
            names.push_back(taken.claim(wanted));
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
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, plan.name + ".Create") + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: subclass it in Pascal and override its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: subclass it in Pascal";
    }
    std::string symbol_prefix() const override { return "rosetta_pascal_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_PASCAL"; }
    std::string module_prefix() const override { return "rosetta_pascal_module_"; }

    // ------------------------------------------------------------ types
    bool is_enum(const std::string& type) const {
        for (const auto& [qualified, name] : enum_names_) if (name == type) return true;
        return false;
    }
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    PT pascal_type(const Type& type, bool parameter) const;
    PT pascal_spelled(std::string cpp) const;
    PT scalar(const std::string& type, const std::string& put, const std::string& to) const { return {type, put, to, type, false, false}; }
    PT class_type(const std::string& name) const {
        PT result{name, "$W.Obj($V)", name + "(RosettaToObject($V, " + name + "))", name.substr(1), true, false};
        return result;
    }
    PT array_of(const PT& item) const;
    PT map_of(const PT& key, const PT& item) const;
    PT optional_of(const PT& item) const;
    PT tuple_of(const std::vector<PT>& items, bool pair) const;
    PT function_of(const std::vector<PT>& args, const PT& result, bool returns, bool parameter) const;
    std::string helper_name(const std::string& mangle) const;
    std::string pascal_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    std::vector<Variant> plan_variants(const std::vector<Overload>& overloads) const;

    // ------------------------------------------------------------ naming and emission
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    bool conflicts(const std::string& owner, const Group& group) const;
    Doc variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const;
    Doc describe(const Documentation& doc, const std::string& fallback) const;
    std::string parameter_list(const Variant& variant, const std::set<std::string>& avoid, std::vector<std::string>* renamed) const;
    void emit_body(std::ostream& out, const Variant& variant, const std::vector<std::string>& names, std::size_t index, const std::string& receiver,
                   const std::string& result_target) const;
    void emit_routine(std::ostream& decl, std::ostream& body, const Group& group, const std::string& indent, const std::string& qualifier,
                      const std::string& modifiers, const std::set<std::string>& avoid) const;
    void emit_members(std::ostream& decl, std::ostream& body, const std::string& owner, const std::string& from, bool copied);
    void emit_class(std::ostream& decl, std::ostream& body, const std::string& owner);
    void emit_enum(std::ostream& decl, std::ostream& helpers, std::ostream& body, const std::string& qualified) const;
    std::string dispatch();
};

// ---------------------------------------------------------------- types
std::string Generator::helper_name(const std::string& mangle) const {
    const auto found = helper_types_.find(mangle);
    if (found != helper_types_.end()) return found->second;
    const auto name = helper_scope_->claim_type("T" + mangle);
    helper_types_[mangle] = name;
    return name;
}

PT Generator::array_of(const PT& item) const {
    const auto mangle = "ArrayOf" + item.mangle;
    if (item.type == "UInt8") {
        // Byte vectors are TBytes.
        PT result{"TBytes", "_PutBytes($W, $V)", "_ToBytes($V)", mangle, false, false};
        if (!helper_types_.count(mangle)) {
            helper_types_[mangle] = "TBytes";
            codec_decls_.push_back("procedure _PutBytes(var W: TRosettaWriter; const V: TBytes);\nvar\n  I: Integer;\nbegin\n  W.List(System.Length(V));\n"
                                   "  for I := 0 to High(V) do W.Int(V[I]);\nend;\n\nfunction _ToBytes(const V: TRosettaValue): TBytes;\nvar\n  I: Integer;\n"
                                   "begin\n  Result := nil;\n  if V.Tag in [5, 15] then\n  begin\n    SetLength(Result, System.Length(V.S));\n"
                                   "    if V.S <> '' then Move(V.S[1], Result[0], System.Length(V.S));\n    Exit;\n  end;\n"
                                   "  RosettaItems(V, 'TBytes');\n  SetLength(Result, System.Length(V.Items));\n"
                                   "  for I := 0 to High(V.Items) do Result[I] := RosettaToUInt8(V.Items[I]);\nend;\n");
        }
        return result;
    }
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name(mangle);
    if (!known) {
        type_decls_.push_back("  /// <summary>An array of <c>" + xml(item.type) + "</c>.</summary>\n  " + name + " = array of " + item.type + ";\n");
        std::ostringstream codec;
        codec << "procedure _Put" << mangle << "(var W: TRosettaWriter; const V: " << name << ");\nvar\n  I: Integer;\nbegin\n  W.List(System.Length(V));\n"
              << "  for I := 0 to High(V) do " << apply(item.put, "W", "V[I]") << ";\nend;\n\n"
              << "function _To" << mangle << "(const V: TRosettaValue): " << name << ";\nvar\n  I: Integer;\nbegin\n  RosettaItems(V, '" << name
              << "');\n  Result := nil;\n  SetLength(Result, System.Length(V.Items));\n  for I := 0 to High(V.Items) do Result[I] := " << apply(item.to, "", "V.Items[I]")
              << ";\nend;\n";
        codec_decls_.push_back(codec.str());
    }
    return {name, "_Put" + mangle + "($W, $V)", "_To" + mangle + "($V)", mangle, false, false};
}

PT Generator::map_of(const PT& key, const PT& item) const {
    const auto entry_mangle = "EntryOf" + key.mangle + "To" + item.mangle;
    const auto mangle = "MapOf" + key.mangle + "To" + item.mangle;
    const bool known = helper_types_.count(mangle) != 0;
    const auto entry = helper_name(entry_mangle);
    const auto name = helper_name(mangle);
    if (!known) {
        type_decls_.push_back("  /// <summary>A key and value of a <see cref=\"" + name + "\"/>.</summary>\n  " + entry + " = record\n    Key: " + key.type +
                              ";\n    Value: " + item.type + ";\n    /// <summary>The entry <c>AKey</c>, <c>AValue</c>.</summary>\n    class function Make(const AKey: " +
                              key.type + "; const AValue: " + item.type + "): " + entry + "; static;\n  end;\n"
                              "  /// <summary>A C++ map: an array of keys and values.</summary>\n  " + name + " = array of " + entry + ";\n");
        std::ostringstream codec;
        codec << "class function " << entry << ".Make(const AKey: " << key.type << "; const AValue: " << item.type << "): " << entry
              << ";\nbegin\n  Result.Key := AKey;\n  Result.Value := AValue;\nend;\n\n"
              << "procedure _Put" << mangle << "(var W: TRosettaWriter; const V: " << name << ");\nvar\n  I: Integer;\nbegin\n  W.Map(System.Length(V));\n"
              << "  for I := 0 to High(V) do\n  begin\n    " << apply(key.put, "W", "V[I].Key") << ";\n    " << apply(item.put, "W", "V[I].Value")
              << ";\n  end;\nend;\n\n"
              << "function _To" << mangle << "(const V: TRosettaValue): " << name << ";\nvar\n  P: TRosettaValues;\n  I: Integer;\nbegin\n"
              << "  P := RosettaPairs(V);\n  Result := nil;\n  SetLength(Result, System.Length(P) div 2);\n  for I := 0 to High(Result) do\n  begin\n"
              << "    Result[I].Key := " << apply(key.to, "", "P[2 * I]") << ";\n    Result[I].Value := " << apply(item.to, "", "P[2 * I + 1]")
              << ";\n  end;\nend;\n";
        codec_decls_.push_back(codec.str());
    }
    return {name, "_Put" + mangle + "($W, $V)", "_To" + mangle + "($V)", mangle, false, false};
}

PT Generator::optional_of(const PT& item) const {
    if (item.object || item.callback || starts(item.type, "TNative")) return item;   // nil already means none
    const auto mangle = "Optional" + item.mangle;
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name(mangle);
    if (!known) {
        type_decls_.push_back("  /// <summary>A C++ <c>std::optional</c> of <c>" + xml(item.type) + "</c>: <c>HasValue</c> and the value.</summary>\n  " + name +
                              " = record\n    HasValue: Boolean;\n    Value: " + item.type + ";\n    /// <summary>A value.</summary>\n"
                              "    class function Some(const AValue: " + item.type + "): " + name + "; static;\n    /// <summary>No value.</summary>\n"
                              "    class function None: " + name + "; static;\n  end;\n");
        std::ostringstream codec;
        codec << "class function " << name << ".Some(const AValue: " << item.type << "): " << name << ";\nbegin\n  Result.HasValue := True;\n"
              << "  Result.Value := AValue;\nend;\n\nclass function " << name << ".None: " << name << ";\nbegin\n  Result := Default(" << name
              << ");\nend;\n\nprocedure _Put" << mangle << "(var W: TRosettaWriter; const V: " << name << ");\nbegin\n  if V.HasValue then "
              << apply(item.put, "W", "V.Value") << " else W.Nil_;\nend;\n\nfunction _To" << mangle << "(const V: TRosettaValue): " << name
              << ";\nbegin\n  Result := Default(" << name << ");\n  if V.Tag = 0 then Exit;\n  Result.HasValue := True;\n  Result.Value := "
              << apply(item.to, "", "V") << ";\nend;\n";
        codec_decls_.push_back(codec.str());
    }
    return {name, "_Put" + mangle + "($W, $V)", "_To" + mangle + "($V)", mangle, false, false};
}

PT Generator::tuple_of(const std::vector<PT>& items, bool pair) const {
    std::string mangle = pair ? "PairOf" : "TupleOf";
    std::vector<std::string> fields;
    for (std::size_t i = 0; i < items.size(); ++i) {
        mangle += (i ? "And" : "") + items[i].mangle;
        fields.push_back(pair ? (i == 0 ? "First" : "Second") : "Item" + std::to_string(i + 1));
    }
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name(mangle);
    if (!known) {
        std::ostringstream decl, codec;
        std::vector<std::string> params, types;
        for (std::size_t i = 0; i < items.size(); ++i) {
            params.push_back("const A" + fields[i] + ": " + items[i].type);
            types.push_back(items[i].type);
        }
        decl << "  /// <summary>A C++ " << (pair ? "pair" : "tuple") << " of <c>" << xml(join(types, ", ")) << "</c>.</summary>\n  " << name << " = record\n";
        for (std::size_t i = 0; i < items.size(); ++i) decl << "    " << fields[i] << ": " << items[i].type << ";\n";
        decl << "    /// <summary>The " << (pair ? "pair" : "tuple") << " of the arguments.</summary>\n    class function Make(" << join(params, "; ") << "): " << name
             << "; static;\n  end;\n";
        type_decls_.push_back(decl.str());
        codec << "class function " << name << ".Make(" << join(params, "; ") << "): " << name << ";\nbegin\n";
        for (const auto& field : fields) codec << "  Result." << field << " := A" << field << ";\n";
        codec << "end;\n\nprocedure _Put" << mangle << "(var W: TRosettaWriter; const V: " << name << ");\nbegin\n  W.List(" << items.size() << ");\n";
        for (std::size_t i = 0; i < items.size(); ++i) codec << "  " << apply(items[i].put, "W", "V." + fields[i]) << ";\n";
        codec << "end;\n\nfunction _To" << mangle << "(const V: TRosettaValue): " << name << ";\nbegin\n  Result := Default(" << name << ");\n  RosettaTuple(V, " << items.size() << ");\n";
        for (std::size_t i = 0; i < items.size(); ++i)
            codec << "  Result." << fields[i] << " := " << apply(items[i].to, "", "V.Items[" + std::to_string(i) + "]") << ";\n";
        codec << "end;\n";
        codec_decls_.push_back(codec.str());
    }
    return {name, "_Put" + mangle + "($W, $V)", "_To" + mangle + "($V)", mangle, false, false};
}

// std::function: as a parameter, a nested procedural type (Pascal routines, nested ones included);
// as a result, a TNative class whose Invoke calls the C++ function.
PT Generator::function_of(const std::vector<PT>& args, const PT& result, bool returns, bool parameter) const {
    std::string mangle;
    for (const auto& arg : args) mangle += arg.mangle;
    mangle += returns ? "To" + result.mangle : std::string(args.empty() ? "Procedure" : "");
    if (mangle.empty()) mangle = "Procedure";
    std::vector<std::string> params;
    for (std::size_t i = 0; i < args.size(); ++i) params.push_back("A" + std::to_string(i + 1) + ": " + args[i].type);
    const auto header = std::string(returns ? "function" : "procedure") + (params.empty() ? "" : "(" + join(params, "; ") + ")") +
                        (returns ? ": " + result.type : std::string());
    if (parameter) {
        const auto callback_mangle = "Callback" + mangle;
        const bool known = helper_types_.count(callback_mangle) != 0;
        const auto name = helper_name(callback_mangle);
        const auto box = "_TBox" + mangle;
        if (!known) {
            type_decls_.push_back("  /// <summary>A Pascal routine C++ calls (a C++ <c>std::function</c>): a global routine, or a nested one while C++ "
                                  "only calls it during the call it was passed to.</summary>\n  " + name + " = " + header + " is nested;\n");
            std::ostringstream decl, body;
            decl << "  " << box << " = class(TRosettaCallback)\n    F: " << name << ";\n    constructor Create(AF: " << name
                 << ");\n    procedure Call(const Args: TRosettaValues; var W: TRosettaWriter); override;\n  end;\n";
            body << "constructor " << box << ".Create(AF: " << name << ");\nbegin\n  inherited Create;\n  F := AF;\nend;\n\n"
                 << "procedure " << box << ".Call(const Args: TRosettaValues; var W: TRosettaWriter);\nbegin\n  RosettaArity(Args, " << args.size() << ");\n";
            std::vector<std::string> call;
            for (std::size_t i = 0; i < args.size(); ++i) call.push_back(apply(args[i].to, "", "Args[" + std::to_string(i) + "]"));
            const auto invocation = "F(" + join(call, ", ") + ")";
            if (returns) body << "  " << apply(result.put, "W", invocation) << ";\n";
            else body << "  " << invocation << ";\n  W.Nil_;\n";
            body << "end;\n";
            box_decls_.push_back(decl.str());
            native_bodies_.push_back(body.str());
        }
        PT pt{name, "if Assigned($V) then $W.Callback(" + box + ".Create($V)) else $W.Nil_", "", callback_mangle, false, true};
        return pt;
    }
    const auto native_mangle = "Native" + mangle;
    const bool known = helper_types_.count(native_mangle) != 0;
    const auto name = helper_name(native_mangle);
    if (!known) {
        std::vector<std::string> invoke_params;
        for (std::size_t i = 0; i < args.size(); ++i) invoke_params.push_back("const A" + std::to_string(i + 1) + ": " + args[i].type);
        const auto invoke = std::string(returns ? "function" : "procedure") + " Invoke" + (invoke_params.empty() ? "" : "(" + join(invoke_params, "; ") + ")") +
                            (returns ? ": " + result.type : std::string());
        native_decls_.push_back("  /// <summary>A C++ <c>std::function</c>: call it with <c>Invoke</c>, and free it when done.</summary>\n  " + name +
                                " = class(TNativeFunction)\n  public\n    /// <summary>Calls the C++ function.</summary>\n    " + invoke + ";\n  end;\n");
        std::ostringstream body;
        body << replace_all(invoke, " Invoke", " " + name + ".Invoke") << ";\nvar\n  _c: TRosettaCall;\n  _r: TRosettaReply;\nbegin\n  _c.Init(0);\n"
             << "  _c.Arg;\n  _c.W.Holder(RosettaHolder);\n";
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto value = "A" + std::to_string(i + 1);
            if (args[i].object) body << "  _c.ObjArg(" << value << ");\n";
            else body << "  _c.Arg;\n  " << apply(args[i].put, "_c.W", value) << ";\n";
        }
        body << "  _c.Run(_r);\n";
        if (returns) body << "  Result := " << apply(result.to, "", "_r.Value") << ";\n";
        body << "end;\n";
        native_bodies_.push_back(body.str());
    }
    return {name, "$W.Obj($V)", name + "(RosettaToFunction($V, " + name + "))", native_mangle, false, false};
}

PT Generator::pascal_type(const Type& type, bool parameter) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return scalar("String", "$W.Str($V)", "RosettaToString($V)");
        if (is_void(pointee)) return {};
        return optional_of(pascal_type(pointee, parameter));
    }
    if (is_void(target)) return {"", "$W.Nil_", "", "Nothing", false, false};
    if (!conversion(target).empty()) return pascal_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        const auto spelled = normalize(strip_cv(target.spelling));
        if (spelled == "size_t" || spelled == "std::size_t" || spelled == "ssize_t" || spelled == "ptrdiff_t" || spelled == "std::ptrdiff_t")
            return scalar("Int64", "$W.Int($V)", "RosettaToInt64($V)");
        static const std::map<std::string, std::string> fixed = {{"int8_t", "Int8"}, {"int16_t", "Int16"}, {"int32_t", "Int32"},
            {"int64_t", "Int64"}, {"uint8_t", "UInt8"}, {"uint16_t", "UInt16"}, {"uint32_t", "UInt32"}, {"uint64_t", "UInt64"}};
        auto found = fixed.find(starts(spelled, "std::") ? spelled.substr(5) : spelled);
        std::string name;
        if (found != fixed.end()) name = found->second;
        else {
            static const std::map<std::string, std::string> builtins = {{"bool", "Boolean"}, {"char", "AnsiChar"}, {"signed char", "Int8"},
                {"unsigned char", "UInt8"}, {"short", "Int16"}, {"unsigned short", "UInt16"}, {"int", "Int32"}, {"unsigned int", "UInt32"},
                {"long", "Int64"}, {"unsigned long", "UInt64"}, {"long long", "Int64"}, {"unsigned long long", "UInt64"}, {"float", "Single"},
                {"double", "Double"}, {"long double", "Double"}, {"char16_t", "UInt16"}, {"char32_t", "UInt32"}, {"wchar_t", "Int32"}};
            const auto builtin = builtins.find(bare);
            if (builtin == builtins.end()) return {};
            name = builtin->second;
        }
        if (name == "Boolean") return scalar(name, "$W.Bool($V)", "RosettaToBool($V)");
        if (name == "AnsiChar") return scalar(name, "$W.Char_($V)", "RosettaToChar($V)");
        if (name == "Single" || name == "Double") return scalar(name, "$W.Real($V)", "RosettaTo" + name + "($V)");
        if (name == "UInt64") return scalar(name, "$W.UInt($V)", "RosettaToUInt64($V)");
        return scalar(name, "$W.Int($V)", "RosettaTo" + name + "($V)");
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end() || enum_int64_.count(target.declaration)) return scalar("Int64", "$W.Int($V)", "RosettaToInt64($V)");
        return {found->second, "$W.Int(Ord($V))", found->second + "(RosettaToInt64($V))", found->second.substr(1), false, false};
    }
    if (bound(target)) return class_type(classes_.at(target.declaration).name);
    if (bare == "std::filesystem::path") return scalar("String", "$W.Str($V)", "RosettaToString($V)");
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? pascal_type(target.arguments[i], parameter) : PT{}; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return scalar("String", "$W.Str($V)", "RosettaToString($V)");
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span" || declaration == "std::set" || declaration == "std::unordered_set" ||
            declaration == "std::multiset" || declaration == "std::unordered_multiset")
            return array_of(argument(0));
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" || declaration == "std::unordered_multimap")
            return map_of(argument(0), argument(1));
        if (declaration == "std::optional") return target.arguments.empty() ? PT{} : optional_of(argument(0));
        if (declaration == "std::complex") return {"TNativeComplex", "$W.Complex($V)", "RosettaToComplex($V)", "Complex", false, false};
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return target.arguments.empty() ? PT{} : argument(0);
        if ((declaration == "std::pair" || declaration == "std::tuple") && target.arguments.size() >= 2 && target.arguments.size() <= 6) {
            std::vector<PT> items;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) items.push_back(argument(i));
            return tuple_of(items, declaration == "std::pair");
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            if (function.arguments.size() > 5) return {};
            std::vector<PT> args;
            // A function inside a function type is the other direction's: no Pascal type.
            for (std::size_t i = 1; i < function.arguments.size(); ++i) {
                auto item = pascal_type(function.arguments[i], !parameter);
                if (item.callback || starts(item.mangle, "Native")) return {};
                if (writeback(function.arguments[i])) return {};
                args.push_back(item);
            }
            const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
            auto result = returns ? pascal_type(function.arguments[0], parameter) : PT{};
            if (result.callback || starts(result.mangle, "Native")) return {};
            return function_of(args, result, returns, parameter);
        }
        if (declaration == "std::chrono::duration") return scalar("Double", "$W.Real($V)", "RosettaToDouble($V)");
    }
    return {};
}

// A type a policy conversion spells in C++.
PT Generator::pascal_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::map<std::string, std::string> scalars = {{"bool", "Boolean"}, {"char", "AnsiChar"}, {"signed char", "Int8"},
        {"unsigned char", "UInt8"}, {"short", "Int16"}, {"unsigned short", "UInt16"}, {"int", "Int32"}, {"unsigned int", "UInt32"}, {"long", "Int64"},
        {"unsigned long", "UInt64"}, {"long long", "Int64"}, {"unsigned long long", "UInt64"}, {"float", "Single"}, {"double", "Double"},
        {"long double", "Double"}, {"std::size_t", "Int64"}, {"size_t", "Int64"}, {"std::int64_t", "Int64"}, {"std::uint64_t", "UInt64"},
        {"std::int32_t", "Int32"}, {"std::uint32_t", "UInt32"}, {"std::int16_t", "Int16"}, {"std::uint16_t", "UInt16"}, {"std::int8_t", "Int8"},
        {"std::uint8_t", "UInt8"}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) {
        const auto& name = found->second;
        if (name == "Boolean") return scalar(name, "$W.Bool($V)", "RosettaToBool($V)");
        if (name == "AnsiChar") return scalar(name, "$W.Char_($V)", "RosettaToChar($V)");
        if (name == "Single" || name == "Double") return scalar(name, "$W.Real($V)", "RosettaTo" + name + "($V)");
        if (name == "UInt64") return scalar(name, "$W.UInt($V)", "RosettaToUInt64($V)");
        return scalar(name, "$W.Int($V)", "RosettaTo" + name + "($V)");
    }
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return scalar("String", "$W.Str($V)", "RosettaToString($V)");
    for (const auto& [qualified, plan] : classes_)
        if (normalize(qualified) == cpp) return class_type(plan.name);
    for (const auto& [qualified, name] : enum_names_)
        if (normalize(qualified) == cpp && !enum_int64_.count(qualified))
            return {name, "$W.Int(Ord($V))", name + "(RosettaToInt64($V))", name.substr(1), false, false};
    return {};
}

std::string Generator::pascal_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "True" : "False";
    if (value.kind == "null") return "nil";
    if (value.kind == "char") return value.value.size() == 1 ? pascal_string(value.value) : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos)
            return pascal_string(text.substr(1, text.size() - 2));
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        const auto found = enum_names_.find(qualified);
        if (found == enum_names_.end() || !enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value && !enum_cases_.at(qualified)[i].empty())
                return found->second + "." + ident(enum_cases_.at(qualified)[i]);
    }
    return {};
}

std::string Generator::overload_signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& type = function.parameters[i];
        auto text = std::string(writeback(type) ? "var " : "") + names[i - first] + ": " + pascal_type(type, true).type;
        if (i >= function.parameters.size() - function.default_arguments) {
            const auto shown = pascal_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, type);
            text += " = " + (shown.empty() ? std::string("<C++ default>") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + "(" + join(parameters, "; ") + ")";
    if (!function.is_constructor && !is_void(function.result)) result += ": " + pascal_type(function.result, false).type;
    return result;
}

std::vector<Variant> Generator::plan_variants(const std::vector<Overload>& overloads) const {
    std::vector<Variant> result;
    std::set<std::string> keys;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto names = parameter_names(overload);
        const std::size_t total = function.parameters.size();
        const std::size_t required = total - std::min(function.default_arguments, total - first);
        for (std::size_t arity = required; arity <= total; ++arity) {
            Variant variant;
            variant.overload = &overload;
            variant.arity = arity;
            for (std::size_t i = first; i < arity; ++i) {
                const auto& type = function.parameters[i];
                variant.params.push_back(pascal_type(type, true));
                variant.names.push_back(names[i - first]);
                variant.var.push_back(writeback(type) && !function.is_constructor);
                variant.key += (i > first ? ", " : "") + fold(variant.params.back().type);
            }
            variant.returns = !function.is_constructor && !is_void(function.result);
            variant.result = variant.returns ? pascal_type(function.result, false) : PT{"", "", "", "Nothing", false, false};
            if (keys.insert(variant.key).second) result.push_back(std::move(variant));
        }
    }
    return result;
}

// ---------------------------------------------------------------- naming
// Whether a method of `owner` has the Pascal parameters of a base class's method of the same C++
// name but another result: Pascal could neither override nor overload it, so it takes its own name.
bool Generator::conflicts(const std::string& owner, const Group& group) const {
    std::map<std::string, std::string> mine;
    for (const auto& variant : plan_variants(group.overloads)) mine[variant.key] = variant.result.type;
    std::set<std::string> seen;
    std::function<bool(const std::string&)> walk = [&](const std::string& cls) {
        if (!seen.insert(cls).second) return false;
        for (const auto& base : classes_.at(cls).bases) {
            for (const auto& other : classes_.at(base).methods) {
                if (other.name != group.name || other.is_static != group.is_static) continue;
                for (const auto& variant : plan_variants(other.overloads)) {
                    const auto found = mine.find(variant.key);
                    if (found != mine.end() && found->second != variant.result.type) return true;
                }
            }
            if (walk(base)) return true;
        }
        return false;
    };
    return walk(owner);
}

void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        auto& mapped = mapped_[owner];
        // A C++ name a base class binds keeps its Pascal name (an override, an overload, or a further
        // base's member declared again).
        for (const auto& base : cls.bases) {
            if (!members_.count(base)) continue;
            for (const auto& name : members_.at(base).taken) members.taken.insert(name);
            for (const auto& [key, name] : mapped_.at(base)) mapped.emplace(key, name);
        }
        // TObject's and TNativeObject's members.
        for (const char* reserved : {"Create", "Destroy", "Free", "ClassName", "ClassType", "ClassParent", "ClassInfo", "ClassNameIs", "InstanceSize",
                                     "InheritsFrom", "Equals", "GetHashCode", "ToString", "Dispatch", "DispatchStr", "FieldAddress", "MethodAddress",
                                     "MethodName", "AfterConstruction", "BeforeDestruction", "NewInstance", "FreeInstance", "DefaultHandler",
                                     "DefaultHandlerStr", "GetInterface", "GetInterfaceByStr", "GetInterfaceEntry", "GetInterfaceEntryByStr",
                                     "GetInterfaceWeak", "GetInterfaceTable", "SafeCallException", "InitInstance", "CleanupInstance", "UnitName",
                                     "QualifiedClassName", "Close", "IsClosed", "Cast", "RosettaHolder", "RosettaClassIndex", "RosettaId",
                                     "RosettaDirector", "RosettaShared", "RosettaKey", "RosettaOwner", "RosettaOwned", "RosettaAdopt", "RosettaFind",
                                     "RosettaCreate", "RosettaWrap", "Result", "Self"})
            members.reserve(reserved);
        // operator[] is also the default array property Items.
        for (const auto& group : cls.operators)
            if (group.name == "At") members.reserve("Items");
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
        for (auto& group : cls.methods) {
            const auto key = "m:" + group.name;
            // to_string() is TObject's ToString; its other argument counts (default arguments, other
            // overloads) are ToString overloads.
            bool to_string = false;
            if (group.name == "to_string" && !group.is_static)
                for (const auto& variant : plan_variants(group.overloads))
                    to_string = to_string || (variant.params.empty() && variant.result.type == "String");
            if (to_string) names_[&group] = mapped[key] = "ToString";
            else if (mapped.count(key) && conflicts(owner, group)) names_[&group] = mapped[key] = members.claim(pascal(group.name));
            else names_[&group] = name_member(key, pascal(group.name));
        }
        for (auto& group : cls.operators) names_[&group] = name_member("o:" + group.name, group.name);
        for (const auto& field : cls.fields) field_names_[owner + "::" + field.field->name] = name_member("f:" + field.field->name, pascal(field.name));
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(pascal(constant.name));
        for (std::size_t i = 1; i < cls.bases.size(); ++i) {
            auto base_name = classes_.at(cls.bases[i]).name.substr(1);
            if (!base_name.empty() && base_name.back() == '_') base_name.pop_back();
            secondary_names_[owner + "::" + cls.bases[i]] = name_member("a:" + cls.bases[i], "As" + base_name);
        }
    }
    for (auto& group : functions) {
        auto& scope = group.module.empty() ? globals_ : modules_[group.module];
        names_[&group] = scope.claim(pascal(group.name));
    }
    for (auto& constant : constants) constant_names_[&constant] = globals_.claim(pascal(constant.name));
}

// ---------------------------------------------------------------- documentation
Doc Generator::describe(const Documentation& doc, const std::string& fallback) const {
    Doc result;
    std::vector<std::string> details;
    for (const auto& block : doc.details) details.push_back(doc_block(block));
    if (!doc.brief.empty()) result.summary = inline_xml(one_line(doc.brief));
    else if (!details.empty() && doc.details.front().kind == "paragraph") {
        result.summary = inline_xml(one_line(doc.details.front().text));
        details.erase(details.begin());
    } else result.summary = fallback;
    result.remarks = details;
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        result.remarks.push_back("<para><b>" + xml(label) + ":</b> " + inline_xml(one_line(item.text)) + "</para>");
    }
    if (!doc.see.empty()) {
        std::string see = "<para>See also: ";
        for (std::size_t i = 0; i < doc.see.size(); ++i) see += (i ? ", " : "") + std::string("<c>") + xml(doc.see[i]) + "</c>";
        result.remarks.push_back(see + ".</para>");
    }
    for (const auto& item : doc.throws)
        result.exceptions.push_back("Kind <c>" + error_kind(item.name) + "</c>" + (item.name.empty() ? std::string() : " (C++ <c>" + xml(item.name) + "</c>)") +
                                    (item.text.empty() ? std::string() : ": " + inline_xml(one_line(item.text))));
    return result;
}

Doc Generator::variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const auto& doc = function.documentation;
    auto result = describe(doc, "Calls " + what + ".");
    if (!doc.brief.empty() || !doc.details.empty()) result.remarks.insert(result.remarks.begin(), "<para>Calls " + what + ".</para>");
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> omitted;
    for (std::size_t i = variant.arity; i < function.parameters.size(); ++i) {
        if (i < first) continue;
        const auto shown = pascal_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        const auto cpp = i < function.default_values.size() ? function.default_values[i].value : std::string();
        omitted.push_back("<c>" + xml(names[i - first] + " = " + (shown.empty() ? cpp.empty() ? std::string("C++ default") : cpp : shown)) + "</c>");
    }
    if (!omitted.empty()) {
        std::size_t longer = 0;
        for (const auto& sibling : siblings) if (sibling.overload == variant.overload && sibling.arity > variant.arity) ++longer;
        result.remarks.push_back("<para>Leaves out the C++ default arguments " + join(omitted, ", ") + (longer ? " (other overloads pass them)." : ".") + "</para>");
    }
    for (std::size_t i = first; i < variant.arity; ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : doc.params) if (item.name == cpp && !item.text.empty()) text = inline_xml(one_line(item.text));
        if (variant.var[i - first]) text += std::string(text.empty() ? "" : " ") + "(C++ writes it back)";
        result.params.emplace_back(variant.names[i - first], text.empty() ? "A <c>" + xml(variant.params[i - first].type) + "</c>." : text);
    }
    if (variant.returns && !doc.returns.empty()) result.returns = inline_xml(one_line(doc.returns));
    return result;
}

// ---------------------------------------------------------------- emission
// The parameter list; `renamed` gets the names, renamed where they meet a member name in `avoid`.
std::string Generator::parameter_list(const Variant& variant, const std::set<std::string>& avoid, std::vector<std::string>* renamed) const {
    std::vector<std::string> items;
    Names taken;
    for (const auto& name : avoid) taken.taken.insert(name);
    for (std::size_t i = 0; i < variant.params.size(); ++i) {
        auto name = variant.names[i];
        if (!taken.free(name)) name = taken.claim(name);
        else taken.reserve(name);
        if (renamed) renamed->push_back(name);
        const auto& type = variant.params[i];
        const std::string mode = variant.var[i] ? "var " : (type.object || type.callback || type.type == "Boolean" || type.type == "AnsiChar" ||
                                                             type.type == "Double" || type.type == "Single" || starts(type.type, "Int") ||
                                                             starts(type.type, "UInt") || is_enum(type.type))
                                                                ? ""
                                                                : "const ";
        items.push_back(mode + ident(name) + ": " + type.type);
    }
    return items.empty() ? std::string() : "(" + join(items, "; ") + ")";
}

// The body of a routine calling native function `index` (with `receiver` first, unless empty).
void Generator::emit_body(std::ostream& out, const Variant& variant, const std::vector<std::string>& names, std::size_t index,
                          const std::string& receiver, const std::string& result_target) const {
    bool writebacks = false;
    for (const bool var : variant.var) writebacks = writebacks || var;
    out << "var\n  _c: TRosettaCall;\n  _r: TRosettaReply;\n" << (writebacks ? "  _v: TRosettaValue;\n" : "") << "begin\n  _c.Init(" << index << ");\n";
    if (!receiver.empty()) out << "  _c.ObjArg(" << receiver << ");\n";
    std::size_t slot = 0;
    for (std::size_t i = 0; i < variant.params.size(); ++i) {
        const auto& type = variant.params[i];
        const auto name = ident(names[i]);
        if (type.object && !variant.var[i]) {
            out << "  _c.ObjArg(" << name << ");\n";
            continue;
        }
        out << "  _c.Arg;\n";
        if (variant.var[i]) out << "  _c.Slot(" << slot++ << ");\n";
        out << "  " << apply(type.put, "_c.W", name) << ";\n";
    }
    out << "  _c.Run(_r);\n";
    if (variant.returns) out << "  " << result_target << " := " << apply(variant.result.to, "", "_r.Value") << ";\n";
    slot = 0;
    for (std::size_t i = 0; i < variant.params.size(); ++i)
        if (variant.var[i]) out << "  if _r.Find(" << slot++ << ", _v) then " << ident(names[i]) << " := " << apply(variant.params[i].to, "", "_v") << ";\n";
    out << "end;\n";
}

// A module function, or a function of a policy module (qualifier "Data." and modifiers "class ... static").
void Generator::emit_routine(std::ostream& decl, std::ostream& body, const Group& group, const std::string& indent, const std::string& qualifier,
                             const std::string& modifiers, const std::set<std::string>& avoid) const {
    const auto variants = plan_variants(group.overloads);
    const auto& name = names_.at(&group);
    for (const auto& variant : variants) {
        const auto& function = *variant.overload->function;
        std::vector<std::string> names;
        const auto params = parameter_list(variant, avoid, &names);
        auto doc = variant_doc(variant, variants, "C++ <c>" + xml(function.qualified_name()) + "</c>");
        for (std::size_t i = 0; i < names.size() && i < doc.params.size(); ++i) doc.params[i].first = names[i];
        const auto kind = std::string(variant.returns ? "function " : "procedure ");
        const auto result = variant.returns ? ": " + variant.result.type : std::string();
        decl << doc.render(indent) << indent << (modifiers.empty() ? "" : "class ") << kind << ident(name) << params << result << "; "
             << (modifiers.empty() ? "" : "static; ") << "overload;\n";
        body << "\n" << (modifiers.empty() ? "" : "class ") << kind << qualifier << ident(name) << params << result << ";\n";
        emit_body(body, variant, names, group.index, "", "Result");
    }
}

void Generator::emit_members(std::ostream& decl, std::ostream& body, const std::string& owner, const std::string& from, bool copied) {
    const auto& plan = classes_.at(owner);
    const auto& source = classes_.at(from);
    const bool open = plan.director && !plan.record->is_final;
    auto& emitted = emitted_[owner];
    std::set<std::string> avoid = members_.at(owner).taken;
    const auto inherited = [&](const std::string& key) -> const Emitted* {
        const auto found = emitted.find(key);
        return found == emitted.end() ? nullptr : &found->second;
    };
    const auto emit_group = [&](const Group& group, bool is_operator) {
        if (copied && group.is_static) return;
        if (is_operator && group.operand != 0) return;
        const auto& name = names_.at(&group);
        const auto variants = plan_variants(group.overloads);
        for (const auto& variant : variants) {
            const auto& function = *variant.overload->function;
            const auto key = std::string(group.is_static ? "static " : "") + fold(name) + "(" + variant.key + ")";
            const auto* previous = inherited(key);
            if (previous && (copied || previous->result != variant.result.type)) continue;
            // A method a base declares virtual is an override in every subclass, sealed ones included.
            const bool overrides = previous && previous->is_virtual && !group.is_static;
            const bool virtual_method = overrides || (!group.is_static && function.is_virtual && !variant.overload->extension && open);
            const bool to_string = name == "ToString" && variant.params.empty();
            std::string what = variant.overload->extension ? "C++ <c>" + xml(function.qualified_name()) + "</c>"
                                                           : "the C++ method <c>" + xml(source.record->qualified_name() + "::" + function.name) + "</c>";
            auto doc = variant_doc(variant, variants, what);
            if (virtual_method && open && !copied)
                doc.remarks.push_back(std::string("<para>") + (function.is_pure ? "Pure virtual in C++: a" : "A") +
                                      " Pascal subclass overrides it, and C++ calls the override.</para>");
            std::vector<std::string> names;
            const auto params = parameter_list(variant, avoid, &names);
            for (std::size_t i = 0; i < names.size() && i < doc.params.size(); ++i) doc.params[i].first = names[i];
            const auto kind = std::string(variant.returns ? "function " : "procedure ");
            const auto result = variant.returns ? ": " + (to_string ? std::string("AnsiString") : variant.result.type) : std::string();
            std::string directives;
            if (to_string) directives = " override;";
            else {
                directives = " overload;";
                if (group.is_static) directives += " static;";
                else if (overrides) directives += " override;";
                else if (virtual_method) directives += " virtual;";
            }
            decl << doc.render("    ") << "    " << (group.is_static ? "class " : "") << kind << ident(name) << params << result << ";" << directives << "\n";
            body << "\n" << (group.is_static ? "class " : "") << kind << plan.name << "." << ident(name) << params << result << ";\n";
            emit_body(body, variant, names, group.index, group.is_static ? "" : "Self", "Result");
            emitted[key] = {variant.result.type, virtual_method || to_string};
            // operator[] is also the default array property.
            if (is_operator && group.name == "At" && !copied && items_names_[owner].empty() && variant.returns && variant.params.size() == 1 &&
                !variant.var[0] && !inherited("items")) {
                items_names_[owner] = "Items";
                decl << "    /// <summary><c>" << ident(name) << "</c> as the default array property (<c>X[I]</c>).</summary>\n    property Items["
                     << ident(names[0]) << ": " << variant.params[0].type << "]: " << variant.result.type << " read " << ident(name) << "; default;\n";
                emitted["items"] = {variant.result.type, false};
            }
        }
    };
    for (const auto& group : source.methods) emit_group(group, false);
    for (const auto& group : source.operators) emit_group(group, true);
    // Fields are properties.
    for (const auto& field : source.fields) {
        const auto& f = *field.field;
        const auto& name = field_names_.at(from + "::" + f.name);
        const auto key = "property " + fold(name);
        if (inherited(key)) continue;
        const bool writable = !f.type.is_const && !readonly(from + "::" + f.name, from) && f.type.kind != TypeKind::lvalue_reference;
        const auto type = pascal_type(f.type, false);
        const auto getter = "_Get" + std::to_string(field.getter_index);
        const auto setter = "_Set" + std::to_string(field.setter_index);
        const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        // Members of an object (not pointers) keep their address: one Pascal object for each.
        const bool member = type.object && referent(f.type).kind != TypeKind::pointer && f.type.kind != TypeKind::pointer;
        decl << "  private\n    function " << getter << ": " << type.type << ";\n";
        if (writable) decl << "    procedure " << setter << "(const AValue: " << type.type << ");\n";
        decl << "  public\n    /// <summary>" << (about.empty() ? "The C++ field <c>" + xml(from + "::" + f.name) + "</c>" : inline_xml(about))
             << (writable ? "" : " (read-only)") << (member ? "; the object belongs to this one" : "") << ".</summary>\n    property " << ident(name) << ": "
             << type.type << " read " << getter << (writable ? " write " + setter : std::string()) << ";\n";
        body << "\nfunction " << plan.name << "." << getter << ": " << type.type << ";\nvar\n  _c: TRosettaCall;\n  _r: TRosettaReply;\nbegin\n";
        if (member) body << "  Result := " << type.type << "(RosettaFind(" << field.getter_index << "));\n  if (Result <> nil) and not Result.IsClosed then Exit;\n";
        body << "  _c.Init(" << field.getter_index << ");\n  _c.ObjArg(Self);\n  _c.Run(_r);\n";
        if (member) {
            body << "  if Result <> nil then Result.Free;\n  if _r.Value.Tag <> 10 then\n  begin\n    Result := " << apply(type.to, "", "_r.Value")
                 << ";\n    Exit;\n  end;\n  Result := " << type.type << "(" << type.type << ".RosettaWrap(_r.Value.Holder, _r.Value.ClassIndex));\n"
                 << "  RosettaAdopt(Result, " << field.getter_index << ");\n";
        } else {
            body << "  Result := " << apply(type.to, "", "_r.Value") << ";\n";
        }
        body << "end;\n";
        if (writable) {
            body << "\nprocedure " << plan.name << "." << setter << "(const AValue: " << type.type << ");\nvar\n  _c: TRosettaCall;\n  _r: TRosettaReply;\n"
                 << "begin\n  _c.Init(" << field.setter_index << ");\n  _c.ObjArg(Self);\n";
            if (type.object) body << "  _c.ObjArg(AValue);\n";
            else body << "  _c.Arg;\n  " << apply(type.put, "_c.W", "AValue") << ";\n";
            body << "  _c.Run(_r);\nend;\n";
        }
        emitted[key] = {type.type, false};
    }
}

void Generator::emit_class(std::ostream& decl, std::ostream& body, const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto primary = plan.bases.empty() ? std::string() : plan.bases.front();
    const auto parent = primary.empty() ? std::string("TNativeObject") : classes_.at(primary).name;
    if (!primary.empty()) emitted_[owner] = emitted_[primary];
    auto doc = describe(record.documentation, "Wraps the C++ class <c>" + xml(owner) + "</c>.");
    if (!record.documentation.brief.empty() || !record.documentation.details.empty())
        doc.remarks.insert(doc.remarks.begin(), "<para>Wraps the C++ class <c>" + xml(owner) + "</c>.</para>");
    if (plan.bases.size() > 1) {
        std::vector<std::string> further;
        for (std::size_t i = 1; i < plan.bases.size(); ++i)
            further.push_back("<see cref=\"" + classes_.at(plan.bases[i]).name + "\"/> (<c>" + secondary_names_.at(owner + "::" + plan.bases[i]) + "</c>)");
        doc.remarks.push_back("<para>Its further C++ base classes' members are declared again here; pass it where one of them is expected through "
                              "its <c>As</c> function: " + join(further, ", ") + ".</para>");
    }
    if (plan.director && !record.is_final)
        doc.remarks.push_back(std::string("<para>") + (record.abstract ? "Abstract in C++: subclass" : "Subclass") +
                              " it in Pascal to override its virtual methods: the object of a Pascal subclass is a C++ subclass whose virtual "
                              "methods call the Pascal overrides, and <c>inherited</c> runs the C++ implementation.</para>");
    else if (record.abstract) doc.remarks.push_back("<para>Abstract in C++: C++ functions return objects of derived classes.</para>");
    decl << "\n" << doc.render("  ") << "  " << plan.name << " = class" << (record.is_final ? " sealed" : "") << "(" << parent << ")\n  public\n";
    const std::set<std::string> avoid = members_.at(owner).taken;
    // Constructors.
    const auto overloads = constructor_overloads_.count(owner) ? constructor_overloads_.at(owner) : std::vector<Overload>{};
    const auto variants = plan_variants(overloads);
    for (const auto& variant : variants) {
        std::vector<std::string> names;
        const auto params = parameter_list(variant, avoid, &names);
        auto about = variant_doc(variant, variants, "the C++ constructor of <c>" + xml(owner) + "</c>");
        for (std::size_t i = 0; i < names.size() && i < about.params.size(); ++i) about.params[i].first = names[i];
        if (plan.director && !record.is_final)
            about.remarks.push_back("<para>The object of a Pascal subclass is a C++ subclass whose virtual methods call the Pascal overrides.</para>");
        decl << about.render("    ") << "    constructor Create" << params << "; overload;\n";
        body << "\nconstructor " << plan.name << ".Create" << params << ";\nvar\n  _c: TRosettaCall;\nbegin\n  inherited RosettaCreate;\n"
             << "  RosettaConstruct(Self, _c, " << plan.constructor_index << ", "
             << (plan.director && !record.is_final ? "ClassType <> " + plan.name : std::string("False")) << ");\n";
        for (std::size_t i = 0; i < variant.params.size(); ++i) {
            if (variant.params[i].object) body << "  _c.ObjArg(" << ident(names[i]) << ");\n";
            else body << "  _c.Arg;\n  " << apply(variant.params[i].put, "_c.W", ident(names[i])) << ";\n";
        }
        body << "  RosettaFinish(Self, _c);\nend;\n";
    }
    if (variants.empty()) {
        decl << "    /// <summary>Raises <see cref=\"ENativeError\"/>: C++ does not construct a <c>" << xml(owner)
             << "</c> (functions return them).</summary>\n    constructor Create; overload;\n";
        body << "\nconstructor " << plan.name << ".Create;\nbegin\n  inherited RosettaCreate;\n  RosettaFail(TNativeErrorKind.Binding, "
             << pascal_string(record.abstract ? abstract_message(plan) : plan.name + " has no public constructor") << ");\nend;\n";
    }
    emit_members(decl, body, owner, owner, false);
    // Further bases: their members again, and AsBase.
    std::set<std::string> primary_chain;
    std::function<void(const std::string&)> collect = [&](const std::string& cls) {
        if (!primary_chain.insert(cls).second) return;
        for (const auto& base : classes_.at(cls).bases) collect(base);
    };
    if (!primary.empty()) collect(primary);
    std::set<std::string> done = primary_chain;
    std::function<void(const std::string&)> copy = [&](const std::string& cls) {
        if (!done.insert(cls).second) return;
        decl << "  public\n";
        emit_members(decl, body, owner, cls, true);
        for (const auto& base : classes_.at(cls).bases) copy(base);
    };
    for (std::size_t i = 1; i < plan.bases.size(); ++i) {
        const auto& base = plan.bases[i];
        const auto& name = classes_.at(base).name;
        const auto& as = secondary_names_.at(owner + "::" + base);
        decl << "  public\n    /// <summary>This object as its further C++ base class <see cref=\"" << name
             << "\"/>; the result belongs to this object.</summary>\n    function " << as << ": " << name << ";\n";
        body << "\nfunction " << plan.name << "." << as << ": " << name << ";\nbegin\n  Result := " << name << "(Cast(" << name << "));\nend;\n";
        copy(base);
    }
    if (!plan.constants.empty()) decl << "  public\n";
    for (const auto& constant : plan.constants) {
        const auto type = pascal_type(*constant.type, false);
        auto about = describe(*constant.documentation, "The C++ constant <c>" + xml(constant.expression) + "</c>.");
        const auto& name = constant_names_.at(&constant);
        decl << about.render("    ") << "    class function " << ident(name) << ": " << type.type << "; static;\n";
        body << "\nclass function " << plan.name << "." << ident(name) << ": " << type.type << ";\nvar\n  _c: TRosettaCall;\n  _r: TRosettaReply;\nbegin\n"
             << "  _c.Init(" << constant.index << ");\n  _c.Run(_r);\n  Result := " << apply(type.to, "", "_r.Value") << ";\nend;\n";
    }
    decl << "  end;\n";
}

void Generator::emit_enum(std::ostream& decl, std::ostream& helpers, std::ostream& body, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto doc = describe(enumeration.documentation, "The C++ enumeration <c>" + xml(qualified) + "</c>.");
    doc.remarks.push_back("<para>A scoped enumeration (<c>" + name + "." + (cases.empty() ? std::string("X") : ident(cases.front())) +
                          "</c>); other values of the C++ type are casts (<c>" + name + "(42)</c>), and <c>ToString</c> names a value.</para>");
    decl << "\n" << doc.render("  ") << "  " << name << " = (";
    // Pascal enumerators ascend and have distinct values; aliases of a value are left out.
    std::vector<std::pair<long long, std::size_t>> order;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
        if (!cases[i].empty()) order.emplace_back(std::stoll(enumeration.enumerator_values[i]), i);
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t k = 0; k < order.size(); ++k) {
        const auto i = order[k].second;
        decl << (k ? "," : "") << "\n    /// <summary>C++ <c>" << xml(qualified + "::" + enumeration.enumerators[i]) << "</c>.</summary>\n    " << ident(cases[i])
             << " = " << order[k].first;
    }
    decl << ");\n";
    helpers << "\n  /// <summary>Names a <see cref=\"" << name << "\"/> value.</summary>\n  " << name << "Helper = type helper for " << name
            << "\n    /// <summary>The enumerator's name, else the value.</summary>\n    function ToString: String;\n  end;\n";
    body << "\nfunction " << name << "Helper.ToString: String;\nbegin\n  case Ord(Self) of\n";
    for (const auto& [value, i] : order) body << "    " << value << ": Result := " << pascal_string(cases[i]) << ";\n";
    body << "  else\n    Result := IntToStr(Ord(Self));\n  end;\nend;\n";
}

// The routine C++ calls a director's virtual methods through: the Pascal method of the object.
std::string Generator::dispatch() {
    std::ostringstream entries, chain;
    std::size_t count = 0;
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director || plan.record->is_final) continue;
        for (const auto& item : overridable(plan)) {
            const auto generic = generic_index_.find(item.name);
            const auto name = mapped_.at(owner).find("m:" + item.name);
            if (generic == generic_index_.end() || name == mapped_.at(owner).end()) continue;
            const auto& method = *item.method;
            std::vector<std::string> types, arguments, locals, conversions;
            bool supported = true;
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                const auto& type = method.parameters[i];
                const auto pt = pascal_type(type, true);
                if (pt.callback) supported = false;
                types.push_back(fold(pt.type));
                locals.push_back("  _a" + std::to_string(i) + ": " + pt.type + ";\n");
                conversions.push_back("  _a" + std::to_string(i) + " := " + apply(pt.callback ? "nil" : pt.to, "", "Args[" + std::to_string(i) + "]") + ";\n");
                arguments.push_back("_a" + std::to_string(i));
            }
            const auto key = fold(name->second) + "(" + join(types, ", ") + ")";
            if (!supported || !emitted_[owner].count(key)) continue;
            const bool returns = !is_void(method.result);
            const auto entry = "_Dispatch" + std::to_string(count++);
            entries << "\nprocedure " << entry << "(Obj: " << plan.name << "; const Args: TRosettaValues; var W: TRosettaWriter);\n";
            if (!locals.empty()) entries << "var\n" << join(locals, "");
            entries << "begin\n" << join(conversions, "");
            const auto call = "Obj." + ident(name->second) + (arguments.empty() ? std::string() : "(" + join(arguments, ", ") + ")");
            if (returns) entries << "  " << apply(pascal_type(method.result, false).put, "W", call) << ";\n";
            else entries << "  " << call << ";\n  W.Nil_;\n";
            entries << "end;\n";
            chain << "  if (Generic = " << generic->second << ") and (System.Length(Args) = " << method.parameters.size() << ") and (Obj is " << plan.name
                  << ") then\n  begin\n    " << entry << "(" << plan.name << "(Obj), Args, W);\n    Exit(True);\n  end;\n";
        }
    }
    return entries.str() + "\n// The Pascal methods C++ calls for the virtual methods of Pascal subclasses.\n"
                           "function RosettaDispatch(Obj: TNativeObject; Generic: LongWord; const Args: TRosettaValues; var W: TRosettaWriter): Boolean;\n"
                           "begin\n" + chain.str() + "  Result := False;\nend;\n";
}

GeneratedPascalSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    unit_ = options_.unit.empty() ? name_ : options_.unit;
    if (!identifier(unit_) || pascal_keyword(unit_) || runtime_names().count(fold(unit_)))
        throw BindingError("Pascal unit name must be an identifier and not a Pascal keyword: " + unit_);
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Pascal library name must be a plain file name: " + library_);
    GeneratedPascalSource result;
    prepare();
    for (const auto& name : runtime_names()) globals_.taken.insert(name);
    globals_.reserve(unit_);
    helper_scope_ = &globals_;
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = globals_.claim_type("T" + pascal(plan.name));
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name.substr(1) + "_" + name;
        enum_names_[qualified] = globals_.claim_type("T" + pascal(name));
        globals_.reserve(enum_names_[qualified] + "Helper");
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        Names scope;
        scope.reserve("Self");
        std::set<long long> values;
        bool fits = true;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto& text = enumeration.enumerator_values[i];
            long long value = 0;
            try { value = std::stoll(text); } catch (...) { fits = false; break; }
            if (value < -2147483648LL || value > 2147483647LL) fits = false;
            // An alias of an earlier value has no Pascal enumerator.
            enum_cases_[qualified].push_back(values.insert(value).second ? scope.claim(pascal(renamed(qualified + "::" + enumeration.enumerators[i],
                                                                                                    enumeration.enumerators[i])))
                                                                         : std::string());
        }
        if (!fits) enum_int64_.insert(qualified);
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Policy modules are classes of class functions (a.b nests).
    for (const auto& group : functions) {
        if (group.module.empty() || submodules_.count(group.module)) continue;
        const auto dot = group.module.find('.');
        std::string name;
        for (const auto& [module, path] : submodules_)
            if (module.substr(0, module.find('.')) == group.module.substr(0, dot)) name = path.substr(0, path.find('.'));
        if (name.empty()) name = globals_.claim_type(pascal(group.module.substr(0, dot)));
        std::size_t start = dot;
        while (start != std::string::npos) {
            const auto next = group.module.find('.', start + 1);
            name += "." + pascal(group.module.substr(start + 1, next == std::string::npos ? std::string::npos : next - start - 1));
            start = next;
        }
        submodules_[group.module] = name;
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Pascal unit " + unit_ + " (" + name_ + ").");

    // ------------------------------------------------------------ the unit
    std::ostringstream classes_decl, classes_body, enums_decl, helpers_decl, enums_body, functions_decl, functions_body, modules_decl, modules_body;
    for (const auto& [qualified, name] : enum_names_)
        if (!enum_int64_.count(qualified)) emit_enum(enums_decl, helpers_decl, enums_body, qualified);
    for (const auto& owner : class_order_) emit_class(classes_decl, classes_body, owner);
    for (const auto& group : functions)
        if (group.module.empty()) emit_routine(functions_decl, functions_body, group, "", "", "", {});
    for (const auto& constant : constants) {
        const auto type = pascal_type(*constant.type, false);
        auto about = describe(*constant.documentation, "The C++ constant <c>" + xml(normalize(constant.expression)) + "</c>.");
        const auto& name = constant_names_.at(&constant);
        functions_decl << about.render("") << "function " << ident(name) << ": " << type.type << ";\n";
        functions_body << "\nfunction " << ident(name) << ": " << type.type << ";\nvar\n  _c: TRosettaCall;\n  _r: TRosettaReply;\nbegin\n  _c.Init("
                       << constant.index << ");\n  _c.Run(_r);\n  Result := " << apply(type.to, "", "_r.Value") << ";\nend;\n";
    }
    // Policy modules: nested classes.
    std::map<std::string, std::vector<std::string>> children;
    std::set<std::string> paths;
    for (const auto& [module, path] : submodules_) {
        std::string current;
        std::size_t start = 0;
        while (true) {
            const auto dot = path.find('.', start);
            const auto part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            const auto next = current.empty() ? part : current + "." + part;
            if (paths.insert(next).second) children[current].push_back(next);
            current = next;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }
    std::function<void(const std::string&, const std::string&)> emit_namespace = [&](const std::string& path, const std::string& indent) {
        std::string module, from;
        for (const auto& [m, p] : submodules_) if (p == path) module = m;
        for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) from = ns;
        const auto name = path.substr(path.rfind('.') == std::string::npos ? 0 : path.rfind('.') + 1);
        modules_decl << "\n" << indent << "/// <summary>" << (module.empty() ? std::string("Policy modules.")
                                                                             : "The functions of the C++ namespace <c>" + xml(from) + "</c> (policy module <c>" + xml(module) + "</c>).")
                     << "</summary>\n" << indent << name << " = class\n" << indent << "public\n";
        Names local;
        for (const auto& group : functions)
            if (!module.empty() && group.module == module) emit_routine(modules_decl, modules_body, group, indent + "  ", path + ".", "static", {});
        if (!children[path].empty()) {
            modules_decl << indent << "type\n";
            for (const auto& child : children[path]) emit_namespace(child, indent + "  ");
        }
        modules_decl << indent << "end;\n";
    };
    for (const auto& top : children[""]) emit_namespace(top, "  ");

    std::ostringstream unit;
    unit << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n//\n"
         << "// The Pascal interface to the C++ library " << name_ << " (Free Pascal 3.2+, objfpc mode). Each C++ class is\n"
         << "// a class over TNativeObject (TCircle.Create(2)), each module function a function, each C++ constant a\n"
         << "// function. Overloaded C++ functions are Pascal overloads, one per argument count for default\n"
         << "// arguments, which C++ resolves again at run time. Calls raise ENativeError for C++ exceptions and\n"
         << "// values C++ does not accept. Link with the native library " << library_ << ".\n"
         << "unit " << unit_ << ";\n\n{$mode objfpc}{$H+}\n{$modeswitch advancedrecords}\n{$modeswitch nestedprocvars}\n{$modeswitch typehelpers}\n"
         << "{$scopedenums on}\n{$minenumsize 4}\n\ninterface\n\nuses\n  SysUtils, Math;\n\n";
    for (const auto* part : detail::pascal_full_runtime_interface) unit << part;
    unit << "type" << enums_decl.str() << "\n";
    for (const auto& owner : class_order_) unit << "  " << classes_.at(owner).name << " = class;\n";
    for (const auto& text : type_decls_) unit << text;
    for (const auto& text : native_decls_) unit << text;
    unit << classes_decl.str() << helpers_decl.str() << modules_decl.str() << "\n" << functions_decl.str() << "\nimplementation\n\n";
    std::string runtime;
    for (const auto* part : detail::pascal_full_runtime_implementation) runtime += part;
    runtime = replace_all(replace_all(runtime, "@C@", "rosetta_" + name_), "@LIB@", library_);
    unit << runtime;
    if (!box_decls_.empty()) {
        unit << "\ntype\n";
        for (const auto& text : box_decls_) unit << text;
    }
    for (const auto& text : codec_decls_) unit << "\n" << text;
    for (const auto& text : native_bodies_) unit << "\n" << text;
    unit << enums_body.str() << classes_body.str() << functions_body.str() << modules_body.str() << dispatch();
    unit << "\n// The C++ classes: name, base classes (indexes) and Pascal class.\nprocedure RosettaRegisterClasses;\nbegin\n  SetLength(RosettaClasses, "
         << class_order_.size() << ");\n";
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& plan = classes_.at(class_order_[i]);
        std::vector<std::string> bases;
        for (const auto& base : plan.bases) {
            std::size_t at = 0;
            while (class_order_[at] != base) ++at;
            bases.push_back(std::to_string(at));
        }
        unit << "  RosettaClasses[" << i << "].Name := " << pascal_string(plan.name) << ";\n  RosettaClasses[" << i << "].Cls := " << plan.name << ";\n";
        if (!bases.empty()) {
            unit << "  SetLength(RosettaClasses[" << i << "].Bases, " << bases.size() << ");\n";
            for (std::size_t k = 0; k < bases.size(); ++k) unit << "  RosettaClasses[" << i << "].Bases[" << k << "] := " << bases[k] << ";\n";
        }
    }
    unit << "end;\n\ninitialization\n  RosettaRegisterClasses;\n  RosettaStart;\nend.\n";
    result.pascal = unit.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedPascalSource generate_pascal(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                      const PascalOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Pascal C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
