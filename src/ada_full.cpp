// Ada backend (--language ada): an Ada 2022 package (spec and body) over the shared C ABI library of
// the full C ABI backends (cabi_full.hpp, lisp_full_runtime.hpp).
//
// Each C++ class is a tagged type (Circle_Record, a private extension of Native_Object_Record) with
// an access type (Circle) that functions take and return; the first C++ base class is the parent
// type, and the members of further bases are declared again (C++ upcasts the object), with an
// As_Base function for passing the object where a further base is expected. Methods are primitive
// operations whose first parameter Self is the record (const methods in mode in, others in out), so
// prefix notation works on access values (C.Area). An Ada type extension overrides C++ virtual
// methods: constructing it (Construct_Shape) creates a C++ subclass whose virtual methods call the
// Ada operations (a view conversion to the parent runs the C++ implementation). Overloads and
// default arguments become Ada overloads (one per overload and argument count, so C++ applies its
// default arguments), resolved again at run time in C++. C++ exceptions raise the Ada exceptions of
// their kinds; enums are Ada enumerations with representation clauses, containers Ada.Containers
// instances and records, std::function access-to-subprogram types (and objects with a Call
// function as results), non-const reference parameters in out parameters, operators Ada operators
// where Ada has them, and to_string Image. Doxygen becomes GNATdoc comments.
//
// Ada names are case-insensitive: every scope claims them case-insensitively. Keywords take a _Kw
// suffix (names cannot end in an underscore in Ada).
#include "rosetta/ada.hpp"
#include "cabi_full.hpp"
#include "ada_full_runtime.hpp"
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

// Ada 2022's reserved words.
bool ada_keyword(const std::string& name) {
    static const std::set<std::string> words = {"abort", "abs", "abstract", "accept", "access", "aliased", "all", "and", "array", "at",
        "begin", "body", "case", "constant", "declare", "delay", "delta", "digits", "do", "else", "elsif", "end", "entry", "exception",
        "exit", "for", "function", "generic", "goto", "if", "in", "interface", "is", "limited", "loop", "mod", "new", "not", "null", "of",
        "or", "others", "out", "overriding", "package", "parallel", "pragma", "private", "procedure", "protected", "raise", "range",
        "record", "rem", "renames", "requeue", "return", "reverse", "select", "separate", "some", "subtype", "synchronized", "tagged",
        "task", "terminate", "then", "type", "until", "use", "when", "while", "with", "xor"};
    return words.count(fold(name)) != 0;
}
// Names the package declares itself, or that its code uses unqualified: a C++ name like one takes
// another name.
const std::set<std::string>& reserved_names() {
    static const std::set<std::string> words = [] {
        std::set<std::string> folded;
        for (const char* name : {"Standard", "Ada", "Interfaces", "System", "GNAT", "Boolean", "Integer", "Natural", "Positive", "Float",
                                 "Long_Float", "Short_Float", "Long_Long_Float", "Short_Integer", "Long_Integer", "Long_Long_Integer",
                                 "Short_Short_Integer", "Character", "Wide_Character", "Wide_Wide_Character", "String", "Wide_String",
                                 "Wide_Wide_String", "Duration", "True", "False", "ASCII", "Constraint_Error", "Program_Error", "Storage_Error",
                                 "Tasking_Error", "Numeric_Error", "Rosetta", "Native_Error", "Invalid_Argument_Error", "Out_Of_Range_Error",
                                 "Domain_Error", "Length_Error", "Arithmetic_Error", "Allocation_Error", "Binding_Error", "Callback_Error",
                                 "Native_Error_Kind", "Error_Kind", "Complex", "Native_Value", "No_Value", "Native_Object_Record", "Native_Object",
                                 "Native_Function_Record", "Rosetta_Dispatch"})
            folded.insert(fold(name));
        return folded;
    }();
    return words;
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
// Ada_Case: snake_case words, each capitalized.
std::string ada_case(const std::string& name) {
    auto result = snake_words(name);
    bool start = true;
    for (auto& c : result) {
        if (c == '_') { start = true; continue; }
        if (start) c = upper(c);
        start = false;
    }
    return result;
}
// An Ada string literal.
std::string ada_string(const std::string& text) {
    std::string result;
    bool open = false;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || byte >= 127) {
            if (open) { result += "\""; open = false; }
            result += (result.empty() ? "" : " & ") + std::string("Character'Val (") + std::to_string(byte) + ")";
            continue;
        }
        if (!open) { result += (result.empty() ? "\"" : " & \""); open = true; }
        result += c == '"' ? std::string("\"\"") : std::string(1, c);
    }
    if (open) result += "\"";
    return result.empty() ? "\"\"" : result;
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

// Names of the generated code: Rosetta, its locals and the components of Native_Object_Record (a
// numbered name is free), and its families of subprograms (Rosetta_Get_Shape: The_ escapes them).
bool generated_family(const std::string& name) {
    const auto folded = fold(name);
    for (const char* prefix : {"rosetta_get_", "rosetta_put_", "rosetta_make_", "rosetta_box_", "rosetta_free", "rosetta_dispatch"})
        if (starts(folded, prefix)) return true;
    return false;
}
bool generated_prefix(const std::string& name) {
    static const std::set<std::string> exact = {"rosetta", "rosetta_c", "rosetta_r", "rosetta_w", "rosetta_v", "rosetta_e", "rosetta_k",
        "rosetta_n", "rosetta_result", "rosetta_key", "rosetta_pairs", "rosetta_obj", "rosetta_method", "rosetta_count", "rosetta_holder",
        "rosetta_class", "rosetta_id", "rosetta_shared", "rosetta_owner", "rosetta_first", "rosetta_next"};
    const auto folded = fold(name);
    if (exact.count(folded) || generated_family(name)) return true;
    if (!starts(folded, "rosetta_") || folded.size() == 8) return false;
    for (std::size_t i = 8; i < folded.size(); ++i)
        if (!is_digit(folded[i])) return false;
    return true;   // Rosetta_1, a generated local
}

// Names in one Ada scope (case-insensitive).
struct Names {
    std::set<std::string> taken;   // folded
    bool reserve(const std::string& name) { return taken.insert(fold(name)).second; }
    bool free(const std::string& name) const { return !taken.count(fold(name)) && !generated_prefix(name); }
    // The wanted name (with _Kw if it is a keyword), else with a number (Get_Name_2).
    std::string claim(std::string wanted) {
        if (ada_keyword(wanted)) wanted += "_Kw";
        if (generated_family(wanted)) wanted = "The_" + wanted;
        if (free(wanted) && reserve(wanted)) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + "_" + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
    }
};

// ---------------------------------------------------------------- doc comments (GNATdoc)
struct Doc {
    std::vector<std::string> lines;
    void blank() { if (!lines.empty() && !lines.back().empty()) lines.push_back(""); }
    void text(const std::string& paragraph) {
        blank();
        std::istringstream in(paragraph);
        bool item = false;   // inside a list item: continuation lines line up with its text
        for (std::string line; std::getline(in, line);) {
            const auto stripped = trim(line);
            if (stripped.empty()) {
                blank();
                item = false;
                continue;
            }
            const bool bullet = starts(stripped, "- ") || starts(stripped, "* ");
            lines.push_back(item && !bullet ? "  " + stripped : stripped);
            item = item || bullet;
        }
    }
    void code(const std::string& block) {
        blank();
        std::istringstream in(block);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        for (const auto& line : body) lines.push_back("   " + line);
    }
    void tag(const std::string& line) { lines.push_back(line); }
    std::string render(const std::string& indent) const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        while (!copy.empty() && copy.front().empty()) copy.erase(copy.begin());
        std::string out;
        for (const auto& line : copy) out += indent + "--" + (line.empty() ? "" : "  " + line) + "\n";
        return out;
    }
};

std::string error_name(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "Invalid_Argument_Error";
    if (bare == "std::out_of_range") return "Out_Of_Range_Error";
    if (bare == "std::domain_error") return "Domain_Error";
    if (bare == "std::length_error") return "Length_Error";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "Arithmetic_Error";
    if (bare == "std::bad_alloc") return "Allocation_Error";
    return "Native_Error";
}

