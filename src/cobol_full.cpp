// The full COBOL backend (--cobol-api full): a copybook of parameter records for GnuCOBOL programs
// that CALL C++ stubs, covering classes with inheritance, overloads, defaults, enums, containers
// (handles plus OCCURS tables), callbacks and COBOL overrides of virtual methods (directors), with
// the C++ documentation as doc comments. See docs/architecture.md and the copybook's header.
#include "rosetta/cobol.hpp"
#include "cobol_full_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '?') result += "\\?";
        else result += c;
    }
    return result + '"';
}
std::string join(const std::vector<std::string>& parts, const std::string& separator = ", ") {
    std::string text;
    for (const auto& part : parts) text += (text.empty() ? "" : separator) + part;
    return text;
}
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}
bool identifier(const std::string& text) {
    auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    return !text.empty() && start(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return start(c) || (c >= '0' && c <= '9');
    });
}
// A C++ name as a COBOL word part: upper case, hyphens for underscores, between camelCase words
// and for any other character; none doubled, leading or trailing.
std::string word(const std::string& text) {
    std::string out;
    const auto hyphen = [&] { if (!out.empty() && out.back() != '-') out += '-'; };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c)) {
            if (std::isupper(c) && i > 0) {
                const auto before = static_cast<unsigned char>(text[i - 1]);
                const bool acronym_end = std::isupper(before) && i + 1 < text.size() && std::islower(static_cast<unsigned char>(text[i + 1]));
                if (std::islower(before) || acronym_end) hyphen();
            }
            out += static_cast<char>(std::toupper(c));
        } else hyphen();
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}
std::string symbol_of(const std::string& cobol_word) {
    std::string result;
    for (char c : cobol_word) result += c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
// C++ identifier text for a COBOL word.
std::string ident(const std::string& cobol_word) {
    std::string result;
    for (char c : cobol_word) result += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    return result;
}
// Every name fits fixed format's area B (columns 12-72) at its level, with its period.
constexpr std::size_t max_word = 55;
// A word over the limit, shortened to a prefix and a hash of the whole.
std::string hashed(const std::string& text) {
    unsigned hash = 2166136261u;
    for (char c : text) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
    std::string suffix = "-";
    for (int i = 0; i < 5; ++i) { suffix += digits[hash & 31]; hash >>= 5; }
    auto head = text.substr(0, max_word - suffix.size());
    while (!head.empty() && head.back() == '-') head.pop_back();
    return head + suffix;
}
// Abbreviates a word over the limit so every part stays readable. Parts after the prefix lose their
// inner vowels left to right (CLASSIFIER -> CLSSFR), the last part (the field) last, so a record
// and its fields abbreviate alike; then they lose letters, down to three, also left to right.
// Falls back to hashed() when that cannot fit, or when two words would abbreviate alike.
std::string fit(const std::string& text) {
    if (text.size() <= max_word) return text;
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto dash = text.find('-', start);
        parts.push_back(text.substr(start, dash == std::string::npos ? std::string::npos : dash - start));
        if (dash == std::string::npos) break;
        start = dash + 1;
    }
    const auto length = [&] {
        std::size_t size = parts.size() - 1;
        for (const auto& part : parts) size += part.size();
        return size;
    };
    const auto is_vowel = [](char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; };
    for (std::size_t i = 1; i < parts.size() && length() > max_word; ++i) {
        auto& part = parts[i];
        std::string kept(1, part.front());
        for (std::size_t j = 1; j < part.size(); ++j) if (!is_vowel(part[j])) kept += part[j];
        if (kept.size() >= 3) part = kept;
    }
    for (std::size_t i = 1; i < parts.size() && length() > max_word; ++i)
        while (parts[i].size() > 3 && length() > max_word) parts[i].pop_back();
    if (length() > max_word) return hashed(text);
    std::string result = join(parts, "-");
    // Distinct words must stay distinct: the first to claim an abbreviation keeps it.
    thread_local std::map<std::string, std::string> claimed;
    const auto [found, inserted] = claimed.emplace(result, text);
    if (!inserted && found->second != text) return hashed(text);
    return result;
}
std::string normalize(std::string text) {
    for (const char* inline_namespace : {"__cxx11::", "__1::", "__debug::", "__ndk1::", "__fs::"})
        for (std::size_t i = 0; (i = text.find(inline_namespace, i)) != std::string::npos;) text.erase(i, std::string(inline_namespace).size());
    if (text.rfind("::", 0) == 0) text.erase(0, 2);
    return text;
}
std::string strip_cv(std::string text) {
    text = detail::trim_type(text);
    for (bool changed = true; changed;) {
        changed = false;
        for (const std::string prefix : {"const ", "volatile "})
            if (text.rfind(prefix, 0) == 0) { text = detail::trim_type(text.substr(prefix.size())); changed = true; }
        for (const std::string suffix : {" const", " volatile"})
            if (text.size() > suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
                text = detail::trim_type(text.substr(0, text.size() - suffix.size())); changed = true;
            }
    }
    return text;
}
const Type& referent(const Type& type) {
    return (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) && !type.arguments.empty()
        ? type.arguments.front() : type;
}
bool is_void(const Type& type) { return strip_cv(type.canonical) == "void"; }
bool is_operator(const std::string& name) {
    return name.rfind("operator", 0) == 0 && (name.size() == 8 || !identifier(name.substr(8)));
}
// The COBOL word of an operator method, by arity (parameters besides the object).
std::string operator_word(const std::string& name, std::size_t arity) {
    const auto op = detail::trim_type(name.substr(8));
    static const std::map<std::string, std::pair<const char*, const char*>> table = {
        {"+", {"POSITIVE", "PLUS"}}, {"-", {"NEGATE", "MINUS"}}, {"*", {"", "TIMES"}}, {"/", {"", "DIVIDE"}},
        {"%", {"", "MODULO"}}, {"==", {"", "EQUAL"}}, {"!=", {"", "NOT-EQUAL"}}, {"<", {"", "LESS"}},
        {"<=", {"", "LESS-EQUAL"}}, {">", {"", "GREATER"}}, {">=", {"", "GREATER-EQUAL"}}, {"[]", {"", "AT"}},
        {"()", {"CALL", "CALL"}}, {"+=", {"", "ADD"}}, {"-=", {"", "SUBTRACT"}}, {"*=", {"", "MULTIPLY"}},
        {"/=", {"", "DIVIDE-BY"}}, {"!", {"NOT", ""}}, {"&&", {"", "AND"}}, {"||", {"", "OR"}}};
    const auto found = table.find(op);
    if (found == table.end()) return {};
    if (op == "()") return "CALL";
    return arity == 0 ? found->second.first : arity == 1 ? found->second.second : "";
}

// A C++ scalar: its field usage, field size, the fixed-width type the field holds, and a code
// (i signed, u unsigned, f real, b boolean, c character).
struct Scalar { std::string cpp, usage, abi; std::size_t size = 0; char code = 0; };
std::optional<Scalar> scalar(const std::string& bare) {
    struct Row { const char* usage; const char* abi; std::size_t size; char code; };
    static const std::map<std::string, Row> table = {
        {"bool", {"USAGE BINARY-CHAR UNSIGNED", "std::uint8_t", 1, 'b'}}, {"char", {"PIC X", "char", 1, 'c'}},
        {"signed char", {"USAGE BINARY-CHAR SIGNED", "std::int8_t", 1, 'i'}}, {"unsigned char", {"USAGE BINARY-CHAR UNSIGNED", "std::uint8_t", 1, 'u'}},
        {"short", {"USAGE BINARY-SHORT SIGNED", "std::int16_t", 2, 'i'}}, {"unsigned short", {"USAGE BINARY-SHORT UNSIGNED", "std::uint16_t", 2, 'u'}},
        {"int", {"USAGE BINARY-LONG SIGNED", "std::int32_t", 4, 'i'}}, {"unsigned int", {"USAGE BINARY-LONG UNSIGNED", "std::uint32_t", 4, 'u'}},
        {"long", {"USAGE BINARY-DOUBLE SIGNED", "std::int64_t", 8, 'i'}}, {"unsigned long", {"USAGE BINARY-DOUBLE UNSIGNED", "std::uint64_t", 8, 'u'}},
        {"long long", {"USAGE BINARY-DOUBLE SIGNED", "std::int64_t", 8, 'i'}},
        {"unsigned long long", {"USAGE BINARY-DOUBLE UNSIGNED", "std::uint64_t", 8, 'u'}},
        {"float", {"USAGE COMP-1", "float", 4, 'f'}}, {"double", {"USAGE COMP-2", "double", 8, 'f'}},
        {"long double", {"USAGE COMP-2", "double", 8, 'f'}}};
    const auto found = table.find(bare);
    if (found == table.end()) return std::nullopt;
    return Scalar{bare, found->second.usage, found->second.abi, found->second.size, found->second.code};
}
std::string scalar_word(const std::string& bare) {
    static const std::map<std::string, std::string> table = {{"bool", "BOOL"}, {"char", "CHAR"}, {"signed char", "SCHAR"},
        {"unsigned char", "UCHAR"}, {"short", "SHORT"}, {"unsigned short", "USHORT"}, {"int", "INT"}, {"unsigned int", "UINT"},
        {"long", "LONG"}, {"unsigned long", "ULONG"}, {"long long", "LLONG"}, {"unsigned long long", "ULLONG"},
        {"float", "FLOAT"}, {"double", "DOUBLE"}, {"long double", "LDOUBLE"}, {"void", "VOID"}};
    const auto found = table.find(bare);
    return found == table.end() ? word(bare) : found->second;
}

// How a C++ value lies in a record: one or more fields.
struct Shape {
    enum Kind { k_none, k_scalar, k_string, k_enumeration, k_handle, k_tuple, k_optional, k_program } kind = k_none;
    Scalar number;                   // scalar; enumeration (its field)
    std::string cpp;                 // the C++ value type
    std::size_t handle = 0;          // handle: index of its handle type
    bool shared = false;             // handle held by std::shared_ptr (container elements)
    std::vector<Shape> items;        // tuple elements; optional: the value
    std::vector<std::string> labels; // tuple field suffixes
    bool complex = false;            // tuple read and written as std::complex
    std::string text;                // string: "std::string", "std::string_view" or "std::filesystem::path"
    std::string enumeration_name;    // enumeration: qualified C++ name
    std::size_t callback = 0;        // program: index of the callback use
    std::size_t capacity = 0;        // string: its PIC X size (0: the --cobol-string-size)
};
std::size_t shape_size(const Shape& shape, std::size_t string_size) {
    switch (shape.kind) {
    case Shape::k_scalar: case Shape::k_enumeration: return shape.number.size;
    case Shape::k_string: return (shape.capacity ? shape.capacity : string_size) + 4;
    case Shape::k_handle: case Shape::k_program: return 8;
    case Shape::k_tuple: { std::size_t size = 0; for (const auto& item : shape.items) size += shape_size(item, string_size); return size; }
    case Shape::k_optional: return 1 + shape_size(shape.items.at(0), string_size);
    default: return 0;
    }
}

// Whether a shape is a plain value (scalars, strings and enums, possibly in pairs, tuples and
// optionals): one a non-const reference parameter can take in and write back.
bool plain_value(const Shape& shape) {
    switch (shape.kind) {
    case Shape::k_scalar: case Shape::k_string: case Shape::k_enumeration: return true;
    case Shape::k_tuple: case Shape::k_optional:
        return std::all_of(shape.items.begin(), shape.items.end(), plain_value);
    default: return false;
    }
}

// How a parameter is passed (top level): value-like fields, in/out fields (non-const references to
// scalars and strings, written back), handles (T, T&, const T&), copies for T&&, pointers (0 is
// null), shared_ptr and unique_ptr, a C string, or a COBOL program (std::function).
enum class Form { value, inout, ref, rvalue, pointer, shared, unique, cstring, program };
// How a result comes back.
enum class Ret { none, value, cstring, adopt, copy, owned, borrowed, shared, unique };

struct Slot {
    std::string word;      // field suffix: X, RESULT, ARG-1
    std::string name;      // C++ name, for docs
    const Type* type = nullptr;
    Shape shape;
    Form form = Form::value;
    Ret ret = Ret::none;
    DefaultValue default_value;
    std::string doc;
    std::string field;     // laid out: the COBOL field name
    std::size_t offset = 0;
};

enum class Container { none, sequence, array, set, map, function };
struct HandleType {
    std::string word;               // KMEANS, VECTOR-DOUBLE
    std::string cpp;                // C++ type
    std::string id;                 // C++ identifier part: class_<id>, resolve_<id>
    std::string owner;              // classes: qualified name
    Container container = Container::none;
    std::string family;             // vector, deque, list, valarray, set, unordered_set, map, unordered_map, array
    Shape element, key;             // sequence/array/set element; map key and value (element)
    std::size_t array_size = 0;
    std::vector<Slot> arguments;    // function: its parameters
    Slot result;                    // function: its result
    std::vector<std::size_t> derived; // classes: bound classes deriving from this one, deepest first
    std::vector<std::size_t> bases;   // classes: all bound bases (transitive)
    bool polymorphic = false;
    bool table = false;             // has a -TABLE record
    std::size_t keys = 0;           // map: the handle type of its key vector
    std::string record, table_record; // laid out
    std::size_t value_offset = 0, key_offset = 0, item_size = 0, capacity = 0;
    bool used = false;
};

// A use of a callback signature: a std::function parameter (callback = true), or an overridable
// virtual method of a director class.
struct CallbackUse {
    std::string record;             // its argument record in the callbacks copybook (claimed later)
    std::string base;               // preferred record name
    std::string what;               // for messages
    std::string cpp;                // std::function type (callbacks)
    std::vector<Slot> arguments;
    Slot result;
    std::string result_cpp;         // C++ result type
    std::string doc;                // signature line for docs
    std::size_t size = 0;
    bool has_self = false;          // overrides: -SELF at offset 0
    std::size_t handle = 0;         // overrides: the class
};

struct ClassPlan {
    const Record* record = nullptr;
    std::string word;               // KMEANS (nested: OUTER-INNER)
    std::vector<std::string> bases; // direct bound bases (minimal)
    std::string parent;             // enclosing class for nested classes
    std::size_t handle = 0;
    bool director = false;
    std::vector<const Function*> overridable; // director: overridable virtual methods
    std::map<const Function*, bool> pure;
    std::map<const Function*, std::size_t> override_use; // method -> callback use
};

// A COBOL call record and its stubs.
struct Call {
    enum Kind { k_function, k_method, k_constructor, k_extend, k_field, k_static_field } kind = k_function;
    std::string base;                // preferred record word
    std::string label;               // C++ name for messages
    std::string signature;           // first doc line
    const Function* function = nullptr;
    const Field* field = nullptr;
    const Variable* variable = nullptr;
    std::string owner;               // class (methods, constructors, fields)
    std::string invocation;          // C++ expression with @ARGS@ (and @SELF@)
    bool self = false;
    bool receives_self = false;      // an extension function whose first parameter is the object
    std::vector<Slot> params;
    Slot result;
    std::size_t required = 0;
    std::string base_call;           // director classes: the non-virtual call of the C++ method
    bool writable = false;           // fields
    std::string doc;                 // documentation body (markdown)
    Slot self_slot;                  // extension functions: how the object is passed
    std::vector<Slot> programs;      // extend: one PROGRAM-POINTER per overridable method
    // Laid out:
    std::string record, symbol, setter, base_symbol;
    std::size_t self_offset = 0, given_offset = 0, size = 0;
    bool has_given = false;
};
} // namespace

namespace {
enum class Use { parameter, result, element, callback_argument, callback_result };

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const CobolFullOptions& options)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), options_(options) { validate_modules(policy_, module_); }
    GeneratedCobolFullSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    CobolFullOptions options_;
    std::string prefix_;                       // DM
    std::size_t string_size_ = 256;
    std::size_t result_size_ = 256;
    // A string shape's PIC X size.
    std::size_t capacity(const Shape& shape) const { return shape.capacity ? shape.capacity : string_size_; }
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_, function_template_names_;
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
    std::map<std::string, std::string> enum_words_;
    std::map<std::string, const Type*> types_;  // every type in the module, by normalized spelling
    std::vector<HandleType> handles_;
    std::map<std::string, std::size_t> handle_index_; // word -> handle type
    std::vector<CallbackUse> callbacks_;
    std::vector<Call> calls_;
    std::set<std::string> words_, symbols_;
    std::map<std::string, bool> copyable_cache_;

    void reject(const std::string& context, const std::string& reason) { diagnostics_.push_back(context + ": " + reason); }
    bool excluded(const std::string& qualified, const std::string& identity = {}) const {
        for (const auto& pattern : policy_.exclude)
            if (policy_match(pattern, qualified) || (!identity.empty() && policy_match(pattern, identity))) return true;
        return false;
    }
    bool readonly(const std::string& qualified, const std::string& owner) const {
        for (const auto& pattern : policy_.readonly)
            if (policy_match(pattern, qualified) || (!owner.empty() && policy_match(pattern, owner))) return true;
        return false;
    }
    std::string renamed(const std::string& qualified, const std::string& fallback) const {
        const auto found = policy_.rename.find(qualified);
        if (found == policy_.rename.end()) return fallback;
        if (found->second.empty()) throw BindingError("empty COBOL rename target for " + qualified);
        return word(found->second);
    }
    bool flat(const std::vector<std::string>& namespaces) const {
        if (!flat_) return false;
        std::string scope;
        for (const auto& part : namespaces) scope += (scope.empty() ? "" : "::") + part;
        for (const auto& pattern : policy_.qualify) if (policy_match(pattern, scope)) return false;
        return true;
    }
    std::string scoped_word(const std::vector<std::string>& namespaces, const std::string& name) const {
        // COBOL has no nested modules: a policy submodule becomes a name prefix (DATA-IRIS).
        if (const auto submodule = policy_module(policy_, namespaces); !submodule.empty()) return word(submodule) + "-" + word(name);
        if (flat(namespaces)) return word(name);
        std::string text;
        for (const auto& part : namespaces) text += word(part) + "-";
        return text + word(name);
    }
    bool bound(const Type& type) const { return type.kind == TypeKind::record && classes_.count(type.declaration); }
    std::string conversion(const Type& type) const {
        const auto spelling = normalize(strip_cv(type.canonical));
        for (const auto& item : policy_.conversions)
            if (normalize(item.first) == spelling) return item.second;
        if (type.kind == TypeKind::template_instance && spelling.size() > 1 && spelling.back() == '>') {
            const auto stem = spelling.substr(0, spelling.size() - 1);
            for (const auto& item : policy_.conversions) {
                const auto key = normalize(item.first);
                if (key.size() <= stem.size() || key.compare(0, stem.size(), stem) != 0) continue;
                const char next = key[stem.size()];
                if (stem.back() == '<' || next == ',') return item.second;
            }
        }
        return {};
    }
    bool copy_type(const Type& type) const {
        if (type.kind == TypeKind::pointer || type.kind == TypeKind::rvalue_reference) return false;
        const auto& target = referent(type);
        return target.kind == TypeKind::record && policy_.copy_types.count(target.declaration) != 0;
    }
    std::string returns_policy(const Function& function) const {
        auto found = policy_.returns.find(function.identity());
        if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
        return found == policy_.returns.end() ? std::string() : found->second;
    }

    void index_types(const Type& type, unsigned depth = 0) {
        if (depth > 32) return;
        types_.emplace(normalize(strip_cv(type.canonical)), &type);
        for (const auto& argument : type.arguments) index_types(argument, depth + 1);
    }
    bool copyable_type(const Type& type, unsigned depth);
    bool copyable_record(const std::string& name, unsigned depth = 0, bool as_base = false);
    std::string class_word(const Record& record) const;
    void plan_classes();
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    void plan_directors();

    std::string type_word(const Type& type) const;
    std::optional<Shape> value_shape(const Type& type, Use use, std::string& why, unsigned depth = 0);
    std::optional<std::size_t> container_handle(const Type& type, std::string& why, unsigned depth);
    std::string handle_word(const Type& type) const { return handles_[handle_of(type)].word; }
    std::size_t handle_of(const Type& type) const;
    bool parameter(const Type& type, Slot& slot, std::string& why, bool allow_program = true);
    bool result(const Type& type, const Function* function, bool member, Slot& slot, std::string& why);
    bool callback_argument(const Type& type, Slot& slot, std::string& why);
    bool callback_result(const Type& type, Slot& slot, std::string& why);
    std::optional<std::size_t> callback_use(const Type& function_type, const std::string& what, std::string& why);
    bool plan_call(Call& call, std::string& why);

    // Emission.
    std::string meaning(const Slot& slot, bool in_callback) const;
    std::string read(const Shape& shape, const std::string& at, const std::string& what, bool consume = false) const;
    std::string write(const Shape& shape, const std::string& at, const std::string& value, const std::string& what, bool temporary, unsigned depth = 0) const;
    void fill_strings(std::ostream& out, const Shape& shape, const std::string& at) const;
};

