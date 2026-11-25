// Objective-C backend (--language objective-c): Objective-C classes (Foundation and ARC) over the
// shared C ABI library of the full C ABI backends (cabi_full.hpp, lisp_full_runtime.hpp), which the
// generated code calls directly.
//
// Every generated name starts with the prefix (default: the module name in PascalCase). Classes
// are NSObject subclasses over PREFIXObject; the first C++ base class is the superclass, and the
// members of further bases are declared again (C++ upcasts the object), with an asBase property
// for passing the object where a further base is expected. An Objective-C subclass overrides C++
// virtual methods: initializing it creates a C++ subclass whose virtual methods send the
// Objective-C messages (super runs the C++ implementation). Constructors are initializers
// (initWithX:y:), methods instance methods, static methods class methods, fields properties, and
// module functions and constants class methods and class properties of the module class (named
// the prefix). Selectors come from the C++ names: the method name, then the parameter names
// (scale:extra:). One selector per argument count covers the overloads and default arguments of a
// C++ function: a parameter whose type the overloads disagree on is an `id`, a result an `id`
// unless all agree, and C++ chooses the overload at run time. Calls raise PREFIXError; enums are
// NS_ENUMs, containers NSArray, NSSet, NSDictionary (tuples NSArray), optional scalars NSNumber,
// std::function blocks, non-const reference parameters pointers, operators named methods
// (add:, at:, call...) and to_string() -description (its zero-argument call, when it has others). Doxygen becomes /// comments.
//
// The output is PREFIX.h (the API and the runtime's public types) and PREFIX.m (the runtime and
// the implementation; compile it with -fobjc-arc -fobjc-arc-exceptions).
#include "rosetta/objc.hpp"
#include "cabi_full.hpp"
#include "objc_full_runtime.hpp"
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

// C and Objective-C keywords, and names the generated code or Foundation's headers give a meaning
// (macros, types a parameter would hide).
bool objc_keyword(const std::string& name) {
    static const std::set<std::string> words = {"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
        "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed",
        "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "asm", "typeof", "alignas",
        "alignof", "bool", "true", "false", "id", "self", "super", "nil", "Nil", "YES", "NO", "SEL", "BOOL", "Class", "IMP", "in", "out",
        "inout", "bycopy", "byref", "oneway", "instancetype", "NULL", "TRUE", "FALSE", "Protocol", "errno", "assert", "va_list",
        "NSInteger", "NSUInteger", "CGFloat", "complex", "imaginary", "I", "unix", "linux", "isa", "_Bool", "_Complex", "_Imaginary", "_Nonnull", "_Nullable", "__strong", "__weak", "__block", "main"};
    return words.count(name) != 0;
}
// Selectors of NSObject (and PREFIXObject) a generated method must not take over.
bool objc_reserved_selector(const std::string& name) {
    static const std::set<std::string> words = {"init", "new", "alloc", "allocWithZone", "copy", "copyWithZone", "mutableCopy",
        "mutableCopyWithZone", "dealloc", "retain", "release", "autorelease", "retainCount", "description", "debugDescription", "hash",
        "isEqual", "self", "class", "superclass", "zone", "isProxy", "isKindOfClass", "isMemberOfClass", "respondsToSelector",
        "conformsToProtocol", "performSelector", "methodForSelector", "methodSignatureForSelector", "forwardInvocation",
        "forwardingTargetForSelector", "doesNotRecognizeSelector", "finalize", "load", "initialize", "instancesRespondToSelector",
        "instanceMethodForSelector", "instanceMethodSignatureForSelector", "resolveClassMethod", "resolveInstanceMethod", "version",
        "setVersion", "className", "classForCoder", "replacementObjectForCoder", "awakeAfterUsingCoder", "valueForKey", "setValue",
        "valueForKeyPath", "subclassResponsibility", "notImplemented", "shouldNotImplement", "close", "isClosed", "cast", "isa",
        "initRosetta", "initRosettaWithHandle"};
    return words.count(name) != 0;
}
// Whether ARC would put a selector in a method family (alloc, copy, init, mutableCopy, new).
bool method_family(const std::string& selector) {
    for (const std::string family : {"alloc", "copy", "mutableCopy", "new", "init"}) {
        if (selector.rfind(family, 0) != 0) continue;
        if (selector.size() == family.size() || !is_lower(selector[family.size()])) return true;
    }
    return false;
}

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
std::string cap(std::string text) {
    if (!text.empty()) text[0] = upper(text[0]);
    return text;
}
std::string objc_string(const std::string& text) {
    std::string result = "@\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') { result += "\\n"; continue; }
        result += c;
    }
    return result + "\"";
}
std::string c_string(const std::string& text) { return objc_string(text).substr(1); }
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