// C++ operators: group names ("" = not bound), and their Ada operator symbols.
std::string operator_group(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "Call";
    static const std::map<std::string, std::pair<const char*, const char*>> table = {
        {"operator+", {"Plus", "Add"}}, {"operator-", {"Negate", "Subtract"}}, {"operator*", {nullptr, "Multiply"}},
        {"operator/", {nullptr, "Divide"}}, {"operator%", {nullptr, "Remainder"}}, {"operator==", {nullptr, "Equals"}},
        {"operator!=", {nullptr, "Not_Equals"}}, {"operator<", {nullptr, "Less"}}, {"operator<=", {nullptr, "Less_Or_Equal"}},
        {"operator>", {nullptr, "Greater"}}, {"operator>=", {nullptr, "Greater_Or_Equal"}}, {"operator&", {nullptr, "Bit_And"}},
        {"operator|", {nullptr, "Bit_Or"}}, {"operator^", {nullptr, "Bit_Xor"}}, {"operator~", {"Complement", nullptr}},
        {"operator!", {"Logical_Not", nullptr}}, {"operator<<", {nullptr, "Shift_Left"}}, {"operator>>", {nullptr, "Shift_Right"}},
        {"operator[]", {nullptr, "Element"}}, {"operator+=", {nullptr, "Add_Assign"}}, {"operator-=", {nullptr, "Subtract_Assign"}},
        {"operator*=", {nullptr, "Multiply_Assign"}}, {"operator/=", {nullptr, "Divide_Assign"}}};
    const auto found = table.find(name);
    if (found == table.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}
// The Ada operator of a group ("" = a named function).
std::string ada_symbol(const std::string& group) {
    static const std::map<std::string, std::string> table = {{"Plus", "\"+\""}, {"Add", "\"+\""}, {"Negate", "\"-\""}, {"Subtract", "\"-\""},
        {"Multiply", "\"*\""}, {"Divide", "\"/\""}, {"Remainder", "\"rem\""}, {"Less", "\"<\""}, {"Less_Or_Equal", "\"<=\""},
        {"Greater", "\">\""}, {"Greater_Or_Equal", "\">=\""}, {"Bit_And", "\"and\""}, {"Bit_Or", "\"or\""}, {"Bit_Xor", "\"xor\""},
        {"Complement", "\"not\""}, {"Logical_Not", "\"not\""}};
    const auto found = table.find(group);
    return found == table.end() ? std::string() : found->second;
}

// A C++ type in Ada: its spelling for parameters and results, its spelling as a component (of a
// record or an optional; String is Unbounded_String there), how a value goes to C++ ($W the writer,
// $V the value) and comes back ($R the reader), and the part of generated type names it gives.
struct AT {
    std::string type = "Native_Value";
    std::string comp;              // "" = type
    std::string put = "Rosetta.Put_Value ($W, $V)";
    std::string get = "Rosetta.Get_Value ($R)";
    std::string comp_put, comp_get;   // "" = put, get
    std::string mangle = "Value";
    std::string scope;             // where "=" and "<" are declared, when not directly visible (Interfaces)
    std::string record;            // a class: its record type (parameters are access X_Record'Class)
    bool nullable = false;         // a class pointer (null allowed)
    bool indefinite = false;       // String
    bool ordered = false;          // has "<": a set element or map key
    bool callback = false;         // an access-to-subprogram type
    bool native = false;           // a C++ function object
    std::string component() const { return comp.empty() ? type : comp; }
    // Actual parameters for a generic's "=" (and "<") when they are not directly visible.
    std::string equality() const { return scope.empty() ? std::string() : ", \"=\" => " + scope + ".\"=\""; }
    std::string ordering() const { return scope.empty() ? std::string() : ", \"<\" => " + scope + ".\"<\""; }
    std::string component_put() const { return comp_put.empty() ? put : comp_put; }
    std::string component_get() const { return comp_get.empty() ? get : comp_get; }
};
std::string apply(const std::string& pattern, const std::string& writer, const std::string& value) {
    return replace_all(replace_all(replace_all(pattern, "$W", writer), "$V", value), "$R", writer);
}

// One Ada subprogram of a group: an overload called with its first `arity` C++ parameters.
struct Variant {
    const Overload* overload;
    std::size_t arity;
    std::vector<AT> params;     // the Ada parameters (receiver excluded)
    std::vector<std::string> names;
    std::vector<bool> out;      // in out (C++ writes it back)
    AT result;
    bool returns = false;
    std::string key;            // the Ada parameter types (folded)
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const AdaOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedAdaSource run();

private:
    AdaOptions options_;
    std::string package_, library_;
    Names types_;                                            // non-overloadable names: types, packages, exceptions
    Names functions_;                                        // module functions, constructors, statics and constants
    std::set<std::string> subprograms_;                      // every subprogram name (folded): types avoid them
    std::map<std::string, Names> modules_;                   // policy module -> function names
    std::map<std::string, Names> members_;                   // class -> member names (inherited ones included)
    std::map<std::string, std::map<std::string, std::string>> mapped_;   // class -> C++ member key -> Ada name
    std::map<const Group*, std::string> names_;
    std::map<std::string, std::string> field_names_;         // class::field -> getter
    std::map<std::string, std::string> setter_names_;        // class::field -> setter
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::set<std::string> enum_int64_;                       // enums Ada does not declare: Long_Long_Integer
    std::map<std::string, std::string> submodules_;          // policy module -> nested package path (Data.Inner)
    std::map<std::string, std::string> secondary_names_;     // class::base -> As_Base
    std::map<std::string, std::string> record_names_;        // class -> its record type
    std::map<std::string, std::string> constructor_names_;   // class -> New_X
    std::map<std::string, std::string> construct_names_;     // class -> Construct_X
    std::map<std::string, std::vector<Overload>> constructor_overloads_;
    struct Emitted { std::string result; bool is_virtual; bool in_out; };
    std::map<std::string, std::map<std::string, Emitted>> emitted_;   // class -> member key -> what it is (inherited ones included)
    std::map<std::string, std::size_t> class_index_;

    // Generated helper types, in dependency order: spec declarations, private completions and body codecs.
    mutable std::vector<std::string> type_decls_, native_decls_, native_private_, codec_decls_, maker_decls_;
    mutable std::map<std::string, std::string> helper_types_;   // mangle -> type name
    mutable std::set<std::string> spec_withs_;

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Ada"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return ada_case(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return operator_group(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        Names taken;
        taken.taken = types_.taken;
        for (const char* local : {"Self", "Left", "Right"}) taken.reserve(local);
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto wanted = cpp.empty() || !identifier(cpp) ? "Arg_" + std::to_string(i - first + 1) : ada_case(cpp);
            // A parameter named like a type would hide it: The_Shape.
            if (!taken.free(wanted) || ada_keyword(wanted) || starts(fold(wanted), "rosetta")) wanted = "The_" + wanted;
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
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, "New_" + plan.name) + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: extend " + plan.name + "_Record in Ada and override its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: extend " + plan.name + "_Record in Ada";
    }
    std::string symbol_prefix() const override { return "rosetta_ada_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_ADA"; }
    std::string module_prefix() const override { return "rosetta_ada_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    // A type name: not taken by a type or a subprogram.
    std::string claim_type(std::string wanted, const std::string& suffix) const {
        auto& names = const_cast<Names&>(types_);
        const auto usable = [&](const std::string& name) {
            return !ada_keyword(name) && names.free(name) && !subprograms_.count(fold(name)) && !generated_prefix(name);
        };
        if (generated_family(wanted)) wanted = "The_" + wanted;
        if (!usable(wanted)) wanted += suffix;
        for (int i = 2; !usable(wanted); ++i) wanted = wanted.substr(0, wanted.rfind(suffix) + suffix.size()) + "_" + std::to_string(i);
        names.reserve(wanted);
        return wanted;
    }
    std::string claim_subprogram(Names& scope, const std::string& wanted) {
        const auto name = scope.claim(wanted);
        subprograms_.insert(fold(name));
        return name;
    }
    AT ada_type(const Type& type, bool parameter = true) const;
    AT ada_spelled(std::string cpp) const;
    AT scalar(const std::string& type, const std::string& mangle) const;
    AT class_type(const std::string& qualified, bool nullable) const;
    AT enum_type(const std::string& qualified) const;
    AT vector_of(const AT& item) const;
    AT set_of(const AT& item) const;
    AT map_of(const AT& key, const AT& item, bool multi) const;
    AT optional_of(const AT& item) const;
    AT tuple_of(const std::vector<AT>& items, bool pair) const;
    AT function_of(const std::vector<AT>& args, const AT& result, bool returns, bool parameter) const;
    std::string helper_name(const std::string& mangle, const std::string& suffix) const;
    static std::string param_type(const AT& type, bool out);
    std::string ada_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    std::vector<Variant> plan_variants(const std::vector<Overload>& overloads) const;

    // ------------------------------------------------------------ naming and emission
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    bool conflicts(const std::string& owner, const Group& group) const;
    Doc variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const;
    Doc describe(const Documentation& doc, const std::string& fallback) const;
    std::string parameter_list(const Variant& variant, const std::string& self) const;
    void emit_call(std::ostream& out, const Variant& variant, std::size_t index, const std::string& receiver, bool returns_result,
                   const std::string& indent) const;
    void emit_routine(std::ostream& spec, std::ostream& body, const Group& group, const std::string& indent) const;
    void emit_members(std::ostream& spec, std::ostream& body, const std::string& owner, const std::string& from, bool copied);
    void emit_class(std::ostream& spec, std::ostream& body, const std::string& owner);
    void emit_enum(std::ostream& spec, std::ostream& body, const std::string& qualified) const;
    std::string dispatch();
};

// ---------------------------------------------------------------- types
std::string Generator::helper_name(const std::string& mangle, const std::string& suffix) const {
    const auto key = mangle + suffix;
    const auto found = helper_types_.find(key);
    if (found != helper_types_.end()) return found->second;
    const auto name = claim_type(mangle + suffix, "_Type");
    helper_types_[key] = name;
    return name;
}

std::string Generator::param_type(const AT& type, bool out) {
    if (out) return "in out " + type.component();
    if (!type.record.empty()) return std::string(type.nullable ? "access " : "not null access ") + type.record + "'Class";
    return type.type;
}

AT Generator::scalar(const std::string& type, const std::string& mangle) const {
    AT result;
    result.type = type;
    result.mangle = mangle;
    result.ordered = true;
    if (starts(type, "Interfaces.")) result.scope = "Interfaces";
    if (type == "Boolean") {
        result.put = "Rosetta.Put_Bool ($W, $V)";
        result.get = "Rosetta.Get_Bool ($R)";
    } else if (type == "Character") {
        result.put = "Rosetta.Put_Character ($W, $V)";
        result.get = "Rosetta.Get_Character ($R)";
    } else if (type == "Long_Float") {
        result.put = "Rosetta.Put_Real ($W, $V)";
        result.get = "Rosetta.Get_Real ($R)";
    } else if (type == "Float") {
        result.put = "Rosetta.Put_Real ($W, Long_Float ($V))";
        result.get = "Float (Rosetta.Get_Real ($R))";
    } else if (type == "Interfaces.Unsigned_64") {
        result.put = "Rosetta.Put_Unsigned ($W, $V)";
        result.get = "Rosetta.Get_Unsigned_64 ($R)";
    } else if (type == "String") {
        result.put = "Rosetta.Put_String ($W, $V)";
        result.get = "Rosetta.Get_String ($R)";
        result.comp = "Ada.Strings.Unbounded.Unbounded_String";
        result.comp_put = "Rosetta.Put_String ($W, Ada.Strings.Unbounded.To_String ($V))";
        result.comp_get = "Ada.Strings.Unbounded.To_Unbounded_String (Rosetta.Get_String ($R))";
        result.indefinite = true;
    } else if (type == "Complex") {
        result.put = "Rosetta.Put_Complex ($W, $V)";
        result.get = "Rosetta.Get_Complex ($R)";
        result.ordered = false;
        result.scope = "Ada.Numerics.Long_Complex_Types";
    } else {
        // Integers.
        result.put = "Rosetta.Put_Int ($W, Interfaces.Integer_64 ($V))";
        result.get = type + " (Rosetta.Get_Int ($R, Interfaces.Integer_64 (" + type + "'First), Interfaces.Integer_64 (" + type + "'Last), " +
                     ada_string(mangle) + "))";
    }
    return result;
}

AT Generator::class_type(const std::string& qualified, bool nullable) const {
    const auto& name = classes_.at(qualified).name;
    AT result;
    result.type = name;
    result.record = record_names_.at(qualified);
    result.put = "Rosetta.Put_Object ($W, $V)";
    result.get = name + " (Rosetta.Get_Object ($R, " + std::to_string(class_index_.at(qualified)) + "))";
    result.mangle = name;
    result.nullable = nullable;
    return result;
}

AT Generator::enum_type(const std::string& qualified) const {
    const auto found = enum_names_.find(qualified);
    if (found == enum_names_.end() || enum_int64_.count(qualified)) return scalar("Long_Long_Integer", "Long_Long_Integer");
    AT result;
    result.type = found->second;
    result.mangle = found->second;
    result.put = "Rosetta.Put_Int ($W, " + found->second + "'Enum_Rep ($V))";
    result.get = "Rosetta_Get_" + found->second + " ($R)";
    result.ordered = true;
    return result;
}