bool Generator::copyable_type(const Type& type, unsigned depth) {
    if (depth > 32) return true;
    const auto bare = normalize(strip_cv(type.canonical));
    static const std::set<std::string> fixed = {"std::mutex", "std::recursive_mutex", "std::timed_mutex", "std::shared_mutex",
        "std::condition_variable", "std::condition_variable_any", "std::thread", "std::jthread", "std::stop_source"};
    if (fixed.count(bare)) return false;
    if (type.kind == TypeKind::rvalue_reference) return false;
    if (type.kind == TypeKind::record) return copyable_record(type.declaration, depth + 1);
    if (type.kind == TypeKind::template_instance) {
        const auto declaration = normalize(type.declaration);
        static const std::set<std::string> movable_only = {"std::unique_ptr", "std::atomic", "std::promise", "std::future",
            "std::packaged_task", "std::basic_ifstream", "std::basic_ofstream", "std::basic_fstream", "std::basic_stringstream",
            "std::basic_istringstream", "std::basic_ostringstream", "std::basic_filebuf", "std::unique_lock", "std::lock_guard",
            "std::scoped_lock", "std::move_only_function"};
        if (movable_only.count(declaration)) return false;
        if (declaration == "std::shared_ptr" || declaration == "std::weak_ptr" || declaration == "std::function") return true;
        for (const auto& argument : type.arguments) if (!copyable_type(argument, depth + 1)) return false;
    }
    return true;
}
bool Generator::copyable_record(const std::string& name, unsigned depth, bool as_base) {
    const auto found = records_.find(name);
    if (as_base && found != records_.end() && found->second->abstract) {
        const auto& record = *found->second;
        for (const auto& constructor : record.constructors)
            if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::lvalue_reference &&
                referent(constructor.parameters[0]).declaration == name && (constructor.is_deleted || constructor.access == "private"))
                return false;
        for (const auto& field : record.fields) if (!copyable_type(field.type, depth + 1)) return false;
        for (const auto& base : record.bases)
            if (!copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) return false;
        return true;
    }
    const auto cached = copyable_cache_.find(name);
    if (cached != copyable_cache_.end()) return cached->second;
    if (found == records_.end() || depth > 32) return true;
    copyable_cache_[name] = true; // provisional for recursive types
    const auto& record = *found->second;
    bool copyable = !record.abstract;
    bool copy_declared = false, move_declared = false;
    for (const auto& constructor : record.constructors) {
        if (constructor.parameters.size() != 1) continue;
        const auto& parameter = constructor.parameters[0];
        if (referent(parameter).declaration != name) continue;
        if (parameter.kind == TypeKind::lvalue_reference) {
            copy_declared = true;
            if (constructor.is_deleted || constructor.access != "public") copyable = false;
        } else if (parameter.kind == TypeKind::rvalue_reference) move_declared = true;
    }
    if (move_declared && !copy_declared) copyable = false;
    for (const auto& field : record.fields) if (copyable && !copyable_type(field.type, depth + 1)) copyable = false;
    for (const auto& base : record.bases)
        if (copyable && !copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) copyable = false;
    copyable_cache_[name] = copyable;
    return copyable;
}

std::string Generator::class_word(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, word(instantiated->second));
    if (!record.owner.empty()) return renamed(qualified, word(record.name));
    return renamed(qualified, scoped_word(record.namespaces, record.name));
}

void Generator::plan_classes() {
    for (const auto& requested : policy_.templates) {
        const auto found = std::find_if(module_.records.begin(), module_.records.end(), [&](const Record& record) {
            return !record.template_name.empty() && normalize(record.qualified_name()) == normalize(requested.second);
        });
        if (!identifier(requested.first)) throw BindingError("template binding name is not an identifier: " + requested.first);
        if (found == module_.records.end()) {
            const auto function = std::find_if(module_.functions.begin(), module_.functions.end(), [&](const Function& f) {
                return !f.template_name.empty() && normalize(f.qualified_name()) == normalize(requested.second);
            });
            if (function == module_.functions.end()) { reject(requested.second, "template instantiation was not exported by the frontend"); continue; }
            function_template_names_[function->qualified_name()] = requested.first;
            continue;
        }
        template_names_[found->qualified_name()] = requested.first;
    }
    for (const auto& selection : policy_.classes)
        if (std::none_of(module_.records.begin(), module_.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in COBOL policy: " + selection.first);
    for (const auto& record : module_.records) records_[record.qualified_name()] = &record;
    for (const auto& record : module_.records) {
        const auto owner = record.qualified_name();
        const bool listed = policy_.classes.count(owner) != 0;
        const bool instantiated = template_names_.count(owner) != 0;
        if (!record.template_name.empty() && !instantiated) continue;
        if (!listed && !instantiated && !policy_.select_all) {
            if (!policy_.explicit_class_selection && policy_.classes.empty()) reject(owner, "class requires an owned class policy");
            continue;
        }
        if (excluded(owner)) continue;
        if (!record.complete) { if (listed) reject(owner, "COBOL requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed COBOL results are not yet supported"); continue;
        }
        ClassPlan plan;
        plan.record = &record;
        classes_.emplace(owner, std::move(plan));
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = classes_.begin(); it != classes_.end();) {
            const auto& outer = it->second.record->owner;
            if (!outer.empty() && !classes_.count(outer)) { it = classes_.erase(it); changed = true; }
            else ++it;
        }
    }
    for (auto& [owner, plan] : classes_) plan.parent = plan.record->owner;
    for (auto& [owner, plan] : classes_) {
        std::vector<std::string> found;
        const auto visit = [&](const auto& self, const Record& record, unsigned depth) -> void {
            if (depth > 64) return;
            for (const auto& base : record.bases) {
                if (base.access != "public") continue;
                const auto name = base.type.declaration.empty() ? base.type.canonical : base.type.declaration;
                if (classes_.count(name)) { if (std::find(found.begin(), found.end(), name) == found.end()) found.push_back(name); continue; }
                const auto record_found = records_.find(name);
                if (record_found != records_.end()) self(self, *record_found->second, depth + 1);
            }
        };
        visit(visit, *plan.record, 0);
        plan.bases = found;
    }
    std::set<std::string> placed;
    const std::function<void(const std::string&, unsigned)> place = [&](const std::string& owner, unsigned depth) {
        if (placed.count(owner) || depth > 256) return;
        const auto& plan = classes_.at(owner);
        for (const auto& base : plan.bases) place(base, depth + 1);
        if (!plan.parent.empty()) place(plan.parent, depth + 1);
        if (placed.insert(owner).second) class_order_.push_back(owner);
    };
    for (const auto& record : module_.records) if (classes_.count(record.qualified_name())) place(record.qualified_name(), 0);
    // Words and handle types, in declaration order; a repeated word gets a number.
    std::set<std::string> taken;
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::string text = class_word(*plan.record);
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            text = class_word(*classes_.at(parent).record) + "-" + text;
        if (text.empty()) text = "CLASS";
        auto unique = text;
        for (int n = 2; !taken.insert(unique).second; ++n) unique = text + "-" + std::to_string(n);
        plan.word = unique;
        HandleType handle;
        handle.word = unique;
        handle.cpp = "::" + owner;
        handle.id = "k_" + ident(unique);
        handle.owner = owner;
        handle.polymorphic = plan.record->polymorphic;
        plan.handle = handles_.size();
        handle_index_[unique] = handles_.size();
        handles_.push_back(std::move(handle));
    }
    // Every bound base (transitively), and the bound classes deriving from each class.
    std::map<std::string, std::set<std::string>> all_bases;
    const std::function<void(const std::string&, std::set<std::string>&, unsigned)> collect =
        [&](const std::string& owner, std::set<std::string>& into, unsigned depth) {
            if (depth > 64) return;
            for (const auto& base : classes_.at(owner).bases) if (into.insert(base).second) collect(base, into, depth + 1);
        };
    std::map<std::string, std::size_t> depth_of;
    for (const auto& owner : class_order_) {
        collect(owner, all_bases[owner], 0);
        depth_of[owner] = all_bases[owner].size();
    }
    for (const auto& owner : class_order_) {
        auto& handle = handles_[classes_.at(owner).handle];
        for (const auto& base : all_bases[owner]) handle.bases.push_back(classes_.at(base).handle);
    }
    std::vector<std::string> deepest = class_order_;
    std::stable_sort(deepest.begin(), deepest.end(), [&](const std::string& a, const std::string& b) { return depth_of[a] > depth_of[b]; });
    for (const auto& owner : deepest)
        for (const auto& base : all_bases[owner]) handles_[classes_.at(base).handle].derived.push_back(classes_.at(owner).handle);
}

std::vector<const Function*> Generator::virtuals(const Record& record, std::map<std::string, bool>& pure) const {
    std::vector<const Function*> result;
    std::set<std::string> seen;
    const auto key = [](const Function& function) {
        std::string text = function.name + "(";
        for (const auto& parameter : function.parameters) text += parameter.canonical + ",";
        return text + (function.is_const ? ")const" : ")");
    };
    const std::function<bool(const Record&, unsigned)> visit = [&](const Record& current, unsigned depth) {
        if (depth > 64) return false;
        for (const auto& method : current.methods) {
            if (!method.is_virtual || method.is_static || method.name.rfind("~", 0) == 0) continue;
            if (seen.insert(key(method)).second) { result.push_back(&method); pure[key(method)] = method.is_pure; }
        }
        for (const auto& base : current.bases) {
            const auto found = records_.find(base.type.declaration.empty() ? base.type.canonical : base.type.declaration);
            if (found == records_.end()) return false;
            if (!visit(*found->second, depth + 1)) return false;
        }
        return true;
    };
    if (!visit(record, 0)) { pure.clear(); return {}; }
    return result;
}

void Generator::plan_directors() {
    if (policy_.directors == "none") return;
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        if (!record.polymorphic || !record.virtual_destructor || record.is_final || !record.public_destructor) continue;
        std::map<std::string, bool> pure;
        const auto methods = virtuals(record, pure);
        if (methods.empty()) continue;
        const bool constructible = std::any_of(record.constructors.begin(), record.constructors.end(), [](const Function& constructor) {
            return !constructor.is_deleted && constructor.access != "private";
        });
        if (!constructible) continue;
        bool possible = true;
        std::vector<std::pair<const Function*, std::size_t>> uses;
        std::map<const Function*, bool> is_pure;
        for (const auto* method : methods) {
            std::string key = method->name + "(";
            for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
            key += method->is_const ? ")const" : ")";
            const bool abstract = pure[key];
            std::string why;
            if (is_operator(method->name) || !method->unsupported_reason.empty()) why = "operator";
            CallbackUse use;
            use.what = "COBOL override of " + owner + "::" + method->name;
            if (why.empty()) {
                for (std::size_t i = 0; i < method->parameters.size() && why.empty(); ++i) {
                    Slot slot;
                    if (!callback_argument(method->parameters[i], slot, why)) break;
                    const auto given = i < method->parameter_names.size() ? method->parameter_names[i] : std::string();
                    slot.name = given;
                    slot.word = word(given);
                    use.arguments.push_back(slot);
                }
            }
            if (why.empty()) callback_result(method->result, use.result, why);
            if (!why.empty()) {
                if (abstract) possible = false;
                continue;
            }
            if (method->access == "private" && !abstract) continue;
            use.result_cpp = method->result.canonical;
            std::vector<std::string> parameters;
            for (std::size_t i = 0; i < method->parameters.size(); ++i)
                parameters.push_back(method->parameters[i].spelling + (use.arguments[i].name.empty() ? "" : " " + use.arguments[i].name));
            use.doc = method->qualified_name() + "(" + join(parameters) + ")" + (method->is_const ? " const" : "") + " -> " + method->result.spelling;
            uses.push_back({method, callbacks_.size()});
            is_pure[method] = abstract;
            callbacks_.push_back(std::move(use));
        }
        if (!possible || uses.empty()) continue;
        plan.director = true;
        for (const auto& [method, index] : uses) {
            plan.overridable.push_back(method);
            plan.override_use[method] = index;
            plan.pure[method] = is_pure[method];
        }
    }
}
} // namespace

namespace {
std::string declaration_of(const Type& type) { return normalize(type.declaration); }
bool string_type(const Type& type) {
    const auto declaration = declaration_of(type);
    return type.kind == TypeKind::template_instance && (declaration == "std::basic_string" || declaration == "std::basic_string_view") &&
        !type.arguments.empty() && strip_cv(type.arguments[0].canonical) == "char";
}
bool path_type(const Type& type) { return normalize(strip_cv(type.canonical)) == "std::filesystem::path"; }
std::string family_word(const std::string& declaration) {
    static const std::map<std::string, std::string> table = {{"std::vector", "VECTOR"}, {"std::deque", "DEQUE"}, {"std::list", "LIST"},
        {"std::valarray", "VALARRAY"}, {"std::set", "SET"}, {"std::unordered_set", "HASH-SET"}, {"std::map", "MAP"},
        {"std::unordered_map", "HASH-MAP"}, {"std::array", "ARRAY"}};
    const auto found = table.find(declaration);
    return found == table.end() ? std::string() : found->second;
}
std::size_t array_extent(const Type& type) {
    const auto text = strip_cv(type.canonical);
    const auto comma = text.rfind(',');
    if (comma == std::string::npos) return 0;
    try { return static_cast<std::size_t>(std::stoull(text.substr(comma + 1))); } catch (...) { return 0; }
}
} // namespace

std::string Generator::type_word(const Type& type) const {
    const Type& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "STRING";
        return type_word(pointee);
    }
    const auto bare = strip_cv(target.canonical);
    if (target.kind == TypeKind::builtin) return scalar_word(bare);
    if (target.kind == TypeKind::enumeration) {
        const auto found = enum_words_.find(target.declaration);
        return found != enum_words_.end() ? found->second : word(target.declaration.substr(target.declaration.rfind(':') + 1));
    }
    if (target.kind == TypeKind::record) {
        if (bound(target)) return classes_.at(target.declaration).word;
        if (path_type(target)) return "PATH";
        const auto name = target.declaration.empty() ? bare : target.declaration;
        return word(name.substr(name.rfind(':') == std::string::npos ? 0 : name.rfind(':') + 1));
    }
    if (target.kind == TypeKind::template_instance) {
        const auto conversion_target = conversion(target);
        if (!conversion_target.empty()) {
            const auto found = types_.find(normalize(conversion_target));
            if (found != types_.end()) return type_word(*found->second);
        }
        if (string_type(target)) return "STRING";
        const auto declaration = declaration_of(target);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? type_word(target.arguments[i]) : std::string("X"); };
        if (declaration == "std::array") return "ARRAY-" + argument(0) + "-" + std::to_string(array_extent(target));
        if (declaration == "std::map" || declaration == "std::unordered_map") return family_word(declaration) + "-" + argument(0) + "-" + argument(1);
        const auto family = family_word(declaration);
        if (!family.empty()) return family + "-" + argument(0);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string text = declaration == "std::pair" ? "PAIR" : "TUPLE";
            for (const auto& item : target.arguments) text += "-" + type_word(item);
            return text;
        }
        if (declaration == "std::optional") return "OPTIONAL-" + argument(0);
        if (declaration == "std::complex") return "COMPLEX-" + argument(0);
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return argument(0);
        if (declaration == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& signature = target.arguments[0];
            std::string text = "FN-" + (signature.arguments.empty() ? std::string("VOID") : type_word(signature.arguments[0]));
            for (std::size_t i = 1; i < signature.arguments.size(); ++i) text += (i == 1 ? "-OF-" : "-") + type_word(signature.arguments[i]);
            return text;
        }
        return word(declaration.substr(declaration.rfind(':') == std::string::npos ? 0 : declaration.rfind(':') + 1));
    }
    return word(bare);
}