// Names in one Objective-C scope.
struct Names {
    std::set<std::string> taken;
    bool reserve(const std::string& name) { return taken.insert(name).second; }
    bool free(const std::string& name) const { return !taken.count(name); }
    // A member, function or parameter name: with an underscore if it is a keyword or a reserved
    // selector, else with a number (area2).
    std::string claim(std::string wanted) {
        if (objc_keyword(wanted) || objc_reserved_selector(wanted)) wanted += "_";
        if (reserve(wanted)) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
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
    void code(const std::string& text) {
        blank();
        lines.push_back("@code");
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        lines.insert(lines.end(), body.begin(), body.end());
        lines.push_back("@endcode");
        lines.push_back("");
    }
    void list(const std::string& title, const std::vector<std::string>& items) {
        if (items.empty()) return;
        blank();
        if (!title.empty()) lines.push_back(title);
        for (const auto& item : items) lines.push_back("- " + item);
        lines.push_back("");
    }
    void tag(const std::string& text) { lines.push_back(text); }
    std::string render(const std::string& indent = {}) const {
        auto copy = lines;
        while (!copy.empty() && copy.back().empty()) copy.pop_back();
        std::string out;
        bool previous_blank = true;
        for (const auto& line : copy) {
            if (line.empty() && previous_blank) continue;
            previous_blank = line.empty();
            out += indent + "///" + (line.empty() ? "" : " " + line) + "\n";
        }
        return out;
    }
};

std::string error_kind(const std::string& prefix, const std::string& name) {
    const auto bare = normalize(name);
    std::string kind = "Native";
    if (bare == "std::invalid_argument") kind = "InvalidArgument";
    else if (bare == "std::out_of_range") kind = "OutOfRange";
    else if (bare == "std::domain_error") kind = "DomainError";
    else if (bare == "std::length_error") kind = "LengthError";
    else if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") kind = "ArithmeticError";
    else if (bare == "std::bad_alloc") kind = "Allocation";
    return prefix + "ErrorKind" + kind;
}

// C++ operators: their method names ("" = not bound).
std::string objc_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "call";
    static const std::map<std::string, std::pair<const char*, const char*>> table = {{"operator+", {"pos", "add"}},
        {"operator-", {"neg", "subtract"}}, {"operator*", {nullptr, "multiply"}}, {"operator/", {nullptr, "divide"}},
        {"operator%", {nullptr, "modulo"}}, {"operator==", {nullptr, "equals"}}, {"operator!=", {nullptr, "notEquals"}},
        {"operator<", {nullptr, "less"}}, {"operator<=", {nullptr, "lessEqual"}}, {"operator>", {nullptr, "greater"}},
        {"operator>=", {nullptr, "greaterEqual"}}, {"operator&", {nullptr, "bitAnd"}}, {"operator|", {nullptr, "bitOr"}},
        {"operator^", {nullptr, "bitXor"}}, {"operator~", {"complement", nullptr}}, {"operator!", {"not", nullptr}},
        {"operator<<", {nullptr, "shiftLeft"}}, {"operator>>", {nullptr, "shiftRight"}}, {"operator[]", {nullptr, "at"}},
        {"operator+=", {nullptr, "addAssign"}}, {"operator-=", {nullptr, "subtractAssign"}}, {"operator*=", {nullptr, "multiplyAssign"}},
        {"operator/=", {nullptr, "divideAssign"}}};
    const auto found = table.find(name);
    if (found == table.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

// A C++ type in Objective-C: its spelling, its descriptor (see the runtime), and what it is.
struct OT {
    std::string type = "id";       // without nullability: int, NSString *, id, the result of a block
    std::string desc = "?";
    std::string cls;               // a class type: its qualified C++ name
    char scalar = 0;               // a C scalar: its descriptor letter ('E': an enum), 'v' void
    bool nullable = true;
    bool block = false;            // a block: parts[0] the result, then the parameters
    std::vector<OT> parts;
    bool any() const { return desc == "?"; }
    bool is_void() const { return scalar == 'v'; }
};
OT scalar(const std::string& type, char desc) {
    OT result;
    result.type = type;
    result.desc = std::string(1, desc);
    result.scalar = desc;
    result.nullable = false;
    return result;
}
OT object(const std::string& type, const std::string& desc) {
    OT result;
    result.type = type;
    result.desc = desc;
    result.nullable = false;
    return result;
}
OT void_type() {
    OT result = scalar("void", 'v');
    return result;
}

// The type with a declarator name (empty: an abstract declarator).
std::string spell(const OT& type, const std::string& name = {}) {
    if (type.is_void()) return name.empty() ? "void" : "void " + name;
    if (type.scalar) return name.empty() ? type.type : type.type + " " + name;
    if (type.block) {
        std::vector<std::string> parameters;
        for (std::size_t i = 1; i < type.parts.size(); ++i) parameters.push_back(spell(type.parts[i]));
        return spell(type.parts[0]) + " (^" + (type.nullable ? std::string("_Nullable") + (name.empty() ? "" : " ") : "") + name + ")(" +
               (parameters.empty() ? std::string("void") : join(parameters, ", ")) + ")";
    }
    if (type.nullable) return type.type + " _Nullable" + (name.empty() ? "" : " " + name);
    if (name.empty()) return type.type;
    return ends(type.type, "*") ? type.type + name : type.type + " " + name;
}
// A pointer to the type (a parameter C++ writes back).
std::string spell_pointer(const OT& type) {
    if (type.scalar) return type.type + " *";
    return type.type + (type.nullable ? " _Nullable" : " _Nonnull") + " * _Nonnull";
}
// The type as an element of a lightweight generic.
std::string element(const OT& type) {
    if (type.scalar) return "NSNumber *";
    if (type.nullable) return "id";
    return type.type;
}
const char* accessor(char desc) {
    switch (desc) {
    case 'B': return "boolValue";
    case 'c': case 'a': return "charValue";
    case 'C': return "unsignedCharValue";
    case 's': return "shortValue";
    case 'S': return "unsignedShortValue";
    case 'i': return "intValue";
    case 'I': return "unsignedIntValue";
    case 'Q': return "unsignedLongLongValue";
    case 'f': return "floatValue";
    case 'd': return "doubleValue";
    default: return "longLongValue";
    }
}

// One overload a selector calls: the C++ function and its native function number.
struct Entry {
    const Overload* overload;
    const std::size_t* at;   // the group's native function number (assigned when the C++ is emitted)
};
struct Param {
    OT type;
    std::string name;
    bool ref = false;        // a pointer C++ writes back through
};
// One selector of a C++ function: its argument count, parameters, labels and result.
struct Sel {
    std::size_t count = 0;
    std::vector<Param> params;
    std::vector<std::string> labels;   // labels[0]: the selector's first part
    OT result;
    const std::size_t* at = nullptr;
    std::vector<Entry> entries;        // the overloads taking this many arguments
    std::string selector() const {
        if (count == 0) return labels[0];
        std::string text;
        for (std::size_t i = 0; i < count; ++i) text += labels[i] + ":";
        return text;
    }
};
struct Member {
    char kind = 'm';                    // m: method, s: class method, f: property, d: -description
    std::vector<Entry> entries;
    std::map<std::size_t, Sel> sels;    // by argument count
    std::string from;                   // the class declaring it
    const FieldPlan* field = nullptr;
    OT type;                            // a property's type
    std::string setter;                 // its setter's first selector part
    bool is_virtual = false;
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const ObjcOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedObjcSource run();

private:
    ObjcOptions options_;
    std::string prefix_, library_;
    Names types_;                                            // global names (classes, enums, enumerators)
    std::map<std::string, std::size_t> class_index_;
    std::map<std::string, Names> members_;                   // class -> member names (inherited ones included)
    std::map<std::string, std::map<std::string, std::string>> mapped_;   // class -> C++ member key -> name
    std::map<std::string, std::map<std::string, Member>> table_;         // class -> member name -> what it is
    std::map<std::string, std::vector<std::string>> order_;              // class -> its own and copied members, in order
    std::map<std::string, std::map<std::size_t, Sel>> constructors_;     // class -> initializers by argument count
    std::map<std::string, std::map<std::string, std::string>> inits_;    // class -> initializer selector -> declaration
    std::map<std::string, std::string> secondary_names_;
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::map<std::string, std::string> submodules_;          // policy module -> Objective-C class
    std::map<std::string, Names> modules_;                   // module class -> names
    std::map<const Group*, Member> functions_;

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Objective-C"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return objc_operator(cpp, arity); }
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
        return plan.name + " is abstract: subclass it in Objective-C and override its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: subclass it in Objective-C";
    }
    std::string symbol_prefix() const override { return "rosetta_objc_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_OBJC"; }
    std::string module_prefix() const override { return "rosetta_objc_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    bool derives_class(const std::string& from, const std::string& to) const {
        if (from == to) return true;
        const auto found = classes_.find(from);
        if (found == classes_.end()) return false;
        for (const auto& base : found->second.bases) if (derives_class(base, to)) return true;
        return false;
    }
    OT class_type(const std::string& qualified) const {
        OT result = object(classes_.at(qualified).name + " *", "O" + std::to_string(class_index_.at(qualified)) + ";");
        result.cls = qualified;
        return result;
    }
    static OT optional(const OT& inner) {
        if (inner.nullable || inner.is_void()) return inner;
        OT result = inner;
        if (inner.scalar) {
            result.type = "NSNumber *";
            result.scalar = 0;
        }
        result.desc = "n" + inner.desc;
        result.nullable = true;
        return result;
    }
    OT objc_type(const Type& type, bool top = true) const;
    // Whether an enum's underlying type is unsigned (its NS_ENUM is over uint64_t).
    bool unsigned_enum(const std::string& qualified) const {
        const auto& type = enums_.at(qualified)->underlying_type;
        return type.find("unsigned") != std::string::npos || type.find("uint") != std::string::npos || type.find("size_t") != std::string::npos ||
               type == "bool" || type == "char16_t" || type == "char32_t";
    }
    OT enum_type(const std::string& qualified) const {
        OT result = scalar(enum_names_.at(qualified), unsigned_enum(qualified) ? 'Q' : 'q');
        result.scalar = 'E';
        return result;
    }
    OT objc_spelled(std::string cpp) const;
    std::string objc_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    std::map<std::size_t, Sel> make_sels(const std::vector<Entry>& entries, const std::string& first,
                                         const std::map<std::size_t, Sel>* inherited, bool constructor) const;
    OT union_result(const std::vector<OT>& types) const;
    bool same_sel(const Sel& sel, const Sel& inherited) const;
    std::string to_id(const OT& type, const std::string& expression) const;
    std::string from_id(const OT& type, const std::string& expression) const;

    // ------------------------------------------------------------ naming and emission
    std::string claim_type(std::string wanted);
    // A name with the prefix (unless it starts with it already: a C++ namespace named as the module).
    std::string prefixed(const std::string& name) const {
        return starts(name, prefix_) && name.size() > prefix_.size() && is_upper(name[prefix_.size()]) ? name : prefix_ + name;
    }
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    Doc doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Doc& comment, const Documentation& doc) const;
    Doc sel_doc(const Sel& sel, const std::string& what) const;
    std::string declaration(const Sel& sel, bool is_static, const std::string& result, bool implementation = false) const;
    void emit_body(std::ostream& out, const Sel& sel, const std::string& receiver) const;
    void emit_sels(std::ostream& header, std::ostream& impl, const Member& member, const std::string& what, bool is_static,
                   bool declare) const;
    void emit_property(std::ostream& header, std::ostream& impl, const std::string& name, const Member& member, bool declare) const;
    void emit_class(std::ostream& header, std::ostream& impl, const std::string& owner);
    void emit_enum(std::ostream& out, const std::string& qualified) const;
    std::string dispatch() const;
};