AT Generator::vector_of(const AT& item) const {
    const auto mangle = item.mangle + "_Vector";
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name(item.mangle, "_Vector");
    const auto package = known ? helper_types_.at(mangle + "s") : helper_name(item.mangle, "_Vectors");
    if (!known) {
        const auto generic = item.indefinite ? std::string("Indefinite_Vectors") : std::string("Vectors");
        spec_withs_.insert("Ada.Containers." + generic);
        type_decls_.push_back("   package " + package + " is new Ada.Containers." + generic + " (Positive, " + item.type + item.equality() + ");\n   subtype " + name +
                              " is " + package + ".Vector;\n   --  A C++ sequence of " + item.type + ".\n");
        std::ostringstream codec;
        codec << "   procedure Rosetta_Put_" << name << " (Rosetta_W : in out Rosetta.Writer; Rosetta_V : " << name << ") is\n   begin\n"
              << "      Rosetta.Put_List (Rosetta_W, Natural (Rosetta_V.Length));\n      for Rosetta_E of Rosetta_V loop\n         "
              << apply(item.put, "Rosetta_W", "Rosetta_E") << ";\n      end loop;\n   end Rosetta_Put_" << name << ";\n\n"
              << "   function Rosetta_Get_" << name << " (Rosetta_R : in out Rosetta.Reader) return " << name << " is\n"
              << "      Rosetta_N : constant Natural := Rosetta.List_Length (Rosetta_R, " << ada_string(name) << ");\n   begin\n"
              << "      return Rosetta_V : " << name << " do\n         for Rosetta_K in 1 .. Rosetta_N loop\n            Rosetta_V.Append ("
              << apply(item.get, "Rosetta_R", "") << ");\n         end loop;\n      end return;\n   end Rosetta_Get_" << name << ";\n";
        codec_decls_.push_back(codec.str());
    }
    AT result;
    result.type = name;
    result.put = "Rosetta_Put_" + name + " ($W, $V)";
    result.get = "Rosetta_Get_" + name + " ($R)";
    result.mangle = name;
    result.scope = package;
    return result;
}

AT Generator::set_of(const AT& item) const {
    if (!item.ordered) return vector_of(item);
    const auto mangle = item.mangle + "_Set";
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name(item.mangle, "_Set");
    const auto package = known ? helper_types_.at(mangle + "s") : helper_name(item.mangle, "_Sets");
    if (!known) {
        const auto generic = item.indefinite ? std::string("Indefinite_Ordered_Sets") : std::string("Ordered_Sets");
        spec_withs_.insert("Ada.Containers." + generic);
        type_decls_.push_back("   package " + package + " is new Ada.Containers." + generic + " (" + item.type + item.ordering() + item.equality() + ");\n   subtype " + name + " is " +
                              package + ".Set;\n   --  A C++ set of " + item.type + ".\n");
        std::ostringstream codec;
        codec << "   procedure Rosetta_Put_" << name << " (Rosetta_W : in out Rosetta.Writer; Rosetta_V : " << name << ") is\n   begin\n"
              << "      Rosetta.Put_List (Rosetta_W, Natural (Rosetta_V.Length));\n      for Rosetta_E of Rosetta_V loop\n         "
              << apply(item.put, "Rosetta_W", "Rosetta_E") << ";\n      end loop;\n   end Rosetta_Put_" << name << ";\n\n"
              << "   function Rosetta_Get_" << name << " (Rosetta_R : in out Rosetta.Reader) return " << name << " is\n"
              << "      Rosetta_N : constant Natural := Rosetta.List_Length (Rosetta_R, " << ada_string(name) << ");\n   begin\n"
              << "      return Rosetta_V : " << name << " do\n         for Rosetta_K in 1 .. Rosetta_N loop\n            Rosetta_V.Include ("
              << apply(item.get, "Rosetta_R", "") << ");\n         end loop;\n      end return;\n   end Rosetta_Get_" << name << ";\n";
        codec_decls_.push_back(codec.str());
    }
    AT result;
    result.type = name;
    result.put = "Rosetta_Put_" + name + " ($W, $V)";
    result.get = "Rosetta_Get_" + name + " ($R)";
    result.mangle = name;
    result.scope = package;
    return result;
}

AT Generator::map_of(const AT& key, const AT& item, bool multi) const {
    if (multi || !key.ordered) return vector_of(tuple_of({key, item}, true));
    const auto mangle = key.mangle + "_To_" + item.mangle + "_Map";
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name(key.mangle + "_To_" + item.mangle, "_Map");
    const auto package = known ? helper_types_.at(mangle + "s") : helper_name(key.mangle + "_To_" + item.mangle, "_Maps");
    if (!known) {
        const auto generic = key.indefinite || item.indefinite ? std::string("Indefinite_Ordered_Maps") : std::string("Ordered_Maps");
        spec_withs_.insert("Ada.Containers." + generic);
        type_decls_.push_back("   package " + package + " is new Ada.Containers." + generic + " (" + key.type + ", " + item.type + key.ordering() + item.equality() + ");\n   subtype " +
                              name + " is " + package + ".Map;\n   --  A C++ map from " + key.type + " to " + item.type + ".\n");
        std::ostringstream codec;
        codec << "   procedure Rosetta_Put_" << name << " (Rosetta_W : in out Rosetta.Writer; Rosetta_V : " << name << ") is\n   begin\n"
              << "      Rosetta.Put_Map (Rosetta_W, Natural (Rosetta_V.Length));\n      for Rosetta_C in Rosetta_V.Iterate loop\n         "
              << apply(key.put, "Rosetta_W", package + ".Key (Rosetta_C)") << ";\n         " << apply(item.put, "Rosetta_W", package + ".Element (Rosetta_C)")
              << ";\n      end loop;\n   end Rosetta_Put_" << name << ";\n\n"
              << "   function Rosetta_Get_" << name << " (Rosetta_R : in out Rosetta.Reader) return " << name << " is\n"
              << "      Rosetta_Pairs : Boolean;\n      Rosetta_N : constant Natural := Rosetta.Map_Length (Rosetta_R, Rosetta_Pairs);\n   begin\n"
              << "      return Rosetta_V : " << name << " do\n         for Rosetta_K in 1 .. Rosetta_N loop\n"
              << "            Rosetta.Entry_Start (Rosetta_R, Rosetta_Pairs);\n            declare\n               Rosetta_Key : constant " << key.type
              << " := " << apply(key.get, "Rosetta_R", "") << ";\n            begin\n               Rosetta_V.Include (Rosetta_Key, "
              << apply(item.get, "Rosetta_R", "") << ");\n            end;\n         end loop;\n      end return;\n   end Rosetta_Get_" << name << ";\n";
        codec_decls_.push_back(codec.str());
    }
    AT result;
    result.type = name;
    result.put = "Rosetta_Put_" + name + " ($W, $V)";
    result.get = "Rosetta_Get_" + name + " ($R)";
    result.mangle = name;
    result.scope = package;
    return result;
}

AT Generator::optional_of(const AT& item) const {
    if (!item.record.empty() || item.callback || item.native || item.type == "Native_Value") {
        AT result = item;   // null already means none
        result.nullable = true;
        return result;
    }
    const auto mangle = "Optional_" + item.mangle;
    const bool known = helper_types_.count(mangle) != 0;
    const auto name = helper_name("Optional_" + item.mangle, "");
    if (!known) {
        type_decls_.push_back("   type " + name + " (Has_Value : Boolean := False) is record\n      case Has_Value is\n         when True => Value : " +
                              item.component() + ";\n         when False => null;\n      end case;\n   end record;\n   --  A C++ std::optional of " +
                              item.type + ".\n");
        std::ostringstream codec;
        codec << "   procedure Rosetta_Put_" << name << " (Rosetta_W : in out Rosetta.Writer; Rosetta_V : " << name << ") is\n   begin\n"
              << "      if Rosetta_V.Has_Value then\n         " << apply(item.component_put(), "Rosetta_W", "Rosetta_V.Value")
              << ";\n      else\n         Rosetta.Put_Nil (Rosetta_W);\n      end if;\n   end Rosetta_Put_" << name << ";\n\n"
              << "   function Rosetta_Get_" << name << " (Rosetta_R : in out Rosetta.Reader) return " << name << " is\n   begin\n"
              << "      if Rosetta.Take_Nil (Rosetta_R) then\n         return (Has_Value => False);\n      end if;\n"
              << "      return (Has_Value => True, Value => " << apply(item.component_get(), "Rosetta_R", "") << ");\n   end Rosetta_Get_" << name << ";\n";
        codec_decls_.push_back(codec.str());
    }
    AT result;
    result.type = name;
    result.put = "Rosetta_Put_" + name + " ($W, $V)";
    result.get = "Rosetta_Get_" + name + " ($R)";
    result.mangle = name;
    return result;
}

AT Generator::tuple_of(const std::vector<AT>& items, bool pair) const {
    std::string mangle;
    std::vector<std::string> fields;
    for (std::size_t i = 0; i < items.size(); ++i) {
        mangle += (i ? "_" : "") + items[i].mangle;
        fields.push_back(pair ? (i == 0 ? "First" : "Second") : "Item_" + std::to_string(i + 1));
    }
    const auto suffix = pair ? std::string("_Pair") : std::string("_Tuple");
    const bool known = helper_types_.count(mangle + suffix) != 0;
    const auto name = helper_name(mangle, suffix);
    if (!known) {
        std::ostringstream decl, codec;
        std::vector<std::string> types;
        for (const auto& item : items) types.push_back(item.type);
        decl << "   type " << name << " is record\n";
        for (std::size_t i = 0; i < items.size(); ++i) decl << "      " << fields[i] << " : " << items[i].component() << ";\n";
        decl << "   end record;\n   --  A C++ " << (pair ? "pair" : "tuple") << " of " << join(types, ", ") << ".\n";
        type_decls_.push_back(decl.str());
        codec << "   procedure Rosetta_Put_" << name << " (Rosetta_W : in out Rosetta.Writer; Rosetta_V : " << name << ") is\n   begin\n"
              << "      Rosetta.Put_List (Rosetta_W, " << items.size() << ");\n";
        for (std::size_t i = 0; i < items.size(); ++i) codec << "      " << apply(items[i].component_put(), "Rosetta_W", "Rosetta_V." + fields[i]) << ";\n";
        codec << "   end Rosetta_Put_" << name << ";\n\n   function Rosetta_Get_" << name << " (Rosetta_R : in out Rosetta.Reader) return " << name
              << " is\n   begin\n      Rosetta.Tuple_Start (Rosetta_R, " << items.size() << ", " << ada_string(name) << ");\n      declare\n";
        std::vector<std::string> values;
        for (std::size_t i = 0; i < items.size(); ++i) {
            codec << "         Rosetta_" << i + 1 << " : constant " << items[i].component() << " := " << apply(items[i].component_get(), "Rosetta_R", "")
                  << ";\n";
            values.push_back(fields[i] + " => Rosetta_" + std::to_string(i + 1));
        }
        codec << "      begin\n         return (" << join(values, ", ") << ");\n      end;\n   end Rosetta_Get_" << name << ";\n";
        codec_decls_.push_back(codec.str());
    }
    AT result;
    result.type = name;
    result.put = "Rosetta_Put_" + name + " ($W, $V)";
    result.get = "Rosetta_Get_" + name + " ($R)";
    result.mangle = name;
    return result;
}