std::size_t Generator::handle_of(const Type& type) const {
    const auto& target = referent(type);
    if (bound(target)) return classes_.at(target.declaration).handle;
    return handle_index_.at(fit(type_word(target)));
}

// The fields of a value (no reference or pointer at this level).
std::optional<Shape> Generator::value_shape(const Type& type, Use use, std::string& why, unsigned depth) {
    if (depth > 16) { why = "type nesting is too deep"; return std::nullopt; }
    if (type.is_volatile) { why = "volatile types are unsupported"; return std::nullopt; }
    const auto bare = strip_cv(type.canonical);
    const auto target_of_conversion = conversion(type);
    if (!target_of_conversion.empty()) {
        const auto found = types_.find(normalize(target_of_conversion));
        if (found == types_.end()) { why = "conversion target " + target_of_conversion + " is not used by the module"; return std::nullopt; }
        return value_shape(*found->second, use, why, depth + 1);
    }
    Shape shape;
    shape.cpp = bare;
    switch (type.kind) {
    case TypeKind::builtin: {
        const auto number = scalar(bare);
        if (!number) { why = "unsupported COBOL value type " + type.canonical; return std::nullopt; }
        shape.kind = Shape::k_scalar;
        shape.number = *number;
        return shape;
    }
    case TypeKind::enumeration: {
        const auto found = enums_.find(type.declaration);
        if (found == enums_.end()) { why = "enum " + type.declaration + " is not exported to COBOL"; return std::nullopt; }
        const auto underlying = scalar(found->second->underlying_type.empty() ? "int" : strip_cv(found->second->underlying_type));
        const bool wide = underlying && underlying->size == 8;
        const bool unsigned_wide = wide && underlying->code == 'u';
        shape.kind = Shape::k_enumeration;
        shape.number = Scalar{bare, wide ? (unsigned_wide ? "USAGE BINARY-DOUBLE UNSIGNED" : "USAGE BINARY-DOUBLE SIGNED") : "USAGE BINARY-LONG SIGNED",
                              wide ? (unsigned_wide ? "std::uint64_t" : "std::int64_t") : "std::int32_t", wide ? 8u : 4u, 'e'};
        shape.cpp = "::" + type.declaration;
        shape.enumeration_name = type.declaration;
        return shape;
    }
    case TypeKind::record:
        if (bound(type)) { shape.kind = Shape::k_handle; shape.handle = classes_.at(type.declaration).handle; shape.cpp = handles_[shape.handle].cpp; return shape; }
        if (path_type(type)) { shape.kind = Shape::k_string; shape.text = "std::filesystem::path"; shape.cpp = "std::filesystem::path"; return shape; }
        why = "class " + bare + " is not exported to COBOL";
        return std::nullopt;
    case TypeKind::template_instance: {
        if (string_type(type)) {
            shape.kind = Shape::k_string;
            shape.text = declaration_of(type) == "std::basic_string_view" ? "std::string_view" : "std::string";
            return shape;
        }
        const auto declaration = declaration_of(type);
        if (!family_word(declaration).empty() || declaration == "std::function") {
            if (declaration == "std::function" && use != Use::result && use != Use::element) {
                why = "std::function values are only supported as parameters (COBOL programs) and results";
                return std::nullopt;
            }
            const auto handle = container_handle(type, why, depth);
            if (!handle) return std::nullopt;
            shape.kind = Shape::k_handle;
            shape.handle = *handle;
            shape.cpp = handles_[*handle].cpp;
            return shape;
        }
        if (declaration == "std::pair" || declaration == "std::tuple" || declaration == "std::complex") {
            shape.kind = Shape::k_tuple;
            if (declaration == "std::complex") {
                if (type.arguments.empty() || type.arguments[0].kind != TypeKind::builtin) { why = "unsupported complex type"; return std::nullopt; }
                auto part = value_shape(type.arguments[0], Use::element, why, depth + 1);
                if (!part) return std::nullopt;
                shape.complex = true;
                shape.items = {*part, *part};
                shape.labels = {"RE", "IM"};
                return shape;
            }
            if (type.arguments.empty()) { why = "empty tuples are unsupported"; return std::nullopt; }
            for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                auto item = value_shape(type.arguments[i], Use::element, why, depth + 1);
                if (!item) return std::nullopt;
                if (item->kind == Shape::k_program) { why = "callbacks inside tuples are unsupported"; return std::nullopt; }
                shape.items.push_back(*item);
                shape.labels.push_back(declaration == "std::pair" ? (i == 0 ? "FIRST" : "SECOND") : std::to_string(i + 1));
            }
            return shape;
        }
        if (declaration == "std::optional") {
            if (type.arguments.empty()) { why = "unsupported optional"; return std::nullopt; }
            auto inner = value_shape(type.arguments[0], Use::element, why, depth + 1);
            if (!inner) return std::nullopt;
            if (inner->kind == Shape::k_optional) { why = "nested optionals are unsupported"; return std::nullopt; }
            shape.kind = Shape::k_optional;
            shape.items = {*inner};
            return shape;
        }
        if (declaration == "std::shared_ptr" && use == Use::element) {
            if (type.arguments.empty() || !bound(type.arguments[0])) { why = "smart pointers require an exported class: " + type.canonical; return std::nullopt; }
            shape.kind = Shape::k_handle;
            shape.handle = classes_.at(type.arguments[0].declaration).handle;
            shape.shared = true;
            return shape;
        }
        why = "unsupported COBOL template type " + type.canonical;
        return std::nullopt;
    }
    default:
        why = "unsupported COBOL value type " + type.canonical;
        return std::nullopt;
    }
}

// The handle type of a container or std::function value type, registered on first use.
std::optional<std::size_t> Generator::container_handle(const Type& type, std::string& why, unsigned depth) {
    const auto handle_word = fit(type_word(type));
    const auto found = handle_index_.find(handle_word);
    if (found != handle_index_.end()) return found->second;
    const auto declaration = declaration_of(type);
    HandleType handle;
    handle.word = handle_word;
    handle.cpp = strip_cv(type.canonical);
    handle.id = "v_" + ident(handle_word);
    handle.family = declaration.substr(5);
    const auto element = [&](std::size_t i, Shape& into) {
        if (type.arguments.size() <= i) { why = "incomplete template arguments in " + type.canonical; return false; }
        if (declaration != "std::optional" && type.arguments[i].kind == TypeKind::template_instance &&
            declaration_of(type.arguments[i]) == "std::unique_ptr") { why = "containers of unique_ptr are unsupported"; return false; }
        auto shape = value_shape(type.arguments[i], Use::element, why, depth + 1);
        if (!shape) return false;
        if (shape->kind == Shape::k_handle && handles_[shape->handle].container == Container::function) {
            why = "containers of functions are unsupported"; return false;
        }
        into = *shape;
        return true;
    };
    if (declaration == "std::function") {
        if (type.arguments.empty() || type.arguments[0].kind != TypeKind::function) { why = "unsupported std::function signature"; return std::nullopt; }
        const auto& signature = type.arguments[0];
        handle.container = Container::function;
        for (std::size_t i = 1; i < signature.arguments.size(); ++i) {
            Slot slot;
            if (!parameter(signature.arguments[i], slot, why, false)) { why = "function argument: " + why; return std::nullopt; }
            slot.word = "ARG-" + std::to_string(i);
            handle.arguments.push_back(slot);
        }
        if (!signature.arguments.empty() && !is_void(signature.arguments[0])) {
            if (!result(signature.arguments[0], nullptr, false, handle.result, why)) { why = "function result: " + why; return std::nullopt; }
            handle.result.word = "RESULT";
        }
    } else if (declaration == "std::map" || declaration == "std::unordered_map") {
        handle.container = Container::map;
        if (!element(0, handle.key) || !element(1, handle.element)) return std::nullopt;
        // The keys call returns them as a vector.
        const auto keys_word = fit("VECTOR-" + type_word(type.arguments[0]));
        if (!handle_index_.count(keys_word)) {
            HandleType keys;
            keys.word = keys_word;
            keys.cpp = "std::vector<" + strip_cv(type.arguments[0].canonical) + ">";
            keys.id = "v_" + ident(keys_word);
            keys.family = "vector";
            keys.container = Container::sequence;
            keys.element = handle.key;
            keys.table = true;
            handle_index_[keys_word] = handles_.size();
            handles_.push_back(std::move(keys));
        }
        handle.keys = handle_index_.at(keys_word);
    } else if (declaration == "std::set" || declaration == "std::unordered_set") {
        handle.container = Container::set;
        if (!element(0, handle.element)) return std::nullopt;
        handle.table = true;
    } else if (declaration == "std::array") {
        handle.container = Container::array;
        handle.array_size = array_extent(type);
        if (handle.array_size == 0) { why = "unsupported array extent in " + type.canonical; return std::nullopt; }
        if (!element(0, handle.element)) return std::nullopt;
        handle.table = true;
    } else {
        handle.container = Container::sequence;
        if (!element(0, handle.element)) return std::nullopt;
        handle.table = true;
    }
    // Elements were registered first: the handle's index follows theirs.
    const auto again = handle_index_.find(handle_word);
    if (again != handle_index_.end()) return again->second;
    handle_index_[handle_word] = handles_.size();
    handles_.push_back(std::move(handle));
    return handles_.size() - 1;
}

// A function or method parameter.
bool Generator::parameter(const Type& type, Slot& slot, std::string& why, bool allow_program) {
    slot.type = &type;
    if (type.is_volatile) { why = "volatile types are unsupported"; return false; }
    const auto value = [&](const Type& target, Form form) {
        auto shape = value_shape(target, Use::parameter, why);
        if (!shape) return false;
        slot.shape = *shape;
        slot.form = form;
        if (shape->kind == Shape::k_handle)
            slot.form = form == Form::rvalue ? Form::rvalue : Form::ref;
        return true;
    };
    const auto function_parameter = [&](const Type& target) {
        if (!allow_program) { why = "nested callbacks are unsupported"; return false; }
        const auto use = callback_use(target, "", why);
        if (!use) return false;
        slot.shape.kind = Shape::k_program;
        slot.shape.cpp = strip_cv(target.canonical);
        slot.shape.callback = *use;
        slot.form = Form::program;
        return true;
    };
    const auto smart = [&](const Type& target, Form form) {
        if (target.arguments.empty() || !bound(target.arguments[0])) { why = "smart pointers require an exported class: " + target.canonical; return false; }
        if (form == Form::unique && target.arguments.size() > 1 && declaration_of(target.arguments[1]) != "std::default_delete") {
            why = "unique_ptr with a custom deleter is unsupported"; return false;
        }
        slot.shape.kind = Shape::k_handle;
        slot.shape.handle = classes_.at(target.arguments[0].declaration).handle;
        slot.shape.cpp = handles_[slot.shape.handle].cpp;
        slot.form = form;
        return true;
    };
    if (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) {
        const auto& target = referent(type);
        const bool constant = target.is_const;
        if (target.kind == TypeKind::pointer) { why = "references to pointers are unsupported"; return false; }
        const auto declaration = declaration_of(target);
        if (target.kind == TypeKind::template_instance && declaration == "std::function") return function_parameter(target);
        if (target.kind == TypeKind::template_instance && declaration == "std::shared_ptr") {
            if (!constant && type.kind == TypeKind::lvalue_reference) { why = "non-const shared_ptr references are unsupported"; return false; }
            return smart(target, Form::shared);
        }
        if (target.kind == TypeKind::template_instance && declaration == "std::unique_ptr") {
            if (type.kind != TypeKind::rvalue_reference) { why = "unique_ptr references are unsupported"; return false; }
            return smart(target, Form::unique);
        }
        if (type.kind == TypeKind::rvalue_reference) return value(target, Form::rvalue) || false;
        if (!value(target, Form::value)) return false;
        if (slot.shape.kind == Shape::k_handle) {
            if (!constant && !conversion(target).empty()) { why = "non-const references to converted types are unsupported"; return false; }
            return true;
        }
        if (!constant) {
            if (!plain_value(slot.shape)) {
                why = "non-const references are only supported for scalars, strings, enums and pairs, tuples and optionals of them: " + type.canonical;
                return false;
            }
            if (!conversion(target).empty()) { why = "non-const references to converted types are unsupported"; return false; }
            slot.form = Form::inout;
        }
        return true;
    }
    if (type.kind == TypeKind::pointer) {
        if (type.arguments.empty()) { why = "unsupported pointer"; return false; }
        const auto& pointee = type.arguments.front();
        if (bound(pointee)) {
            slot.shape.kind = Shape::k_handle;
            slot.shape.handle = classes_.at(pointee.declaration).handle;
            slot.shape.cpp = handles_[slot.shape.handle].cpp;
            slot.form = Form::pointer;
            return true;
        }
        if (pointee.kind == TypeKind::builtin && strip_cv(pointee.canonical) == "char" && pointee.is_const) {
            slot.shape.kind = Shape::k_string;
            slot.shape.text = "std::string";
            slot.shape.cpp = "std::string";
            slot.form = Form::cstring;
            return true;
        }
        why = "unsupported COBOL pointer type " + type.canonical;
        return false;
    }
    if (type.kind == TypeKind::function) { why = "function types are unsupported"; return false; }
    const auto declaration = declaration_of(type);
    if (type.kind == TypeKind::template_instance && declaration == "std::function") return function_parameter(type);
    if (type.kind == TypeKind::template_instance && declaration == "std::shared_ptr") return smart(type, Form::shared);
    if (type.kind == TypeKind::template_instance && declaration == "std::unique_ptr") return smart(type, Form::unique);
    return value(type, Form::value);
}

// A function's result (FUNCTION is null for std::function results). MEMBER: borrowed results keep
// the object alive.
bool Generator::result(const Type& type, const Function* function, bool member, Slot& slot, std::string& why) {
    slot.type = &type;
    slot.word = "RESULT";
    if (is_void(type)) { slot.ret = Ret::none; return true; }
    if (type.is_volatile) { why = "volatile types are unsupported"; return false; }
    const auto policy = function ? returns_policy(*function) : std::string();
    const auto handle_class = [&](const Type& target) {
        slot.shape.kind = Shape::k_handle;
        slot.shape.handle = classes_.at(target.declaration).handle;
        slot.shape.cpp = handles_[slot.shape.handle].cpp;
    };
    if (type.kind == TypeKind::rvalue_reference) { why = "rvalue reference results are unsupported"; return false; }
    if (type.kind == TypeKind::lvalue_reference) {
        const auto& target = referent(type);
        if (target.kind == TypeKind::pointer) { why = "references to pointers are unsupported"; return false; }
        if (bound(target)) {
            handle_class(target);
            slot.ret = policy == "copy" || copy_type(type) ? Ret::copy : policy == "owned" ? Ret::owned : Ret::borrowed;
            if (slot.ret == Ret::owned) { why = "owned results must be pointers"; return false; }
            if (slot.ret == Ret::copy && !copyable_record(target.declaration)) { why = "copied result class is not copyable"; return false; }
            return true;
        }
        auto shape = value_shape(target, Use::result, why);
        if (!shape) return false;
        slot.shape = *shape;
        if (shape->kind == Shape::k_handle) {
            // Containers: a const reference is copied, a mutable one borrowed from the object.
            slot.ret = !target.is_const && member && conversion(target).empty() && policy != "copy" ? Ret::borrowed : Ret::adopt;
            return true;
        }
        slot.ret = Ret::value;
        return true;
    }
    if (type.kind == TypeKind::pointer) {
        if (type.arguments.empty()) { why = "unsupported pointer"; return false; }
        const auto& pointee = type.arguments.front();
        if (bound(pointee)) {
            handle_class(pointee);
            slot.ret = policy == "owned" ? Ret::owned : policy == "copy" ? Ret::copy : Ret::borrowed;
            if (slot.ret == Ret::copy && !copyable_record(pointee.declaration)) { why = "copied result class is not copyable"; return false; }
            return true;
        }
        if (pointee.kind == TypeKind::builtin && strip_cv(pointee.canonical) == "char") {
            slot.shape.kind = Shape::k_string;
            slot.shape.text = "std::string";
            slot.shape.cpp = "std::string";
            slot.ret = Ret::cstring;
            return true;
        }
        why = "unsupported COBOL pointer type " + type.canonical;
        return false;
    }
    const auto declaration = declaration_of(type);
    if (type.kind == TypeKind::template_instance && (declaration == "std::shared_ptr" || declaration == "std::unique_ptr")) {
        if (type.arguments.empty() || !bound(type.arguments[0])) { why = "smart pointers require an exported class: " + type.canonical; return false; }
        if (declaration == "std::unique_ptr" && type.arguments.size() > 1 && declaration_of(type.arguments[1]) != "std::default_delete") {
            why = "unique_ptr with a custom deleter is unsupported"; return false;
        }
        handle_class(type.arguments[0]);
        slot.ret = declaration == "std::shared_ptr" ? Ret::shared : Ret::unique;
        return true;
    }
    auto shape = value_shape(type, Use::result, why);
    if (!shape) return false;
    slot.shape = *shape;
    slot.ret = shape->kind == Shape::k_handle ? Ret::adopt : Ret::value;
    return true;
}

// An argument C++ passes to a COBOL program: values are copied into fields, objects and
// containers become handles that close when the program returns, and non-const references to
// scalars and strings are read back.
bool Generator::callback_argument(const Type& type, Slot& slot, std::string& why) {
    slot.type = &type;
    const auto& target = type.kind == TypeKind::pointer && !type.arguments.empty() ? type.arguments.front() : referent(type);
    if (type.kind == TypeKind::pointer && !bound(target)) { why = "unsupported callback pointer argument " + type.canonical; return false; }
    const auto declaration = declaration_of(target);
    if (target.kind == TypeKind::template_instance && (declaration == "std::shared_ptr" || declaration == "std::unique_ptr")) {
        if (declaration == "std::unique_ptr" || target.arguments.empty() || !bound(target.arguments[0])) {
            why = "unsupported callback argument " + type.canonical; return false;
        }
        slot.shape.kind = Shape::k_handle;
        slot.shape.handle = classes_.at(target.arguments[0].declaration).handle;
        slot.shape.cpp = handles_[slot.shape.handle].cpp;
        slot.form = Form::shared;
        return true;
    }
    auto shape = value_shape(target, Use::callback_argument, why);
    if (!shape) return false;
    slot.shape = *shape;
    slot.form = type.kind == TypeKind::pointer ? Form::pointer : Form::value;
    if (type.kind == TypeKind::lvalue_reference && !target.is_const && shape->kind != Shape::k_handle) {
        if (!plain_value(*shape)) {
            why = "non-const callback references are only supported for scalars, strings, enums and pairs, tuples and optionals of them";
            return false;
        }
        slot.form = Form::inout;
    }
    if (type.kind == TypeKind::rvalue_reference && shape->kind != Shape::k_handle) slot.form = Form::value;
    return true;
}