// ---------------------------------------------------------------- types
OT Generator::objc_type(const Type& type, bool top) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return object("NSString *", "@");
        if (is_void(pointee)) return {};
        return optional(objc_type(pointee, top));
    }
    if (is_void(target)) return void_type();
    if (!conversion(target).empty()) return objc_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        const auto spelled = normalize(strip_cv(target.spelling));
        if (spelled == "size_t" || spelled == "std::size_t") return scalar("size_t", 'Q');
        if (spelled == "ssize_t" || spelled == "ptrdiff_t" || spelled == "std::ptrdiff_t") return scalar("ptrdiff_t", 'q');
        static const std::map<std::string, char> fixed = {{"int8_t", 'a'}, {"int16_t", 's'}, {"int32_t", 'i'}, {"int64_t", 'q'},
            {"uint8_t", 'C'}, {"uint16_t", 'S'}, {"uint32_t", 'I'}, {"uint64_t", 'Q'}};
        const auto plain = starts(spelled, "std::") ? spelled.substr(5) : spelled;
        const auto width = fixed.find(plain);
        if (width != fixed.end()) return scalar(plain, width->second);
        static const std::map<std::string, std::pair<std::string, char>> builtins = {{"bool", {"BOOL", 'B'}}, {"char", {"char", 'c'}},
            {"signed char", {"int8_t", 'a'}}, {"unsigned char", {"uint8_t", 'C'}}, {"short", {"short", 's'}},
            {"unsigned short", {"unsigned short", 'S'}}, {"int", {"int", 'i'}}, {"unsigned int", {"unsigned int", 'I'}},
            {"long", {"long", 'q'}}, {"unsigned long", {"unsigned long", 'Q'}}, {"long long", {"long long", 'q'}},
            {"unsigned long long", {"unsigned long long", 'Q'}}, {"float", {"float", 'f'}}, {"double", {"double", 'd'}},
            {"long double", {"double", 'd'}}, {"char16_t", {"uint16_t", 'S'}}, {"char32_t", {"uint32_t", 'I'}}, {"wchar_t", {"int32_t", 'i'}}};
        const auto found = builtins.find(bare);
        if (found == builtins.end()) return {};
        return scalar(found->second.first, found->second.second);
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_names_.find(target.declaration);
        if (found == enum_names_.end()) return scalar("long long", 'q');
        return enum_type(target.declaration);
    }
    if (bound(target)) return class_type(target.declaration);
    if (bare == "std::filesystem::path") return object("NSString *", "@");
    if (bare == "std::nullptr_t" || bare == "std::monostate") return void_type();
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? objc_type(target.arguments[i], false) : OT{}; };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return object("NSString *", "@");
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::span") {
            const auto item = argument(0);
            if (item.desc == "C") return object("NSData *", "y");
            return object("NSArray<" + element(item) + "> *", "[" + item.desc);
        }
        if (declaration == "std::set" || declaration == "std::unordered_set" || declaration == "std::multiset" || declaration == "std::unordered_multiset") {
            const auto item = argument(0);
            if (declaration.find("multi") == std::string::npos) return object("NSSet<" + element(item) + "> *", "{" + item.desc);
            return object("NSArray<" + element(item) + "> *", "[" + item.desc);
        }
        if (declaration == "std::map" || declaration == "std::unordered_map" || declaration == "std::multimap" || declaration == "std::unordered_multimap") {
            const auto key = argument(0), item = argument(1);
            if (declaration.find("multi") == std::string::npos && key.cls.empty() && !key.nullable)
                return object("NSDictionary<" + element(key) + ", " + element(item) + "> *", "<" + key.desc + item.desc);
            return object("NSArray<NSArray *> *", "p" + key.desc + item.desc);
        }
        if (declaration == "std::optional") return target.arguments.empty() ? OT{} : optional(objc_type(target.arguments[0], top));
        if (declaration == "std::complex") return object(prefix_ + "Complex *", "z");
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return target.arguments.empty() ? OT{} : objc_type(target.arguments[0], top);
        if ((declaration == "std::pair" || declaration == "std::tuple") && target.arguments.size() >= 2 && target.arguments.size() <= 6) {
            std::string desc = "(" + std::to_string(target.arguments.size());
            for (std::size_t i = 0; i < target.arguments.size(); ++i) desc += argument(i).desc;
            return object("NSArray *", desc);
        }
        if (declaration == "std::tuple" && target.arguments.empty()) return void_type();
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            if (function.arguments.size() > 5) return {};
            if (!top) return object(prefix_ + "Function *", "F");
            OT block = object("", "F");
            block.block = true;
            block.parts.push_back(function.arguments.empty() ? void_type() : objc_type(function.arguments[0], false));
            for (std::size_t i = 1; i < function.arguments.size(); ++i) block.parts.push_back(objc_type(function.arguments[i], false));
            return block;
        }
        if (declaration == "std::chrono::duration") return scalar("double", 'd');
    }
    return {};
}

OT Generator::objc_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    static const std::map<std::string, std::pair<std::string, char>> scalars = {{"bool", {"BOOL", 'B'}}, {"char", {"char", 'c'}},
        {"signed char", {"int8_t", 'a'}}, {"unsigned char", {"uint8_t", 'C'}}, {"short", {"short", 's'}}, {"unsigned short", {"unsigned short", 'S'}},
        {"int", {"int", 'i'}}, {"unsigned int", {"unsigned int", 'I'}}, {"long", {"long", 'q'}}, {"unsigned long", {"unsigned long", 'Q'}},
        {"long long", {"long long", 'q'}}, {"unsigned long long", {"unsigned long long", 'Q'}}, {"float", {"float", 'f'}},
        {"double", {"double", 'd'}}, {"long double", {"double", 'd'}}, {"std::size_t", {"size_t", 'Q'}}, {"size_t", {"size_t", 'Q'}},
        {"std::int64_t", {"int64_t", 'q'}}, {"std::uint64_t", {"uint64_t", 'Q'}}, {"std::int32_t", {"int32_t", 'i'}},
        {"std::uint32_t", {"uint32_t", 'I'}}, {"std::int16_t", {"int16_t", 's'}}, {"std::uint16_t", {"uint16_t", 'S'}},
        {"std::int8_t", {"int8_t", 'a'}}, {"std::uint8_t", {"uint8_t", 'C'}}};
    if (const auto found = scalars.find(cpp); found != scalars.end()) return scalar(found->second.first, found->second.second);
    if (cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return object("NSString *", "@");
    for (const auto& [qualified, plan] : classes_)
        if (normalize(qualified) == cpp) return class_type(qualified);
    for (const auto& [qualified, name] : enum_names_)
        if (normalize(qualified) == cpp) return enum_type(qualified);
    return {};
}