// std::function: as a parameter, an access-to-subprogram type (library-level subprograms, which
// C++ may keep); as a result, a type over Native_Function_Record whose Call calls the C++ function.
AT Generator::function_of(const std::vector<AT>& args, const AT& result, bool returns, bool parameter) const {
    std::vector<std::string> mangles;
    for (const auto& arg : args) mangles.push_back(arg.mangle);
    std::string mangle = join(mangles, "_");
    if (returns) mangle += (mangle.empty() ? "To_" : "_To_") + result.mangle;
    if (mangle.empty()) mangle = "Procedure";
    std::vector<std::string> params;
    for (std::size_t i = 0; i < args.size(); ++i) {
        AT arg = args[i];
        arg.nullable = true;
        params.push_back("A" + std::to_string(i + 1) + " : " + param_type(arg, false));
    }
    const auto profile = std::string(returns ? "function" : "procedure") + (params.empty() ? "" : " (" + join(params, "; ") + ")") +
                         (returns ? " return " + result.type : std::string());
    if (parameter) {
        const bool known = helper_types_.count(mangle + "_Callback") != 0;
        const auto name = helper_name(mangle, "_Callback");
        const auto box = "Rosetta_Box_" + name;
        if (!known) {
            type_decls_.push_back("   type " + name + " is access " + profile + ";\n   --  An Ada subprogram C++ calls (a C++ std::function).\n");
            std::ostringstream codec;
            codec << "   type " << box << " is new Rosetta.Callback_Record with record\n      F : " << name << ";\n   end record;\n"
                  << "   overriding procedure Invoke_Callback\n     (Self : in out " << box
                  << "; Rosetta_R : in out Rosetta.Reader; Rosetta_Count : Natural; Rosetta_W : in out Rosetta.Writer);\n\n"
                  << "   overriding procedure Invoke_Callback\n     (Self : in out " << box
                  << "; Rosetta_R : in out Rosetta.Reader; Rosetta_Count : Natural; Rosetta_W : in out Rosetta.Writer)\n   is\n   begin\n"
                  << "      Rosetta.Arity (Rosetta_Count, " << args.size() << ");\n";
            std::vector<std::string> call;
            std::string indent = "      ";
            if (!args.empty()) {
                codec << "      declare\n";
                for (std::size_t i = 0; i < args.size(); ++i) {
                    codec << "         Rosetta_" << i + 1 << " : constant " << args[i].type << " := " << apply(args[i].get, "Rosetta_R", "") << ";\n";
                    call.push_back("Rosetta_" + std::to_string(i + 1));
                }
                codec << "      begin\n";
                indent = "         ";
            }
            const auto invocation = call.empty() ? std::string("Self.F.all") : "Self.F (" + join(call, ", ") + ")";
            if (returns) codec << indent << apply(result.put, "Rosetta_W", invocation) << ";\n";
            else codec << indent << invocation << ";\n" << indent << "Rosetta.Put_Nil (Rosetta_W);\n";
            if (!args.empty()) codec << "      end;\n";
            codec << "   end Invoke_Callback;\n";
            codec_decls_.push_back(codec.str());
        }
        AT type;
        type.type = name;
        type.put = "Rosetta.Put_Callback ($W, (if $V = null then null else new " + box + "'(Rosetta.Callback_Record with F => $V)))";
        type.get = "null";
        type.mangle = name;
        type.callback = true;
        return type;
    }
    const bool known = helper_types_.count(mangle + "_Function") != 0;
    const auto name = helper_name(mangle, "_Function");
    const auto record = known ? helper_types_.at(mangle + "_Function_Record") : helper_name(mangle, "_Function_Record");
    if (!known) {
        type_decls_.push_back("   type " + record + " is tagged;\n   type " + name + " is access all " + record + "'Class;\n   --  A C++ std::function: call it with Call, and free it when done.\n");
        std::vector<std::string> call_params = {"Self : " + record};
        for (std::size_t i = 0; i < args.size(); ++i) call_params.push_back("A" + std::to_string(i + 1) + " : " + param_type(args[i], false));
        const auto call = std::string(returns ? "function" : "procedure") + " Call (" + join(call_params, "; ") + ")" +
                          (returns ? " return " + result.type : std::string());
        native_decls_.push_back("   type " + record + " is new Native_Function_Record with private;\n\n   " + call +
                                ";\n   --  Calls the C++ function.\n\n   procedure Free (Object : in out " + name + ");\n   --  Frees the function.\n\n");
        native_private_.push_back("   type " + record + " is new Native_Function_Record with null record;\n");
        maker_decls_.push_back("   function Rosetta_Make_" + name + " return Native_Object is (new " + record + ");\n   procedure Rosetta_Free is new Ada.Unchecked_Deallocation (" +
                               record + "'Class, " + name + ");\n");
        std::ostringstream body;
        body << "   " << call << " is\n      Rosetta_C : Rosetta.Call := Rosetta.Start (0);\n      Rosetta_R : Rosetta.Reader;\n   begin\n"
             << "      Rosetta.Arg (Rosetta_C);\n      Rosetta.Put_Holder (Rosetta_C.W, Self.Rosetta_Holder);\n";
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto value = "A" + std::to_string(i + 1);
            if (!args[i].record.empty()) body << "      Rosetta.Object_Arg (Rosetta_C, " << value << ");\n";
            else body << "      Rosetta.Arg (Rosetta_C);\n      " << apply(args[i].put, "Rosetta_C.W", value) << ";\n";
        }
        body << "      Rosetta.Run (Rosetta_C, Rosetta_R);\n";
        if (returns) body << "      return " << apply(result.get, "Rosetta_R", "") << ";\n";
        body << "   end Call;\n\n   procedure Free (Object : in out " << name << ") is\n   begin\n      Rosetta_Free (Object);\n   end Free;\n";
        codec_decls_.push_back(body.str());
    }
    AT type;
    type.type = name;
    type.put = "Rosetta.Put_Object ($W, $V)";
    type.get = name + " (Rosetta.Get_Function ($R, Rosetta_Make_" + name + "'Access))";
    type.mangle = name;
    type.native = true;
    return type;
}

AT Generator::ada_type(const Type& type, bool parameter) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return scalar("String", "String");
        if (is_void(pointee)) return {};
        return optional_of(ada_type(pointee, parameter));
    }
    if (is_void(target)) return {};
    if (!conversion(target).empty()) return ada_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        const auto spelled = normalize(strip_cv(target.spelling));
        if (spelled == "size_t" || spelled == "std::size_t" || spelled == "ssize_t" || spelled == "ptrdiff_t" || spelled == "std::ptrdiff_t")
            return scalar("Long_Long_Integer", "Long_Long_Integer");
        static const std::map<std::string, std::string> fixed = {{"int8_t", "Interfaces.Integer_8"}, {"int16_t", "Short_Integer"},
            {"int32_t", "Integer"}, {"int64_t", "Long_Long_Integer"}, {"uint8_t", "Interfaces.Unsigned_8"}, {"uint16_t", "Interfaces.Unsigned_16"},
            {"uint32_t", "Interfaces.Unsigned_32"}, {"uint64_t", "Interfaces.Unsigned_64"}};
        std::string name;
        if (const auto found = fixed.find(starts(spelled, "std::") ? spelled.substr(5) : spelled); found != fixed.end()) name = found->second;
        else {
            static const std::map<std::string, std::string> builtins = {{"bool", "Boolean"}, {"char", "Character"},
                {"signed char", "Interfaces.Integer_8"}, {"unsigned char", "Interfaces.Unsigned_8"}, {"short", "Short_Integer"},
                {"unsigned short", "Interfaces.Unsigned_16"}, {"int", "Integer"}, {"unsigned int", "Interfaces.Unsigned_32"},
                {"long", "Long_Long_Integer"}, {"unsigned long", "Interfaces.Unsigned_64"}, {"long long", "Long_Long_Integer"},
                {"unsigned long long", "Interfaces.Unsigned_64"}, {"float", "Float"}, {"double", "Long_Float"}, {"long double", "Long_Float"},
                {"char16_t", "Interfaces.Unsigned_16"}, {"char32_t", "Interfaces.Unsigned_32"}, {"wchar_t", "Integer"}};
            const auto builtin = builtins.find(bare);
            if (builtin == builtins.end()) return {};
            name = builtin->second;
        }
        return scalar(name, starts(name, "Interfaces.") ? name.substr(11) : name);
    }
    if (target.kind == TypeKind::enumeration) return enum_type(target.declaration);
    if (bound(target)) return class_type(target.declaration, false);
    if (bare == "std::filesystem::path") return scalar("String", "String");
    if (bare == "std::nullptr_t" || bare == "std::monostate") return {};
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? ada_type(target.arguments[i], parameter) : AT{}; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return scalar("String", "String");
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span" || declaration == "std::multiset" || declaration == "std::unordered_multiset")
            return vector_of(argument(0));
        if (declaration == "std::set" || declaration == "std::unordered_set") return set_of(argument(0));
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" || declaration == "std::unordered_multimap")
            return map_of(argument(0), argument(1), declaration.find("multi") != std::string::npos);
        if (declaration == "std::optional") return target.arguments.empty() ? AT{} : optional_of(argument(0));
        if (declaration == "std::complex") return scalar("Complex", "Complex");
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            if (target.arguments.empty()) return {};
            auto item = argument(0);
            item.nullable = true;
            return item;
        }
        if ((declaration == "std::pair" || declaration == "std::tuple") && target.arguments.size() >= 2 && target.arguments.size() <= 6) {
            std::vector<AT> items;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) items.push_back(argument(i));
            return tuple_of(items, declaration == "std::pair");
        }
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            if (function.arguments.size() > 5) return {};
            std::vector<AT> args;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) {
                auto item = ada_type(function.arguments[i]);
                // A function inside a function type is the other direction's: no Ada type.
                if (item.callback || item.native || writeback(function.arguments[i])) return {};
                args.push_back(item);
            }
            const bool returns = !function.arguments.empty() && !is_void(function.arguments[0]);
            auto result = returns ? ada_type(function.arguments[0]) : AT{};
            if (result.callback || result.native) return {};
            // Parameters are callbacks; results C++ functions.
            return function_of(args, result, returns, parameter);
        }
        if (declaration == "std::chrono::duration") return scalar("Long_Float", "Long_Float");
    }
    return {};
}