// The result a COBOL program returns to C++: handles of values are consumed (closed after copying).
bool Generator::callback_result(const Type& type, Slot& slot, std::string& why) {
    slot.type = &type;
    slot.word = "RESULT";
    if (is_void(type)) { slot.ret = Ret::none; return true; }
    if (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference || type.kind == TypeKind::pointer) {
        why = "callback results must be values: " + type.canonical; return false;
    }
    const auto declaration = declaration_of(type);
    if (type.kind == TypeKind::template_instance && (declaration == "std::shared_ptr" || declaration == "std::unique_ptr")) {
        if (type.arguments.empty() || !bound(type.arguments[0])) { why = "smart pointers require an exported class: " + type.canonical; return false; }
        slot.shape.kind = Shape::k_handle;
        slot.shape.handle = classes_.at(type.arguments[0].declaration).handle;
        slot.shape.cpp = handles_[slot.shape.handle].cpp;
        slot.ret = declaration == "std::shared_ptr" ? Ret::shared : Ret::unique;
        return true;
    }
    auto shape = value_shape(type, Use::callback_result, why);
    if (!shape) return false;
    if (shape->kind == Shape::k_handle && handles_[shape->handle].container == Container::function) {
        why = "callbacks cannot return functions"; return false;
    }
    slot.shape = *shape;
    slot.ret = shape->kind == Shape::k_handle ? Ret::adopt : Ret::value;
    return true;
}

// A std::function parameter: the COBOL program's argument record.
std::optional<std::size_t> Generator::callback_use(const Type& function_type, const std::string& what, std::string& why) {
    if (function_type.arguments.empty() || function_type.arguments[0].kind != TypeKind::function) { why = "unsupported std::function signature"; return std::nullopt; }
    const auto& signature = function_type.arguments[0];
    CallbackUse use;
    use.what = what;
    use.cpp = strip_cv(function_type.canonical);
    for (std::size_t i = 1; i < signature.arguments.size(); ++i) {
        Slot slot;
        if (!callback_argument(signature.arguments[i], slot, why)) { why = "callback argument: " + why; return std::nullopt; }
        slot.word = "ARG-" + std::to_string(i);
        use.arguments.push_back(slot);
    }
    if (!signature.arguments.empty() && !callback_result(signature.arguments[0], use.result, why)) { why = "callback result: " + why; return std::nullopt; }
    if (signature.arguments.empty()) use.result.ret = Ret::none;
    use.result_cpp = signature.arguments.empty() ? "void" : signature.arguments[0].canonical;
    use.doc = function_type.spelling;
    callbacks_.push_back(std::move(use));
    return callbacks_.size() - 1;
}

bool Generator::plan_call(Call& call, std::string& why) {
    const auto& function = *call.function;
    if (!function.unsupported_reason.empty() && function.unsupported_reason.rfind("default calls with ", 0) != 0) { why = function.unsupported_reason; return false; }
    if (function.is_deleted) { why = "deleted function"; return false; }
    if (function.default_arguments > function.parameters.size()) { why = "invalid default argument metadata"; return false; }
    const std::size_t skip = call.receives_self ? 1 : 0;
    if (function.parameters.size() < skip) { why = "missing self parameter"; return false; }
    if (skip && !parameter(function.parameters[0], call.self_slot, why, false)) { why = "unsupported COBOL self parameter: " + why; return false; }
    if (skip && call.self_slot.shape.kind != Shape::k_handle) { why = "the first parameter is not an object"; return false; }
    for (std::size_t i = skip; i < function.parameters.size(); ++i) {
        Slot slot;
        if (!parameter(function.parameters[i], slot, why)) { why = "unsupported COBOL parameter type: " + why; return false; }
        slot.name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
        slot.word = word(slot.name);
        if (i < function.default_values.size()) slot.default_value = function.default_values[i];
        for (const auto& item : function.documentation.params) if (!slot.name.empty() && item.name == slot.name) slot.doc = item.text;
        call.params.push_back(slot);
    }
    const auto total = function.parameters.size();
    call.required = total - function.default_arguments >= skip ? total - function.default_arguments - skip : 0;
    if (function.is_constructor) {
        call.result.word = "RESULT";
        call.result.shape.kind = Shape::k_handle;
        call.result.shape.handle = classes_.at(call.owner).handle;
        call.result.shape.cpp = handles_[call.result.shape.handle].cpp;
        call.result.ret = Ret::unique;
        return true;
    }
    if (!result(function.result, &function, call.self || call.receives_self, call.result, why)) { why = "unsupported COBOL result type: " + why; return false; }
    call.result.doc = function.documentation.returns;
    return true;
}

namespace {
// The C++ text of a default value, for docs.
std::string default_text(const DefaultValue& value) {
    if (value.kind.empty()) return "";
    if (value.kind == "default") return "{}";
    if (value.kind == "null") return "nullptr";
    if (value.kind == "char") return "'" + value.value + "'";
    return value.value;
}
std::string signature_of(const Function& function, const std::string& name, std::size_t skip, bool show_result) {
    std::vector<std::string> parameters;
    for (std::size_t i = skip; i < function.parameters.size(); ++i) {
        std::string text = function.parameters[i].spelling;
        if (i < function.parameter_names.size() && !function.parameter_names[i].empty()) text += " " + function.parameter_names[i];
        if (i < function.default_values.size() && !function.default_values[i].kind.empty()) text += " = " + default_text(function.default_values[i]);
        else if (i >= function.parameters.size() - function.default_arguments) text += " = ...";
        parameters.push_back(text);
    }
    std::string text = name + "(" + join(parameters) + ")";
    if (function.is_const) text += " const";
    if (show_result && !function.is_constructor) text += " -> " + function.result.spelling;
    return text;
}
std::string doc_block(const DocBlock& block) {
    if (block.kind == "code") {
        std::string language = block.language;
        if (language.empty() || language == "c++" || language == "cxx" || language == "cc") language = "cpp";
        return "```" + language + "\n" + block.text + "\n```";
    }
    if (block.kind == "math") return "```\n" + block.text + "\n```";
    if (block.kind == "heading") return "**" + block.text + "**";
    return block.text;
}
// Description of a declaration: brief, details, notes, errors, see also.
std::string description(const Documentation& doc) {
    std::vector<std::string> paragraphs;
    if (!doc.brief.empty()) paragraphs.push_back(doc.brief);
    for (const auto& block : doc.details) paragraphs.push_back(doc_block(block));
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        paragraphs.push_back(label + ": " + item.text);
    }
    if (!doc.throws.empty()) {
        std::string errors = "Errors (a non-zero status):";
        for (const auto& item : doc.throws) {
            std::string text = item.text;
            for (auto& c : text) if (c == '\n') c = ' ';
            errors += "\n- " + (item.name.empty() ? std::string() : item.name + (text.empty() ? "" : ": ")) + text;
        }
        paragraphs.push_back(errors);
    }
    if (!doc.see.empty()) paragraphs.push_back("See also: " + join(doc.see));
    return join(paragraphs, "\n\n");
}
} // namespace

namespace {
std::string at_offset(const std::string& at, std::size_t offset) { return offset ? "(" + at + " + " + std::to_string(offset) + ")" : at; }
} // namespace

std::string Generator::read(const Shape& shape, const std::string& at, const std::string& what, bool consume) const {
    const auto w = quote(what);
    const auto n = std::to_string(capacity(shape));
    switch (shape.kind) {
    case Shape::k_scalar:
        switch (shape.number.code) {
        case 'b': return "rc::boolean(rc::load<std::uint8_t>(" + at + "), " + w + ")";
        case 'c': return "rc::load<char>(" + at + ")";
        case 'f': return "static_cast<" + shape.number.cpp + ">(rc::load<" + shape.number.abi + ">(" + at + "))";
        default: return "rc::integer<" + shape.number.cpp + ">(rc::load<" + shape.number.abi + ">(" + at + "), " + w + ")";
        }
    case Shape::k_enumeration:
        return "enum_" + ident(enum_words_.at(shape.enumeration_name)) + "(rc::load<" + shape.number.abi + ">(" + at + "), " + w + ")";
    case Shape::k_string: {
        const auto text = "rc::text(" + at + ", " + n + ", " + at + " + " + n + ", " + w + ")";
        return shape.text == "std::filesystem::path" ? "std::filesystem::path(" + text + ")" : text;
    }
    case Shape::k_handle: {
        const auto& handle = handles_[shape.handle];
        const auto id = "rc::load<std::uint64_t>(" + at + ")";
        const auto function = shape.shared ? "shared" : consume ? "consume" : "object";
        return std::string("rc::") + function + "<" + handle.cpp + ">(" + id + ", class_" + handle.id + ", " + w + ")";
    }
    case Shape::k_tuple: {
        std::vector<std::string> items;
        std::size_t offset = 0;
        for (const auto& item : shape.items) {
            items.push_back(read(item, at_offset(at, offset), what, consume));
            offset += shape_size(item, string_size_);
        }
        return shape.cpp + "(" + join(items) + ")";
    }
    case Shape::k_optional:
        return "(rc::boolean(rc::load<std::uint8_t>(" + at + "), " + w + ") ? " + shape.cpp + "(" +
               read(shape.items.at(0), at_offset(at, 1), what, consume) + ") : " + shape.cpp + "())";
    case Shape::k_program:
        return "callback_" + std::to_string(shape.callback) + "(rc::program_of(" + at + "))";
    default: return "/* unsupported */";
    }
}

std::string Generator::write(const Shape& shape, const std::string& at, const std::string& value, const std::string& what, bool temporary, unsigned depth) const {
    const auto w = quote(what);
    const auto n = std::to_string(capacity(shape));
    switch (shape.kind) {
    case Shape::k_scalar:
        if (shape.number.code == 'b') return "rc::store<std::uint8_t>(" + at + ", (" + value + ") ? 1 : 0);\n";
        return "rc::store<" + shape.number.abi + ">(" + at + ", static_cast<" + shape.number.abi + ">(" + value + "));\n";
    case Shape::k_enumeration:
        return "rc::store<" + shape.number.abi + ">(" + at + ", static_cast<" + shape.number.abi + ">(" + value + "));\n";
    case Shape::k_string: {
        auto text = value;
        if (shape.text == "std::filesystem::path") text = "(" + value + ").string()";
        else if (shape.text == "std::string_view") text = "std::string(" + value + ")";
        return "rc::put_text(" + at + ", " + n + ", " + at + " + " + n + ", " + text + ", " + w + ");\n";
    }
    case Shape::k_handle: {
        const auto& handle = handles_[shape.handle];
        std::string id;
        if (temporary) id = shape.shared ? "temps.share<" + handle.cpp + ">(" + value + ", &resolve_" + handle.id + ")"
                                         : "temps.borrow<" + handle.cpp + ">(&(" + value + "), &resolve_" + handle.id + ")";
        else id = shape.shared ? "rc::share<" + handle.cpp + ">(" + value + ", &resolve_" + handle.id + ")"
                               : "rc::adopt<" + handle.cpp + ">(std::make_unique<" + handle.cpp + ">(" + value + "), &resolve_" + handle.id + ")";
        return "rc::store<std::uint64_t>(" + at + ", " + id + ");\n";
    }
    case Shape::k_tuple: {
        const auto name = "t" + std::to_string(depth) + "_";
        std::string out = "{\nauto&& " + name + " = " + value + ";\n";
        std::size_t offset = 0;
        for (std::size_t i = 0; i < shape.items.size(); ++i) {
            const auto item = shape.complex ? name + (i == 0 ? ".real()" : ".imag()") : "std::get<" + std::to_string(i) + ">(" + name + ")";
            out += write(shape.items[i], at_offset(at, offset), item, what, temporary, depth + 1);
            offset += shape_size(shape.items[i], string_size_);
        }
        return out + "}\n";
    }
    case Shape::k_optional: {
        const auto name = "o" + std::to_string(depth) + "_";
        return "{\nauto&& " + name + " = " + value + ";\nrc::store<std::uint8_t>(" + at + ", " + name + " ? 1 : 0);\nif (" + name + ") {\n" +
               write(shape.items.at(0), at_offset(at, 1), "*" + name, what, temporary, depth + 1) + "}\n}\n";
    }
    default: return "/* unsupported */;\n";
    }
}

// String fields of a callback buffer start as spaces with length -1 (trim), like the copybook's VALUEs.
void Generator::fill_strings(std::ostream& out, const Shape& shape, const std::string& at) const {
    const auto n = std::to_string(capacity(shape));
    if (shape.kind == Shape::k_string) {
        out << "std::memset(" << at << ", ' ', " << n << ");\nrc::store<std::int32_t>(" << at << " + " << n << ", -1);\n";
        return;
    }
    std::size_t offset = shape.kind == Shape::k_optional ? 1 : 0;
    for (const auto& item : shape.items) {
        fill_strings(out, item, at_offset(at, offset));
        offset += shape_size(item, string_size_);
    }
}

// What a field holds, for its doc comment.
std::string Generator::meaning(const Slot& slot, bool in_callback) const {
    const auto& shape = slot.shape;
    std::function<std::string(const Shape&)> describe = [&](const Shape& s) -> std::string {
        switch (s.kind) {
        case Shape::k_scalar: return s.number.code == 'b' ? "0 or 1" : s.number.code == 'c' ? "a character" : s.number.cpp;
        case Shape::k_enumeration: return enum_words_.at(s.enumeration_name) + " value (see the 88 levels)";
        case Shape::k_string: {
            const auto pic = "text in PIC X(" + std::to_string(capacity(s)) + "); ";
            if (!in_callback && slot.word == "RESULT") return pic + "-LENGTH is its length in bytes";
            if (!in_callback && slot.form == Form::inout) return pic + "-LENGTH -1 trims trailing spaces, and holds the length written back";
            return pic + "-LENGTH -1 trims trailing spaces";
        }
        case Shape::k_handle: return handles_[s.handle].word + " handle";
        case Shape::k_tuple: {
            std::vector<std::string> parts;
            for (std::size_t i = 0; i < s.items.size(); ++i) parts.push_back("-" + s.labels[i] + ": " + describe(s.items[i]));
            return join(parts, "; ");
        }
        case Shape::k_optional: return "-PRESENT 0 or 1, then " + describe(s.items.at(0));
        case Shape::k_program: return "PROGRAM-POINTER (SET it TO ENTRY \"PROGRAM-ID\"), or NULL";
        default: return "";
        }
    };
    std::string text = describe(shape);
    switch (slot.form) {
    case Form::pointer: text += ", 0 for null"; break;
    case Form::shared: text += in_callback ? "" : " (C++ shares the object)"; break;
    case Form::unique: text += " (C++ takes the object: the handle closes)"; break;
    case Form::inout: text += in_callback ? " (in/out: read back after the program returns)" : " (in/out: written back after the call)"; break;
    default: break;
    }
    if (in_callback && shape.kind == Shape::k_handle) text += ", valid until the program returns";
    switch (slot.ret) {
    case Ret::adopt: case Ret::copy: case Ret::owned: case Ret::unique: case Ret::shared:
        text += in_callback ? " (C++ takes it: the handle closes)" : " (new; close it)"; break;
    case Ret::borrowed: text += " (new; refers into its owner, which it keeps alive)"; break;
    default: break;
    }
    return text;
}

namespace {
// A record being laid out: its entries, names (for claiming) and size.
struct Line { int level; std::string name, usage, value; std::vector<std::string> doc; };
struct Built {
    std::vector<Line> lines;
    std::vector<std::string> names;
    std::size_t size = 0;
    std::size_t add(int level, const std::string& name, const std::string& usage, const std::string& value, std::size_t bytes,
                    const std::string& doc = {}) {
        Line line{level, name, usage, value, {}};
        if (!doc.empty()) line.doc.push_back(doc);
        lines.push_back(std::move(line));
        if (level != 1) names.push_back(name);
        const auto at = size;
        size += bytes;
        return at;
    }
};
// Copybook text: entries in fixed format's columns (and so free format's), doc comments wrapped.
class Book {
public:
    explicit Book(bool values) : values_(values) {}
    void comment(const std::string& text, const char* marker = "*>") {
        std::istringstream lines(text);
        std::string paragraph;
        bool any = false;
        while (std::getline(lines, paragraph)) {
            any = true;
            const auto indent = paragraph.find_first_not_of(' ');
            if (indent == std::string::npos) { out_ << "      " << marker << '\n'; continue; }
            const std::string lead(indent, ' ');
            std::istringstream words(paragraph.substr(indent));
            std::string current, piece;
            const std::size_t width = 72 - 7 - std::string(marker).size();
            while (words >> piece) {
                if (!current.empty() && current.size() + 1 + piece.size() > width) {
                    out_ << "      " << marker << ' ' << current << '\n';
                    current = lead + "  ";
                }
                current += (current.empty() || current.back() == ' ' ? (current.empty() ? lead : "") : " ") + piece;
            }
            out_ << "      " << marker << ' ' << current << '\n';
        }
        if (!any) out_ << "      " << marker << '\n';
    }
    void doc(const std::string& text) { comment(text, "*>>"); }
    void blank() { out_ << '\n'; }
    void entry(const Line& line) {
        for (const auto& text : line.doc) doc(text);
        const std::size_t indent = line.level == 1 || line.level == 78 ? 7 : line.level == 5 ? 10 : line.level == 10 ? 12 : 13;
        std::vector<std::string> tokens{(line.level < 10 ? "0" : "") + std::to_string(line.level), line.name};
        std::istringstream clause(line.usage + (values_ && !line.value.empty() ? " VALUE " + line.value : ""));
        for (std::string token; clause >> token;) tokens.push_back(token);
        std::string current(indent, ' ');
        bool first = true;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const bool last = i + 1 == tokens.size();
            const auto& token = tokens[i];
            if (!first && current.size() + 1 + token.size() + (last ? 1 : 0) > 72) {
                out_ << current << '\n';
                current = std::string(indent + 4, ' ') + token;
            } else current += (first ? "" : " ") + token;
            first = false;
        }
        out_ << current << ".\n";
    }
    void record(const Built& built) { for (const auto& line : built.lines) entry(line); }
    std::string str() const { return out_.str(); }
private:
    bool values_;
    std::ostringstream out_;
};
} // namespace

