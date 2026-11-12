#pragma once

// C++ runtime of the full Common Lisp backend (--lisp-api full): emitted (as text) into every
// generated source, inside the module's namespace ROSETTA_LISP_MODULE so that split sources of
// one module share it and two modules never accept each other's objects.
//
// Lisp and C++ exchange values as a compact binary encoding (see the tags below). Lisp encodes
// the arguments of a call into one buffer and calls the module's single entry point with the
// index of the generated function; C++ decodes them into Nodes, resolves the overload, converts,
// calls, and encodes the result. Callbacks and Lisp overrides of virtual methods go the other
// way through two function pointers the Lisp side registers at load time. No C++ exception
// crosses into Lisp, and Lisp errors in callbacks come back as C++ exceptions.
namespace rosetta::detail {
inline constexpr const char* lisp_full_runtime_parts[] = {
R"cpp(
namespace ROSETTA_LISP_MODULE { namespace rosetta_lisp {
// ---------------------------------------------------------------- errors
// Errors of the binding itself (conversions, overload resolution, closed objects).
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
// An error raised by Lisp code in a callback or override: Lisp signals the original condition.
struct LispFailure : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] inline void fail(const std::string& message) { throw Failure(message); }
// Lisp and C++ buffer standard output separately; each side flushes before control crosses to
// the other, so their output appears in program order even when it goes to a pipe or file.
inline void flush_output() { std::cout.flush(); std::fflush(stdout); }
// The argument being converted ("argument 'seed'"), named in conversion errors; null outside conversions.
inline const char*& converting() { static thread_local const char* label = nullptr; return label; }

// ---------------------------------------------------------------- the value encoding
enum Tag : unsigned char {
    tag_nil = 0, tag_t = 1, tag_int = 2, tag_bigint = 3, tag_real = 4, tag_string = 5, tag_list = 6,
    tag_reals = 7, tag_cons = 8, tag_map = 9, tag_object = 10, tag_lisp_object = 11, tag_function = 12,
    tag_ref = 13, tag_symbol = 14, tag_bytes = 15, tag_tuple = 16, tag_argument = 17, tag_complex = 18
};
struct Holder;
// A decoded Lisp value (arguments), or a value to encode (results).
struct Node {
    Tag kind = tag_nil;
    long long integer = 0;           // int; ref slot; argument index; object parent (-1: none)
    double real = 0, imag = 0;       // real, complex
    std::string text;                // string, symbol (lower case), bigint (decimal), bytes
    std::vector<Node> items;         // list, tuple, cons (2), map (key, value, ...), ref (1)
    std::vector<double> reals;       // reals
    Holder* holder = nullptr;        // object, native function (results)
    unsigned long long id = 0;       // object: Lisp object id (0: none yet); function: Lisp function id
    unsigned klass = 0;              // object results: index of the Lisp class
    Node() = default;
    explicit Node(Tag k) : kind(k) {}
    bool nil() const { return kind == tag_nil; }
};
inline Node boolean(bool value) { return Node(value ? tag_t : tag_nil); }

struct Reader {
    const unsigned char* at;
    const unsigned char* end;
    void need(std::size_t n) const { if (static_cast<std::size_t>(end - at) < n) fail("malformed value encoding"); }
    unsigned char u8() { need(1); return *at++; }
    template<class T> T raw() { need(sizeof(T)); T value; std::memcpy(&value, at, sizeof(T)); at += sizeof(T); return value; }
    std::string bytes(std::size_t n) { need(n); std::string value(reinterpret_cast<const char*>(at), n); at += n; return value; }
    Node node(unsigned depth = 0) {
        if (depth > 10000) fail("value nesting is too deep");
        Node n(static_cast<Tag>(u8()));
        switch (n.kind) {
        case tag_nil: case tag_t: break;
        case tag_int: n.integer = raw<long long>(); break;
        case tag_real: n.real = raw<double>(); break;
        case tag_complex: n.real = raw<double>(); n.imag = raw<double>(); break;
        case tag_bigint: case tag_string: case tag_symbol: case tag_bytes: n.text = bytes(static_cast<std::size_t>(raw<unsigned long long>())); break;
        case tag_list: case tag_tuple: case tag_map: {
            auto count = raw<unsigned long long>();
            if (n.kind == tag_map) count *= 2;
            if (count > static_cast<std::size_t>(end - at)) fail("malformed value encoding");
            n.items.reserve(static_cast<std::size_t>(count));
            for (unsigned long long i = 0; i < count; ++i) n.items.push_back(node(depth + 1));
            break;
        }
        case tag_cons: n.items.push_back(node(depth + 1)); n.items.push_back(node(depth + 1)); break;
        case tag_reals: {
            const auto count = static_cast<std::size_t>(raw<unsigned long long>());
            if (count > static_cast<std::size_t>(end - at) / sizeof(double)) fail("malformed value encoding");
            n.reals.resize(count);
            if (count) std::memcpy(n.reals.data(), at, count * sizeof(double));
            at += count * sizeof(double);
            break;
        }
        case tag_object: n.holder = reinterpret_cast<Holder*>(static_cast<std::uintptr_t>(raw<unsigned long long>())); n.id = raw<unsigned long long>(); break;
        case tag_function: n.id = raw<unsigned long long>(); break;
        case tag_ref: n.integer = raw<unsigned>(); n.items.push_back(node(depth + 1)); break;
        default: fail("malformed value encoding");
        }
        return n;
    }
};
struct Writer {
    std::string out;
    void u8(unsigned char v) { out.push_back(static_cast<char>(v)); }
    template<class T> void raw(T value) { out.append(reinterpret_cast<const char*>(&value), sizeof(T)); }
    void text(const std::string& value) { raw<unsigned long long>(value.size()); out += value; }
    void node(const Node& n) {
        u8(n.kind);
        switch (n.kind) {
        case tag_nil: case tag_t: break;
        case tag_int: raw<long long>(n.integer); break;
        case tag_real: raw<double>(n.real); break;
        case tag_complex: raw<double>(n.real); raw<double>(n.imag); break;
        case tag_bigint: case tag_string: case tag_symbol: case tag_bytes: text(n.text); break;
        case tag_list: case tag_tuple: raw<unsigned long long>(n.items.size()); for (const auto& item : n.items) node(item); break;
        case tag_map: raw<unsigned long long>(n.items.size() / 2); for (const auto& item : n.items) node(item); break;
        case tag_cons: node(n.items.at(0)); node(n.items.at(1)); break;
        case tag_reals:
            raw<unsigned long long>(n.reals.size());
            out.append(reinterpret_cast<const char*>(n.reals.data()), n.reals.size() * sizeof(double));
            break;
        case tag_object: raw<unsigned long long>(reinterpret_cast<std::uintptr_t>(n.holder)); raw<unsigned>(n.klass); raw<long long>(n.integer); break;
        case tag_lisp_object: raw<unsigned long long>(n.id); break;
        case tag_function: raw<unsigned long long>(reinterpret_cast<std::uintptr_t>(n.holder)); break;
        case tag_argument: raw<unsigned>(static_cast<unsigned>(n.integer)); break;
        default: fail("cannot encode value");
        }
    }
};

inline std::string type_name(const Node& n);

// ---------------------------------------------------------------- calls from Lisp
// One call: the decoded arguments, the result, and updated values of ref cells.
struct Call {
    std::vector<Node> args;
    Node result;
    std::vector<std::pair<unsigned, Node>> writebacks;
};

// Functions the Lisp side registers when it loads the module.
// invoke(kind, id, generic, arguments, size, &result, &size): kind 0 calls Lisp function `id`;
// kind 1 calls generic function number `generic` on Lisp object `id` (a director's override).
// Status 0: result is the value; 1: result is an error message; 2: the same, and Lisp keeps the
// condition to signal again. retain(id, delta) adjusts C++'s references to a Lisp value.
using InvokeFn = int (*)(int, unsigned long long, unsigned, const unsigned char*, std::size_t, unsigned char**, std::size_t*);
using RetainFn = void (*)(unsigned long long, int);
struct Hooks { static inline InvokeFn invoke = nullptr; static inline RetainFn retain = nullptr; };

// A Lisp value kept alive by C++ (a callback, or the Lisp object a borrowed native lives in).
struct LispRef {
    unsigned long long id;
    explicit LispRef(unsigned long long value) : id(value) { if (!Hooks::retain) fail("module is not initialized"); Hooks::retain(id, 1); }
    ~LispRef() { if (Hooks::retain) Hooks::retain(id, -1); }
    LispRef(const LispRef&) = delete;
    LispRef& operator=(const LispRef&) = delete;
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- classes
struct TypeInfo;
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct TypeInfo {
    const char* name = nullptr; // Lisp class name
    unsigned index = 0;         // position in the Lisp class vector
    std::vector<BaseLink> bases;
    void (*destroy)(void*) = nullptr;
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;
inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }
template<class T> void register_type(const char* name, unsigned index) {
    auto& info = Info<T>::value;
    info.name = name;
    info.index = index;
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* p) { delete static_cast<T*>(p); };
    registry()[std::type_index(typeid(T))] = &info;
}
template<class Derived, class Base> void add_base() {
    Info<Derived>::value.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* {
        return static_cast<Base*>(static_cast<Derived*>(ptr));
    }});
}

struct Director;
// The native side of a Lisp object; the Lisp object holds its address and frees it (finalizer or close).
struct Holder {
    static constexpr unsigned long long live = 0x526f736574746121ULL;
    unsigned long long magic = live;
    void* ptr = nullptr;
    TypeInfo* info = nullptr;
    bool owned = false;
    std::shared_ptr<void>* shared = nullptr;
    Director* director = nullptr; // a Lisp subclass instance implemented through a generated director
};
inline Holder* holder_of(const Node& n) {
    if (n.kind != tag_object || !n.holder) return nullptr;
    if (n.holder->magic != Holder::live) fail("invalid native object reference");
    return n.holder;
}
inline void* cast_path(void* ptr, const TypeInfo* from, const TypeInfo* to) {
    if (from == to) return ptr;
    for (const auto& link : from->bases)
        if (void* result = cast_path(link.upcast(ptr), link.base, to)) return result;
    return nullptr;
}
inline bool derives(const TypeInfo* from, const TypeInfo* to) {
    if (from == to) return true;
    for (const auto& link : from->bases) if (derives(link.base, to)) return true;
    return false;
}
template<class T> bool is_instance(const Node& n) {
    if (n.kind != tag_object || !n.holder || n.holder->magic != Holder::live) return false;
    return n.holder->info && derives(n.holder->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> T* native(const Node& n) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    if (n.kind == tag_object && !n.holder) fail("native object is closed");
    auto* holder = holder_of(n);
    if (!holder) fail(std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + type_name(n));
    if (!holder->ptr) fail("native object is closed");
    void* result = cast_path(holder->ptr, holder->info, info);
    if (!result) fail(std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + (holder->info && holder->info->name ? holder->info->name : "a native object"));
    return static_cast<U*>(result);
}

// Base of generated director classes: C++ subclasses forwarding virtual calls to Lisp methods.
// The Lisp object owns its director; once C++ takes ownership, C++ keeps the Lisp object alive.
struct Director {
    unsigned long long rosetta_id = 0;  // the Lisp object
    Holder* rosetta_holder = nullptr;   // its holder, while it exists
    bool rosetta_retained = false;      // C++ owns the object and keeps the Lisp object alive
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    virtual ~Director() {
        if (rosetta_holder) { rosetta_holder->ptr = nullptr; rosetta_holder->owned = false; rosetta_holder->director = nullptr; }
        if (rosetta_retained && Hooks::retain) Hooks::retain(rosetta_id, -1);
    }
    bool rosetta_has_self() const { return rosetta_id != 0 && Hooks::invoke; }
};
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(std::string("pure virtual method ") + name + " is not implemented by the Lisp subclass");
}
inline bool is_director(const Node& n) {
    auto* holder = holder_of(n);
    return holder && holder->director;
}

// Holders the Lisp garbage collector released (finalizers may run on any thread): destroyed on
// the next call, never while native code runs.
struct Pending { std::mutex lock; std::vector<Holder*> holders; };
inline Pending& pending() { static Pending value; return value; }
inline void destroy(Holder* holder) noexcept {
    if (!holder || holder->magic != Holder::live) return;
    if (holder->director) holder->director->rosetta_holder = nullptr;
    if (holder->shared) { delete holder->shared; holder->shared = nullptr; }
    else if (holder->owned && holder->ptr && holder->info && holder->info->destroy) holder->info->destroy(holder->ptr);
    holder->magic = 0;
    delete holder;
}
inline int& depth() { static thread_local int value = 0; return value; }
inline void drain() {
    std::vector<Holder*> holders;
    { std::lock_guard<std::mutex> guard(pending().lock); holders.swap(pending().holders); }
    for (auto* holder : holders) destroy(holder);
}

enum class Ownership { borrowed, owned };
inline Node object(void* ptr, TypeInfo* info, Ownership ownership, int parent) {
    Node n(tag_object);
    n.holder = new Holder{Holder::live, ptr, info, ownership == Ownership::owned, nullptr, nullptr};
    n.klass = info->index;
    n.integer = parent;
    return n;
}
inline Node lisp_object(const Director* director) { Node n(tag_lisp_object); n.id = director->rosetta_id; return n; }
template<class T> Node wrap(T* value, Ownership ownership, int parent) {
    using U = std::remove_cv_t<T>;
    if (!value) return Node();
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value); director && director->rosetta_holder) {
            if (ownership == Ownership::owned) {
                // C++ handed back ownership of a Lisp-implemented object.
                auto* self = const_cast<Director*>(director);
                if (self->rosetta_retained && self->rosetta_holder->ptr) {
                    self->rosetta_holder->owned = true;
                    self->rosetta_retained = false;
                    if (Hooks::retain) Hooks::retain(self->rosetta_id, -1);
                }
            }
            return lisp_object(director);
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    if (!info->name) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail("C++ result type is not exported to Lisp");
    }
    return object(ptr, info, ownership, parent);
}
template<class T> Node wrap_shared(std::shared_ptr<T> value) {
    if (!value) return Node();
    using U = std::remove_cv_t<T>;
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value.get());
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()); director && director->rosetta_holder) return lisp_object(director);
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value.get()));
        }
    }
    if (!info->name) fail("C++ result type is not exported to Lisp");
    Node n = object(ptr, info, Ownership::borrowed, -1);
    n.holder->shared = new std::shared_ptr<void>(std::const_pointer_cast<U>(value), ptr);
    return n;
}
template<class T> std::shared_ptr<T> shared(const Node& n) {
    if (n.nil()) return {};
    T* ptr = native<T>(n);
    auto* holder = holder_of(n);
    // A borrowed object (or a director, owned by its Lisp object) stays alive through its Lisp object.
    if ((holder->director || !holder->owned) && !holder->shared) {
        if (!n.id) fail("native object cannot be shared");
        return std::shared_ptr<T>(std::make_shared<LispRef>(n.id), ptr);
    }
    if (!holder->shared) {
        auto* info = holder->info;
        holder->shared = new std::shared_ptr<void>(holder->ptr, [info](void* p) { if (info->destroy) info->destroy(p); });
        holder->owned = false;
    }
    return std::shared_ptr<T>(*holder->shared, ptr);
}
template<class T> std::unique_ptr<T> take(const Node& n) {
    if (n.nil()) return {};
    T* ptr = native<T>(n);
    auto* holder = holder_of(n);
    if (holder->shared || !holder->owned) fail("cannot transfer ownership of a shared or borrowed native object");
    if (holder->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail("cannot transfer a derived object without a virtual destructor");
    holder->owned = false;
    if (holder->director) {
        // The C++ owner now keeps the Lisp implementation alive; the Lisp object stays usable.
        if (!holder->director->rosetta_retained && Hooks::retain) { Hooks::retain(holder->director->rosetta_id, 1); holder->director->rosetta_retained = true; }
    } else holder->ptr = nullptr;
    return std::unique_ptr<T>(ptr);
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- values
inline std::string type_name(const Node& n) {
    switch (n.kind) {
    case tag_nil: return "NIL";
    case tag_t: return "T";
    case tag_int: case tag_bigint: return "integer";
    case tag_real: return "float";
    case tag_complex: return "complex";
    case tag_string: return "string";
    case tag_symbol: return "symbol :" + n.text;
    case tag_bytes: return "octet vector";
    case tag_reals: return "double-float vector[" + std::to_string(n.reals.size()) + "]";
    case tag_list: return "sequence[" + std::to_string(n.items.size()) + "]";
    case tag_cons: return "cons";
    case tag_map: return "hash-table";
    case tag_function: return "function";
    case tag_ref: return "ref";
    case tag_object:
        if (!n.holder) return "closed native object";
        return n.holder->magic == Holder::live && n.holder->info && n.holder->info->name ? n.holder->info->name : "native object";
    default: return "value";
    }
}
template<class T, class = void> struct Value;

// Strict checks (the first overload pass) need the exact Lisp type: integers for C++ integers,
// floats for C++ floating point. Lenient checks also accept whole floats and integers.
template<class T> bool fits(const Node& n, bool strict) {
    if (n.kind == tag_int) {
        const long long v = n.integer;
        if constexpr (std::is_unsigned_v<T>) return v >= 0 && static_cast<unsigned long long>(v) <= std::numeric_limits<T>::max();
        else return v >= static_cast<long long>(std::numeric_limits<T>::lowest()) && v <= static_cast<long long>(std::numeric_limits<T>::max());
    }
    if (n.kind == tag_bigint) {
        if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(unsigned long long)) {
            if (n.text.empty() || n.text[0] == '-') return false;
            errno = 0;
            char* end = nullptr;
            std::strtoull(n.text.c_str(), &end, 10);
            return errno == 0 && end && *end == 0;
        } else return false;
    }
    if (strict || n.kind != tag_real) return false;
    if (!std::isfinite(n.real) || std::trunc(n.real) != n.real) return false;
    const long double wide = n.real;
    return wide >= static_cast<long double>(std::numeric_limits<T>::lowest()) && wide <= static_cast<long double>(std::numeric_limits<T>::max());
}
template<class T> Node number(T value) {
    Node n;
    if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(long long)) {
            if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) { n.kind = tag_bigint; n.text = std::to_string(value); return n; }
        }
        n.kind = tag_int; n.integer = static_cast<long long>(value);
    } else { n.kind = tag_real; n.real = static_cast<double>(value); }
    return n;
}
inline double real_of(const Node& n) {
    if (n.kind == tag_int) return static_cast<double>(n.integer);
    if (n.kind == tag_bigint) return std::strtod(n.text.c_str(), nullptr);
    return n.real;
}

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool check(const Node& n, bool) { return n.kind == tag_t || n.kind == tag_nil; }
    static bool load(const Node& n) {
        if (!check(n, false)) fail("expected T or NIL, got " + type_name(n));
        return n.kind == tag_t;
    }
    static Node make(bool value) { return boolean(value); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(const Node& n, bool strict) { return fits<T>(n, strict); }
    static T load(const Node& n) {
        if (n.kind != tag_int && n.kind != tag_bigint && n.kind != tag_real) fail("expected an integer, got " + type_name(n));
        if (!fits<T>(n, false)) {
            if (n.kind != tag_real) fail("integer outside C++ type range");
            fail("expected an integer, got a float with a fraction or outside C++ type range");
        }
        if (n.kind == tag_int) return static_cast<T>(n.integer);
        if (n.kind == tag_bigint) return static_cast<T>(std::strtoull(n.text.c_str(), nullptr, 10));
        return static_cast<T>(n.real);
    }
    static Node make(T value) { return number(value); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(const Node& n, bool strict) {
        return n.kind == tag_real || (!strict && (n.kind == tag_int || n.kind == tag_bigint));
    }
    static T load(const Node& n) {
        if (!check(n, false)) fail("expected a number, got " + type_name(n));
        const double v = real_of(n);
        if (std::isfinite(v) && std::abs(v) > static_cast<double>(std::numeric_limits<T>::max())) fail("number outside C++ type range");
        return static_cast<T>(v);
    }
    static Node make(T value) { return number(value); }
};
inline Node text_node(Tag kind, std::string value) { Node n(kind); n.text = std::move(value); return n; }
template<> struct Value<char> {
    static bool check(const Node& n, bool strict) {
        if (n.kind == tag_string) return n.text.size() == 1;
        return !strict && Value<int>::check(n, false);
    }
    static char load(const Node& n) {
        if (n.kind == tag_string) {
            if (n.text.size() != 1) fail("expected a single (ASCII) character");
            return n.text[0];
        }
        return static_cast<char>(Value<int>::load(n));
    }
    static Node make(char value) { return text_node(tag_string, std::string(1, value)); }
};
template<> struct Value<std::string> {
    static bool check(const Node& n, bool) { return n.kind == tag_string || n.kind == tag_bytes; }
    static std::string load(const Node& n) {
        if (!check(n, false)) fail("expected a string, got " + type_name(n));
        return n.text;
    }
    static Node make(const std::string& value) { return text_node(tag_string, value); }
};
template<> struct Value<std::string_view> {
    static bool check(const Node& n, bool strict) { return Value<std::string>::check(n, strict); }
    static Node make(std::string_view value) { return text_node(tag_string, std::string(value)); }
};
template<> struct Value<const char*> {
    static Node make(const char* value) { return value ? text_node(tag_string, value) : Node(); }
};
template<> struct Value<char*> {
    static Node make(const char* value) { return Value<const char*>::make(value); }
};
template<> struct Value<std::filesystem::path> {
    static bool check(const Node& n, bool) { return n.kind == tag_string; }
    static std::filesystem::path load(const Node& n) {
        const std::string value = Value<std::string>::load(n);
#if defined(__cpp_char8_t)
        return std::filesystem::path(std::u8string(value.begin(), value.end()));
#else
        return std::filesystem::u8path(value);
#endif
    }
    static Node make(const std::filesystem::path& value) {
        const auto utf8 = value.u8string();
        return text_node(tag_string, std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
    }
};
template<> struct Value<std::nullptr_t> {
    static bool check(const Node& n, bool) { return n.nil(); }
    static std::nullptr_t load(const Node&) { return nullptr; }
    static Node make(std::nullptr_t) { return Node(); }
};
template<> struct Value<std::monostate> {
    static bool check(const Node& n, bool) { return n.nil(); }
    static std::monostate load(const Node&) { return {}; }
    static Node make(std::monostate) { return Node(); }
};
// Complex numbers are Lisp complexes (reals are accepted on input).
template<class T> struct Value<std::complex<T>> {
    static bool check(const Node& n, bool strict) {
        return n.kind == tag_complex || (!strict && (n.kind == tag_real || n.kind == tag_int));
    }
    static std::complex<T> load(const Node& n) {
        if (n.kind == tag_complex) return {static_cast<T>(n.real), static_cast<T>(n.imag)};
        if (n.kind == tag_real || n.kind == tag_int) return {static_cast<T>(real_of(n)), T{}};
        fail("expected a number, got " + type_name(n));
    }
    static Node make(const std::complex<T>& value) {
        Node n(tag_complex);
        n.real = static_cast<double>(value.real());
        n.imag = static_cast<double>(value.imag());
        return n;
    }
};

// Enumerations cross as keywords (:two-sided); integers and strings are accepted on input.
template<class E> struct EnumTable { static inline std::vector<std::pair<std::string, long long>> entries; };
// Lisp spelling of a name for comparison: lower case, '_' as '-'.
inline std::string folded(std::string text) {
    for (auto& c : text) c = c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    static const std::pair<std::string, long long>* named(const std::string& name) {
        const auto key = folded(name);
        for (const auto& entry : EnumTable<T>::entries) if (entry.first == key) return &entry;
        return nullptr;
    }
    static bool check(const Node& n, bool strict) {
        if (n.kind == tag_symbol || n.kind == tag_string) return !strict || named(n.text);
        if (!Value<U>::check(n, false)) return false;
        if (!strict || EnumTable<T>::entries.empty()) return true;
        const auto v = static_cast<long long>(Value<U>::load(n));
        for (const auto& entry : EnumTable<T>::entries) if (entry.second == v) return true;
        return false;
    }
    static T load(const Node& n) {
        if (n.kind == tag_symbol || n.kind == tag_string) {
            if (const auto* entry = named(n.text)) return static_cast<T>(entry->second);
            std::string names;
            for (const auto& entry : EnumTable<T>::entries) names += (names.empty() ? ":" : ", :") + entry.first;
            fail("unknown enumerator " + n.text + " (expected one of " + names + ")");
        }
        return static_cast<T>(Value<U>::load(n));
    }
    static Node make(T value) {
        const auto v = static_cast<long long>(static_cast<U>(value));
        for (const auto& entry : EnumTable<T>::entries) if (entry.second == v) return text_node(tag_symbol, entry.first);
        return number(static_cast<U>(value));
    }
};

// Exported classes by value: copies in, owned copies out.
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(const Node& n, bool) { return is_instance<T>(n); }
    static T load(const Node& n) {
        if constexpr (copyable_v<T>) return *native<T>(n);
        else { (void)n; fail("C++ class is not copyable"); }
    }
    static Node make(T&& value) {
        if constexpr (std::is_move_constructible_v<T>) return wrap(new T(std::move(value)), Ownership::owned, -1);
        else { (void)value; fail("C++ class is not movable"); }
    }
    static Node make(const T& value) {
        if constexpr (copyable_v<T>) return wrap(new T(value), Ownership::owned, -1);
        else { (void)value; fail("C++ class is not copyable"); }
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(const Node& n, bool) { return n.nil() || is_instance<T>(n); }
    static T* load(const Node& n) { return n.nil() ? nullptr : native<T>(n); }
    static Node make(T* value) { return wrap(value, Ownership::borrowed, -1); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(const Node& n, bool) { return n.nil() || is_instance<T>(n); }
    static std::shared_ptr<T> load(const Node& n) { return shared<T>(n); }
    static Node make(const std::shared_ptr<T>& value) { return wrap_shared(value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(const Node& n, bool) { return n.nil() || is_instance<T>(n); }
    static std::unique_ptr<T, D> load(const Node& n) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(n);
        else fail("unique_ptr with a custom deleter cannot take ownership");
    }
    static Node make(std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return wrap(value.release(), Ownership::owned, -1);
        else fail("unique_ptr with a custom deleter cannot be returned");
    }
};
template<class T> struct Value<std::optional<T>> {
    static bool check(const Node& n, bool strict) { return n.nil() || Value<T>::check(n, strict); }
    static std::optional<T> load(const Node& n) {
        if (n.nil()) return std::nullopt;
        return Value<T>::load(n);
    }
    static Node make(const std::optional<T>& value) { return value ? Value<T>::make(*value) : Node(); }
    static Node make(std::optional<T>&& value) { return value ? Value<T>::make(std::move(*value)) : Node(); }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static Node make(std::reference_wrapper<T> value) {
        using U = std::remove_cv_t<T>;
        if constexpr (bound_v<U>) return wrap(const_cast<U*>(&value.get()), Ownership::borrowed, -1);
        else return Value<U>::make(value.get());
    }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- sequences and maps
// Sequences are Lisp lists or vectors in; vectors out (double-float vectors for floating point).
// NIL is an empty container when checks are lenient.
inline bool sequence(const Node& n) { return n.kind == tag_list || n.kind == tag_reals || n.kind == tag_bytes; }
inline std::size_t length(const Node& n) {
    return n.kind == tag_reals ? n.reals.size() : n.kind == tag_bytes ? n.text.size() : n.items.size();
}
// Element i of a sequence node (reals and bytes are expanded on demand).
inline Node element(const Node& n, std::size_t i) {
    if (n.kind == tag_reals) { Node e(tag_real); e.real = n.reals[i]; return e; }
    if (n.kind == tag_bytes) { Node e(tag_int); e.integer = static_cast<unsigned char>(n.text[i]); return e; }
    return n.items[i];
}
template<class E> bool check_elements(const Node& n, bool strict) {
    if (n.nil()) return !strict;
    if (!sequence(n)) return false;
    if (n.kind == tag_reals) {
        if constexpr (std::is_floating_point_v<E>) return true;
        else if (!n.reals.empty()) return Value<E>::check(element(n, 0), strict) && [&] {
            for (std::size_t i = 1; i < n.reals.size(); ++i) if (!Value<E>::check(element(n, i), strict)) return false;
            return true;
        }();
        return true;
    }
    if (n.kind == tag_bytes) {
        if constexpr (std::is_integral_v<E> && !std::is_same_v<E, bool>) return true;
        else return n.text.empty();
    }
    for (const auto& item : n.items) if (!Value<E>::check(item, strict)) return false;
    return true;
}
template<class E> E load_element(const Node& n, std::size_t i) {
    if (n.kind == tag_reals) {
        if constexpr (std::is_floating_point_v<E>) return static_cast<E>(n.reals[i]);
    }
    if (n.kind == tag_list) return Value<E>::load(n.items[i]);
    return Value<E>::load(element(n, i));
}
template<class E, class Range> Node make_elements(const Range& range, std::size_t size) {
    if constexpr (std::is_floating_point_v<E>) {
        Node n(tag_reals);
        n.reals.reserve(size);
        for (const auto& item : range) n.reals.push_back(static_cast<double>(item));
        return n;
    } else {
        Node n(tag_list);
        n.items.reserve(size);
        for (const auto& item : range) n.items.push_back(Value<E>::make(item));
        return n;
    }
}
template<class C, class E> struct Sequence {
    static bool check(const Node& n, bool strict) { return check_elements<E>(n, strict); }
    static C load(const Node& n) {
        C result;
        if (n.nil()) return result;
        if (!sequence(n)) fail("expected a list or vector, got " + type_name(n));
        const auto count = length(n);
        if constexpr (std::is_same_v<C, std::vector<E>>) result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(load_element<E>(n, i));
            else result.push_back(load_element<E>(n, i));
        }
        return result;
    }
    static Node make(const C& value) { return make_elements<E>(value, value.size()); }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
template<class A> struct Value<std::vector<bool, A>> {
    static bool check(const Node& n, bool strict) { return check_elements<bool>(n, strict); }
    static std::vector<bool, A> load(const Node& n) {
        std::vector<bool, A> result;
        if (n.nil()) return result;
        if (!sequence(n)) fail("expected a list or vector, got " + type_name(n));
        for (std::size_t i = 0; i < length(n); ++i) result.push_back(load_element<bool>(n, i));
        return result;
    }
    static Node make(const std::vector<bool, A>& value) {
        Node n(tag_list);
        for (const bool item : value) n.items.push_back(boolean(item));
        return n;
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
template<class E> struct Value<std::set<E>> : Sequence<std::set<E>, E> {};
template<class E> struct Value<std::unordered_set<E>> : Sequence<std::unordered_set<E>, E> {};
template<class E> struct Value<std::valarray<E>> {
    static bool check(const Node& n, bool strict) { return check_elements<E>(n, strict); }
    static std::valarray<E> load(const Node& n) {
        const auto items = Value<std::vector<E>>::load(n);
        return std::valarray<E>(items.data(), items.size());
    }
    static Node make(const std::valarray<E>& value) { return make_elements<E>(std::vector<E>(std::begin(value), std::end(value)), value.size()); }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(const Node& n, bool strict) {
        if (!sequence(n) || length(n) != N) return N == 0 && n.nil();
        return check_elements<E>(n, strict);
    }
    static std::array<E, N> load(const Node& n) {
        if (!(sequence(n) && length(n) == N) && !(N == 0 && n.nil())) fail("expected a sequence of " + std::to_string(N) + " elements");
        std::array<E, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = load_element<E>(n, i);
        return result;
    }
    static Node make(const std::array<E, N>& value) { return make_elements<E>(value, N); }
};
// Maps are hash tables (EQUAL) out; hash tables and association lists in.
template<class M, class K, class V> struct Mapping {
    template<class F> static bool entries(const Node& n, F&& each) {
        if (n.kind == tag_map) {
            for (std::size_t i = 0; i + 1 < n.items.size(); i += 2) if (!each(n.items[i], n.items[i + 1])) return false;
            return true;
        }
        if (n.kind != tag_list) return false;
        for (const auto& item : n.items) {
            if (item.kind == tag_cons) { if (!each(item.items[0], item.items[1])) return false; }
            else return false;
        }
        return true;
    }
    static bool check(const Node& n, bool strict) {
        if (n.nil()) return !strict;
        return entries(n, [&](const Node& k, const Node& v) { return Value<K>::check(k, false) && Value<V>::check(v, false); });
    }
    static M load(const Node& n) {
        M result;
        if (n.nil()) return result;
        const bool ok = entries(n, [&](const Node& k, const Node& v) { result.emplace(Value<K>::load(k), Value<V>::load(v)); return true; });
        if (!ok) fail("expected a hash table or an association list, got " + type_name(n));
        return result;
    }
    static Node make(const M& value) {
        Node n(tag_map);
        n.items.reserve(value.size() * 2);
        for (const auto& item : value) { n.items.push_back(Value<K>::make(item.first)); n.items.push_back(Value<V>::make(item.second)); }
        return n;
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};

// Pairs and tuples are lists ((a b)); pairs also accept conses ((a . b)).
template<class... T> struct Product {
    static constexpr std::size_t N = sizeof...(T);
    static const std::vector<Node>* parts(const Node& n) {
        if (n.kind == tag_cons && N == 2) return &n.items;
        if (n.kind == tag_list && n.items.size() == N) return &n.items;
        return nullptr;
    }
    template<std::size_t... I> static bool check_each(const Node& n, bool strict, std::index_sequence<I...>) {
        const auto* items = parts(n);
        if (!items) return false;
        return (Value<T>::check((*items)[I], strict) && ...);
    }
    template<class Tuple, std::size_t... I> static Tuple load_each(const Node& n, std::index_sequence<I...>) {
        const auto* items = parts(n);
        if (!items) fail("expected a list of " + std::to_string(N) + " elements, got " + type_name(n));
        return Tuple(Value<T>::load((*items)[I])...);
    }
    template<class Tuple, std::size_t... I> static Node make_each(const Tuple& value, std::index_sequence<I...>) {
        Node n(tag_tuple);
        (n.items.push_back(Value<T>::make(std::get<I>(value))), ...);
        return n;
    }
};
template<class A, class B> struct Value<std::pair<A, B>> {
    using P = Product<A, B>;
    static bool check(const Node& n, bool strict) { return P::check_each(n, strict, std::index_sequence_for<A, B>{}); }
    static std::pair<A, B> load(const Node& n) { return P::template load_each<std::pair<A, B>>(n, std::index_sequence_for<A, B>{}); }
    static Node make(const std::pair<A, B>& value) { return P::make_each(value, std::index_sequence_for<A, B>{}); }
};
template<class... T> struct Value<std::tuple<T...>> {
    using P = Product<T...>;
    static bool check(const Node& n, bool strict) { return P::check_each(n, strict, std::index_sequence_for<T...>{}); }
    static std::tuple<T...> load(const Node& n) { return P::template load_each<std::tuple<T...>>(n, std::index_sequence_for<T...>{}); }
    static Node make(const std::tuple<T...>& value) { return P::make_each(value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(const Node& n, bool strict) { return (Value<T>::check(n, strict) || ...); }
    static std::variant<T...> load(const Node& n) {
        std::optional<std::variant<T...>> result;
        for (const bool strict : {true, false}) {
            ((!result && Value<T>::check(n, strict) ? (void)result.emplace(std::in_place_type<T>, Value<T>::load(n)) : (void)0), ...);
            if (result) return std::move(*result);
        }
        fail("value matches no alternative of the C++ variant");
    }
    static Node make(const std::variant<T...>& value) {
        return std::visit([](const auto& item) { return Value<std::remove_cv_t<std::remove_reference_t<decltype(item)>>>::make(item); }, value);
    }
};
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(const Node& n, bool) { return n.kind == tag_real || n.kind == tag_int; }
    static D load(const Node& n) { return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(n))); }
    static Node make(const D& value) { return number(std::chrono::duration<double>(value).count()); }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arguments
// A (ref value) cell: a mutable reference parameter reads its value and stores the update back.
template<class V, class = void> struct makeable : std::false_type {};
template<class V> struct makeable<V, std::void_t<decltype(Value<V>::make(std::declval<const V&>()))>> : std::true_type {};
inline const Node& source(const Node& n, bool out) { return out && n.kind == tag_ref ? n.items.at(0) : n; }
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static constexpr bool out = std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>>;
    static bool check(const Node& n, bool strict) { return Value<V>::check(source(n, out), strict); }
    explicit Param(const Node& n) : value_(Value<V>::load(source(n, out))) {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    // A mutable reference parameter passed a ref cell stores the updated value back in it.
    void writeback(Call& call, const Node& n) {
        if constexpr (out && makeable<V>::value) {
            if (n.kind != tag_ref) return;
            call.writebacks.emplace_back(static_cast<unsigned>(n.integer), Value<V>::make(value_));
        } else { (void)call; (void)n; }
    }
private:
    V value_;
};
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(const Node& n, bool) { return is_instance<V>(n); }
    explicit Param(const Node& n) : ptr_(native<V>(n)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
    void writeback(Call&, const Node&) {}
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(const Node& n, bool) { return n.nil() || is_instance<V>(n); }
    explicit Param(const Node& n) : ptr_(n.nil() ? nullptr : native<V>(n)) {}
    P get() { return ptr_; }
    void writeback(Call&, const Node&) {}
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(const Node& n, bool strict) { return n.nil() || Value<std::string>::check(n, strict); }
    explicit Param(const Node& n) { if (!n.nil()) text_ = Value<std::string>::load(n); }
    const char* get() { return text_ ? text_->c_str() : nullptr; }
    void writeback(Call&, const Node&) {}
private:
    std::optional<std::string> text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(const Node& n, bool strict) { return Value<std::string>::check(n, strict); }
    explicit Param(const Node& n) : text_(Value<std::string>::load(n)) {}
    std::string_view get() { return text_; }
    void writeback(Call&, const Node&) {}
private:
    std::string text_;
};

[[noreturn]] inline void no_overload(const Call& call, const char* name, const char* signatures, std::size_t first) {
    std::string received;
    for (std::size_t i = first; i < call.args.size(); ++i) received += std::string(i > first ? ", " : "") + type_name(call.args[i]);
    fail(std::string(name) + ": incompatible arguments (" + received + "); supported signatures:\n" + signatures);
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
inline bool same_object(const Call& call, int parent, void* ptr, TypeInfo* info) {
    if (parent < 0 || static_cast<std::size_t>(parent) >= call.args.size()) return false;
    const auto& n = call.args[static_cast<std::size_t>(parent)];
    if (n.kind != tag_object || !n.holder || n.holder->magic != Holder::live || !n.holder->ptr) return false;
    return cast_path(n.holder->ptr, n.holder->info, info) == ptr;
}
inline Node argument_node(int index) { Node n(tag_argument); n.integer = index; return n; }
// Converts a C++ result; parent (an argument index, or -1) is the object a borrowed result lives in.
template<class R> Node result(const Call& call, R&& value, int parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) return Node();
        if (same_object(call, parent, ptr, &Info<B>::value)) return argument_node(parent);
        if (policy == Return::owned) return wrap(ptr, Ownership::owned, -1);
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) return wrap(new B(*ptr), Ownership::owned, -1);
            else fail("C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (same_object(call, parent, ptr, &Info<V>::value)) return argument_node(parent);
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) return wrap(new V(value), Ownership::owned, -1);
            else fail("C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V>) {
        return Value<V>::make(std::move(value));
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::make(value);
    } else if constexpr (std::is_lvalue_reference_v<R>) {
        return Value<V>::make(static_cast<const V&>(value));
    } else {
        return Value<V>::make(std::move(value));
    }
}
template<class F> void call(Call& c, F&& function, int parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); c.result = Node(); }
    else c.result = result<R>(c, function(), parent, policy);
}
template<class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail("field type is not assignable");
}

// ---------------------------------------------------------------- C++ -> Lisp calls
template<class A> Node argument(A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) return wrap(const_cast<V*>(&value), Ownership::borrowed, -1);
        else return Value<V>::make(std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        return wrap(const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed, -1);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::make(value);
    } else {
        return Value<V>::make(value);
    }
}
// Holders of borrowed arguments made for a callback are freed by the Lisp objects wrapping them.
template<class R> R returned(const Node& n) {
    if constexpr (std::is_void_v<R>) { (void)n; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_lvalue_reference_v<R> && bound_v<V>) return *native<V>(n);
        else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(n);
        else {
            if (!Value<V>::check(n, false)) fail("Lisp callback returned " + type_name(n) + ", which does not convert to the C++ result type");
            return Value<V>::load(n);
        }
    }
}
// Calls into Lisp (a function, or a generic function on a director's Lisp object); Lisp errors
// become C++ exceptions.
template<class R> R invoke(int kind, unsigned long long id, unsigned generic, std::vector<Node>&& arguments) {
    if (!Hooks::invoke) fail("the Lisp side of the module is not initialized");
    Writer writer;
    writer.raw<unsigned>(static_cast<unsigned>(arguments.size()));
    for (const auto& item : arguments) writer.node(item);
    arguments.clear();
    unsigned char* out = nullptr;
    std::size_t size = 0;
    flush_output();
    const int status = Hooks::invoke(kind, id, generic, reinterpret_cast<const unsigned char*>(writer.out.data()), writer.out.size(), &out, &size);
    struct Free { unsigned char* p; ~Free() { std::free(p); } } free_out{out};
    Reader reader{out, out + size};
    if (status != 0) {
        std::string message = "error in Lisp callback";
        if (out && size) { const Node n = reader.node(); if (n.kind == tag_string) message = n.text; }
        if (status == 2) throw LispFailure(message);
        fail(message);
    }
    if constexpr (std::is_void_v<R>) return;
    else {
        if (!out) fail("Lisp callback returned no value");
        const Node n = reader.node();
        return returned<R>(n);
    }
}
template<class R, class... A> R invoke_lisp(const LispRef& function, A&&... arguments) {
    std::vector<Node> nodes;
    nodes.reserve(sizeof...(A));
    (nodes.push_back(argument<A>(std::forward<A>(arguments))), ...);
    return invoke<R>(0, function.id, 0, std::move(nodes));
}
// A director's override: generic function number `generic` on the Lisp object.
template<class R, class... A> R invoke_override(const Director& self, unsigned generic, A&&... arguments) {
    std::vector<Node> nodes;
    nodes.reserve(sizeof...(A) + 1);
    Node object(tag_lisp_object);
    object.id = self.rosetta_id;
    nodes.push_back(object);
    (nodes.push_back(argument<A>(std::forward<A>(arguments))), ...);
    return invoke<R>(1, self.rosetta_id, generic, std::move(nodes));
}
// A native std::function returned to Lisp: Lisp wraps it in a closure.
struct Callable { std::function<void(Call&)> call; };
inline TypeInfo& callable_info() { static TypeInfo info{"native-function", 0, {}, [](void* p) { delete static_cast<Callable*>(p); }}; return info; }
template<class R, class... A, std::size_t... I>
void invoke_native(Call& c, const std::function<R(A...)>& function, std::index_sequence<I...>) {
    if (c.args.size() - 1 != sizeof...(A)) fail("wrong number of arguments for native function: expected " + std::to_string(sizeof...(A)));
    std::tuple<Param<A>...> params{Param<A>(c.args[I + 1])...};
    call(c, [&]() -> decltype(auto) { return function(std::get<I>(params).get()...); }, -1, Return::copy);
}
inline void call_native(Call& c) {
    if (c.args.empty() || !holder_of(c.args[0]) || c.args[0].holder->info != &callable_info() || !c.args[0].holder->ptr)
        fail("native function is no longer available");
    static_cast<Callable*>(c.args[0].holder->ptr)->call(c);
}
template<class R, class... A> struct Value<std::function<R(A...)>> {
    using F = std::function<R(A...)>;
    static bool check(const Node& n, bool) { return n.nil() || n.kind == tag_function; }
    static F load(const Node& n) {
        if (n.nil()) return {};
        if (n.kind != tag_function) fail("expected a function, got " + type_name(n));
        auto target = std::make_shared<LispRef>(n.id);
        return [target](A... arguments) -> R { return invoke_lisp<R, A...>(*target, std::forward<A>(arguments)...); };
    }
    static Node make(const F& value) {
        if (!value) return Node();
        auto* callable = new Callable{[value](Call& c) { invoke_native(c, value, std::index_sequence_for<A...>{}); }};
        Node n(tag_function);
        n.holder = new Holder{Holder::live, callable, &callable_info(), true, nullptr, nullptr};
        return n;
    }
};

// ---------------------------------------------------------------- the entry point
using Function = void (*)(Call&);
inline const char* error_kind(const std::exception& error) {
    if (dynamic_cast<const Failure*>(&error)) return "binding";
    if (dynamic_cast<const std::invalid_argument*>(&error)) return "invalid-argument";
    if (dynamic_cast<const std::out_of_range*>(&error)) return "out-of-range";
    if (dynamic_cast<const std::domain_error*>(&error)) return "domain-error";
    if (dynamic_cast<const std::length_error*>(&error)) return "length-error";
    if (dynamic_cast<const std::overflow_error*>(&error) || dynamic_cast<const std::underflow_error*>(&error) ||
        dynamic_cast<const std::range_error*>(&error)) return "arithmetic-error";
    if (dynamic_cast<const std::bad_alloc*>(&error)) return "allocation";
    return "native";
}
// Encodes the outcome into a malloc'd buffer: status 0 (value, writebacks), 1 (kind, message)
// or 2 (a Lisp error from a callback: Lisp signals its condition again).
inline int run(const Function* table, std::size_t size, unsigned index, const unsigned char* in, std::size_t length, unsigned char** out, std::size_t* out_size) {
    Writer writer;
    int status = 0;
    const char* outer_label = converting();
    converting() = nullptr;
    if (depth()++ == 0) drain();
    try {
        if (index >= size || !table[index]) fail("unknown native function");
        Call c;
        Reader reader{in, in + length};
        const auto count = reader.raw<unsigned>();
        c.args.reserve(count);
        for (unsigned i = 0; i < count; ++i) c.args.push_back(reader.node());
        table[index](c);
        flush_output();
        writer.node(c.result);
        writer.raw<unsigned>(static_cast<unsigned>(c.writebacks.size()));
        for (const auto& [slot, value] : c.writebacks) { writer.raw<unsigned>(slot); writer.node(value); }
    } catch (const LispFailure& error) {
        status = 2;
        writer.out.clear();
        writer.node(text_node(tag_symbol, "lisp"));
        writer.node(text_node(tag_string, error.what()));
    } catch (const std::exception& error) {
        status = 1;
        writer.out.clear();
        const std::string text = converting() ? std::string(converting()) + ": " + error.what() : std::string(error.what());
        writer.node(text_node(tag_symbol, error_kind(error)));
        writer.node(text_node(tag_string, text));
    } catch (...) {
        status = 1;
        writer.out.clear();
        writer.node(text_node(tag_symbol, "unknown"));
        writer.node(text_node(tag_string, "unknown C++ exception"));
    }
    --depth();
    converting() = outer_label;
    *out_size = writer.out.size();
    *out = static_cast<unsigned char*>(std::malloc(writer.out.size() ? writer.out.size() : 1));
    if (!*out) { *out_size = 0; return 3; }
    std::memcpy(*out, writer.out.data(), writer.out.size());
    return status;
}
// Frees a holder now (close), or later (garbage collection on any thread).
inline void release(Holder* holder, int now) {
    if (!holder) return;
    if (now) { destroy(holder); return; }
    std::lock_guard<std::mutex> guard(pending().lock);
    pending().holders.push_back(holder);
}
} } // ROSETTA_LISP_MODULE::rosetta_lisp
)cpp"};
} // namespace rosetta::detail