// A type a policy conversion spells in C++.
AT Generator::ada_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::map<std::string, std::string> scalars = {{"bool", "Boolean"}, {"char", "Character"}, {"signed char", "Interfaces.Integer_8"},
        {"unsigned char", "Interfaces.Unsigned_8"}, {"short", "Short_Integer"}, {"unsigned short", "Interfaces.Unsigned_16"}, {"int", "Integer"},
        {"unsigned int", "Interfaces.Unsigned_32"}, {"long", "Long_Long_Integer"}, {"unsigned long", "Interfaces.Unsigned_64"},
        {"long long", "Long_Long_Integer"}, {"unsigned long long", "Interfaces.Unsigned_64"}, {"float", "Float"}, {"double", "Long_Float"},
        {"long double", "Long_Float"}, {"std::size_t", "Long_Long_Integer"}, {"size_t", "Long_Long_Integer"}, {"std::int64_t", "Long_Long_Integer"},
        {"std::uint64_t", "Interfaces.Unsigned_64"}, {"std::int32_t", "Integer"}, {"std::uint32_t", "Interfaces.Unsigned_32"},
        {"std::int16_t", "Short_Integer"}, {"std::uint16_t", "Interfaces.Unsigned_16"}, {"std::int8_t", "Interfaces.Integer_8"},
        {"std::uint8_t", "Interfaces.Unsigned_8"}};
    if (const auto found = scalars.find(cpp); found != scalars.end())
        return scalar(found->second, starts(found->second, "Interfaces.") ? found->second.substr(11) : found->second);
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return scalar("String", "String");
    for (const auto& [qualified, plan] : classes_)
        if (normalize(qualified) == cpp) return class_type(qualified, false);
    for (const auto& [qualified, name] : enum_names_)
        if (normalize(qualified) == cpp) return enum_type(qualified);
    return {};
}

std::string Generator::ada_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") {
        auto text = shortest_double(value.value);
        if (text.find_first_of(".eE") == std::string::npos) text += ".0";
        return text;
    }
    if (value.kind == "bool") return value.value == "true" ? "True" : "False";
    if (value.kind == "null") return "null";
    if (value.kind == "char") return value.value.size() == 1 ? "'" + value.value + "'" : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos)
            return ada_string(text.substr(1, text.size() - 2));
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        const auto found = enum_names_.find(qualified);
        if (found == enum_names_.end() || !enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value && !enum_cases_.at(qualified)[i].empty()) return enum_cases_.at(qualified)[i];
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
        auto text = names[i - first] + " : " + param_type(ada_type(type), writeback(type));
        if (i >= function.parameters.size() - function.default_arguments) {
            const auto shown = ada_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, type);
            text += " := " + (shown.empty() ? std::string("<C++ default>") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + (parameters.empty() ? std::string() : " (" + join(parameters, "; ") + ")");
    if (!function.is_constructor && !is_void(function.result)) result += " return " + ada_type(function.result, false).type;
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
                variant.params.push_back(ada_type(type));
                variant.names.push_back(names[i - first]);
                variant.out.push_back(writeback(type) && !function.is_constructor);
                variant.key += (i > first ? ", " : "") + fold(param_type(variant.params.back(), false));
            }
            variant.returns = !function.is_constructor && !is_void(function.result);
            variant.result = variant.returns ? ada_type(function.result, false) : AT{};
            if (keys.insert(variant.key).second) result.push_back(std::move(variant));
        }
    }
    return result;
}

// ---------------------------------------------------------------- naming
// Whether a method of `owner` has the Ada parameters of a base class's method of the same C++ name
// but another result: it takes its own name (overloading on the result alone would make calls
// ambiguous).
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
    // Subprograms must not hide a type (or the runtime's names).
    functions_.taken.insert(types_.taken.begin(), types_.taken.end());
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        auto& mapped = mapped_[owner];
        // Members must not hide a type, and keep the names their bases give them (overrides, overloads,
        // or a further base's members declared again).
        members.taken = types_.taken;
        for (const auto& base : cls.bases) {
            if (!members_.count(base)) continue;
            for (const auto& name : members_.at(base).taken) members.taken.insert(name);
            for (const auto& [key, name] : mapped_.at(base)) mapped.emplace(key, name);
        }
        // Native_Object_Record's and Limited_Controlled's operations.
        for (const char* reserved : {"Close", "Is_Closed", "Image", "Free", "Finalize", "Initialize", "Adjust", "Self", "Left", "Right"})
            members.reserve(reserved);
        const bool constructible = !cls.record->abstract;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) constructor_overloads_[owner].push_back(overload);
        const auto name_member = [&](const std::string& key, const std::string& wanted) {
            const auto found = mapped.find(key);
            if (found != mapped.end()) return found->second;
            const auto name = claim_subprogram(members, wanted);
            mapped[key] = name;
            return name;
        };
        for (auto& group : cls.methods) {
            const auto key = "m:" + group.name;
            if (group.is_static) {
                names_[&group] = claim_subprogram(functions_, cls.name + "_" + ada_case(group.name));
                continue;
            }
            // to_string() is Image; its other argument counts (default arguments, other overloads) are
            // Image overloads.
            bool to_string = false;
            if (group.name == "to_string")
                for (const auto& variant : plan_variants(group.overloads))
                    to_string = to_string || (variant.params.empty() && variant.result.type == "String");
            if (to_string) names_[&group] = mapped[key] = "Image";
            else if (mapped.count(key) && conflicts(owner, group)) names_[&group] = mapped[key] = claim_subprogram(members, ada_case(group.name));
            else names_[&group] = name_member(key, ada_case(group.name));
        }
        for (auto& group : cls.operators) {
            const auto symbol = ada_symbol(group.name);
            if (!symbol.empty()) names_[&group] = symbol;
            else names_[&group] = name_member("o:" + group.name, group.name);
        }
        for (const auto& field : cls.fields) {
            const auto getter = name_member("f:" + field.field->name, ada_case(field.name));
            field_names_[owner + "::" + field.field->name] = getter;
            setter_names_[owner + "::" + field.field->name] = name_member("s:" + field.field->name, "Set_" + getter);
        }
        for (auto& constant : cls.constants) constant_names_[&constant] = claim_subprogram(functions_, cls.name + "_" + ada_case(constant.name));
        for (std::size_t i = 1; i < cls.bases.size(); ++i)
            secondary_names_[owner + "::" + cls.bases[i]] = name_member("a:" + cls.bases[i], "As_" + classes_.at(cls.bases[i]).name);
        if (constructor_overloads_.count(owner)) {
            if (constructible) constructor_names_[owner] = claim_subprogram(functions_, "New_" + cls.name);
            construct_names_[owner] = claim_subprogram(functions_, "Construct_" + cls.name);
        }
    }
    // Prefix notation (Obj.Name) also finds subprograms whose first parameter is class-wide: a
    // function taking a class first avoids the member names of the class and its descendants.
    const auto descends = [&](const std::string& cls, const std::string& base) {
        std::function<bool(const std::string&)> walk = [&](const std::string& current) {
            if (current == base) return true;
            for (const auto& next : classes_.at(current).bases)
                if (walk(next)) return true;
            return false;
        };
        return walk(cls);
    };
    for (auto& group : functions) {
        auto& scope = group.module.empty() ? functions_ : modules_[group.module];
        if (!group.module.empty() && scope.taken.empty()) scope.taken = types_.taken;
        std::set<std::string> avoid;
        for (const auto& overload : group.overloads) {
            const auto& parameters = overload.function->parameters;
            if (parameters.empty()) continue;
            const auto& first = referent(parameters.front());
            const auto& target = first.kind == TypeKind::pointer && !first.arguments.empty() ? referent(first.arguments.front()) : first;
            if (!bound(target) || !classes_.count(target.declaration)) continue;
            for (const auto& owner : class_order_)
                if (descends(owner, target.declaration)) avoid.insert(members_[owner].taken.begin(), members_[owner].taken.end());
        }
        auto wanted = ada_case(group.name);
        if (avoid.count(fold(wanted))) {
            Names combined;
            combined.taken = scope.taken;
            combined.taken.insert(avoid.begin(), avoid.end());
            wanted = combined.claim(wanted);
        }
        names_[&group] = claim_subprogram(scope, wanted);
    }
    for (auto& constant : constants) constant_names_[&constant] = claim_subprogram(functions_, ada_case(constant.name));
}

// ---------------------------------------------------------------- documentation
Doc Generator::describe(const Documentation& doc, const std::string& fallback) const {
    Doc result;
    auto details = doc.details;
    auto brief = doc.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "paragraph") {
        brief = details.front().text;
        details.erase(details.begin());
    }
    result.text(brief.empty() ? fallback : one_line(brief));
    if (!brief.empty() && !fallback.empty()) result.text(fallback);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") result.code(block.text);
        else if (block.kind == "heading") result.text(one_line(block.text) + ":");
        else result.text(block.text);
    }
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        result.text(label + ": " + one_line(item.text));
    }
    if (!doc.see.empty()) result.text("See also: " + join(doc.see, ", ") + ".");
    return result;
}

Doc Generator::variant_doc(const Variant& variant, const std::vector<Variant>& siblings, const std::string& what) const {
    const auto& overload = *variant.overload;
    const auto& function = *overload.function;
    const auto& doc = function.documentation;
    auto result = describe(doc, "Calls " + what + ".");
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> omitted;
    for (std::size_t i = variant.arity; i < function.parameters.size(); ++i) {
        if (i < first) continue;
        const auto shown = ada_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
        const auto cpp = i < function.default_values.size() ? function.default_values[i].value : std::string();
        omitted.push_back(names[i - first] + " := " + (shown.empty() ? cpp.empty() ? std::string("C++ default") : cpp : shown));
    }
    if (!omitted.empty()) {
        std::size_t longer = 0;
        for (const auto& sibling : siblings) if (sibling.overload == variant.overload && sibling.arity > variant.arity) ++longer;
        result.text("Leaves out the C++ default arguments " + join(omitted, ", ") + (longer ? " (other overloads pass them)." : "."));
    }
    std::vector<std::string> tags;
    for (std::size_t i = first; i < variant.arity; ++i) {
        const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        std::string text;
        for (const auto& item : doc.params) if (item.name == cpp && !item.text.empty()) text = one_line(item.text);
        if (variant.out[i - first]) text += std::string(text.empty() ? "" : " ") + "(C++ writes it back)";
        if (!text.empty()) tags.push_back("@param " + variant.names[i - first] + " " + text);
    }
    if (variant.returns && !doc.returns.empty()) tags.push_back("@return " + one_line(doc.returns));
    for (const auto& item : doc.throws)
        tags.push_back("@exception " + error_name(item.name) + (item.name.empty() ? std::string() : " (C++ " + item.name + ")") +
                       (item.text.empty() ? std::string() : " " + one_line(item.text)));
    if (!tags.empty()) {
        result.blank();
        for (const auto& tag : tags) result.tag(tag);
    }
    return result;
}

// ---------------------------------------------------------------- emission
std::string Generator::parameter_list(const Variant& variant, const std::string& self) const {
    std::vector<std::string> items;
    if (!self.empty()) items.push_back(self);
    for (std::size_t i = 0; i < variant.params.size(); ++i)
        items.push_back(variant.names[i] + " : " + param_type(variant.params[i], variant.out[i]));
    return items.empty() ? std::string() : " (" + join(items, "; ") + ")";
}