GeneratedCobolFullSource Generator::run() {
    if (!identifier(name_)) throw BindingError("COBOL module name must be an ASCII identifier starting with a letter");
    prefix_ = word(options_.prefix.empty() ? name_ : options_.prefix);
    if (prefix_.empty() || !std::isalpha(static_cast<unsigned char>(prefix_[0])) || prefix_.size() > 20)
        throw BindingError("COBOL prefix must start with a letter and have at most 20 characters: " + prefix_);
    string_size_ = options_.string_size;
    if (string_size_ < 1 || string_size_ > 65535) throw BindingError("COBOL string size must be between 1 and 65535");
    result_size_ = options_.result_size ? options_.result_size : string_size_;
    if (result_size_ < 1 || result_size_ > 16777216) throw BindingError("COBOL result size must be between 1 and 16777216");
    if (options_.table_size < 1 || options_.table_size > 1000000) throw BindingError("COBOL table size must be between 1 and 1000000");
    if (options_.parts == 0 || options_.parts > 256) throw BindingError("COBOL source parts must be from 1 to 256");
    const std::string P = prefix_;
    const std::string status_record = P + "-STATUS";
    std::set<std::string> noise;
    for (const auto& variable : module_.variables) noise.insert(variable.qualified_name() + ": unsupported declaration VarDecl");
    for (const auto& record : module_.records)
        if (!record.template_name.empty()) noise.insert(record.template_name + ": unsupported declaration ClassTemplateDecl");
    for (const auto& diagnostic : module_.diagnostics) {
        if (noise.count(diagnostic)) continue;
        const auto colon = diagnostic.find(": ");
        if (colon != std::string::npos && excluded(diagnostic.substr(0, colon))) continue;
        diagnostics_.push_back(diagnostic);
    }
    for (const auto& function : module_.functions) { index_types(function.result); for (const auto& p : function.parameters) index_types(p); }
    for (const auto& record : module_.records) {
        for (const auto* list : {&record.methods, &record.constructors})
            for (const auto& function : *list) { index_types(function.result); for (const auto& p : function.parameters) index_types(p); }
        for (const auto& field : record.fields) index_types(field.type);
    }
    plan_classes();
    // Enums: words for their constants and fields.
    {
        std::set<std::string> taken;
        for (const auto& owner : class_order_) taken.insert(classes_.at(owner).word);
        for (const auto& enumeration : module_.enumerations) {
            const auto qualified = enumeration.qualified_name();
            if (!enumeration.selected || enumeration.enumerators.empty() || excluded(qualified)) continue;
            if (!enumeration.owner.empty() && !classes_.count(enumeration.owner)) continue;
            auto text = enumeration.owner.empty() ? renamed(qualified, scoped_word(enumeration.namespaces, enumeration.name))
                                                  : classes_.at(enumeration.owner).word + "-" + renamed(qualified, word(enumeration.name));
            if (text.empty()) continue;
            auto unique = text;
            for (int n = 2; !taken.insert(unique).second; ++n) unique = text + "-" + std::to_string(n);
            enums_[qualified] = &enumeration;
            enum_words_[qualified] = unique;
        }
    }
    plan_directors();

    // ------------------------------------------------------------------ calls
    std::map<std::string, std::vector<Call>> class_calls;
    std::vector<Call> function_calls;
    const auto plan = [&](Call call, std::vector<Call>& into) {
        std::string why;
        if (!plan_call(call, why)) { reject(call.label, why); return; }
        into.push_back(std::move(call));
    };
    // Free functions attached to classes as methods (policy extend), and free operators.
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner) && excluded(owner)) continue;
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [method, target] : methods) {
            bool any = false;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const bool self = !function.parameters.empty() && [&, cls = owner] {
                    const auto& first = function.parameters[0];
                    const auto& base = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
                    if (base.kind != TypeKind::record) return false;
                    if (base.declaration == cls) return true;
                    const auto& handle = handles_[classes_.at(cls).handle];
                    return classes_.count(base.declaration) &&
                        std::find(handle.bases.begin(), handle.bases.end(), classes_.at(base.declaration).handle) != handle.bases.end();
                }();
                Call call;
                call.kind = Call::k_method;
                call.function = &function;
                call.owner = owner;
                call.receives_self = self;
                call.label = owner + "." + method + " (" + function.qualified_name() + ")";
                call.base = classes_.at(owner).word + "-" + word(method);
                call.invocation = "::" + function.qualified_name() + "(@ARGS@)";
                call.signature = signature_of(function, function.qualified_name(), 0, true);
                call.doc = description(function.documentation);
                plan(std::move(call), class_calls[owner]);
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
        }
    }
    for (const auto& function : module_.functions) {
        if (!is_operator(function.name) || extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity()) || function.parameters.empty() || function.parameters.size() > 2) continue;
        const auto& first = function.parameters[0];
        const auto& target = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
        const auto operator_name = operator_word(function.name, function.parameters.size() - 1);
        if (!bound(target) || operator_name.empty() || operator_name == "CALL" || operator_name == "AT") continue;
        Call call;
        call.kind = Call::k_method;
        call.function = &function;
        call.owner = target.declaration;
        call.receives_self = true;
        call.label = function.qualified_name();
        call.base = classes_.at(target.declaration).word + "-" + operator_name;
        call.invocation = "::" + function.qualified_name() + "(@ARGS@)";
        call.signature = signature_of(function, function.qualified_name(), 0, true);
        call.doc = description(function.documentation);
        plan(std::move(call), class_calls[target.declaration]);
    }
    for (const auto& owner : class_order_) {
        auto& plan_of = classes_.at(owner);
        const auto& record = *plan_of.record;
        const auto& W = plan_of.word;
        const auto config = policy_.classes.find(owner);
        const bool constructors = config == policy_.classes.end() || config->second.constructors;
        if (config != policy_.classes.end()) {
            for (const auto& requested : config->second.methods)
                if (std::none_of(record.methods.begin(), record.methods.end(), [&](const Function& f) { return f.name == requested && f.access == "public"; }))
                    throw BindingError("unknown or non-public COBOL policy method: " + owner + "::" + requested);
            for (const auto& requested : config->second.fields)
                if (std::none_of(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested && f.access == "public"; }))
                    throw BindingError("unknown or non-public COBOL policy field: " + owner + "::" + requested);
        }
        auto& calls = class_calls[owner];
        std::vector<Call> mine;
        const auto copy_or_move = [&](const Function& constructor) {
            return constructor.parameters.size() == 1 && referent(constructor.parameters[0]).declaration == owner &&
                   (constructor.parameters[0].kind == TypeKind::rvalue_reference || plan_of.director);
        };
        if (constructors && record.public_destructor && !record.abstract) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || constructor.access != "public" || excluded(owner + "::" + record.name, constructor.identity())) continue;
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                Call call;
                call.kind = Call::k_constructor;
                call.function = &constructor;
                call.owner = owner;
                call.label = owner + "::" + record.name;
                call.base = W + "-NEW";
                call.invocation = "std::make_unique<::" + owner + ">(@ARGS@)";
                call.signature = signature_of(constructor, owner + "::" + record.name, 0, false) + " -> " + W + " handle";
                call.doc = description(constructor.documentation);
                plan(std::move(call), mine);
            }
        }
        if (plan_of.director && constructors) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || constructor.access == "private" || copy_or_move(constructor)) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                Call call;
                call.kind = Call::k_extend;
                call.function = &constructor;
                call.owner = owner;
                call.label = owner + "::" + record.name + " (COBOL subclass)";
                call.base = W + "-EXTEND";
                call.invocation = "std::make_unique<director_" + handles_[plan_of.handle].id + ">(@ARGS@)";
                call.signature = signature_of(constructor, owner + "::" + record.name, 0, false) + " -> " + W + " handle with COBOL overrides";
                call.doc = description(constructor.documentation);
                std::set<std::string> taken = {"SELF", "GIVEN", "RESULT"};
                for (const auto* method : plan_of.overridable) {
                    Slot program;
                    program.shape.kind = Shape::k_program;
                    program.form = Form::program;
                    program.name = method->name;
                    auto text = is_operator(method->name) ? std::string("CALL") : word(method->name);
                    auto unique = text;
                    for (int n = 2; !taken.insert(unique).second; ++n) unique = text + "-" + std::to_string(n);
                    program.word = unique;
                    const auto& use = callbacks_[plan_of.override_use.at(method)];
                    program.doc = use.doc;
                    call.programs.push_back(program);
                }
                plan(std::move(call), mine);
            }
        }
        // Methods: a const overload duplicating a non-const one is left out.
        std::vector<const Function*> methods;
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            if (method.name.rfind("~", 0) == 0) continue;
            const bool duplicate = method.is_const && std::any_of(record.methods.begin(), record.methods.end(), [&](const Function& other) {
                if (&other == &method || other.name != method.name || other.is_const || other.access != "public" || other.parameters.size() != method.parameters.size()) return false;
                for (std::size_t i = 0; i < other.parameters.size(); ++i)
                    if (other.parameters[i].canonical != method.parameters[i].canonical) return false;
                return true;
            });
            if (!duplicate) methods.push_back(&method);
        }
        for (const auto* method : methods) {
            std::string part;
            if (is_operator(method->name)) {
                if (method->is_static) continue;
                part = operator_word(method->name, method->parameters.size());
                if (part.empty()) continue; // assignment, conversion and other operators stay C++-only
            } else part = renamed(method->qualified_name(), word(method->name));
            if (part.empty()) { reject(method->qualified_name(), "name cannot be a COBOL word"); continue; }
            Call call;
            call.kind = Call::k_method;
            call.function = method;
            call.owner = owner;
            call.self = !method->is_static;
            call.label = method->qualified_name();
            call.base = W + "-" + part;
            call.invocation = method->is_static ? "::" + owner + "::" + method->name + "(@ARGS@)" : "self." + method->name + "(@ARGS@)";
            call.signature = (method->is_static ? "static " : "") + signature_of(*method, method->qualified_name(), 0, true);
            call.doc = description(method->documentation);
            if (plan_of.director && method->is_virtual && !method->is_pure && !method->is_static &&
                std::find(plan_of.overridable.begin(), plan_of.overridable.end(), method) != plan_of.overridable.end())
                call.base_call = "self." + owner + "::" + method->name + "(@ARGS@)";
            plan(std::move(call), mine);
        }
        for (auto& call : calls) mine.push_back(std::move(call));
        calls = std::move(mine);
        // Fields and constants.
        std::set<std::string> member_words;
        for (const auto& call : calls) member_words.insert(call.base);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            std::string why;
            if (field.bitfield || field.type.is_volatile) why = "unsupported COBOL field type";
            else if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            else if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            // Reference and pointer fields are read-only: their getter returns what a method returning
            // the same type would (a borrowed handle into the object for bound classes).
            const bool indirect = field.type.kind == TypeKind::lvalue_reference || field.type.kind == TypeKind::pointer;
            Call call;
            call.kind = Call::k_field;
            call.field = &field;
            call.owner = owner;
            call.self = true;
            call.label = qualified;
            call.base = W + "-" + renamed(qualified, word(field.name));
            if (why.empty() && indirect) {
                if (result(field.type, nullptr, true, call.result, why)) call.result.word = "VALUE";
            } else if (why.empty()) {
                auto shape = value_shape(field.type, Use::result, why);
                if (shape) {
                    call.result.shape = *shape;
                    call.result.word = "VALUE";
                    call.result.type = &field.type;
                    call.result.ret = shape->kind == Shape::k_handle ? (copy_type(field.type) ? Ret::copy : Ret::borrowed) : Ret::value;
                    if (shape->kind == Shape::k_handle && !conversion(field.type).empty()) call.result.ret = Ret::adopt;
                }
            }
            if (!why.empty()) { reject(qualified, why); continue; }
            if (member_words.count(call.base)) { reject(qualified, "field name collides with a method of the same name"); continue; }
            call.writable = !indirect && !field.type.is_const && !readonly(qualified, owner);
            call.invocation = "self." + field.name;
            call.signature = field.type.spelling + " " + qualified;
            call.doc = description(field.documentation);
            call.result.doc = "";
            calls.push_back(std::move(call));
        }
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to COBOL"); continue; }
            std::string why;
            auto shape = value_shape(variable.type, Use::result, why);
            if (!shape) { reject(qualified, why); continue; }
            Call call;
            call.kind = Call::k_static_field;
            call.variable = &variable;
            call.owner = owner;
            call.label = qualified;
            call.base = W + "-" + renamed(qualified, word(variable.name));
            call.result.shape = *shape;
            call.result.word = "VALUE";
            call.result.type = &variable.type;
            call.result.ret = shape->kind == Shape::k_handle ? Ret::adopt : Ret::value;
            call.invocation = "::" + qualified;
            call.signature = "static " + variable.type.spelling + " " + qualified;
            call.doc = description(variable.documentation);
            if (member_words.count(call.base)) { reject(qualified, "constant name collides with a method of the same name"); continue; }
            calls.push_back(std::move(call));
        }
    }
    for (const auto& function : module_.functions) {
        if (extension_functions_.count(function.qualified_name()) || is_operator(function.name)) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        const auto specialization = function_template_names_.find(function.qualified_name());
        if (!function.template_name.empty() && specialization == function_template_names_.end()) continue;
        Call call;
        call.kind = Call::k_function;
        call.function = &function;
        call.label = function.qualified_name();
        call.base = specialization != function_template_names_.end() ? word(specialization->second)
            : renamed(function.qualified_name(), scoped_word(function.namespaces, function.name));
        if (call.base.empty()) { reject(function.qualified_name(), "name cannot be a COBOL word"); continue; }
        call.invocation = "::" + function.qualified_name() + "(@ARGS@)";
        call.signature = signature_of(function, function.qualified_name(), 0, true);
        call.doc = description(function.documentation);
        plan(std::move(call), function_calls);
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for COBOL";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }
    // Overloads: the one with the fewest parameters keeps the name, the others add their
    // parameter types (a number when that is taken).
    const auto name_overloads = [&](std::vector<Call>& calls) {
        std::map<std::string, std::vector<std::size_t>> groups;
        for (std::size_t i = 0; i < calls.size(); ++i) groups[calls[i].base].push_back(i);
        for (auto& [base, members] : groups) {
            if (members.size() < 2) continue;
            std::size_t primary = members.front();
            for (const auto i : members) if (calls[i].params.size() < calls[primary].params.size()) primary = i;
            for (const auto i : members) {
                if (i == primary) continue;
                std::string suffix;
                for (const auto& slot : calls[i].params) suffix += "-" + (slot.type ? type_word(*slot.type) : std::string("X"));
                calls[i].base = base + (suffix.empty() ? "-2" : suffix);
            }
        }
    };
    for (auto& [owner, calls] : class_calls) name_overloads(calls);
    name_overloads(function_calls);

    // ------------------------------------------------------------------ layout
    const auto claim = [&](const std::vector<std::string>& names, const std::vector<std::string>& symbols) {
        std::set<std::string> seen, seen_symbols;
        for (const auto& name : names) if (words_.count(name) || !seen.insert(name).second) return false;
        for (const auto& symbol : symbols) if (symbols_.count(symbol) || !seen_symbols.insert(symbol).second) return false;
        words_.insert(names.begin(), names.end());
        symbols_.insert(symbols.begin(), symbols.end());
        return true;
    };
    using Symbols = std::function<std::vector<std::string>(const std::string&)>;
    // Lays out a record named BASE, or BASE-2, BASE-3, ... when a name is taken.
    const auto place = [&](const std::string& base, const std::function<Built(const std::string&)>& build, const Symbols& symbols) {
        for (int n = 1; n < 1000; ++n) {
            const auto record = fit(n == 1 ? base : base + "-" + std::to_string(n));
            auto built = build(record);
            auto names = built.names;
            names.push_back(record);
            if (claim(names, symbols(record))) return std::make_pair(record, std::move(built));
        }
        throw BindingError("COBOL name collision: " + base);
    };
    const auto none = [](const std::string&) { return std::vector<std::string>{}; };
    const auto n_text = std::to_string(string_size_);
    std::function<void(Built&, int, const std::string&, const Shape&, const std::string&, bool)> add_shape =
        [&](Built& built, int level, const std::string& name, const Shape& shape, const std::string& doc, bool input) {
            switch (shape.kind) {
            case Shape::k_scalar: built.add(level, name, shape.number.usage, "", shape.number.size, doc); break;
            case Shape::k_enumeration: {
                built.add(level, name, shape.number.usage, "", shape.number.size, doc);
                const auto& enumeration = *enums_.at(shape.enumeration_name);
                std::set<std::string> values, names;
                for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
                    const auto value = i < enumeration.enumerator_values.size() ? enumeration.enumerator_values[i] : std::to_string(i);
                    const auto condition = fit(name + "-" + word(enumeration.enumerators[i]));
                    if (!names.insert(condition).second) continue;
                    built.add(88, condition, "VALUE " + value, "", 0);
                }
                break;
            }
            case Shape::k_string:
                built.add(level, name, "PIC X(" + std::to_string(capacity(shape)) + ")", "", capacity(shape), doc);
                built.add(level, fit(name + "-LENGTH"), "USAGE BINARY-LONG SIGNED", input ? "-1" : "0", 4);
                break;
            case Shape::k_handle: built.add(level, name, "USAGE BINARY-DOUBLE UNSIGNED", "", 8, doc); break;
            case Shape::k_program: built.add(level, name, "USAGE PROGRAM-POINTER", "", 8, doc); break;
            case Shape::k_tuple:
                for (std::size_t i = 0; i < shape.items.size(); ++i)
                    add_shape(built, level, fit(name + "-" + shape.labels[i]), shape.items[i], i == 0 ? doc : std::string(), input);
                break;
            case Shape::k_optional:
                built.add(level, fit(name + "-PRESENT"), "USAGE BINARY-CHAR UNSIGNED", "", 1, doc);
                add_shape(built, level, name, shape.items.at(0), "", input);
                break;
            default: break;
            }
        };
    std::function<void(const Shape&)> mark = [&](const Shape& shape) {
        for (const auto& item : shape.items) mark(item);
        if (shape.kind != Shape::k_handle || handles_[shape.handle].used) return;
        handles_[shape.handle].used = true;
        const auto index = shape.handle;
        if (handles_[index].container == Container::none) return;
        mark(handles_[index].element);
        mark(handles_[index].key);
        if (handles_[index].container == Container::map) { Shape keys; keys.kind = Shape::k_handle; keys.handle = handles_[index].keys; mark(keys); }
        for (const auto& argument : handles_[index].arguments) mark(argument.shape);
        mark(handles_[index].result.shape);
    };
    const auto slot_words = [](std::vector<Slot>& slots, std::set<std::string> taken) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            auto text = slots[i].word;
            if (text.empty() || taken.count(text)) text = "ARG-" + std::to_string(i + 1);
            while (taken.count(text)) text += "-X";
            taken.insert(text);
            slots[i].word = text;
        }
    };
    const auto slot_doc = [&](const Slot& slot, bool in_callback) {
        std::string head = slot.word == "RESULT" ? "Result" : slot.word == "VALUE" ? "Value" : slot.name.empty() ? slot.word : slot.name;
        std::string type = slot.type ? slot.type->spelling : std::string();
        if (!slot.default_value.kind.empty()) type += " = " + default_text(slot.default_value);
        std::string text = head + (type.empty() ? "" : " (" + type + ")") + ": " + meaning(slot, in_callback) + ".";
        if (!slot.doc.empty()) {
            auto doc = slot.doc;
            for (auto& c : doc) if (c == '\n') c = ' ';
            text += " " + doc;
        }
        return text;
    };
    const auto call_line = [&](const std::string& symbol, const std::string& record, const std::string& note = {}) {
        return "CALL \"" + symbol + "\" USING " + record + " " + status_record + (note.empty() ? "" : " (" + note + ")");
    };

    // Lays out a call's record; returns its copybook lines.
    // String results (and strings inside pair, tuple and optional results) take the result size.
    std::function<void(Shape&)> widen = [&](Shape& shape) {
        if (shape.kind == Shape::k_string) shape.capacity = result_size_;
        for (auto& item : shape.items) widen(item);
    };
    const auto lay_call = [&](Call& call) {
        if (call.result.word == "RESULT" && result_size_ != string_size_) widen(call.result.shape);
        std::set<std::string> reserved = {"SELF", "GIVEN", "RESULT"};
        if (call.kind == Call::k_field || call.kind == Call::k_static_field) reserved.insert("VALUE");
        for (const auto& program : call.programs) reserved.insert(program.word);
        slot_words(call.params, reserved);
        call.has_given = call.function && call.required < call.params.size();
        const bool self = call.self || call.receives_self;
        const auto& owner_word = call.owner.empty() ? std::string() : classes_.at(call.owner).word;
        const Symbols symbols = [&](const std::string& record) {
            const auto base = symbol_of(record);
            if (call.kind == Call::k_field) return call.writable ? std::vector<std::string>{base + "_get", base + "_set"} : std::vector<std::string>{base + "_get"};
            if (call.kind == Call::k_static_field) return std::vector<std::string>{base + "_get"};
            if (!call.base_call.empty()) return std::vector<std::string>{base, base + "_base"};
            return std::vector<std::string>{base};
        };
        auto [record, built] = place(P + "-" + call.base, [&](const std::string& record) {
            Built b;
            b.add(1, record, "", "", 0);
            if (self) call.self_offset = b.add(5, fit(record + "-SELF"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "Self: the " + owner_word + " handle.");
            for (auto& program : call.programs) {
                program.field = fit(record + "-" + program.word);
                program.offset = b.add(5, program.field, "USAGE PROGRAM-POINTER", "", 8,
                                       "Override of " + program.doc + ": SET it TO ENTRY \"PROGRAM-ID\", or leave it NULL" +
                                       (classes_.at(call.owner).pure.at(classes_.at(call.owner).overridable[static_cast<std::size_t>(&program - call.programs.data())])
                                            ? " (required: pure virtual)." : " (keeps the C++ method)."));
            }
            if (call.has_given)
                call.given_offset = b.add(5, fit(record + "-GIVEN"), "USAGE BINARY-LONG SIGNED", std::to_string(call.required), 4,
                                          "Arguments to send: " + std::to_string(call.required) + " to " + std::to_string(call.params.size()) +
                                          " (C++ defaults fill the rest); initially " + std::to_string(call.required) + ".");
            for (auto& slot : call.params) {
                slot.field = fit(record + "-" + slot.word);
                slot.offset = b.size;
                add_shape(b, 5, slot.field, slot.shape, slot_doc(slot, false), true);
            }
            if (call.result.ret != Ret::none) {
                call.result.field = fit(record + "-" + call.result.word);
                call.result.offset = b.size;
                add_shape(b, 5, call.result.field, call.result.shape, slot_doc(call.result, false), call.kind == Call::k_field && call.writable);
            }
            if (b.size == 0) { b.lines.push_back({5, "FILLER", "PIC X", "", {}}); b.size = 1; }
            return b;
        }, symbols);
        call.record = record;
        const auto list = symbols(record);
        call.symbol = list[0];
        if (call.kind == Call::k_field && call.writable) call.setter = list[1];
        if (!call.base_call.empty()) call.base_symbol = list[1];
        call.size = built.size;
        for (const auto& slot : call.params) mark(slot.shape);
        mark(call.result.shape);
        // The record's doc block: signature, CALL lines, documentation.
        std::vector<std::string> doc{call.signature};
        if (call.kind == Call::k_field || call.kind == Call::k_static_field) {
            doc.push_back(call_line(call.symbol, record, "reads -VALUE"));
            if (call.kind == Call::k_field && call.writable) doc.push_back(call_line(call.setter, record, "writes -VALUE"));
        } else doc.push_back(call_line(call.symbol, record));
        if (!call.base_symbol.empty()) doc.push_back(call_line(call.base_symbol, record, "runs the C++ method, never a COBOL override"));
        std::string body = call.doc;
        if (call.kind == Call::k_constructor || call.kind == Call::k_extend) {
            const auto& plan_of = classes_.at(call.owner);
            auto class_doc = description(plan_of.record->documentation);
            std::string head = "C++ class " + call.owner + (plan_of.bases.empty() ? "" : " (a subclass of " + [&] {
                std::vector<std::string> words;
                for (const auto& base : plan_of.bases) words.push_back(classes_.at(base).word);
                std::vector<std::string> records;
                for (const auto& base : plan_of.bases) records.push_back(P + "-" + classes_.at(base).word + "-*");
                return join(words) + "; the inherited methods' records, " + join(records) + ", take this handle as -SELF";
            }() + ")") + ". Close the handle with CALL \"" + symbol_of(P) + "_close\".";
            if (call.kind == Call::k_extend) {
                std::vector<std::string> must, may;
                for (std::size_t i = 0; i < plan_of.overridable.size(); ++i)
                    (plan_of.pure.at(plan_of.overridable[i]) ? must : may).push_back(call.programs[i].word);
                head += "\n\nA COBOL subclass: each PROGRAM-POINTER names the COBOL program that overrides that virtual method. The "
                        "program receives its -ARGS record (in the callbacks copybook) and the callback status.";
                if (!must.empty()) head += "\n\nMust override: " + join(must) + ".";
                if (!may.empty()) head += "\n\nMay override: " + join(may) + ".";
            }
            body = head + (class_doc.empty() ? "" : "\n\n" + class_doc) + (body.empty() ? "" : "\n\n" + body);
        }
        if (!body.empty()) { doc.push_back(""); doc.push_back(body); }
        built.lines[0].doc = {join(doc, "\n")};
        // std::function parameters name their programs' argument records.
        for (const auto& slot : call.params)
            if (slot.shape.kind == Shape::k_program) {
                auto& use = callbacks_[slot.shape.callback];
                use.base = slot.field + "-ARGS";
                use.what = "the COBOL program in " + slot.field;
            }
        return built;
    };

    const auto lay_callback = [&](CallbackUse& use) {
        slot_words(use.arguments, {"SELF", "RESULT"});
        auto [record, built] = place(use.base, [&](const std::string& record) {
            Built b;
            b.add(1, record, "", "", 0);
            if (use.has_self) b.add(5, fit(record + "-SELF"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "Self: the object's handle (" + handles_[use.handle].word + ").");
            for (auto& slot : use.arguments) {
                slot.field = fit(record + "-" + slot.word);
                slot.offset = b.size;
                add_shape(b, 5, slot.field, slot.shape, slot_doc(slot, true), false);
            }
            if (use.result.ret != Ret::none) {
                use.result.field = fit(record + "-RESULT");
                use.result.offset = b.size;
                add_shape(b, 5, use.result.field, use.result.shape, slot_doc(use.result, true), false);
            }
            if (b.size == 0) { b.lines.push_back({5, "FILLER", "PIC X", "", {}}); b.size = 1; }
            return b;
        }, none);
        use.record = record;
        use.size = built.size;
        for (const auto& slot : use.arguments) mark(slot.shape);
        mark(use.result.shape);
        built.lines[0].doc = {use.doc + "\nPROCEDURE DIVISION USING " + record + " " + P + "-CALLBACK-STATUS\n\n"
            "The record of a COBOL program that C++ calls (" + use.what + "). Set " + P + "-CALLBACK-STATUS-CODE to a non-zero "
            "value (and the message) to fail the call; the error reaches the original caller with kind CALLBACK."};
        return built;
    };

    Book book(true), callbacks_book(false);
    // The status, handle and callback status records.
    const char* kinds[] = {"OK", "ALLOCATION", "RANGE", "INDEX", "ARGUMENT", "NATIVE", "CLOSED", "CALLBACK"};
    const std::string message_text = std::to_string(options_.message_size);
    const auto status_lines = [&](const std::string& record, bool values) {
        Built b;
        b.add(1, record, "", "", 0);
        b.add(5, record + "-CODE", "USAGE BINARY-LONG SIGNED", values ? "0" : "", 4, "0 after a successful call; otherwise the error kind.");
        for (int i = 0; i < 8; ++i) b.add(88, record + "-" + kinds[i], "VALUE " + std::to_string(i), "", 0);
        b.add(5, record + "-KIND", "PIC X(10)", values ? "SPACES" : "", 10, "The kind in lower case: allocation, range, index, argument, native, closed or callback.");
        b.add(5, record + "-LENGTH", "USAGE BINARY-LONG SIGNED", values ? "0" : "", 4, "Bytes of -MESSAGE used.");
        b.add(5, record + "-MESSAGE", "PIC X(" + message_text + ")", values ? "SPACES" : "", options_.message_size, "The error message: the call and what went wrong.");
        if (!claim(b.names, {})) throw BindingError("COBOL name collision: " + record);
        words_.insert(record);
        return b;
    };
    book.comment("Generated by Rosetta: COBOL bindings for the C++ module " + name_ + ". COPY this copybook into WORKING-STORAGE; "
        "COPY \"" + options_.callbacks_name + "\" into the LINKAGE SECTION of programs that C++ calls back.");
    book.comment("Each call has a parameter record: fill its fields, then CALL its entry USING the record and " + status_record +
        ". RETURN-CODE and " + status_record + "-CODE receive 0 or the error kind; " + status_record + "-MESSAGE says what failed. STOP RUN exits with RETURN-CODE, "
        "so MOVE 0 TO RETURN-CODE after an expected failure.");
    book.comment("Fields have their C++ types' exact sizes. Strings are PIC X(" + n_text + ") fields with a -LENGTH: -1 sends the "
        "text without trailing spaces, 0 to " + n_text + " sends that many bytes; results set it" +
        (result_size_ != string_size_ ? std::string(" (a call's string results are PIC X(") + std::to_string(result_size_) + "))" : std::string()) + ". Calls with C++ defaults have a "
        "-GIVEN count of the arguments to send. Objects and containers are handles (BINARY-DOUBLE UNSIGNED, 0 for null): "
        "close each with CALL \"" + symbol_of(P) + "_close\" USING " + P + "-HANDLE " + status_record + ".");
    book.comment("Doc comments (*>>) carry the C++ documentation: the first lines of a record's block are its C++ signature and "
        "its CALL statements.");
    book.blank();
    {
        auto status = status_lines(status_record, true);
        status.lines[0].doc = {"The status of the last call. Every call sets it (pass OMITTED to skip it)."};
        book.record(status);
        book.blank();
        auto handle = place(P + "-HANDLE", [&](const std::string& record) {
            Built b;
            b.add(1, record, "", "", 0);
            b.add(5, record + "-SELF", "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "The handle.");
            b.add(5, record + "-TYPE", "PIC X(" + n_text + ")", "", string_size_, "Its type (from the type call).");
            b.add(5, record + "-TYPE-LENGTH", "USAGE BINARY-LONG SIGNED", "0", 4);
            b.add(5, record + "-OPEN", "USAGE BINARY-CHAR UNSIGNED", "", 1, "1 while the handle is open (from the open call).");
            return b;
        }, [&](const std::string&) { return std::vector<std::string>{symbol_of(P) + "_close", symbol_of(P) + "_handle_type", symbol_of(P) + "_handle_open"}; });
        handle.second.lines[0].doc = {"Any handle: object, container or function.\n" + call_line(symbol_of(P) + "_close", handle.first, "destroys its object; closing twice is harmless") +
            "\n" + call_line(symbol_of(P) + "_handle_type", handle.first, "sets -TYPE") + "\n" + call_line(symbol_of(P) + "_handle_open", handle.first, "sets -OPEN")};
        book.record(handle.second);
    }
    // Enum constants.
    for (const auto& [qualified, enumeration] : enums_) {
        const auto& W = enum_words_.at(qualified);
        Built b;
        std::set<std::string> names;
        for (std::size_t i = 0; i < enumeration->enumerators.size(); ++i) {
            const auto value = i < enumeration->enumerator_values.size() ? enumeration->enumerator_values[i] : std::to_string(i);
            const auto constant = fit(P + "-" + W + "-" + word(enumeration->enumerators[i]));
            if (words_.count(constant) || !names.insert(constant).second) continue;
            b.add(78, constant, "VALUE " + value, "", 0);
        }
        if (b.lines.empty()) continue;
        words_.insert(names.begin(), names.end());
        auto text = description(enumeration->documentation);
        b.lines[0].doc = {"C++ enum " + qualified + " (" + W + " fields)." + (text.empty() ? "" : "\n\n" + text)};
        book.blank();
        book.record(b);
    }
    // Calls, class by class, then functions.
    std::vector<Call*> calls;
    for (const auto& owner : class_order_) {
        auto& list = class_calls[owner];
        if (list.empty()) continue;
        book.blank();
        book.comment("---------------------------------------------------------------- " + classes_.at(owner).word);
        for (auto& call : list) {
            const auto built = lay_call(call);
            book.blank();
            book.record(built);
            calls.push_back(&call);
        }
    }
    if (!function_calls.empty()) {
        book.blank();
        book.comment("---------------------------------------------------------------- functions");
    }
    for (auto& call : function_calls) {
        const auto built = lay_call(call);
        book.blank();
        book.record(built);
        calls.push_back(&call);
    }

    // Override argument records, then std::function argument records.
    for (const auto& owner : class_order_) {
        auto& plan_of = classes_.at(owner);
        if (!plan_of.director) continue;
        for (const auto* method : plan_of.overridable) {
            auto& use = callbacks_[plan_of.override_use.at(method)];
            use.has_self = true;
            use.handle = plan_of.handle;
            use.base = P + "-" + plan_of.word + "-" + (is_operator(method->name) ? std::string("CALL") : word(method->name)) + "-ARGS";
            use.what = "the COBOL override of " + owner + "::" + method->name;
        }
    }
    {
        const auto status = status_lines(P + "-CALLBACK-STATUS", false);
        callbacks_book.comment("Generated by Rosetta: argument records of the COBOL programs that C++ calls for " + name_ +
            " (callbacks and overrides). COPY this copybook into their LINKAGE SECTION; the program's PROCEDURE DIVISION "
            "USING names its record and " + P + "-CALLBACK-STATUS.");
        callbacks_book.comment("Object and container arguments are handles valid until the program returns; handles a program "
            "returns for values are taken by C++ (closed after their object is copied).");
        callbacks_book.blank();
        auto lines = status;
        lines.lines[0].doc = {"The status a callback or override program sets to fail: a non-zero -CODE (for example 4, ARGUMENT) "
            "and a -MESSAGE with its -LENGTH. It starts as 0 for each call."};
        callbacks_book.record(lines);
    }
    for (auto& use : callbacks_) {
        if (use.base.empty()) continue; // its call was not exported
        const auto built = lay_callback(use);
        callbacks_book.blank();
        callbacks_book.record(built);
    }

    // Containers and function values.
    const auto capacity_for = [&](std::size_t item) {
        std::size_t capacity = options_.table_size;
        while (capacity > 16 && capacity * item > (std::size_t{4} << 20)) capacity /= 2;
        return capacity;
    };
    bool any_container = false;
    for (std::size_t index = 0; index < handles_.size(); ++index) {
        if (!handles_[index].used || handles_[index].container == Container::none) continue;
        if (!any_container) {
            book.blank();
            book.comment("---------------------------------------------------------------- containers and function values");
            any_container = true;
        }
        auto& h = handles_[index];
        const auto element_doc = [&](const Shape& shape, const std::string& what) {
            Slot slot;
            slot.shape = shape;
            return what + ": " + meaning(slot, false) + ".";
        };
        std::vector<std::string> ops;
        switch (h.container) {
        case Container::sequence:
            ops = {"new", "size", "get", "set"};
            if (h.family != "valarray") { ops.push_back("push"); ops.push_back("clear"); }
            break;
        case Container::array: ops = {"new", "size", "get", "set"}; break;
        case Container::set: ops = {"new", "size", "add", "has", "remove", "clear"}; break;
        case Container::map: ops = {"new", "size", "get", "set", "has", "remove", "keys", "clear"}; break;
        case Container::function: ops = {"call"}; break;
        default: break;
        }
        std::vector<std::string> table_ops;
        if (h.table) {
            table_ops = {"from_table", "to_table"};
            if (h.container == Container::sequence && h.family != "valarray") table_ops.insert(table_ops.begin() + 1, "append_table");
            if (h.container == Container::set) table_ops.insert(table_ops.begin() + 1, "append_table");
        }
        slot_words(h.arguments, {"SELF", "RESULT"});
        auto [record, built] = place(P + "-" + h.word, [&](const std::string& record) {
            Built b;
            b.add(1, record, "", "", 0);
            b.add(5, fit(record + "-SELF"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "The " + h.word + " handle (new sets it).");
            if (h.container == Container::sequence || h.container == Container::array) {
                b.add(5, fit(record + "-INDEX"), "USAGE BINARY-DOUBLE SIGNED", "1", 8, "1-based position for get and set.");
                b.add(5, fit(record + "-SIZE"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "The element count (size sets it).");
                h.value_offset = b.size;
                add_shape(b, 5, fit(record + "-VALUE"), h.element, element_doc(h.element, "The element for get, set and push"), true);
            } else if (h.container == Container::set) {
                b.add(5, fit(record + "-SIZE"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "The element count (size sets it).");
                b.add(5, fit(record + "-FOUND"), "USAGE BINARY-CHAR UNSIGNED", "", 1, "1 when has finds -VALUE.");
                h.value_offset = b.size;
                add_shape(b, 5, fit(record + "-VALUE"), h.element, element_doc(h.element, "The element for add, has and remove"), true);
            } else if (h.container == Container::map) {
                b.add(5, fit(record + "-SIZE"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "The entry count (size sets it).");
                b.add(5, fit(record + "-FOUND"), "USAGE BINARY-CHAR UNSIGNED", "", 1, "1 when has finds -KEY.");
                b.add(5, fit(record + "-KEYS"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "keys sets it: a new " + handles_[h.keys].word + " handle of the keys, in order.");
                h.key_offset = b.size;
                add_shape(b, 5, fit(record + "-KEY"), h.key, element_doc(h.key, "The key for get, set, has and remove"), true);
                h.value_offset = b.size;
                add_shape(b, 5, fit(record + "-VALUE"), h.element, element_doc(h.element, "The value for get and set"), true);
            } else {
                for (auto& slot : h.arguments) {
                    slot.field = fit(record + "-" + slot.word);
                    slot.offset = b.size;
                    add_shape(b, 5, slot.field, slot.shape, slot_doc(slot, false), true);
                }
                if (h.result.ret != Ret::none) {
                    h.result.field = fit(record + "-RESULT");
                    h.result.offset = b.size;
                    add_shape(b, 5, h.result.field, h.result.shape, slot_doc(h.result, false), false);
                }
            }
            return b;
        }, [&](const std::string& record) {
            std::vector<std::string> list;
            for (const auto& op : ops) list.push_back(symbol_of(record) + "_" + op);
            for (const auto& op : table_ops) list.push_back(symbol_of(record) + "_" + op);
            return list;
        });
        h.record = record;
        std::vector<std::string> doc{"C++ " + h.cpp + " (" + h.word + " handles)."};
        static const std::map<std::string, std::string> notes = {
            {"new", "sets -SELF to a new, empty one"}, {"size", "sets -SIZE"}, {"get", "reads -VALUE"}, {"set", "writes -VALUE"},
            {"push", "appends -VALUE"}, {"clear", "removes every element"}, {"add", "inserts -VALUE"}, {"has", "sets -FOUND"},
            {"remove", "erases it"}, {"keys", "sets -KEYS"}, {"call", "calls the function"}};
        for (const auto& op : ops) {
            auto note = notes.at(op);
            if (h.container == Container::map && op == "get") note = "reads the -VALUE of -KEY";
            if (h.container == Container::map && op == "set") note = "stores -VALUE under -KEY";
            if ((h.container == Container::sequence || h.container == Container::array) && (op == "get" || op == "set")) note += " at -INDEX";
            if (h.container == Container::array && op == "new") note = "sets -SELF to a new one of value-initialized elements";
            doc.push_back(call_line(symbol_of(record) + "_" + op, record, note));
        }
        if (h.element.kind == Shape::k_handle) doc.push_back("\nElement handles from get are copies: close them; set copies the element's object.");
        built.lines[0].doc = {join(doc, "\n")};
        book.blank();
        book.record(built);
        if (!h.table) continue;
        auto table = place(record + "-TABLE", [&](const std::string& table_record) {
            Built b;
            b.add(1, table_record, "", "", 0);
            b.add(5, fit(table_record + "-SELF"), "USAGE BINARY-DOUBLE UNSIGNED", "", 8, "The " + h.word + " handle (from_table sets it).");
            b.add(5, fit(table_record + "-START"), "USAGE BINARY-DOUBLE SIGNED", "1", 8, "to_table: the 1-based position of the first element to copy.");
            b.add(5, fit(table_record + "-COUNT"), "USAGE BINARY-LONG SIGNED", "0", 4, "The number of -ITEM entries used (to_table sets it).");
            Built item;
            add_shape(item, 10, fit(table_record + "-ITEM"), h.element, element_doc(h.element, "An element"), true);
            h.item_size = item.size;
            h.capacity = capacity_for(item.size);
            b.add(5, fit(table_record + "-ITEMS"), "OCCURS 0 TO " + std::to_string(h.capacity) + " TIMES DEPENDING ON " + fit(table_record + "-COUNT"), "", 0);
            for (auto& line : item.lines) { b.lines.push_back(line); b.names.push_back(line.name); }
            b.size += h.capacity * h.item_size;
            return b;
        }, none);
        h.table_record = table.first;
        std::vector<std::string> table_doc{"Copies " + h.word + " elements to or from a COBOL table of up to " + std::to_string(h.capacity) + " entries."};
        for (const auto& op : table_ops) {
            const auto note = op == "from_table" ? "sets -SELF to a new one holding the -COUNT items" : op == "append_table" ? "adds the -COUNT items to -SELF"
                : "copies elements from -START, as many as fit, and sets -COUNT";
            table_doc.push_back(call_line(symbol_of(record) + "_" + op, table.first, note));
        }
        table.second.lines[0].doc = {join(table_doc, "\n")};
        book.blank();
        book.record(table.second);
    }

    // ------------------------------------------------------------------ C++
    std::ostringstream prelude;
    prelude << "// Generated by Rosetta: C++ stubs of the COBOL module " << name_ << " (full API). Each stub takes its\n"
            << "// parameter record and the status record, and returns the status code.\n";
    for (const auto* header : {"algorithm", "array", "complex", "cstddef", "cstdint", "cstdio", "cstdlib", "cstring", "deque", "filesystem",
                               "functional", "iostream", "iterator", "limits", "list", "map", "memory", "mutex", "new", "optional", "set",
                               "stdexcept", "string", "string_view", "tuple", "type_traits", "unordered_map", "unordered_set", "utility",
                               "valarray", "vector"})
        prelude << "#include <" << header << ">\n";
    prelude << "#include " << quote(module_.header) << '\n'
            << "#define ROSETTA_COBOL_MODULE rosetta_cobol_" << name_ << '\n'
            << "#define ROSETTA_COBOL_MESSAGE " << options_.message_size << '\n'
            << detail::cobol_full_runtime
            << "namespace ROSETTA_COBOL_MODULE {\n";
    // Handle descriptors: casts to bound bases, and the most-derived bound class of an object.
    for (const auto& h : handles_) {
        if (h.container != Container::none && !h.used) continue;
        prelude << "inline void* cast_" << h.id << "(void* object, const rc::Class* to);\n"
                << "inline const rc::Class class_" << h.id << "{" << quote(h.word + " (" + normalize(h.cpp) + ")") << ", &cast_" << h.id << "};\n";
    }
    for (const auto& h : handles_) {
        if (h.container != Container::none && !h.used) continue;
        prelude << "inline void* cast_" << h.id << "(void* object, const rc::Class* to) {\n"
                << "    if (to == &class_" << h.id << ") return object;\n";
        if (!h.bases.empty()) {
            prelude << "    auto* self = static_cast<" << h.cpp << "*>(object);\n";
            for (const auto base : h.bases)
                prelude << "    if (to == &class_" << handles_[base].id << ") return static_cast<" << handles_[base].cpp << "*>(self);\n";
        }
        prelude << "    return nullptr;\n}\n"
                << "inline rc::Resolved resolve_" << h.id << "(" << h.cpp << "* object) {\n";
        if (h.polymorphic)
            for (const auto derived : h.derived)
                prelude << "    if (auto* derived = dynamic_cast<" << handles_[derived].cpp << "*>(object)) return {derived, &class_" << handles_[derived].id << "};\n";
        prelude << "    return {object, &class_" << h.id << "};\n}\n";
    }
    // Enum validation.
    for (const auto& [qualified, enumeration] : enums_) {
        const auto& W = enum_words_.at(qualified);
        std::string why;
        Type type;
        type.spelling = type.canonical = type.declaration = qualified;
        type.kind = TypeKind::enumeration;
        const auto shape = value_shape(type, Use::parameter, why);
        if (!shape) continue;
        const bool flags = policy_.enum_flags.count(qualified) != 0;
        prelude << "[[maybe_unused]] inline ::" << qualified << " enum_" << ident(W) << "(" << shape->number.abi << " value, const char* what) {\n";
        if (!flags) {
            std::set<std::string> values;
            prelude << "    switch (value) {\n";
            for (std::size_t i = 0; i < enumeration->enumerators.size(); ++i) {
                const auto value = i < enumeration->enumerator_values.size() ? enumeration->enumerator_values[i] : std::to_string(i);
                if (!values.insert(value).second) continue;
                prelude << "    case " << value << (shape->number.abi == "std::uint64_t" ? "ULL" : shape->number.abi == "std::int64_t" ? "LL" : "") << ":\n";
            }
            prelude << "        return static_cast<::" << qualified << ">(value);\n    default: break;\n    }\n"
                    << "    rc::fail(rc::argument, std::string(what) + \": \" + std::to_string(value) + \" is not a " << W << " value\");\n";
        } else prelude << "    (void)what;\n    return static_cast<::" << qualified << ">(value);\n";
        prelude << "}\n";
    }
    // Calls into COBOL programs: arguments into a fresh record, invoke, results back.
    const auto program_call = [&](std::ostream& out, const CallbackUse& use, const std::vector<std::string>& names, const std::string& program,
                                  const std::string& self_line) {
        out << "        std::lock_guard<std::recursive_mutex> hold(rc::program_mutex());\n"
            << "        rc::Temporaries temps;\n"
            << "        rc::Buffer buffer(" << use.size << ");\n"
            << "        unsigned char* args = buffer.data();\n";
        if (!self_line.empty()) out << self_line;
        for (const auto& slot : use.arguments) fill_strings(out, slot.shape, at_offset("args", slot.offset));
        fill_strings(out, use.result.shape, at_offset("args", use.result.offset));
        for (std::size_t i = 0; i < use.arguments.size(); ++i) {
            const auto& slot = use.arguments[i];
            const auto at = at_offset("args", slot.offset);
            if (slot.shape.kind == Shape::k_handle && (slot.form == Form::pointer || slot.form == Form::shared)) {
                const auto& h = handles_[slot.shape.handle];
                out << "        rc::store<std::uint64_t>(" << at << ", temps." << (slot.form == Form::pointer ? "borrow" : "share") << "<" << h.cpp
                    << ">(" << names[i] << ", &resolve_" << h.id << "));\n";
            } else out << write(slot.shape, at, names[i], slot.field, true);
        }
        out << "        rc::invoke(" << program << ", args, " << quote(use.what) << ");\n";
        for (std::size_t i = 0; i < use.arguments.size(); ++i)
            if (use.arguments[i].form == Form::inout)
                out << "        " << names[i] << " = " << read(use.arguments[i].shape, at_offset("args", use.arguments[i].offset), use.arguments[i].field) << ";\n";
        const auto at = at_offset("args", use.result.offset);
        const auto& r = use.result;
        switch (r.ret) {
        case Ret::none: break;
        case Ret::shared: case Ret::unique: {
            const auto& h = handles_[r.shape.handle];
            out << "        return rc::" << (r.ret == Ret::shared ? "shared" : "release") << "<" << h.cpp << ">(rc::load<std::uint64_t>(" << at
                << "), class_" << h.id << ", " << quote(r.field) << ");\n";
            break;
        }
        default: out << "        return " << read(r.shape, at, r.field, true) << ";\n";
        }
    };
    for (std::size_t k = 0; k < callbacks_.size(); ++k) {
        const auto& use = callbacks_[k];
        if (use.record.empty() || use.cpp.empty()) continue;
        std::vector<std::string> parameters, names;
        for (std::size_t i = 0; i < use.arguments.size(); ++i) {
            names.push_back("p" + std::to_string(i));
            parameters.push_back(use.arguments[i].type->canonical + " " + names.back());
        }
        prelude << "[[maybe_unused]] inline " << use.cpp << " callback_" << k << "(void* program) {\n"
                << "    if (!program) return {};\n"
                << "    return [program](" << join(parameters) << ") -> " << use.result_cpp << " {\n";
        program_call(prelude, use, names, "program", "");
        prelude << "    };\n}\n";
    }
    // Directors: C++ subclasses whose virtual methods call the COBOL programs of their handles.
    for (const auto& owner : class_order_) {
        const auto& plan_of = classes_.at(owner);
        if (!plan_of.director) continue;
        const auto& h = handles_[plan_of.handle];
        prelude << "struct director_" << h.id << " final : ::" << owner << " {\n"
                << "    template<class... A> explicit director_" << h.id << "(A&&... arguments) : ::" << owner << "(std::forward<A>(arguments)...) {}\n"
                << "    void* programs[" << plan_of.overridable.size() << "] = {};\n"
                << "    std::uint64_t self_id = 0;\n";
        for (std::size_t j = 0; j < plan_of.overridable.size(); ++j) {
            const auto* method = plan_of.overridable[j];
            const auto& use = callbacks_[plan_of.override_use.at(method)];
            std::vector<std::string> parameters, names, forward;
            for (std::size_t i = 0; i < method->parameters.size(); ++i) {
                const auto& type = method->parameters[i];
                names.push_back("p" + std::to_string(i));
                parameters.push_back(type.canonical + " " + names.back());
                const bool by_value = type.kind == TypeKind::rvalue_reference || (type.kind != TypeKind::lvalue_reference && type.kind != TypeKind::pointer);
                forward.push_back(by_value ? "std::move(" + names.back() + ")" : names.back());
            }
            prelude << "    " << method->result.canonical << " " << method->name << "(" << join(parameters) << ")" << (method->is_const ? " const" : "")
                    << (method->is_noexcept ? " noexcept" : "") << " override {\n"
                    << "        if (!programs[" << j << "]) ";
            if (plan_of.pure.at(method)) prelude << "rc::pure_virtual(" << quote(method->qualified_name()) << ");\n";
            else prelude << "return ::" << owner << "::" << method->name << "(" << join(forward) << ");\n";
            if (method->is_noexcept) prelude << "        try {\n";
            const auto self_line = "        rc::store<std::uint64_t>(args, rc::open(self_id) ? self_id : temps.borrow<::" + owner +
                                   ">(static_cast<const ::" + owner + "*>(this), &resolve_" + h.id + "));\n";
            program_call(prelude, use, names, "programs[" + std::to_string(j) + "]", self_line);
            if (method->is_noexcept)
                prelude << "        } catch (const std::exception& error) { std::fprintf(stderr, \"error in a noexcept COBOL override: %s\\n\", error.what()); }\n"
                        << "        catch (...) {}\n        std::terminate();\n";
            prelude << "    }\n";
        }
        prelude << "};\n";
    }
    prelude << "} // namespace ROSETTA_COBOL_MODULE\n";

    // Stubs.
    std::vector<std::string> stubs;
    const auto stub = [&](const std::string& symbol, const std::string& label, const std::string& comment, const std::string& body) {
        std::ostringstream out;
        out << "// " << comment << "\nextern \"C\" int " << symbol << "(unsigned char* record, unsigned char* status) {\n"
            << "    return rc::guard(status, " << quote(label) << ", [&] {\n" << body << "    });\n}\n";
        stubs.push_back(out.str());
    };
    const auto R = [](std::size_t offset) { return at_offset("record", offset); };
    // The C++ statements storing a result.
    const auto store_result = [&](const Slot& slot, const std::string& invocation, const std::string& keep) -> std::string {
        const auto at = R(slot.offset);
        const auto& field = slot.field;
        const bool pointer = slot.type && slot.type->kind == TypeKind::pointer;
        const auto handle = [&]() -> const HandleType& { return handles_[slot.shape.handle]; };
        const auto store = [&](const std::string& id) { return "rc::store<std::uint64_t>(" + at + ", " + id + ");\n"; };
        switch (slot.ret) {
        case Ret::none: return invocation + ";\n";
        case Ret::value: case Ret::adopt: return write(slot.shape, at, invocation, field, false);
        case Ret::cstring: {
            const auto n = std::to_string(capacity(slot.shape));
            return "rc::put_text(" + at + ", " + n + ", " + at + " + " + n + ", " + invocation + ", " + quote(field) + ");\n";
        }
        case Ret::copy:
            if (pointer) return "{\nauto* p_ = " + invocation + ";\n" + store("p_ ? rc::adopt<" + handle().cpp + ">(std::make_unique<" + handle().cpp +
                                ">(*p_), &resolve_" + handle().id + ") : 0");
            return store("rc::adopt<" + handle().cpp + ">(std::make_unique<" + handle().cpp + ">(" + invocation + "), &resolve_" + handle().id + ")");
        case Ret::owned: return store("rc::adopt<" + handle().cpp + ">(std::unique_ptr<" + handle().cpp + ">(" + invocation + "), &resolve_" + handle().id + ")");
        case Ret::borrowed:
            return store("rc::borrow<" + handle().cpp + ">(" + (pointer ? invocation : "&(" + invocation + ")") + ", &resolve_" + handle().id + ", " + keep + ")");
        case Ret::shared: return store("rc::share<" + handle().cpp + ">(" + invocation + ", &resolve_" + handle().id + ")");
        case Ret::unique: return store("rc::adopt<" + handle().cpp + ">(" + invocation + ", &resolve_" + handle().id + ")");
        }
        return {};
    };
    // Reads the arguments a call sends, makes the call, and stores its results.
    const auto call_body = [&](const Call& call, const std::string& invocation_template, const std::function<std::string(const std::string&)>& finish) {
        std::ostringstream out;
        std::string self_pass, keep = "std::shared_ptr<void>()";
        if (call.self) {
            const auto& h = handles_[classes_.at(call.owner).handle];
            out << "const auto self_id = rc::load<std::uint64_t>(" << R(call.self_offset) << ");\n"
                << "auto& self = rc::object<" << h.cpp << ">(self_id, class_" << h.id << ", " << quote(call.record + "-SELF") << ");\n";
            keep = "rc::keep_of(self_id)";
        }
        if (call.receives_self) {
            const auto& slot = call.self_slot;
            const auto& h = handles_[slot.shape.handle];
            const auto id = "rc::load<std::uint64_t>(" + R(call.self_offset) + ")";
            const auto what = quote(fit(call.record + "-SELF"));
            out << "const auto self_id = " << id << ";\n";
            keep = "rc::keep_of(self_id)";
            switch (slot.form) {
            case Form::pointer: out << "auto* self = rc::pointer<" << h.cpp << ">(self_id, class_" << h.id << ", " << what << ");\n"; break;
            case Form::shared: out << "auto self = rc::shared<" << h.cpp << ">(self_id, class_" << h.id << ", " << what << ");\n"; break;
            default: out << "auto& self = rc::object<" << h.cpp << ">(self_id, class_" << h.id << ", " << what << ");\n"; break;
            }
            self_pass = "self";
        }
        const auto emit = [&](std::size_t present) {
            std::vector<std::string> args;
            if (!self_pass.empty()) args.push_back(self_pass);
            std::string writebacks;
            for (int pass = 0; pass < 2; ++pass)
                for (std::size_t i = 0; i < present; ++i) {
                    const auto& slot = call.params[i];
                    if ((slot.form == Form::unique) != (pass == 1)) continue;
                    const auto a = "a" + std::to_string(i);
                    const auto at = R(slot.offset);
                    const auto what = quote(slot.field);
                    const auto load = "rc::load<std::uint64_t>(" + at + ")";
                    const auto& h = handles_[slot.shape.handle];
                    switch (slot.form) {
                    case Form::ref: case Form::rvalue:
                        if (slot.shape.kind == Shape::k_handle) { out << "auto& " << a << " = " << read(slot.shape, at, slot.field) << ";\n"; break; }
                        [[fallthrough]];
                    case Form::value: case Form::inout: case Form::cstring: case Form::program:
                        out << "auto " << a << " = " << read(slot.shape, at, slot.field) << ";\n"; break;
                    case Form::pointer: out << "auto* " << a << " = rc::pointer<" << h.cpp << ">(" << load << ", class_" << h.id << ", " << what << ");\n"; break;
                    case Form::shared: out << "auto " << a << " = rc::shared<" << h.cpp << ">(" << load << ", class_" << h.id << ", " << what << ");\n"; break;
                    case Form::unique: out << "auto " << a << " = rc::release<" << h.cpp << ">(" << load << ", class_" << h.id << ", " << what << ");\n"; break;
                    }
                }
            for (std::size_t i = 0; i < present; ++i) {
                const auto& slot = call.params[i];
                const auto a = "a" + std::to_string(i);
                const bool by_value = slot.type && slot.type->kind != TypeKind::lvalue_reference && slot.type->kind != TypeKind::pointer;
                switch (slot.form) {
                case Form::cstring: args.push_back(a + ".c_str()"); break;
                case Form::inout: args.push_back(a); writebacks += write(slot.shape, R(slot.offset), a, slot.field, false); break;
                case Form::rvalue:
                    if (slot.shape.kind == Shape::k_handle) {
                        const auto& h = handles_[slot.shape.handle];
                        const bool copyable = h.owner.empty() || copyable_record(h.owner);
                        args.push_back(copyable ? h.cpp + "(" + a + ")" : "std::move(" + a + ")");
                    } else args.push_back("std::move(" + a + ")");
                    break;
                case Form::unique: args.push_back("std::move(" + a + ")"); break;
                case Form::value: case Form::shared: case Form::program:
                    args.push_back(by_value && slot.shape.kind != Shape::k_handle ? "std::move(" + a + ")" : a); break;
                default: args.push_back(a); break;
                }
            }
            out << finish(replace_all(invocation_template, "@ARGS@", join(args))) << writebacks;
        };
        if (!call.has_given) emit(call.params.size());
        else {
            out << "switch (rc::given(" << R(call.given_offset) << ", " << call.required << ", " << call.params.size() << ")) {\n";
            for (std::size_t present = call.required; present <= call.params.size(); ++present) {
                out << "case " << present << ": {\n";
                emit(present);
                out << "break;\n}\n";
            }
            out << "default: break;\n}\n";
        }
        return out.str();
    };
    for (const auto* call : calls) {
        const auto& c = *call;
        if (c.kind == Call::k_field || c.kind == Call::k_static_field) {
            std::ostringstream get;
            if (c.kind == Call::k_field) {
                const auto& h = handles_[classes_.at(c.owner).handle];
                get << "const auto self_id = rc::load<std::uint64_t>(" << R(c.self_offset) << ");\n"
                    << "auto& self = rc::object<" << h.cpp << ">(self_id, class_" << h.id << ", " << quote(c.record + "-SELF") << ");\n";
            }
            get << store_result(c.result, c.invocation, c.kind == Call::k_field ? "rc::keep_of(self_id)" : "std::shared_ptr<void>()");
            stub(c.symbol, c.label, c.signature + " (read)", get.str());
            if (c.kind == Call::k_field && c.writable) {
                const auto& h = handles_[classes_.at(c.owner).handle];
                std::ostringstream set;
                set << "auto& self = rc::object<" << h.cpp << ">(rc::load<std::uint64_t>(" << R(c.self_offset) << "), class_" << h.id << ", "
                    << quote(c.record + "-SELF") << ");\n"
                    << "rc::assign(" << c.invocation << ", " << read(c.result.shape, R(c.result.offset), c.result.field) << ", " << quote(c.result.field) << ");\n";
                stub(c.setter, c.label, c.signature + " (write)", set.str());
            }
            continue;
        }
        if (c.kind == Call::k_extend) {
            const auto& plan_of = classes_.at(c.owner);
            const auto& h = handles_[plan_of.handle];
            std::ostringstream out;
            out << "void* programs[" << c.programs.size() << "] = {";
            for (std::size_t j = 0; j < c.programs.size(); ++j) out << (j ? ", " : "") << "rc::program_of(" << R(c.programs[j].offset) << ")";
            out << "};\n";
            for (std::size_t j = 0; j < c.programs.size(); ++j)
                if (plan_of.pure.at(plan_of.overridable[j]))
                    out << "if (!programs[" << j << "]) rc::fail(rc::argument, " << quote(c.programs[j].field + " must name the COBOL program overriding " +
                        plan_of.overridable[j]->qualified_name() + " (a pure virtual method)") << ");\n";
            out << "std::unique_ptr<director_" << h.id << "> object;\n"
                << call_body(c, c.invocation, [](const std::string& invocation) { return "object = " + invocation + ";\n"; })
                << "for (std::size_t j = 0; j < " << c.programs.size() << "; ++j) object->programs[j] = programs[j];\n"
                << "auto* raw = object.get();\n"
                << "const auto id = rc::adopt<" << h.cpp << ">(std::unique_ptr<" << h.cpp << ">(object.release()), &resolve_" << h.id << ");\n"
                << "raw->self_id = id;\n"
                << "rc::store<std::uint64_t>(" << R(c.result.offset) << ", id);\n";
            stub(c.symbol, c.label, c.signature, out.str());
            continue;
        }
        const auto keep = c.self || c.receives_self ? std::string("rc::keep_of(self_id)") : std::string("std::shared_ptr<void>()");
        stub(c.symbol, c.label, c.signature, call_body(c, c.invocation, [&](const std::string& invocation) { return store_result(c.result, invocation, keep); }));
        if (!c.base_symbol.empty())
            stub(c.base_symbol, c.label + " (C++ method)", c.signature + " (C++ method)",
                 call_body(c, c.base_call, [&](const std::string& invocation) { return store_result(c.result, invocation, keep); }));
    }
    // The handle record's calls.
    stub(symbol_of(P) + "_close", "close", "Closes any handle", "rc::close(rc::load<std::uint64_t>(record));\n");
    stub(symbol_of(P) + "_handle_type", "handle type", "The type of a handle",
         "const auto box = rc::find(rc::load<std::uint64_t>(record), " + quote(P + "-HANDLE-SELF") + ");\n"
         "rc::put_text(record + 8, " + n_text + ", record + " + std::to_string(8 + string_size_) + ", box->klass->name, " + quote(P + "-HANDLE-TYPE") + ");\n");
    stub(symbol_of(P) + "_handle_open", "handle open", "Whether a handle is open",
         "rc::store<std::uint8_t>(record + " + std::to_string(12 + string_size_) + ", rc::open(rc::load<std::uint64_t>(record)) ? 1 : 0);\n");
    // Containers and function values.
    for (const auto& h : handles_) {
        if (!h.used || h.container == Container::none) continue;
        const auto base = symbol_of(h.record);
        const auto object = "auto& object = rc::object<" + h.cpp + ">(rc::load<std::uint64_t>(record), class_" + h.id + ", " + quote(h.record + "-SELF") + ");\n";
        const auto value_field = fit(h.record + "-VALUE");
        const auto value_at = R(h.value_offset);
        const auto element = [&](const std::string& index) {
            return h.family == "list" ? "*std::next(object.begin(), static_cast<std::ptrdiff_t>(" + index + "))" : "object[" + index + "]";
        };
        const auto adopt_new = "rc::store<std::uint64_t>(record, rc::adopt<" + h.cpp + ">(std::make_unique<" + h.cpp + ">(), &resolve_" + h.id + "));\n";
        const auto size = "rc::store<std::uint64_t>(" + std::string(h.container == Container::sequence || h.container == Container::array ? "record + 16" : "record + 8") +
                          ", static_cast<std::uint64_t>(object.size()));\n";
        switch (h.container) {
        case Container::sequence: case Container::array: {
            stub(base + "_new", h.word + " new", "A new " + h.word, h.container == Container::array
                ? "rc::store<std::uint64_t>(record, rc::adopt<" + h.cpp + ">(std::make_unique<" + h.cpp + ">(), &resolve_" + h.id + "));\n" : adopt_new);
            stub(base + "_size", h.word + " size", "Its size", object + size);
            const auto index = "rc::position(rc::load<std::int64_t>(record + 8), object.size(), " + quote(h.record + "-INDEX") + ")";
            stub(base + "_get", h.word + " get", "An element", object + "const auto at = " + index + ";\n" + write(h.element, value_at, element("at"), value_field, false));
            stub(base + "_set", h.word + " set", "Sets an element", object + "const auto at = " + index + ";\n" + element("at") + " = " + read(h.element, value_at, value_field) + ";\n");
            if (h.container == Container::sequence && h.family != "valarray") {
                stub(base + "_push", h.word + " push", "Appends an element", object + "object.push_back(" + read(h.element, value_at, value_field) + ");\n");
                stub(base + "_clear", h.word + " clear", "Removes every element", object + "object.clear();\n");
            }
            break;
        }
        case Container::set:
            stub(base + "_new", h.word + " new", "A new " + h.word, adopt_new);
            stub(base + "_size", h.word + " size", "Its size", object + size);
            stub(base + "_add", h.word + " add", "Inserts an element", object + "object.insert(" + read(h.element, value_at, value_field) + ");\n");
            stub(base + "_has", h.word + " has", "Whether it holds an element",
                 object + "rc::store<std::uint8_t>(record + 16, object.count(" + read(h.element, value_at, value_field) + ") ? 1 : 0);\n");
            stub(base + "_remove", h.word + " remove", "Erases an element", object + "object.erase(" + read(h.element, value_at, value_field) + ");\n");
            stub(base + "_clear", h.word + " clear", "Removes every element", object + "object.clear();\n");
            break;
        case Container::map: {
            const auto key = read(h.key, R(h.key_offset), fit(h.record + "-KEY"));
            const auto& keys = handles_[h.keys];
            stub(base + "_new", h.word + " new", "A new " + h.word, adopt_new);
            stub(base + "_size", h.word + " size", "Its size", object + size);
            stub(base + "_get", h.word + " get", "The value of a key", object + "const auto found = object.find(" + key + ");\n"
                 "if (found == object.end()) rc::fail(rc::index, " + quote(fit(h.record + "-KEY") + ": the key is not in the map") + ");\n" +
                 write(h.element, value_at, "found->second", value_field, false));
            stub(base + "_set", h.word + " set", "Stores a value under a key", object + "object.insert_or_assign(" + key + ", " + read(h.element, value_at, value_field) + ");\n");
            stub(base + "_has", h.word + " has", "Whether it holds a key", object + "rc::store<std::uint8_t>(record + 16, object.count(" + key + ") ? 1 : 0);\n");
            stub(base + "_remove", h.word + " remove", "Erases a key", object + "object.erase(" + key + ");\n");
            stub(base + "_keys", h.word + " keys", "Its keys", object + "auto keys = std::make_unique<" + keys.cpp + ">();\n"
                 "for (const auto& entry : object) keys->push_back(entry.first);\n"
                 "rc::store<std::uint64_t>(record + 17, rc::adopt<" + keys.cpp + ">(std::move(keys), &resolve_" + keys.id + "));\n");
            stub(base + "_clear", h.word + " clear", "Removes every entry", object + "object.clear();\n");
            break;
        }
        case Container::function: {
            Call call;
            call.record = h.record;
            call.params = h.arguments;
            call.result = h.result;
            const auto body = "auto& self = rc::object<" + h.cpp + ">(rc::load<std::uint64_t>(record), class_" + h.id + ", " + quote(h.record + "-SELF") + ");\n" +
                call_body(call, "self(@ARGS@)", [&](const std::string& invocation) { return store_result(call.result, invocation, "std::shared_ptr<void>()"); });
            stub(base + "_call", h.word + " call", "Calls the function", body);
            break;
        }
        default: break;
        }
        if (!h.table) continue;
        const auto item = [&](const std::string& i) { return "(record + 20 + (" + i + ") * " + std::to_string(h.item_size) + ")"; };
        const auto item_field = fit(h.table_record + "-ITEM");
        const auto count = "rc::table_count(rc::load<std::int32_t>(record + 16), " + std::to_string(h.capacity) + ", " + quote(h.table_record + "-COUNT") + ")";
        const auto table_object = "auto& object = rc::object<" + h.cpp + ">(rc::load<std::uint64_t>(record), class_" + h.id + ", " + quote(h.table_record + "-SELF") + ");\n";
        std::string fill;
        if (h.container == Container::array)
            fill = "if (count != object.size()) rc::fail(rc::argument, " + quote(h.table_record + "-COUNT must be " + std::to_string(h.array_size)) + ");\n"
                   "for (std::size_t i = 0; i < count; ++i) object[i] = " + read(h.element, item("i"), item_field) + ";\n";
        else if (h.family == "valarray")
            fill = "object.resize(count);\nfor (std::size_t i = 0; i < count; ++i) object[i] = " + read(h.element, item("i"), item_field) + ";\n";
        else if (h.container == Container::set)
            fill = "for (std::size_t i = 0; i < count; ++i) object.insert(" + read(h.element, item("i"), item_field) + ");\n";
        else fill = "for (std::size_t i = 0; i < count; ++i) object.push_back(" + read(h.element, item("i"), item_field) + ");\n";
        stub(base + "_from_table", h.word + " from_table", "A new " + h.word + " from a table",
             "const auto count = " + count + ";\nauto made = std::make_unique<" + h.cpp + ">();\nauto& object = *made;\n" + fill +
             "rc::store<std::uint64_t>(record, rc::adopt<" + h.cpp + ">(std::move(made), &resolve_" + h.id + "));\n");
        if (h.container == Container::set || (h.container == Container::sequence && h.family != "valarray"))
            stub(base + "_append_table", h.word + " append_table", "Appends a table", table_object + "const auto count = " + count + ";\n" + fill);
        std::string copy = table_object +
            "const auto start = rc::load<std::int64_t>(record + 8);\n"
            "if (start < 1 || static_cast<std::uint64_t>(start) > object.size() + 1) rc::fail(rc::index, " +
            quote(h.table_record + "-START is outside 1 to the size + 1") + ");\n"
            "const auto total = static_cast<std::size_t>(object.size()) - static_cast<std::size_t>(start - 1);\n"
            "const auto count = std::min<std::size_t>(total, " + std::to_string(h.capacity) + ");\n";
        if (h.container == Container::set || h.family == "list")
            copy += "auto it = std::next(object.begin(), static_cast<std::ptrdiff_t>(start - 1));\n"
                    "for (std::size_t i = 0; i < count; ++i, ++it) {\n" + write(h.element, item("i"), "*it", item_field, false) + "}\n";
        else copy += "for (std::size_t i = 0; i < count; ++i) {\n" + write(h.element, item("i"), "object[static_cast<std::size_t>(start - 1) + i]", item_field, false) + "}\n";
        copy += "rc::store<std::int32_t>(record + 16, static_cast<std::int32_t>(count));\n";
        stub(base + "_to_table", h.word + " to_table", "Copies elements into a table", copy);
    }

    // Sources: the prelude in each part, stubs spread over the parts.
    GeneratedCobolFullSource result;
    std::vector<std::ostringstream> parts(options_.parts);
    std::vector<std::size_t> sizes(options_.parts, 0);
    for (const auto& text : stubs) {
        const auto smallest = static_cast<std::size_t>(std::min_element(sizes.begin(), sizes.end()) - sizes.begin());
        parts[smallest] << text;
        sizes[smallest] += text.size();
    }
    const auto unit = [&](std::size_t i) {
        return prelude.str() + "namespace ROSETTA_COBOL_MODULE {\n" + parts[i].str() + "} // namespace ROSETTA_COBOL_MODULE\n";
    };
    result.source = unit(0);
    for (std::size_t i = 1; i < options_.parts; ++i) result.parts.push_back(unit(i));
    result.copybook = book.str();
    result.callbacks = callbacks_book.str();
    result.diagnostics = diagnostics_;
    return result;
}

GeneratedCobolFullSource generate_cobol_full(const Module& module, const std::string& name, bool skip_unsupported,
                                             const BindingPolicy& policy, const CobolFullOptions& options) {
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