std::string Generator::objc_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "YES" : "NO";
    if (value.kind == "null") return "nil";
    if (value.kind == "char") return value.value.size() == 1 ? "'" + value.value + "'" : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos) return "@" + text;
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        if (!enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return enum_cases_.at(qualified)[i];
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
        const auto ot = objc_type(type);
        auto text = (writeback(type) && !function.is_constructor && !ot.block ? spell_pointer(ot) + names[i - first] : spell(ot, names[i - first]));
        if (i >= defaults_from) {
            const auto shown = objc_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, type);
            text += " = " + (shown.empty() ? std::string("<C++ default>") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + "(" + join(parameters, ", ") + ")";
    if (!function.is_constructor && !is_void(function.result)) result += " -> " + spell(objc_type(function.result));
    return result;
}

// The result type of overloads: the type they agree on, a common base class, else id.
OT Generator::union_result(const std::vector<OT>& types) const {
    if (types.empty()) return void_type();
    bool same = true;
    for (const auto& type : types) same = same && spell(type) == spell(types.front());
    if (same) return types.front();
    bool integers = true;
    for (const auto& type : types) integers = integers && type.scalar && std::string("aCsSiIqQ").find(type.scalar) != std::string::npos;
    if (integers) return scalar("long long", 'q');
    bool classes = true, nullable = false;
    for (const auto& type : types) {
        classes = classes && !type.cls.empty();
        nullable = nullable || type.nullable;
    }
    if (classes) {
        std::vector<std::string> queue = {types.front().cls};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            bool all = true;
            for (const auto& type : types) all = all && derives_class(type.cls, queue[i]);
            if (all) {
                auto result = class_type(queue[i]);
                return nullable ? optional(result) : result;
            }
            for (const auto& base : classes_.at(queue[i]).bases) queue.push_back(base);
        }
    }
    return {};
}

// Whether a selector may override the inherited one: the same parameter types, and the same
// result (or a pointer to a derived class).
bool Generator::same_sel(const Sel& sel, const Sel& inherited) const {
    if (sel.params.size() != inherited.params.size()) return false;
    for (std::size_t i = 0; i < sel.params.size(); ++i)
        if (spell(sel.params[i].type) != spell(inherited.params[i].type) || sel.params[i].ref != inherited.params[i].ref) return false;
    if (spell(sel.result) == spell(inherited.result)) return true;
    return !sel.result.cls.empty() && !inherited.result.cls.empty() && derives_class(sel.result.cls, inherited.result.cls) &&
           (inherited.result.nullable || !sel.result.nullable);
}

std::map<std::size_t, Sel> Generator::make_sels(const std::vector<Entry>& entries, const std::string& first,
                                                const std::map<std::size_t, Sel>* inherited, bool constructor) const {
    struct Shape { std::size_t required, total, first; };
    std::vector<Shape> shapes;
    std::set<std::size_t> counts;
    for (const auto& entry : entries) {
        const auto& function = *entry.overload->function;
        const std::size_t skip = entry.overload->receives_self ? 1 : 0;
        const std::size_t total = function.parameters.size() - skip;
        const std::size_t required = total - std::min(function.default_arguments, total);
        shapes.push_back({required, total, skip});
        for (std::size_t n = required; n <= total; ++n) counts.insert(n);
    }
    std::map<std::size_t, Sel> sels;
    for (const auto n : counts) {
        Sel sel;
        sel.count = n;
        for (std::size_t e = 0; e < entries.size(); ++e)
            if (shapes[e].required <= n && n <= shapes[e].total) sel.entries.push_back(entries[e]);
        sel.at = sel.entries.front().at;
        Names taken;
        for (std::size_t p = 0; p < n; ++p) {
            Param param;
            bool first_seen = true, any = false, any_ref = false;
            std::string spelled;
            for (std::size_t e = 0; e < entries.size(); ++e) {
                if (!(shapes[e].required <= n && n <= shapes[e].total)) continue;
                const auto& function = *entries[e].overload->function;
                const auto& type = function.parameters[shapes[e].first + p];
                const auto ot = objc_type(type);
                const bool ref = writeback(type) && !constructor && !ot.block;
                const auto text = spell(ot) + (ref ? "*" : "");
                if (first_seen) {
                    param.type = ot;
                    param.ref = ref;
                    param.name = parameter_names(*entries[e].overload)[p];
                    spelled = text;
                    first_seen = false;
                } else if (text != spelled) {
                    any = true;
                }
                if (ref) any_ref = true;
            }
            if (any) {
                param.type = OT{};
                param.ref = any_ref;
            }
            param.name = taken.claim(param.name);
            sel.params.push_back(param);
        }
        const Sel* base = nullptr;
        if (inherited && inherited->count(n)) base = &inherited->at(n);
        if (base) {
            sel.labels = base->labels;
        } else {
            sel.labels.push_back(constructor ? (n == 0 ? std::string("init") : "initWith" + cap(sel.params[0].name)) : first);
            for (std::size_t p = 1; p < n; ++p) sel.labels.push_back(sel.params[p].name);
        }
        std::vector<OT> results;
        for (const auto& entry : sel.entries) {
            const auto& function = *entry.overload->function;
            if (function.is_constructor) continue;
            results.push_back(is_void(function.result) ? void_type() : objc_type(function.result));
        }
        sel.result = constructor ? void_type() : union_result(results);
        sels[n] = sel;
    }
    return sels;
}

// An Objective-C value as an id (for the runtime).
std::string Generator::to_id(const OT& type, const std::string& expression) const {
    if (type.block) {
        std::vector<std::string> arguments;
        for (std::size_t i = 1; i < type.parts.size(); ++i)
            arguments.push_back(from_id(type.parts[i], "rosetta_argument(_a, " + std::to_string(i - 1) + ")"));
        const auto call = "_f(" + join(arguments, ", ") + ")";
        const auto body = type.parts[0].is_void() ? call + "; return nil;" : "return " + to_id(type.parts[0], call) + ";";
        return "({ " + spell(type, "_f") + " = " + expression + "; _f ? [" + prefix_ + "Function functionWithBlock:^id(NSArray* _a) { " + body +
               " }] : nil; })";
    }
    switch (type.scalar) {
    case 0: return expression;
    case 'B': return "((" + expression + ") ? @YES : @NO)";
    case 'c': return "rosetta_char(" + expression + ")";
    case 'E': return std::string(type.desc == "Q" ? "@((unsigned long long)(" : "@((long long)(") + expression + "))";
    default: return "@(" + expression + ")";
    }
}

// An id (from the runtime) as an Objective-C value.
std::string Generator::from_id(const OT& type, const std::string& expression) const {
    if (type.block) {
        std::vector<std::string> parameters, arguments;
        for (std::size_t i = 1; i < type.parts.size(); ++i) {
            const auto name = "_p" + std::to_string(i - 1);
            parameters.push_back(spell(type.parts[i], name));
            arguments.push_back("rosetta_boxed(" + to_id(type.parts[i], name) + ")");
        }
        const auto call = "[_g call:@[" + join(arguments, ", ") + "]]";
        const auto& result = type.parts[0];
        const auto body = result.is_void() ? call + ";" : "return " + from_id(result, call) + ";";
        return "({ " + prefix_ + "Function* _g = rosetta_convert(" + expression + ", \"F\"); _g ? ^" + spell(result) + " (" +
               (parameters.empty() ? std::string("void") : join(parameters, ", ")) + ") { " + body + " } : nil; })";
    }
    if (type.any()) return "rosetta_convert(" + expression + ", \"?\")";
    if (!type.scalar) return "rosetta_convert(" + expression + ", " + c_string(type.desc) + ")";
    const char letter = type.scalar == 'E' ? type.desc[0] : type.scalar;
    return "(" + type.type + ")[(NSNumber*)rosetta_convert(" + expression + ", \"" + letter + "\") " + accessor(letter) + "]";
}