// The statements of a subprogram calling native function `index` (with `receiver` first, unless empty).
void Generator::emit_call(std::ostream& out, const Variant& variant, std::size_t index, const std::string& receiver, bool returns_result,
                          const std::string& indent) const {
    bool writebacks = false;
    for (const bool w : variant.out) writebacks = writebacks || w;
    out << indent << "Rosetta_C : Rosetta.Call := Rosetta.Start (" << index << ");\n" << indent << "Rosetta_R : Rosetta.Reader;\n";
    out << indent.substr(3) << "begin\n";
    if (!receiver.empty()) out << indent << "Rosetta.Self_Arg (Rosetta_C, " << receiver << ");\n";
    std::size_t slot = 0;
    for (std::size_t i = 0; i < variant.params.size(); ++i) {
        const auto& type = variant.params[i];
        const auto& name = variant.names[i];
        if (!type.record.empty() && !variant.out[i]) {
            out << indent << "Rosetta.Object_Arg (Rosetta_C, " << name << ");\n";
            continue;
        }
        out << indent << "Rosetta.Arg (Rosetta_C);\n";
        if (variant.out[i]) out << indent << "Rosetta.Slot (Rosetta_C, " << slot++ << ");\n";
        out << indent << apply(variant.out[i] ? type.component_put() : type.put, "Rosetta_C.W", name) << ";\n";
    }
    out << indent << "Rosetta.Run (Rosetta_C, Rosetta_R);\n";
    if (!writebacks) {
        if (returns_result) out << indent << "return " << apply(variant.result.get, "Rosetta_R", "") << ";\n";
        return;
    }
    if (returns_result) out << indent << "declare\n" << indent << "   Rosetta_Result : constant " << variant.result.type << " := " << apply(variant.result.get, "Rosetta_R", "")
                            << ";\n" << indent << "begin\n";
    else out << indent << "Rosetta.Skip (Rosetta_R);\n";
    const auto inner = returns_result ? indent + "   " : indent;
    out << inner << "for Rosetta_K in 1 .. Rosetta.Count (Rosetta_R) loop\n" << inner << "   case Rosetta.Slot (Rosetta_R) is\n";
    slot = 0;
    for (std::size_t i = 0; i < variant.params.size(); ++i)
        if (variant.out[i])
            out << inner << "      when " << slot++ << " => " << variant.names[i] << " := " << apply(variant.params[i].component_get(), "Rosetta_R", "") << ";\n";
    out << inner << "      when others => Rosetta.Skip (Rosetta_R);\n" << inner << "   end case;\n" << inner << "end loop;\n";
    if (returns_result) out << inner << "return Rosetta_Result;\n" << indent << "end;\n";
}

// A module function, or a function of a policy module.
void Generator::emit_routine(std::ostream& spec, std::ostream& body, const Group& group, const std::string& indent) const {
    const auto variants = plan_variants(group.overloads);
    const auto& name = names_.at(&group);
    for (const auto& variant : variants) {
        const auto& function = *variant.overload->function;
        auto doc = variant_doc(variant, variants, "C++ " + function.qualified_name());
        const auto header = std::string(variant.returns ? "function " : "procedure ") + name + parameter_list(variant, "") +
                            (variant.returns ? " return " + variant.result.type : std::string());
        spec << "\n" << indent << header << ";\n" << doc.render(indent);
        body << "\n" << indent << header << " is\n";
        emit_call(body, variant, group.index, "", variant.returns, indent + "   ");
        body << indent << "end " << name << ";\n";
    }
}

void Generator::emit_members(std::ostream& spec, std::ostream& body, const std::string& owner, const std::string& from, bool copied) {
    const auto& plan = classes_.at(owner);
    const auto& source = classes_.at(from);
    const bool open = plan.director;
    const auto& record = record_names_.at(owner);
    auto& emitted = emitted_[owner];
    const auto inherited = [&](const std::string& key) -> const Emitted* {
        const auto found = emitted.find(key);
        return found == emitted.end() ? nullptr : &found->second;
    };
    const auto emit_group = [&](const Group& group, bool is_operator) {
        if (copied && group.is_static) return;
        if (is_operator && group.operand != 0) return;
        const auto& name = names_.at(&group);
        const bool symbol = is_operator && name.front() == '"';
        const auto variants = plan_variants(group.overloads);
        for (const auto& variant : variants) {
            const auto& function = *variant.overload->function;
            if (symbol) {
                // An Ada operator: Left (the object, a controlling access parameter) and Right.
                bool fits = variant.params.size() == (name == "\"not\"" || group.name == "Negate" || group.name == "Plus" ? 0u : 1u) && variant.returns;
                for (const bool w : variant.out) fits = fits && !w;
                if (!fits) continue;
            }
            const auto key = std::string(group.is_static ? "static " : "") + fold(name) + "(" + variant.key + ")";
            const auto* previous = inherited(key);
            if (previous && (copied || previous->result != variant.result.type)) continue;
            const bool to_string = name == "Image" && !is_operator && variant.params.empty();
            const bool overrides = (previous && !group.is_static) || to_string;
            const bool in_out = previous ? previous->in_out : (!function.is_const && !to_string);
            std::string what = variant.overload->extension ? "C++ " + function.qualified_name()
                                                           : "the C++ method " + source.record->qualified_name() + "::" + function.name;
            auto doc = variant_doc(variant, variants, what);
            if (!group.is_static && function.is_virtual && !variant.overload->extension && open && !copied && !previous)
                doc.text(std::string(function.is_pure ? "Pure virtual in C++: a" : "A") + "n Ada type extension overrides it, and C++ calls the override.");
            std::string header;
            if (group.is_static) {
                header = std::string(variant.returns ? "function " : "procedure ") + name + parameter_list(variant, "") +
                         (variant.returns ? " return " + variant.result.type : std::string());
            } else if (symbol) {
                Variant operands = variant;
                std::string self = "Left : not null access " + record;
                if (!operands.names.empty()) operands.names[0] = "Right";
                header = "function " + name + parameter_list(operands, operands.names.empty() ? "Right : not null access " + record : self) + " return " +
                         variant.result.type;
            } else {
                header = std::string(variant.returns ? "function " : "procedure ") + name +
                         parameter_list(variant, "Self : " + std::string(in_out ? "in out " : "") + record) +
                         (variant.returns ? " return " + variant.result.type : std::string());
            }
            spec << "\n   " << (overrides ? "overriding " : "") << header << ";\n" << doc.render("   ");
            body << "\n   " << (overrides ? "overriding " : "") << header << " is\n";
            if (to_string) {
                body << "   begin\n      if Self.Is_Closed then\n         return Native_Object_Record (Self).Image;\n      end if;\n      declare\n";
                emit_call(body, variant, group.index, "Self", true, "         ");
                body << "      end;\n   end " << name << ";\n";
            } else {
                Variant operands = variant;
                if (symbol && !operands.names.empty()) operands.names[0] = "Right";
                const auto receiver = group.is_static ? std::string() : symbol ? (variant.params.empty() ? "Right.all" : "Left.all") : "Self";
                emit_call(body, operands, group.index, receiver, variant.returns, "      ");
                body << "   end " << name << ";\n";
            }
            emitted[key] = {variant.result.type, function.is_virtual || to_string, in_out};
        }
    };
    for (const auto& group : source.methods) emit_group(group, false);
    for (const auto& group : source.operators) emit_group(group, true);
    // Fields are getter and setter operations.
    for (const auto& field : source.fields) {
        const auto& f = *field.field;
        const auto& name = field_names_.at(from + "::" + f.name);
        const auto key = "field " + fold(name);
        if (inherited(key)) continue;
        const bool writable = !f.type.is_const && !readonly(from + "::" + f.name, from) && f.type.kind != TypeKind::lvalue_reference;
        const auto type = ada_type(f.type, false);
        const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        const bool object = !type.record.empty();
        spec << "\n   function " << name << " (Self : " << record << ") return " << type.type << ";\n   --  "
             << (about.empty() ? "The C++ field " + from + "::" + f.name : about) << (writable ? "" : " (read-only)")
             << (object && referent(f.type).kind != TypeKind::pointer && f.type.kind != TypeKind::pointer ? "; the object belongs to this one" : "") << ".\n";
        // Members of an object (not pointers) keep their address: one Ada object for each, which this one owns.
        const bool member = object && bound(referent(f.type)) && f.type.kind != TypeKind::pointer;
        if (member) {
            const auto key = std::to_string(field.getter_index + 1);
            body << "\n   function " << name << " (Self : " << record << ") return " << type.type << " is\n"
                 << "      Rosetta_Self : constant Native_Object := Rosetta.Object_Of (Self);\n"
                 << "      Rosetta_Old : Native_Object := Rosetta.Find (Rosetta_Self, " << key << ");\n   begin\n"
                 << "      if Rosetta_Old /= null and then not Rosetta_Old.Is_Closed then\n         return " << type.type << " (Rosetta_Old);\n      end if;\n"
                 << "      declare\n         Rosetta_C : Rosetta.Call := Rosetta.Start (" << field.getter_index << ");\n         Rosetta_R : Rosetta.Reader;\n"
                 << "      begin\n         Rosetta.Self_Arg (Rosetta_C, Self);\n         Rosetta.Run (Rosetta_C, Rosetta_R);\n         declare\n"
                 << "            Rosetta_Result : constant Native_Object := Rosetta.Get_Object (Rosetta_R, " << class_index_.at(referent(f.type).declaration)
                 << ");\n         begin\n            if Rosetta_Old /= null then\n               Rosetta.Free_Object (Rosetta_Old);\n            end if;\n"
                 << "            if Rosetta_Result /= null then\n               if Rosetta_Result.Rosetta_Owner = null then\n"
                 << "                  Rosetta.Adopt (Rosetta_Self, Rosetta_Result, " << key << ");\n               else\n"
                 << "                  Rosetta_Result.Rosetta_Key := " << key << ";\n               end if;\n            end if;\n"
                 << "            return " << type.type << " (Rosetta_Result);\n         end;\n      end;\n   end " << name << ";\n";
        } else {
            body << "\n   function " << name << " (Self : " << record << ") return " << type.type << " is\n      Rosetta_C : Rosetta.Call := Rosetta.Start ("
                 << field.getter_index << ");\n      Rosetta_R : Rosetta.Reader;\n   begin\n      Rosetta.Self_Arg (Rosetta_C, Self);\n"
                 << "      Rosetta.Run (Rosetta_C, Rosetta_R);\n      return " << apply(type.get, "Rosetta_R", "") << ";\n   end " << name << ";\n";
        }
        if (writable) {
            const auto& setter = setter_names_.at(from + "::" + f.name);
            spec << "\n   procedure " << setter << " (Self : in out " << record << "; Value : " << param_type(type, false) << ");\n   --  Sets "
                 << name << ".\n";
            body << "\n   procedure " << setter << " (Self : in out " << record << "; Value : " << param_type(type, false)
                 << ") is\n      Rosetta_C : Rosetta.Call := Rosetta.Start (" << field.setter_index << ");\n      Rosetta_R : Rosetta.Reader;\n   begin\n"
                 << "      Rosetta.Self_Arg (Rosetta_C, Self);\n";
            if (object) body << "      Rosetta.Object_Arg (Rosetta_C, Value);\n";
            else body << "      Rosetta.Arg (Rosetta_C);\n      " << apply(type.put, "Rosetta_C.W", "Value") << ";\n";
            body << "      Rosetta.Run (Rosetta_C, Rosetta_R);\n   end " << setter << ";\n";
        }
        emitted[key] = {type.type, false, false};
    }
}