// ---------------------------------------------------------------- naming
std::string Generator::claim_type(std::string wanted) {
    if (objc_keyword(wanted) || starts(wanted, prefix_ + "Rosetta")) wanted += "_";
    if (types_.reserve(wanted)) return wanted;
    return types_.claim(wanted + "_");
}

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
        // A C++ name a base class binds keeps its name (an override, a merged method, or a further
        // base's member declared again).
        for (const auto& base : cls.bases) {
            if (!members_.count(base)) continue;
            for (const auto& name : members_.at(base).taken) members.reserve(name);
            for (const auto& [key, name] : mapped_.at(base)) mapped.emplace(key, name);
        }
        if (!cls.bases.empty()) table = table_.at(cls.bases.front());
        const auto name_member = [&](const std::string& key, const std::string& wanted) {
            const auto found = mapped.find(key);
            if (found != mapped.end()) return found->second;
            const auto name = members.claim(wanted);
            mapped[key] = name;
            return name;
        };
        const auto fresh = [&](const std::string& key, const std::string& wanted) {
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
                    text += spell(objc_type(function.parameters[i])) + (writeback(function.parameters[i]) ? "&" : "") + ",";
                return text + std::to_string(function.default_arguments);
            };
            std::set<std::string> own;
            for (const auto& entry : entries) own.insert(key(entry));
            for (const auto& entry : inherited)
                if (own.insert(key(entry)).second) entries.push_back(entry);
        };
        // Initializers: all constructors, by argument count.
        const bool constructible = !cls.record->abstract;
        std::vector<Entry> constructors;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) constructors.push_back({&overload, &cls.constructor_index});
        if (!constructors.empty()) constructors_[owner] = make_sels(constructors, "", nullptr, true);
        for (const auto& [count, sel] : constructors_[owner]) members.reserve(sel.labels[0]);
        // A method group: its name and selectors.
        const auto name_group = [&](Group& group, const std::string& key, const std::string& wanted) {
            std::vector<Entry> own;
            for (const auto& overload : group.overloads) own.push_back({&overload, &group.index});
            auto name = name_member(key, wanted);
            auto entries = own;
            const auto inherited = table.find(name);
            std::map<std::size_t, Sel> sels;
            if (inherited == table.end()) {
                sels = make_sels(own, name, nullptr, false);
            } else {
                const auto& member = inherited->second;
                const char kind = group.is_static ? 's' : 'm';
                bool fits = member.kind == kind;
                if (fits) {
                    if (kind == 'm') merge(entries, member.entries);
                    sels = make_sels(entries, name, &member.sels, false);
                    for (const auto& [count, sel] : member.sels)
                        if (sels.count(count) && !same_sel(sels.at(count), sel)) fits = false;
                }
                if (!fits) {
                    // Another signature: Objective-C could not override it, so it takes its own name.
                    name = fresh(key, wanted);
                    entries = own;
                    sels = make_sels(own, name, nullptr, false);
                }
            }
            Member member;
            member.kind = group.is_static ? 's' : 'm';
            member.entries = entries;
            member.sels = sels;
            member.from = owner;
            for (const auto& overload : group.overloads)
                member.is_virtual = member.is_virtual || (overload.function->is_virtual && !overload.extension && !group.is_static);
            const auto previous = table.find(name);
            if (previous != table.end() && previous->second.kind == 'm' && previous->second.is_virtual && !group.is_static) member.is_virtual = true;
            add(name, member);
        };
        for (auto& group : cls.methods) {
            // to_string() is -description. One with further argument counts (default arguments, other
            // overloads) is -description for its zero-argument call and keeps its own selectors too.
            if (group.name == "to_string" && !group.is_static) {
                std::vector<Entry> own;
                for (const auto& overload : group.overloads) own.push_back({&overload, &group.index});
                auto sels = make_sels(own, "description", nullptr, false);
                if (sels.count(0) && sels.at(0).result.type == "NSString *" && !sels.at(0).result.nullable) {
                    const bool only = sels.size() == 1;
                    Member member;
                    member.kind = 'd';
                    member.entries = sels.at(0).entries;
                    member.sels = {{0, sels.at(0)}};
                    member.from = owner;
                    add("description", member);
                    if (only) continue;
                }
            }
            name_group(group, "m:" + group.name, camel(group.name));
        }
        for (auto& group : cls.operators)
            if (group.operand == 0) name_group(group, "o:" + group.name, group.name);
        for (const auto& field : cls.fields) {
            const auto key = "f:" + field.field->name;
            auto found = mapped.find(key);
            std::string name, setter;
            if (found != mapped.end()) {
                name = found->second;
                setter = mapped.at("s:" + field.field->name);
            } else {
                // The property, and its setter (setX, else another name a method left free).
                name = members.claim(camel(field.name));
                setter = members.free("set" + cap(name)) ? "set" + cap(name) : members.claim("set" + cap(name) + "Value");
                members.reserve(setter);
                mapped[key] = name;
                mapped["s:" + field.field->name] = setter;
            }
            if (table.count(name) && table[name].kind == 'f') continue;
            Member member;
            member.setter = setter;
            member.kind = 'f';
            member.field = &field;
            member.from = owner;
            member.type = objc_type(field.field->type);
            add(name, member);
        }
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(camel(constant.name));
        // Further bases are reached through asBase, and their members are declared again.
        for (std::size_t i = 1; i < cls.bases.size(); ++i) {
            secondary_names_[owner + "::" + cls.bases[i]] = name_member("a:" + cls.bases[i], "as" + pascal(classes_.at(cls.bases[i]).record->name));
            for (const auto& [name, member] : table_.at(cls.bases[i]))
                if (member.kind != 's' && member.kind != 'd' && !table.count(name)) add(name, member);
        }
    }
    for (auto& group : functions) {
        const auto owner = group.module.empty() ? prefix_ : submodules_.at(group.module);
        const auto name = modules_[owner].claim(camel(group.name));
        std::vector<Entry> entries;
        for (const auto& overload : group.overloads) entries.push_back({&overload, &group.index});
        Member member;
        member.kind = 's';
        member.entries = entries;
        member.sels = make_sels(entries, name, nullptr, false);
        member.from = owner;
        functions_[&group] = member;
    }
    for (auto& constant : constants) constant_names_[&constant] = modules_[prefix_].claim(camel(constant.name));
}

// ---------------------------------------------------------------- documentation
void Generator::add_notes(Doc& comment, const Documentation& doc) const {
    for (const auto& item : doc.notes) {
        if (item.name == "deprecated") { comment.paragraph("@deprecated " + one_line(item.text)); continue; }
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        comment.paragraph(cap(label) + ": " + one_line(item.text));
    }
    for (const auto& see : doc.see) comment.tag("@see " + see);
}
Doc Generator::doc_of(const Documentation& doc, const std::string& lead) const {
    Doc comment;
    auto details = doc.details;
    auto brief = doc.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    comment.paragraph(brief.empty() ? lead : brief);
    if (!brief.empty() && !lead.empty()) comment.paragraph(lead);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") comment.code(block.text);
        else comment.paragraph(block.text);
    }
    return comment;
}