void Generator::emit_class(std::ostream& spec, std::ostream& body, const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = record_names_.at(owner);
    const auto primary = plan.bases.empty() ? std::string() : plan.bases.front();
    const auto parent = primary.empty() ? std::string("Native_Object_Record") : record_names_.at(primary);
    if (!primary.empty()) emitted_[owner] = emitted_[primary];
    auto doc = describe(plan.record->documentation, "Wraps the C++ class " + owner + ".");
    if (plan.bases.size() > 1) {
        std::vector<std::string> further;
        for (std::size_t i = 1; i < plan.bases.size(); ++i)
            further.push_back(classes_.at(plan.bases[i]).name + " (" + secondary_names_.at(owner + "::" + plan.bases[i]) + ")");
        doc.text("Its further C++ base classes' members are declared again here; pass it where one of them is expected through its As "
                 "function: " + join(further, ", ") + ".");
    }
    if (plan.director)
        doc.text(std::string(plan.record->abstract ? "Abstract in C++: extend" : "Extend") + " " + record +
                 " in Ada to override its virtual methods: Construct_" + plan.name + " makes the object of an Ada type extension a C++ subclass whose "
                 "virtual methods call the Ada operations, and a view conversion to " + record + " runs the C++ implementation.");
    else if (plan.record->abstract) doc.text("Abstract in C++: C++ functions return objects of derived classes.");
    spec << "\n   type " << record << " is new " << parent << " with private;\n" << doc.render("   ");
    // Constructors.
    const auto overloads = constructor_overloads_.count(owner) ? constructor_overloads_.at(owner) : std::vector<Overload>{};
    const auto variants = plan_variants(overloads);
    for (const auto& variant : variants) {
        auto about = variant_doc(variant, variants, "the C++ constructor of " + owner);
        const auto params = parameter_list(variant, "");
        const auto construct = construct_names_.at(owner);
        if (constructor_names_.count(owner)) {
            const auto& create = constructor_names_.at(owner);
            spec << "\n   function " << create << params << " return " << plan.name << ";\n" << about.render("   ");
            std::vector<std::string> names = variant.names;
            body << "\n   function " << create << params << " return " << plan.name << " is\n      Rosetta_Result : " << plan.name << " := new " << record
                 << ";\n   begin\n      " << construct << " (Rosetta_Result.all" << (names.empty() ? "" : ", " + join(names, ", ")) << ");\n"
                 << "      return Rosetta_Result;\n   exception\n      when others =>\n         Free (Rosetta_Result);\n         raise;\n   end " << create << ";\n";
        }
        const auto header = "procedure " + construct + parameter_list(variant, "Self : in out " + record + "'Class");
        spec << "\n   " << header << ";\n   --  Constructs the C++ object of Self (an object of " << record << " or of an Ada type extension of it,"
             << " whose virtual methods C++ then calls)" << (constructor_names_.count(owner) ? "; " + constructor_names_.at(owner) + " allocates and constructs." : ".")
             << "\n";
        body << "\n   " << header << " is\n      Rosetta_C : Rosetta.Call := Rosetta.Start_Construct (Self, " << plan.constructor_index << ", "
             << (plan.director ? "Self'Tag /= " + record + "'Tag" : std::string("False")) << ");\n   begin\n";
        for (std::size_t i = 0; i < variant.params.size(); ++i) {
            if (!variant.params[i].record.empty()) body << "      Rosetta.Object_Arg (Rosetta_C, " << variant.names[i] << ");\n";
            else body << "      Rosetta.Arg (Rosetta_C);\n      " << apply(variant.params[i].put, "Rosetta_C.W", variant.names[i]) << ";\n";
        }
        body << "      Rosetta.Finish (Self, Rosetta_C);\n   end " << construct << ";\n";
    }
    spec << "\n   procedure Free (Object : in out " << plan.name << ");\n   --  Frees the object (deleting the C++ object if it owns it), and sets Object to null.\n";
    body << "\n   procedure Free (Object : in out " << plan.name << ") is\n   begin\n      Rosetta_Free (Object);\n   end Free;\n";
    emit_members(spec, body, owner, owner, false);
    // Further bases: their members again, and As_Base.
    std::set<std::string> primary_chain;
    std::function<void(const std::string&)> collect = [&](const std::string& cls) {
        if (!primary_chain.insert(cls).second) return;
        for (const auto& base : classes_.at(cls).bases) collect(base);
    };
    if (!primary.empty()) collect(primary);
    std::set<std::string> done = primary_chain;
    std::function<void(const std::string&)> copy = [&](const std::string& cls) {
        if (!done.insert(cls).second) return;
        emit_members(spec, body, owner, cls, true);
        for (const auto& base : classes_.at(cls).bases) copy(base);
    };
    for (std::size_t i = 1; i < plan.bases.size(); ++i) {
        const auto& base = plan.bases[i];
        const auto& name = classes_.at(base).name;
        const auto& as = secondary_names_.at(owner + "::" + base);
        spec << "\n   function " << as << " (Self : " << record << ") return " << name << ";\n   --  This object as its further C++ base class " << name
             << "; the result belongs to this object.\n";
        body << "\n   function " << as << " (Self : " << record << ") return " << name << " is\n   begin\n      return " << name
             << " (Rosetta.Cast (Rosetta.Object_Of (Self), " << class_index_.at(base) << "));\n   end " << as << ";\n";
        copy(base);
    }
    for (const auto& constant : plan.constants) {
        const auto type = ada_type(*constant.type, false);
        auto about = describe(*constant.documentation, "The C++ constant " + constant.expression + ".");
        const auto& name = constant_names_.at(&constant);
        spec << "\n   function " << name << " return " << type.type << ";\n" << about.render("   ");
        body << "\n   function " << name << " return " << type.type << " is\n      Rosetta_C : Rosetta.Call := Rosetta.Start (" << constant.index
             << ");\n      Rosetta_R : Rosetta.Reader;\n   begin\n      Rosetta.Run (Rosetta_C, Rosetta_R);\n      return " << apply(type.get, "Rosetta_R", "")
             << ";\n   end " << name << ";\n";
    }
}

void Generator::emit_enum(std::ostream& spec, std::ostream& body, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto doc = describe(enumeration.documentation, "The C++ enumeration " + qualified + ".");
    // Ada enumerators ascend and have distinct values; aliases of a value are left out.
    std::vector<std::pair<long long, std::size_t>> order;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
        if (!cases[i].empty()) order.emplace_back(std::stoll(enumeration.enumerator_values[i]), i);
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> literals, values;
    bool wide = false;
    for (const auto& [value, i] : order) {
        literals.push_back(cases[i]);
        values.push_back(cases[i] + " => " + std::to_string(value));
        wide = wide || value < -2147483648LL || value > 2147483647LL;
    }
    spec << "\n   type " << name << " is (" << join(literals, ", ") << ");\n" << doc.render("   ") << "   for " << name << " use (" << join(values, ", ") << ");\n";
    if (wide) spec << "   for " << name << "'Size use 64;\n";
    body << "\n   function Rosetta_Get_" << name << " (Rosetta_R : in out Rosetta.Reader) return " << name << " is\n"
         << "      Rosetta_V : constant Interfaces.Integer_64 :=\n        Rosetta.Get_Int (Rosetta_R, Interfaces.Integer_64'First, Interfaces.Integer_64'Last, "
         << ada_string(name) << ");\n   begin\n      case Rosetta_V is\n";
    for (const auto& [value, i] : order) body << "         when " << value << " => return " << cases[i] << ";\n";
    body << "         when others => raise Binding_Error with \"C++ value\" & Rosetta_V'Image & \" is not a " << name << "\";\n      end case;\n   end Rosetta_Get_"
         << name << ";\n";
}

// The function C++ calls the virtual methods of Ada type extensions through: the Ada operation of the object.
std::string Generator::dispatch() {
    std::ostringstream out;
    out << "\n   --  The Ada operations C++ calls for the virtual methods of Ada type extensions.\n   function Rosetta_Dispatch\n"
        << "     (Rosetta_Obj : Native_Object; Rosetta_Method : Natural; Rosetta_R : in out Rosetta.Reader; Rosetta_Count : Natural;\n"
        << "      Rosetta_W : in out Rosetta.Writer) return Boolean\n   is\n   begin\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        if (!plan.director) continue;
        const auto& record = record_names_.at(owner);
        for (const auto& item : overridable(plan)) {
            const auto generic = generic_index_.find(item.name);
            const auto name = mapped_.at(owner).find("m:" + item.name);
            if (generic == generic_index_.end() || name == mapped_.at(owner).end()) continue;
            const auto& method = *item.method;
            std::vector<std::string> types, arguments, locals;
            bool supported = true;
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                const auto& type = method.parameters[i];
                const auto at = ada_type(type);
                if (at.callback) supported = false;
                const bool out_param = writeback(type);
                types.push_back(fold(param_type(at, false)));
                const auto local = "Rosetta_" + std::to_string(i + 1);
                const auto local_type = out_param ? at.component() : at.type;
                locals.push_back("            " + local + " : " + (out_param ? "" : "constant ") + local_type + " := " +
                                 apply(out_param ? at.component_get() : at.get, "Rosetta_R", "") + ";\n");
                arguments.push_back(local);
            }
            const auto key = fold(name->second) + "(" + join(types, ", ") + ")";
            if (!supported || !emitted_[owner].count(key)) continue;
            const bool returns = !is_void(method.result);
            out << "      if Rosetta_Method = " << generic->second << " and then Rosetta_Count = " << method.parameters.size()
                << " and then Rosetta_Obj.all in " << record << "'Class then\n";
            std::string indent = "         ";
            if (!locals.empty()) {
                out << "         declare\n" << join(locals, "") << "         begin\n";
                indent = "            ";
            }
            std::vector<std::string> call_args = {record + "'Class (Rosetta_Obj.all)"};
            call_args.insert(call_args.end(), arguments.begin(), arguments.end());
            const auto call = name->second + " (" + join(call_args, ", ") + ")";
            if (returns) {
                const auto result = ada_type(method.result, false);
                out << indent << apply(result.put, "Rosetta_W", result.type + "'(" + call + ")") << ";\n";
            } else {
                out << indent << call << ";\n" << indent << "Rosetta.Put_Nil (Rosetta_W);\n";
            }
            if (!locals.empty()) out << "         end;\n";
            out << "         return True;\n      end if;\n";
        }
    }
    out << "      return False;\n   end Rosetta_Dispatch;\n";
    return out.str();
}

GeneratedAdaSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = options_.package.empty() ? ada_package_name(name_) : options_.package;
    bool valid = !package_.empty() && std::isalpha(static_cast<unsigned char>(package_[0])) && package_.back() != '_' &&
                 package_.find("__") == std::string::npos;
    for (const char c : package_) valid = valid && (is_alnum(c) || c == '_');
    if (!valid || ada_keyword(package_) || reserved_names().count(fold(package_)))
        throw BindingError("Ada package name must be an Ada identifier and not a keyword or a predefined name: " + package_);
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Ada library name must be a plain file name: " + library_);
    GeneratedAdaSource result;
    prepare();
    for (const auto& name : reserved_names()) types_.taken.insert(name);
    types_.reserve(package_);
    // A type must not hide the runtime's subprograms (Native_Object_Record's operations) or the
    // Native_Error_Kind literals: a C++ class Image or Callback is Image_Access or Callback_Access.
    for (const char* name : {"Close", "Is_Closed", "Image", "Free", "Finalize", "Initialize", "Adjust", "Native", "Invalid_Argument",
                             "Out_Of_Range", "Domain", "Length", "Arithmetic", "Allocation", "Binding", "Callback", "Not_Native"})
        subprograms_.insert(fold(name));
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto base = ada_case(plan.name);
        auto record = claim_type(base + "_Record", "_Type");
        plan.name = claim_type(base, "_Access");
        record_names_[owner] = record;
    }
    for (std::size_t i = 0; i < class_order_.size(); ++i) class_index_[class_order_[i]] = i;
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + ada_case(name);
        enum_names_[qualified] = claim_type(ada_case(name), "_Type");
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        Names scope;
        scope.taken = types_.taken;
        std::set<long long> values;
        bool fits = !enumeration.enumerators.empty();
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto& text = enumeration.enumerator_values[i];
            long long value = 0;
            try { value = std::stoll(text); } catch (...) { fits = false; break; }
            if (!values.insert(value).second) {
                enum_cases_[qualified].push_back({});   // an alias of an earlier value
                continue;
            }
            // An enumerator named like a type takes the enumeration's name (Circle_Kind).
            auto wanted = ada_case(renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]));
            if (!scope.free(wanted) || ada_keyword(wanted)) wanted += "_" + name;
            enum_cases_[qualified].push_back(scope.claim(wanted));
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
    // Policy modules are nested packages (a.b nests).
    for (const auto& group : functions) {
        if (group.module.empty() || submodules_.count(group.module)) continue;
        const auto dot = group.module.find('.');
        std::string name;
        for (const auto& [module, path] : submodules_)
            if (module.substr(0, module.find('.')) == group.module.substr(0, dot)) name = path.substr(0, path.find('.'));
        if (name.empty()) name = claim_type(ada_case(group.module.substr(0, dot)), "_Functions");
        std::size_t start = dot;
        while (start != std::string::npos) {
            const auto next = group.module.find('.', start + 1);
            name += "." + ada_case(group.module.substr(start + 1, next == std::string::npos ? std::string::npos : next - start - 1));
            start = next;
        }
        submodules_[group.module] = name;
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Ada package " + package_ + " (" + name_ + ").");

    // ------------------------------------------------------------ the package
    std::ostringstream enums_spec, enums_body, classes_spec, classes_body, functions_spec, functions_body, modules_spec, modules_body;
    for (const auto& [qualified, name] : enum_names_)
        if (!enum_int64_.count(qualified)) emit_enum(enums_spec, enums_body, qualified);
    for (const auto& owner : class_order_) emit_class(classes_spec, classes_body, owner);
    for (const auto& group : functions)
        if (group.module.empty()) emit_routine(functions_spec, functions_body, group, "   ");
    for (const auto& constant : constants) {
        const auto type = ada_type(*constant.type, false);
        auto about = describe(*constant.documentation, "The C++ constant " + normalize(constant.expression) + ".");
        const auto& name = constant_names_.at(&constant);
        functions_spec << "\n   function " << name << " return " << type.type << ";\n" << about.render("   ");
        functions_body << "\n   function " << name << " return " << type.type << " is\n      Rosetta_C : Rosetta.Call := Rosetta.Start (" << constant.index
                       << ");\n      Rosetta_R : Rosetta.Reader;\n   begin\n      Rosetta.Run (Rosetta_C, Rosetta_R);\n      return "
                       << apply(type.get, "Rosetta_R", "") << ";\n   end " << name << ";\n";
    }
    // Policy modules: nested packages.
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
        modules_spec << "\n" << indent << "package " << name << " is\n" << indent << "   --  "
                     << (module.empty() ? std::string("Policy modules.") : "The functions of the C++ namespace " + from + " (policy module " + module + ").")
                     << "\n";
        modules_body << "\n" << indent << "package body " << name << " is\n";
        for (const auto& group : functions)
            if (!module.empty() && group.module == module) emit_routine(modules_spec, modules_body, group, indent + "   ");
        for (const auto& child : children[path]) emit_namespace(child, indent + "   ");
        modules_spec << indent << "end " << name << ";\n";
        modules_body << indent << "end " << name << ";\n";
    };
    for (const auto& top : children[""]) emit_namespace(top, "   ");

    const auto runtime = [&](const char* const* parts, std::size_t count) {
        std::string text;
        for (std::size_t i = 0; i < count; ++i) text += parts[i];
        return replace_all(replace_all(text, "@C@", "rosetta_" + name_), "@LIB@", library_);
    };
    std::ostringstream spec;
    spec << "--  Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n--\n"
         << "--  The Ada interface to the C++ library " << name_ << " (Ada 2022). Each C++ class is a tagged type\n"
         << "--  (Circle_Record) with an access type (Circle) that functions take and return; objects come from\n"
         << "--  New_Circle and are freed with Free. Each module function is a subprogram, each C++ constant a\n"
         << "--  function. Overloaded C++ functions are Ada overloads, one per argument count for default\n"
         << "--  arguments, which C++ resolves again at run time. Calls raise Native_Error and the other\n"
         << "--  exceptions below for C++ exceptions and values C++ does not accept. Links the native library " << library_ << ".\n\n";
    std::set<std::string> withs = {"Ada.Exceptions", "Ada.Finalization", "Ada.Numerics.Long_Complex_Types", "Ada.Strings.Unbounded", "Interfaces"};
    withs.insert(spec_withs_.begin(), spec_withs_.end());
    for (const auto& with : withs) spec << "with " << with << ";\n";
    spec << "\npackage " << package_ << " is\n\n" << runtime(detail::ada_full_runtime_spec_public, std::size(detail::ada_full_runtime_spec_public));
    if (!enum_names_.empty()) spec << "\n   ---------------------------------------------------------------------------\n   --  Enumerations\n"
                                      "   ---------------------------------------------------------------------------\n" << enums_spec.str();
    spec << "\n   ---------------------------------------------------------------------------\n   --  Classes and helper types\n"
            "   ---------------------------------------------------------------------------\n\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        spec << "   type " << record_names_.at(owner) << " is tagged;\n   type " << plan.name << " is access all " << record_names_.at(owner) << "'Class;\n"
             << "   --  An object of the C++ class " << owner << ".\n";
    }
    for (const auto& text : type_decls_) spec << "\n" << text;
    spec << classes_spec.str();
    for (const auto& text : native_decls_) spec << "\n" << text;
    if (!functions_spec.str().empty())
        spec << "\n   ---------------------------------------------------------------------------\n   --  Functions and constants\n"
                "   ---------------------------------------------------------------------------\n" << functions_spec.str();
    spec << modules_spec.str() << "\nprivate\n\n" << runtime(detail::ada_full_runtime_spec_private, std::size(detail::ada_full_runtime_spec_private));
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const auto parent = plan.bases.empty() ? std::string("Native_Object_Record") : record_names_.at(plan.bases.front());
        spec << "\n   type " << record_names_.at(owner) << " is new " << parent << " with null record;";
    }
    if (!class_order_.empty()) spec << "\n";
    for (const auto& text : native_private_) spec << "\n" << text;
    spec << "\nend " << package_ << ";\n";
    result.spec = spec.str();

    std::ostringstream body;
    body << "--  Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\n";
    std::set<std::string> body_withs = {"Ada.Containers.Ordered_Maps", "Ada.Containers.Vectors", "Ada.Strings.Fixed", "Ada.Tags",
                                        "Ada.Unchecked_Conversion", "Ada.Unchecked_Deallocation", "Interfaces.C", "System.Address_To_Access_Conversions",
                                        "System.Storage_Elements"};
    for (const auto& with : body_withs)
        if (!withs.count(with)) body << "with " << with << ";\n";
    body << "\npackage body " << package_ << " is\n\n   --  Generated codecs serve every API; not every API uses each one.\n   pragma Warnings (Off, \"*is not referenced*\");\n\n" << runtime(detail::ada_full_runtime_body, std::size(detail::ada_full_runtime_body))
         << "\n   ---------------------------------------------------------------------------\n   --  The API\n"
            "   ---------------------------------------------------------------------------\n\n   use type Ada.Tags.Tag;\n\n";
    for (const auto& owner : class_order_) {
        const auto& plan = classes_.at(owner);
        const auto& record = record_names_.at(owner);
        body << "   function Rosetta_Make_" << plan.name << " return Native_Object is (new " << record << ");\n"
             << "   procedure Rosetta_Free is new Ada.Unchecked_Deallocation (" << record << "'Class, " << plan.name << ");\n";
    }
    for (const auto& text : maker_decls_) body << text;
    body << enums_body.str();
    for (const auto& text : codec_decls_) body << "\n" << text;
    body << classes_body.str() << functions_body.str() << modules_body.str() << dispatch();
    body << "\nbegin\n   Rosetta.Classes := new Rosetta.Class_Array'(";
    if (class_order_.empty()) body << "1 .. 0 => <>";
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& plan = classes_.at(class_order_[i]);
        std::vector<std::string> bases;
        for (const auto& base : plan.bases) bases.push_back(std::to_string(bases.size() + 1) + " => " + std::to_string(class_index_.at(base)));
        body << (i ? "," : "") << "\n      " << i << " => (Name => Ada.Strings.Unbounded.To_Unbounded_String (" << ada_string(plan.name) << "), Tag => "
             << record_names_.at(class_order_[i]) << "'Tag,\n            Make => Rosetta_Make_" << plan.name << "'Access, Bases => new Rosetta.Index_Array'("
             << (bases.empty() ? std::string("1 .. 0 => 0") : join(bases, ", ")) << "))";
    }
    body << ");\n   Rosetta.Dispatcher := Rosetta_Dispatch'Access;\n   Rosetta.Initialize_Library;\nend " << package_ << ";\n";
    const auto lower_name = fold(package_);
    result.files.emplace_back(lower_name + ".adb", body.str());
    std::ostringstream project;
    project << "--  Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n\nproject " << package_ << " is\n"
            << "   for Source_Dirs use (\".\");\n   for Languages use (\"Ada\");\n\n   package Compiler is\n      for Default_Switches (\"Ada\") use (\"-gnat2022\");\n"
            << "   end Compiler;\n\n   package Linker is\n      for Linker_Options use (\"-l" << library_ << "\");\n   end Linker;\nend " << package_ << ";\n";
    result.files.emplace_back(lower_name + ".gpr", project.str());
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

std::string ada_package_name(const std::string& name) { return ada_case(name); }
std::string ada_spec_file(const std::string& package) { return fold(package) + ".ads"; }

GeneratedAdaSource generate_ada(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                const AdaOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Ada C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