Doc Generator::sel_doc(const Sel& sel, const std::string& what) const {
    const Function* documented = sel.entries.front().overload->function;
    for (const auto& entry : sel.entries) {
        const auto& doc = entry.overload->function->documentation;
        if (!doc.brief.empty() || !doc.details.empty()) { documented = entry.overload->function; break; }
    }
    const auto& doc = documented->documentation;
    auto comment = doc_of(doc, "Calls " + what + ".");
    if (sel.entries.size() > 1) {
        std::vector<std::string> overloads;
        for (const auto& entry : sel.entries) overloads.push_back("`" + overload_signature(*entry.overload, entry.overload->function->name) + "`");
        comment.list("C++ overloads (C++ chooses among them at run time):", overloads);
    }
    std::vector<std::string> defaults;
    for (const auto& entry : sel.entries) {
        const auto& function = *entry.overload->function;
        const std::size_t skip = entry.overload->receives_self ? 1 : 0;
        if (function.parameters.size() - skip > sel.count) {
            defaults.push_back("C++ applies the default arguments of the parameters after the first " + std::to_string(sel.count) + ".");
            break;
        }
    }
    for (const auto& line : defaults) comment.paragraph(line);
    comment.blank();
    for (std::size_t p = 0; p < sel.params.size(); ++p) {
        const auto& param = sel.params[p];
        std::string text;
        for (const auto& entry : sel.entries) {
            if (entry.overload->function != documented) continue;
            const std::size_t i = p + (entry.overload->receives_self ? 1 : 0);
            const auto cpp = i < documented->parameter_names.size() ? documented->parameter_names[i] : std::string();
            for (const auto& item : doc.params) if (item.name == cpp && !item.text.empty()) text = one_line(item.text);
        }
        if (param.ref) text += std::string(text.empty() ? "" : " ") + "(C++ writes it back)";
        if (!text.empty()) comment.tag("@param " + param.name + " " + text);
    }
    if (!sel.result.is_void() && !doc.returns.empty()) comment.tag("@return " + one_line(doc.returns));
    for (const auto& item : doc.throws)
        comment.tag("@throws " + prefix_ + "Error of kind " + error_kind(prefix_, item.name) + (item.name.empty() ? std::string() : " (C++ `" + item.name + "`)") +
                    (item.text.empty() ? std::string() : ": " + one_line(item.text)));
    add_notes(comment, doc);
    return comment;
}

// ---------------------------------------------------------------- emission
// A method's declaration; in the implementation, block parameters are __unsafe_unretained (the
// generated code copies them, and libobjc2 leaks the copy ARC makes of a block parameter at -O0).
std::string Generator::declaration(const Sel& sel, bool is_static, const std::string& result, bool implementation) const {
    std::string text = std::string(is_static ? "+ (" : "- (") + result + ")";
    if (sel.count == 0) return text + sel.labels[0];
    for (std::size_t i = 0; i < sel.count; ++i) {
        const auto& param = sel.params[i];
        text += (i ? " " : "") + sel.labels[i] + ":(" + (implementation && param.type.block && !param.ref ? "__unsafe_unretained " : "") +
                (param.ref ? spell_pointer(param.type) : spell(param.type)) + ")" + param.name;
    }
    return text;
}

// The statements of a call of the selector's native function (`receiver`: self, or "" for none).
void Generator::emit_body(std::ostream& out, const Sel& sel, const std::string& receiver) const {
    const auto call = prefix_ + "RosettaCall";
    out << "    " << call << "* _c = [" << call << " call:" << *sel.at << "];\n";
    if (!receiver.empty()) out << "    [_c object:" << receiver << "];\n";
    std::vector<std::string> after;
    for (std::size_t i = 0; i < sel.count; ++i) {
        const auto& param = sel.params[i];
        const auto slot = "_s" + std::to_string(i);
        if (param.ref) {
            out << "    const uint32_t " << slot << " = [_c slot:" << to_id(param.type, "*" + param.name) << " type:" << c_string(param.type.desc)
                << "];\n";
            after.push_back("{ id _w = [_r writeback:" + slot + "]; if (_w) *" + param.name + " = " + from_id(param.type, "_w") + "; }");
        } else {
            out << "    [_c arg:" << to_id(param.type, param.name) << " type:" << c_string(param.type.desc) << "];\n";
        }
    }
    const bool returns = !sel.result.is_void();
    if (after.empty()) {
        if (returns) out << "    return " << from_id(sel.result, "[_c run].value") << ";\n";
        else out << "    [_c run];\n";
    } else {
        out << "    " << prefix_ << "RosettaReply* _r = [_c run];\n";
        for (const auto& line : after) out << "    " << line << "\n";
        if (returns) out << "    return " << from_id(sel.result, "_r.value") << ";\n";
    }
}

void Generator::emit_sels(std::ostream& header, std::ostream& impl, const Member& member, const std::string& what, bool is_static,
                          bool declare) const {
    for (const auto& [count, sel] : member.sels) {
        const auto decl = declaration(sel, is_static, spell(sel.result));
        if (declare) {
            auto comment = sel_doc(sel, what);
            if (member.is_virtual && classes_.count(member.from) && classes_.at(member.from).director)
                comment.paragraph(std::string(member.entries.front().overload->function->is_pure ? "Pure virtual in C++: an" : "An") +
                                  " Objective-C subclass overrides it, and C++ calls the override.");
            header << "\n" << comment.render() << decl << (method_family(sel.labels[0]) ? " __attribute__((objc_method_family(none)))" : "") << ";\n";
        }
        impl << "\n" << declaration(sel, is_static, spell(sel.result), true) << " {\n";
        emit_body(impl, sel, is_static ? "" : "self");
        impl << "}\n";
    }
}

void Generator::emit_property(std::ostream& header, std::ostream& impl, const std::string& name, const Member& member, bool declare) const {
    const auto& field = *member.field;
    const auto& f = *field.field;
    const bool writable = !f.type.is_const && !readonly(member.from + "::" + f.name, member.from) && f.type.kind != TypeKind::lvalue_reference;
    const auto& type = member.type;
    if (declare) {
        const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        header << "\n/// " << (about.empty() ? "The C++ field `" + member.from + "::" + f.name + "`" : about) << (writable ? "" : " (read-only)") << ".\n"
               << "@property (nonatomic" << (writable ? "" : ", readonly") << (writable && member.setter != "set" + cap(name) ? ", setter=" + member.setter + ":" : "") << (type.block ? ", copy" : "") << ") " << spell(type, name) << ";\n";
        if (method_family(name)) header << "- (" << spell(type) << ")" << name << " __attribute__((objc_method_family(none)));\n";
    }
    const auto call = prefix_ + "RosettaCall";
    impl << "\n- (" << spell(type) << ")" << name << " {\n    " << call << "* _c = [" << call << " call:" << field.getter_index
         << "];\n    [_c object:self];\n    return " << from_id(type, "[_c run].value") << ";\n}\n";
    if (writable)
        impl << "\n- (void)" << member.setter << ":(" << (type.block ? "__unsafe_unretained " : "") << spell(type) << ")value {\n    " << call << "* _c = [" << call << " call:" << field.setter_index
             << "];\n    [_c object:self];\n    [_c arg:" << to_id(type, "value") << " type:" << c_string(type.desc) << "];\n    [_c run];\n}\n";
}

void Generator::emit_class(std::ostream& header, std::ostream& impl, const std::string& owner) {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto primary = plan.bases.empty() ? std::string() : plan.bases.front();
    const auto superclass = primary.empty() ? prefix_ + "Object" : classes_.at(primary).name;
    const bool director = plan.director && !record.is_final;
    auto comment = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    if (plan.bases.size() > 1) {
        std::vector<std::string> further;
        for (std::size_t i = 1; i < plan.bases.size(); ++i)
            further.push_back(classes_.at(plan.bases[i]).name + " (" + secondary_names_.at(owner + "::" + plan.bases[i]) + ")");
        comment.paragraph("Its further C++ base classes' members are declared again here; pass it where one of them is expected through "
                          "its `as` property: " + join(further, ", ") + ".");
    }
    if (director)
        comment.paragraph(std::string(record.abstract ? "Abstract in C++: subclass" : "Subclass") +
                          " it in Objective-C to override its virtual methods: initializing an object of a subclass creates a C++ subclass "
                          "whose virtual methods send the overriding messages, and `super` runs the C++ implementation.");
    else if (record.abstract) comment.paragraph("Abstract in C++: C++ functions return objects of derived classes.");
    if (!constructors_.count(owner)) comment.paragraph("C++ functions return its objects; Objective-C does not initialize them.");
    add_notes(comment, record.documentation);
    header << "\n" << comment.render() << (record.is_final ? "__attribute__((objc_subclassing_restricted))\n" : "") << "@interface " << plan.name
           << " : " << superclass << "\n";
    impl << "\n@implementation " << plan.name << "\n";
    // Initializers, and the base classes' ones made unavailable.
    auto& inits = inits_[owner];
    if (constructors_.count(owner)) {
        for (const auto& [count, sel] : constructors_.at(owner)) {
            const auto decl = declaration(sel, false, "instancetype");
            inits[sel.selector()] = decl;
            auto about = sel_doc(sel, "the C++ constructor of `" + owner + "`");
            if (director) about.paragraph("The object of an Objective-C subclass is a C++ subclass whose virtual methods send its messages.");
            header << "\n" << about.render() << decl << ";\n";
            impl << "\n" << declaration(sel, false, "instancetype", true) << " {\n    self = [super initRosetta];\n    if (!self) return nil;\n    " << prefix_ << "RosettaCall* _c = rosetta_constructor(self, "
                 << *sel.at << ", " << (director ? "[self class] != [" + plan.name + " class]" : std::string("NO")) << ");\n";
            for (std::size_t i = 0; i < sel.count; ++i)
                impl << "    [_c arg:" << to_id(sel.params[i].type, sel.params[i].name) << " type:" << c_string(sel.params[i].type.desc) << "];\n";
            impl << "    rosetta_construct(self, _c);\n    return self;\n}\n";
            if (count == 0) {
                inits["+new"] = "+ (instancetype)new";
                header << "+ (instancetype)new;\n";
                impl << "\n+ (instancetype)new {\n    return [[self alloc] init];\n}\n";
            }
        }
    }
    if (!primary.empty()) {
        for (const auto& [selector, decl] : inits_.at(primary)) {
            if (inits.count(selector)) continue;
            header << decl << " __attribute__((unavailable));\n";
            inits[selector] = decl;
        }
        // (Kept unavailable in further subclasses.)
    }
    // Members: own ones, then further bases' ones declared again.
    const auto& table = table_.at(owner);
    const std::map<std::string, Member> empty;
    const auto& inherited = primary.empty() ? empty : table_.at(primary);
    for (const auto& name : order_[owner]) {
        const auto& member = table.at(name);
        if (member.from != owner && inherited.count(name)) continue;   // inherited as it is
        if (member.kind == 'f') { emit_property(header, impl, name, member, true); continue; }
        const auto& function = *member.entries.front().overload->function;
        std::string what;
        if (member.entries.front().overload->extension) what = "C++ `" + function.qualified_name() + "`";
        else what = "the C++ method `" + classes_.at(member.from).record->qualified_name() + "::" + function.name + "`";
        if (member.kind == 'd') {
            const auto& sel = member.sels.at(0);
            header << "\n" << sel_doc(sel, what).render() << "- (NSString *)description;\n";
            impl << "\n- (NSString *)description {\n    if (self.isClosed) return [super description];\n";
            emit_body(impl, sel, "self");
            impl << "}\n";
            continue;
        }
        emit_sels(header, impl, member, what, member.kind == 's', true);
    }
    for (std::size_t i = 1; i < plan.bases.size(); ++i) {
        const auto& base = plan.bases[i];
        const auto& name = classes_.at(base).name;
        const auto& getter = secondary_names_.at(owner + "::" + base);
        header << "\n/// This object as its further C++ base class " << name << ".\n@property (nonatomic, readonly) " << name << " *" << getter << ";\n";
        impl << "\n- (" << name << " *)" << getter << " {\n    return [self cast:[" << name << " class]];\n}\n";
    }
    for (const auto& constant : plan.constants) {
        const auto type = objc_type(*constant.type);
        const auto& name = constant_names_.at(&constant);
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        header << "\n" << about.render() << "@property (class, nonatomic, readonly) " << spell(type, name) << ";\n";
        impl << "\n+ (" << spell(type) << ")" << name << " {\n    return " << from_id(type, "[[" + prefix_ + "RosettaCall call:" + std::to_string(constant.index) + "] run].value")
             << ";\n}\n";
    }
    header << "@end\n";
    impl << "@end\n";
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& name = enum_names_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto comment = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    comment.paragraph("Other values of the C++ type are values of the enum too.");
    add_notes(comment, enumeration.documentation);
    const bool is_unsigned = unsigned_enum(qualified);
    out << "\n" << comment.render() << "typedef NS_ENUM(" << (is_unsigned ? "uint64_t" : "int64_t") << ", " << name << ") {\n";
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        auto value = enumeration.enumerator_values[i];
        if (is_unsigned) {
            out << "    /// C++ `" << qualified << "::" << enumeration.enumerators[i] << "`.\n    " << cases[i] << " = " << value << "ULL,\n";
            continue;
        }
        if (!value.empty() && value[0] != '-' && value.size() >= 19) {
            const auto number = std::stoull(value);
            if (number > 9223372036854775807ull) value = std::to_string(static_cast<long long>(number));
        }
        if (value == "-9223372036854775808") value = "INT64_MIN";
        out << "    /// C++ `" << qualified << "::" << enumeration.enumerators[i] << "`.\n    " << cases[i] << " = " << value << (value[0] == '-' || value == "INT64_MIN" ? "" : "LL") << ",\n";
    }
    out << "};\n";
}

// The function C++ calls a director's virtual methods through: the message to the object.
std::string Generator::dispatch() const {
    std::ostringstream out;
    out << "\n// The messages C++ sends for the virtual methods of Objective-C subclasses.\n"
        << "static id rosetta_dispatch(" << prefix_ << "Object* object, uint32_t generic, NSArray* arguments) {\n";
    bool any = false;
    for (auto at = class_order_.rbegin(); at != class_order_.rend(); ++at) {
        const auto& owner = *at;
        const auto& plan = classes_.at(owner);
        if (!plan.director || plan.record->is_final) continue;
        for (const auto& item : overridable(plan)) {
            const auto generic = generic_index_.find(item.name);
            const auto name = mapped_.at(owner).find("m:" + item.name);
            if (generic == generic_index_.end() || name == mapped_.at(owner).end()) continue;
            const auto found = table_.at(owner).find(name->second);
            if (found == table_.at(owner).end() || found->second.kind != 'm') continue;
            const auto& method = *item.method;
            const auto sel = found->second.sels.find(method.parameters.size());
            if (sel == found->second.sels.end()) continue;
            any = true;
            out << "    if (generic == " << generic->second << " && arguments.count == " << method.parameters.size() << " && [object isKindOfClass:["
                << plan.name << " class]]) {\n        " << plan.name << "* target = (" << plan.name << "*)object;\n";
            std::vector<std::string> parts;
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                const auto& param = sel->second.params[i];
                const auto value = from_id(param.type, "arguments[" + std::to_string(i) + "]");
                if (param.ref) {
                    out << "        " << spell(param.type, "_a" + std::to_string(i)) << " = " << value << ";\n";
                    parts.push_back(sel->second.labels[i] + ":&_a" + std::to_string(i));
                } else {
                    parts.push_back(sel->second.labels[i] + ":" + value);
                }
            }
            const auto message = "[target " + (parts.empty() ? sel->second.labels[0] : join(parts, " ")) + "]";
            if (sel->second.result.is_void()) out << "        " << message << ";\n        return nil;\n";
            else out << "        return " << to_id(sel->second.result, message) << ";\n";
            out << "    }\n";
        }
    }
    if (!any) out << "    (void)arguments;\n";
    out << "    rosetta_fail([NSString stringWithFormat:@\"no Objective-C method for C++ virtual method %u of %@\", generic, object]);\n}\n";
    return out.str();
}

GeneratedObjcSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    prefix_ = options_.prefix.empty() ? objc_prefix(name_) : options_.prefix;
    if (!identifier(prefix_) || prefix_[0] == '_' || objc_keyword(prefix_))
        throw BindingError("Objective-C prefix must be an ASCII identifier starting with a letter: " + prefix_);
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Objective-C native library name must be a plain file name: " + library_);
    GeneratedObjcSource result;
    prepare();
    for (const char* reserved : {"", "Object", "Error", "ErrorKind", "Function", "Complex"}) types_.reserve(prefix_ + reserved);
    for (const char* kind : {"Binding", "InvalidArgument", "OutOfRange", "DomainError", "LengthError", "ArithmeticError", "Allocation", "Native", "Callback"})
        types_.reserve(prefix_ + "ErrorKind" + kind);
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        auto& plan = classes_.at(class_order_[i]);
        plan.name = claim_type(prefixed(pascal(plan.name)));
        class_index_[class_order_[i]] = i;
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        enum_names_[qualified] = claim_type(nested ? classes_.at(enumeration.owner).name + pascal(name) : prefixed(pascal(name)));
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        for (const auto& enumerator : enumeration.enumerators)
            enum_cases_[qualified].push_back(claim_type(name + pascal(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Policy modules are classes of class methods (a.b is PREFIXAB).
    for (const auto& group : functions) {
        if (group.module.empty() || submodules_.count(group.module)) continue;
        submodules_[group.module] = claim_type(prefixed(pascal(group.module)));
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Objective-C classes " + prefix_ + " (" + name_ + ").");

    // ------------------------------------------------------------ the API
    std::ostringstream header, impl;
    header << "\n@class " << prefix_ << ";\n";
    for (const auto& owner : class_order_) header << "@class " << classes_.at(owner).name << ";\n";
    for (const auto& [qualified, name] : enum_names_) emit_enum(header, qualified);
    for (const auto& owner : class_order_) emit_class(header, impl, owner);
    // The module class and the policy modules' classes.
    std::vector<std::pair<std::string, std::string>> modules = {{"", prefix_}};
    for (const auto& [module, name] : submodules_) modules.emplace_back(module, name);
    for (const auto& [module, name] : modules) {
        if (module.empty()) {
            header << "\n/// The functions and constants of the C++ library " << name_ << ".\n";
        } else {
            std::string from;
            for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) from = ns;
            header << "\n/// The functions of the C++ namespace `" << from << "` (policy module `" << module << "`).\n";
        }
        header << "@interface " << name << " : NSObject\n- (instancetype)init __attribute__((unavailable));\n+ (instancetype)new __attribute__((unavailable));\n";
        impl << "\n@implementation " << name << "\n";
        for (const auto& group : functions) {
            if (group.module != module) continue;
            const auto& member = functions_.at(&group);
            emit_sels(header, impl, member, "C++ `" + member.entries.front().overload->function->qualified_name() + "`", true, true);
        }
        if (module.empty()) {
            for (const auto& constant : constants) {
                const auto type = objc_type(*constant.type);
                const auto& cname = constant_names_.at(&constant);
                auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
                header << "\n" << about.render() << "@property (class, nonatomic, readonly) " << spell(type, cname) << ";\n";
                impl << "\n+ (" << spell(type) << ")" << cname << " {\n    return "
                     << from_id(type, "[[" + prefix_ + "RosettaCall call:" + std::to_string(constant.index) + "] run].value") << ";\n}\n";
            }
        }
        header << "@end\n";
        impl << "@end\n";
    }
    // The class table.
    impl << "\n// The C++ classes: their Objective-C classes, names and base classes.\n"
         << "static uint32_t rosetta_class_count(void) {\n    return " << class_order_.size() << ";\n}\n"
         << "static Class rosetta_class_at(uint32_t index) {\n    switch (index) {\n";
    for (std::size_t i = 0; i < class_order_.size(); ++i) impl << "    case " << i << ": return [" << classes_.at(class_order_[i]).name << " class];\n";
    impl << "    default: return Nil;\n    }\n}\nstatic NSString* rosetta_class_name(uint32_t index) {\n    switch (index) {\n";
    for (std::size_t i = 0; i < class_order_.size(); ++i) impl << "    case " << i << ": return " << objc_string(classes_.at(class_order_[i]).name) << ";\n";
    impl << "    default: return @\"object\";\n    }\n}\nstatic BOOL rosetta_derives(uint32_t from, uint32_t to) {\n    if (from == to) return YES;\n    switch (from) {\n";
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& bases = classes_.at(class_order_[i]).bases;
        if (bases.empty()) continue;
        std::vector<std::string> checks;
        for (const auto& base : bases) checks.push_back("rosetta_derives(" + std::to_string(class_index_.at(base)) + ", to)");
        impl << "    case " << i << ": return " << join(checks, " || ") << ";\n";
    }
    impl << "    default: return NO;\n    }\n}\n" << dispatch();

    const auto replace = [&](std::string& text, const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    };
    const auto substitute = [&](std::string text) {
        replace(text, "@P@", prefix_);
        replace(text, "@C@", "rosetta_" + name_);
        replace(text, "@LIB@", library_);
        return text;
    };
    std::ostringstream h;
    h << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n//\n"
      << "// The Objective-C interface to the C++ library " << name_ << ": each C++ class is a class over " << prefix_ << "Object, and the\n"
      << "// module functions and constants are class methods and class properties of " << prefix_ << ". One selector per argument\n"
      << "// count covers the overloads and default arguments of a C++ function, and C++ chooses the overload at run time.\n"
      << "// Calls raise " << prefix_ << "Error for C++ exceptions and values C++ does not accept. Link the native library "
      << library_ << ",\n// and compile " << prefix_ << ".m with -fobjc-arc -fobjc-arc-exceptions.\n"
      << "#ifndef " << prefix_ << "_ROSETTA_H\n#define " << prefix_ << "_ROSETTA_H\n\n"
      << substitute(detail::objc_full_runtime_header) << "\nNS_ASSUME_NONNULL_BEGIN\n" << header.str() << "\nNS_ASSUME_NONNULL_END\n\n#endif\n";
    result.header = h.str();
    std::string runtime;
    for (const auto* part : detail::objc_full_runtime_parts) runtime += part;
    std::ostringstream m;
    m << "// Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\n#import \"" << prefix_ << ".h\"\n\n" << substitute(runtime)
      << "\n// ---------------------------------------------------------------- the classes\n" << impl.str();
    result.files.emplace_back(prefix_ + ".m", m.str());
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

std::string objc_prefix(const std::string& name) { return pascal(name); }

GeneratedObjcSource generate_objc(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                  const ObjcOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Objective-C C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
