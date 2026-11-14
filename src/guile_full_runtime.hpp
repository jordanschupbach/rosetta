#pragma once

// Support code emitted into every generated Guile extension (--guile-api full), plus the Scheme
// runtime at the top of the generated module file. The C++ part is split into several literals
// only to stay below compiler limits for one string literal. Everything is inline: a split
// extension repeats it in each source, and the per-module namespace ROSETTA_GG_MODULE keeps two
// extensions in one process apart.
//
// Guile raises by a non-local exit (longjmp) that would skip C++ destructors. Every libguile call
// that may raise or run Scheme code goes through protect(), which catches the Scheme exception
// and turns it into the C++ exception scheme_error. guard() is the outermost frame of every
// native procedure: it catches C++ exceptions once all C++ objects of the call are destroyed and
// only then raises in Scheme (the original Scheme exception object, or a throw to a key).
//
// Objects: every wrapped C++ object is a GOOPS instance of a subclass of <native-object>, whose
// first two slots hold a foreign-object handle (the C++ pointer and its ownership) and the Scheme
// object a borrowed result keeps alive. The handle's finalizer only queues the C++ object; queued
// objects are destroyed at the start of the next native call, on a thread that runs Scheme code,
// so C++ destructors never race the library on Guile's finalizer thread.
namespace rosetta::detail {
inline constexpr const char* guile_full_runtime_parts[] = {
R"cpp(
namespace ROSETTA_GG_MODULE { namespace rosetta_gg {
// ---------------------------------------------------------------- pinning
// A Scheme object kept alive by C++ (callbacks, exceptions in flight, director objects C++ owns).
// scm_gc_protect_object counts, so several pins of one object nest.
class Pinned {
public:
    explicit Pinned(SCM value) : value_(value) { scm_gc_protect_object(value_); }
    Pinned(const Pinned&) = delete;
    Pinned& operator=(const Pinned&) = delete;
    ~Pinned() { scm_gc_unprotect_object(value_); }
    SCM get() const noexcept { return value_; }
private:
    SCM value_;
};
class SharedRef {
public:
    SharedRef() = default;
    explicit SharedRef(SCM value) : pinned_(std::make_shared<Pinned>(value)) {}
    SCM get() const noexcept { return pinned_ ? pinned_->get() : SCM_BOOL_F; }
private:
    std::shared_ptr<Pinned> pinned_;
};
inline SCM permanent(SCM value) { scm_gc_protect_object(value); return value; }

// ---------------------------------------------------------------- errors
// Error kinds: the first three are Guile's own keys, the others the module's keys
// (PREFIX-invalid-argument, ...), which carry the C++ exception class.
enum class Kind { wrong_type, wrong_args, out_of_range, invalid_argument, index, range, type, io, not_implemented, runtime, memory, key,
                  zero_division, count_ };
inline const char* kind_suffix(Kind kind) {
    switch (kind) {
    case Kind::wrong_type: return "wrong-type-arg";
    case Kind::wrong_args: return "wrong-number-of-args";
    case Kind::out_of_range: return "out-of-range";
    case Kind::invalid_argument: return "-invalid-argument";
    case Kind::index: return "-out-of-range";
    case Kind::range: return "-range-error";
    case Kind::type: return "-type-error";
    case Kind::io: return "-io-error";
    case Kind::not_implemented: return "-not-implemented";
    case Kind::memory: return "-memory-error";
    case Kind::key: return "-key-error";
    case Kind::zero_division: return "-zero-division";
    default: return "-runtime-error";
    }
}
struct Runtime {
    SCM module = SCM_BOOL_F, root = SCM_BOOL_F, handle_type = SCM_BOOL_F, callable_type = SCM_BOOL_F;
    SCM allocate_instance = SCM_BOOL_F, raise_exception = SCM_BOOL_F, make_callable = SCM_BOOL_F, cons = SCM_BOOL_F;
    SCM default_keyword = SCM_BOOL_F, exception_key = SCM_BOOL_F, error_keys = SCM_EOL;
    SCM keys[static_cast<int>(Kind::count_)] = {};
    std::string prefix;
};
inline Runtime& runtime() { static Runtime value; return value; }
inline SCM key_of(Kind kind) { return runtime().keys[static_cast<int>(kind)]; }

// A C++-side error raised at the Scheme boundary with this kind and message.
struct native_error { Kind kind; std::string message; std::string cpp_class; };
[[noreturn]] inline void fail(Kind kind, const std::string& message) { throw native_error{kind, message, {}}; }
// A Scheme exception (or throw) raised by Scheme code that C++ called.
struct scheme_error { std::shared_ptr<Pinned> key, args; };

struct Caught { SCM key = SCM_BOOL_F; SCM args = SCM_EOL; bool thrown = false; };
template<class F> SCM catch_body(void* data) { return (*static_cast<F*>(data))(); }
inline SCM catch_handler(void* data, SCM key, SCM args) {
    auto* caught = static_cast<Caught*>(data);
    caught->key = key;
    caught->args = args;
    caught->thrown = true;
    return SCM_UNSPECIFIED;
}
// Runs fn (libguile calls only: it must not throw C++ exceptions) and converts a Scheme exception
// into scheme_error.
template<class F> SCM protect(F fn) {
    Caught caught;
    const SCM result = scm_internal_catch(SCM_BOOL_T, &catch_body<F>, &fn, &catch_handler, &caught);
    if (caught.thrown) throw scheme_error{std::make_shared<Pinned>(caught.key), std::make_shared<Pinned>(caught.args)};
    return result;
}

// Strict UTF-8: no overlong forms, surrogates or values beyond U+10FFFF.
inline bool valid_utf8(const char* data, std::size_t size) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size;) {
        const unsigned c = bytes[i];
        std::size_t extra = 0;
        unsigned point = 0, minimum = 0;
        if (c < 0x80) { ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF) { extra = 1; point = c & 0x1F; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; point = c & 0x0F; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; point = c & 0x07; minimum = 0x10000; }
        else return false;
        if (size - i <= extra) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned next = bytes[i + k];
            if ((next & 0xC0) != 0x80) return false;
            point = (point << 6) | (next & 0x3F);
        }
        if (point < minimum || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}
// Text for messages: UTF-8 when valid, otherwise Latin-1 (never raises).
inline SCM message_string(const std::string& text) {
    return valid_utf8(text.data(), text.size()) ? scm_from_utf8_stringn(text.data(), text.size())
                                                : scm_from_latin1_stringn(text.data(), text.size());
}
// (SUBR MESSAGE IRRITANTS DATA): Guile's error format, so the exception is an &error with
// &origin and &message, and DATA is (C++-CLASS) or #f.
inline SCM error_args(const char* subr, const std::string& message, const std::string& cpp_class) {
    std::string text;
    for (const char c : message) { text += c; if (c == '~') text += '~'; }
    return scm_list_4(subr ? message_string(subr) : SCM_BOOL_F, message_string(text), SCM_EOL,
                      cpp_class.empty() ? SCM_BOOL_F : scm_list_1(message_string(cpp_class)));
}
[[noreturn]] inline void raise(SCM key, SCM args) {
    // An exception object raised by raise-exception comes back as itself.
    if (scm_is_eq(key, runtime().exception_key) && scm_is_pair(args)) scm_call_1(runtime().raise_exception, SCM_CAR(args));
    scm_throw(key, args);
    std::abort();
}

inline std::string demangle(const char* name) {
    int status = 0;
    char* text = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = status == 0 && text ? text : name;
    std::free(text);
    for (const char* inline_namespace : {"__cxx11::", "__1::"})
        for (std::size_t i = 0; (i = result.find(inline_namespace, i)) != std::string::npos;) result.erase(i, std::string(inline_namespace).size());
    return result;
}
// Translates the in-flight C++ exception (inside a catch block).
inline void translate_current(const char* subr, SCM* key, SCM* args) noexcept {
    Kind kind = Kind::runtime;
    std::string message, cpp_class;
    try { throw; }
    catch (const std::bad_alloc& e) { kind = Kind::memory; message = "native allocation failed"; cpp_class = demangle(typeid(e).name()); }
    catch (const std::invalid_argument& e) { kind = Kind::invalid_argument; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::domain_error& e) { kind = Kind::invalid_argument; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::length_error& e) { kind = Kind::invalid_argument; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::out_of_range& e) { kind = Kind::index; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::overflow_error& e) { kind = Kind::range; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::underflow_error& e) { kind = Kind::range; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::range_error& e) { kind = Kind::range; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::filesystem::filesystem_error& e) { kind = Kind::io; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::ios_base::failure& e) { kind = Kind::io; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::bad_cast& e) { kind = Kind::type; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::bad_optional_access& e) { kind = Kind::type; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::bad_variant_access& e) { kind = Kind::type; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (const std::bad_function_call& e) { kind = Kind::runtime; message = "empty native procedure"; cpp_class = demangle(typeid(e).name()); }
    catch (const std::exception& e) { kind = Kind::runtime; message = e.what(); cpp_class = demangle(typeid(e).name()); }
    catch (...) { kind = Kind::runtime; message = "unknown C++ exception"; }
    *key = key_of(kind);
    *args = error_args(subr, message, cpp_class);
}

// ---------------------------------------------------------------- deferred destruction
struct Pending {
    std::mutex mutex;
    std::vector<std::pair<void*, void (*)(void*)>> items;
};
inline Pending& pending() { static auto* value = new Pending; return *value; }
inline void defer(void* pointer, void (*destroy)(void*)) {
    std::lock_guard<std::mutex> lock(pending().mutex);
    pending().items.emplace_back(pointer, destroy);
}
inline void drain() noexcept {
    for (;;) {
        std::vector<std::pair<void*, void (*)(void*)>> items;
        {
            std::lock_guard<std::mutex> lock(pending().mutex);
            items.swap(pending().items);
        }
        if (items.empty()) return;
        for (const auto& [pointer, destroy] : items) destroy(pointer);
    }
}

// Scheme and C++ buffer standard output separately: guard() flushes Scheme's current output port
// before C++ runs and C++'s stdout after, so their output stays in order.
inline void flush_scheme_output() {
    protect([]() { return scm_force_output(scm_current_output_port()); });
}
inline void flush_native_output() noexcept {
    std::cout.flush();
    std::fflush(stdout);
}
// The outermost frame of every native procedure: C++ exceptions become Scheme exceptions once
// every C++ object of the call is destroyed.
template<class F> SCM guard(const char* subr, F&& fn) {
    SCM key = SCM_BOOL_F, args = SCM_EOL;
    try {
        drain();
        flush_scheme_output();
        const SCM result = fn();
        flush_native_output();
        return result;
    }
    catch (const scheme_error& e) { key = e.key->get(); args = e.args->get(); }
    catch (const native_error& e) { key = key_of(e.kind); args = error_args(subr, e.message, e.cpp_class); }
    catch (...) { translate_current(subr, &key, &args); }
    flush_native_output();
    raise(key, args);
}

inline std::string to_utf8(SCM string) {
    std::size_t length = 0;
    char* text = scm_to_utf8_stringn(string, &length);
    std::string result(text, length);
    std::free(text);
    return result;
}
// The GOOPS class name of any value, for messages: <integer>, <string>, <circle>, ...
inline std::string type_name(SCM value) {
    try {
        const SCM name = protect([&]() { return scm_class_name(scm_class_of(value)); });
        if (scm_is_symbol(name)) return to_utf8(scm_symbol_to_string(name));
    } catch (...) {}
    return "<unknown>";
}
inline SCM utf8(const std::string& text) {
    if (valid_utf8(text.data(), text.size())) return scm_from_utf8_stringn(text.data(), text.size());
    return protect([&]() { return scm_from_utf8_stringn(text.data(), text.size()); }); // raises decoding-error
}
inline SCM symbol(const char* name) { return scm_from_utf8_symbol(name); }
inline bool is_default(SCM value) { return scm_is_eq(value, runtime().default_keyword); }

// ---------------------------------------------------------------- classes
struct TypeInfo;
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct TypeInfo {
    const char* name = nullptr;
    SCM klass = SCM_BOOL_F;
    std::vector<BaseLink> bases;
    void (*destroy)(void*) = nullptr;
    void* (*copy)(const void*) = nullptr;
    SCM (*construct)(SCM, SCM) = nullptr;
    std::string (*stream)(void*) = nullptr;
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
// Specialized by generated code for every exported class.
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;
inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }
inline std::map<scm_t_bits, TypeInfo*>& class_registry() { static std::map<scm_t_bits, TypeInfo*> value; return value; }

// The C++ side of a wrapped object, owned by its foreign-object handle.
struct Instance {
    void* ptr = nullptr;
    TypeInfo* info = nullptr;
    std::shared_ptr<void>* holder = nullptr;
    bool owned = false;
    bool director = false;
};
inline void release(Instance* object) noexcept {
    void* ptr = object->ptr;
    auto* holder = object->holder;
    const bool owned = object->owned;
    TypeInfo* info = object->info;
    object->ptr = nullptr;
    object->holder = nullptr;
    object->owned = false;
    if (holder) delete holder;
    else if (owned && ptr && info && info->destroy) info->destroy(ptr);
}
inline void delete_instance(void* data) noexcept {
    auto* object = static_cast<Instance*>(data);
    release(object);
    delete object;
}
inline void handle_finalize(SCM handle) {
    auto* object = static_cast<Instance*>(scm_foreign_object_ref(handle, 0));
    if (!object) return;
    scm_foreign_object_set_x(handle, 0, nullptr);
    defer(object, &delete_instance);
}
inline std::mutex& class_cache_mutex() { static std::mutex value; return value; }
inline std::unordered_map<scm_t_bits, bool>& class_cache() { static std::unordered_map<scm_t_bits, bool> value; return value; }
// Whether value is a GOOPS instance of a subclass of <native-object>.
inline bool is_instance_object(SCM value) {
    if (!SCM_STRUCTP(value) || scm_is_false(runtime().root)) return false;
    if (scm_is_false(scm_instance_p(value))) return false;
    const SCM klass = scm_class_of(value);
    {
        std::lock_guard<std::mutex> lock(class_cache_mutex());
        const auto found = class_cache().find(SCM_UNPACK(klass));
        if (found != class_cache().end()) return found->second;
    }
    const bool derived = scm_is_true(scm_c_memq(runtime().root, scm_class_precedence_list(klass)));
    permanent(klass); // keeps the cache key's address from being reused
    std::lock_guard<std::mutex> lock(class_cache_mutex());
    class_cache()[SCM_UNPACK(klass)] = derived;
    return derived;
}
inline bool is_handle(SCM value) { return SCM_STRUCTP(value) && scm_is_eq(SCM_STRUCT_VTABLE(value), runtime().handle_type); }
// The C++ side of a native object (null before construction).
inline Instance* instance(SCM value) {
    const SCM handle = SCM_STRUCT_SLOT_REF(value, 0);
    return is_handle(handle) ? static_cast<Instance*>(scm_foreign_object_ref(handle, 0)) : nullptr;
}
inline Instance* attach(SCM object) {
    auto* created = new Instance;
    SCM_STRUCT_SLOT_SET(object, 0, scm_make_foreign_object_1(runtime().handle_type, created));
    return created;
}
inline std::string class_name(SCM value) { return type_name(value); }
// A new instance of klass with a fresh handle, without running initialize.
inline SCM new_object(SCM klass) {
    const SCM object = protect([&]() { return scm_call_2(runtime().allocate_instance, klass, SCM_EOL); });
    attach(object);
    SCM_STRUCT_SLOT_SET(object, 1, SCM_BOOL_F);
    return object;
}

// Base of generated director classes: C++ subclasses forwarding virtual calls to Scheme. When C++
// takes ownership of one, it keeps its Scheme object alive until the C++ object is deleted.
struct Director {
    SCM rosetta_self = SCM_BOOL_F;
    bool rosetta_owns_self = false;
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    virtual ~Director() {
        if (rosetta_owns_self && !scm_is_false(rosetta_self)) {
            if (auto* object = instance(rosetta_self)) { object->ptr = nullptr; object->owned = false; }
            scm_gc_unprotect_object(rosetta_self);
        }
    }
};

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
template<class T> bool is_instance(SCM value) {
    if (!is_instance_object(value)) return false;
    auto* object = instance(value);
    return object && object->info && derives(object->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> T* native(SCM value) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    const auto expected = [&] { return std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + class_name(value); };
    if (!is_instance_object(value)) fail(Kind::wrong_type, expected());
    auto* object = instance(value);
    if (!object || !object->ptr) fail(Kind::runtime, "native " + class_name(value) + " object is closed, moved or uninitialized");
    void* result = object->info ? cast_path(object->ptr, object->info, info) : nullptr;
    if (!result) fail(Kind::wrong_type, expected());
    return static_cast<U*>(result);
}
inline bool is_director(SCM value) {
    if (!is_instance_object(value)) return false;
    auto* object = instance(value);
    return object && object->director;
}

enum class Ownership { borrowed, owned };
template<class T> SCM wrap(T* value, Ownership ownership, SCM keepalive) {
    using U = std::remove_cv_t<T>;
    if (!value) return SCM_BOOL_F;
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value); director && !scm_is_false(director->rosetta_self)) {
            auto* object = instance(director->rosetta_self);
            if (object && ownership == Ownership::owned && object->ptr && !object->owned) {
                // C++ handed back ownership of a Scheme-implemented object.
                object->owned = true;
                if (director->rosetta_owns_self) {
                    const_cast<Director*>(director)->rosetta_owns_self = false;
                    scm_gc_unprotect_object(director->rosetta_self);
                }
            }
            return director->rosetta_self;
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info && !scm_is_false(found->second->klass)) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    if (scm_is_false(info->klass)) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail(Kind::type, "C++ result type is not exported to Scheme");
    }
    SCM result;
    try { result = new_object(info->klass); }
    catch (...) { if (ownership == Ownership::owned && info->destroy) info->destroy(ptr); throw; }
    auto* object = instance(result);
    object->ptr = ptr;
    object->info = info;
    object->owned = ownership == Ownership::owned;
    SCM_STRUCT_SLOT_SET(result, 1, keepalive);
    return result;
}
template<class T> SCM wrap_shared(std::shared_ptr<T> value) {
    if (!value) return SCM_BOOL_F;
    if constexpr (std::is_polymorphic_v<std::remove_cv_t<T>>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()); director && !scm_is_false(director->rosetta_self))
            return director->rosetta_self;
    }
    SCM result = wrap(value.get(), Ownership::borrowed, SCM_BOOL_F);
    auto* object = instance(result);
    object->holder = new std::shared_ptr<void>(std::const_pointer_cast<std::remove_cv_t<T>>(value), object->ptr);
    return result;
}
// Shares ownership with Scheme. A uniquely owned plain object becomes shared; a borrowed object or
// a Scheme subclass object stays owned by its Scheme object, which C++ then keeps alive.
template<class T> std::shared_ptr<T> shared(SCM value) {
    if (scm_is_false(value)) return {};
    T* ptr = native<T>(value);
    auto* object = instance(value);
    if (!object->holder) {
        if (!object->owned || object->director) {
            auto keeper = std::make_shared<Pinned>(value);
            return std::shared_ptr<T>(keeper, ptr);
        }
        auto* info = object->info;
        void* raw = object->ptr;
        object->holder = new std::shared_ptr<void>(raw, [info](void* p) { if (info->destroy) info->destroy(p); });
        object->owned = false;
    }
    return std::shared_ptr<T>(*object->holder, ptr);
}
template<class T> std::unique_ptr<T> take(SCM value) {
    if (scm_is_false(value)) return {};
    T* ptr = native<T>(value);
    auto* object = instance(value);
    if (object->holder || !object->owned) fail(Kind::invalid_argument, "cannot transfer ownership of a shared or borrowed native object");
    if (object->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail(Kind::type, "cannot transfer a derived object without a virtual destructor");
    object->owned = false;
    bool kept = false;
    if constexpr (std::is_polymorphic_v<T>) {
        if (auto* director = dynamic_cast<Director*>(ptr); director && object->director) {
            director->rosetta_owns_self = true; // C++ now keeps the Scheme implementation alive
            scm_gc_protect_object(value);
            kept = true;
        }
    }
    if (!kept) object->ptr = nullptr;
    return std::unique_ptr<T>(ptr);
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- values
template<class T, class = void> struct Value;   // by-value conversions

// Proper lists, vectors and (when not strict) other arrays: uniform vectors and bytevectors.
inline bool is_list(SCM value) { return scm_is_null(value) || (scm_is_pair(value) && scm_is_true(scm_list_p(value))); }
inline bool is_sequence(SCM value, bool strict) {
    if (is_list(value) || scm_is_vector(value)) return true;
    return !strict && !scm_is_string(value) && scm_is_array(value);
}
// The elements of a sequence as a list (vectors and arrays are copied).
inline SCM sequence_list(SCM value) {
    if (is_list(value)) return value;
    if (scm_is_vector(value)) return protect([&]() { return scm_vector_to_list(value); });
    return protect([&]() { return scm_array_to_list(value); });
}
// Builds a list front to back; the head lives on the stack, so the list stays visible to the GC.
struct ListBuilder {
    SCM head = SCM_EOL, tail = SCM_EOL;
    void push(SCM item) {
        const SCM cell = scm_cons(item, SCM_EOL);
        if (scm_is_null(tail)) head = cell;
        else SCM_SETCDR(tail, cell);
        tail = cell;
    }
};

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool check(SCM value, bool) { return scm_is_bool(value); }
    static bool load(SCM value) {
        if (!scm_is_bool(value)) fail(Kind::wrong_type, "expected #t or #f, got " + type_name(value));
        return scm_is_true(value);
    }
    static SCM cast(bool value) { return scm_from_bool(value); }
};
// char is a Scheme character (or, when not strict, a one-character string); signed/unsigned char
// stay integers.
template<> struct Value<char> {
    static bool check(SCM value, bool strict) {
        return SCM_CHARP(value) || (!strict && scm_is_string(value) && scm_c_string_length(value) == 1);
    }
    static char load(SCM value) {
        if (scm_is_string(value) && scm_c_string_length(value) == 1) value = scm_c_string_ref(value, 0);
        if (!SCM_CHARP(value)) fail(Kind::wrong_type, "expected a character, got " + type_name(value));
        const auto point = SCM_CHAR(value);
        if (point > 0xFF) fail(Kind::out_of_range, "character outside the range of a C++ char");
        return static_cast<char>(static_cast<unsigned char>(point));
    }
    static SCM cast(char value) { return SCM_MAKE_CHAR(static_cast<unsigned char>(value)); }
};
inline std::string number_text(SCM value) {
    try { return to_utf8(protect([&]() { return scm_number_to_string(value, SCM_UNDEFINED); })); } catch (...) { return "?"; }
}
// Exact integers with range checks (bignums included); reals do not truncate.
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(SCM value, bool) { return scm_is_exact_integer(value); }
    static T load(SCM value) {
        if (!scm_is_exact_integer(value)) fail(Kind::wrong_type, "expected an exact integer, got " + type_name(value));
        if constexpr (std::is_signed_v<T>) {
            if (!scm_is_signed_integer(value, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()))
                fail(Kind::out_of_range, "integer " + number_text(value) + " outside the range of the C++ type");
            return static_cast<T>(scm_to_int64(value));
        } else {
            if (!scm_is_unsigned_integer(value, 0, std::numeric_limits<T>::max()))
                fail(Kind::out_of_range, "integer " + number_text(value) + " outside the range of the C++ type");
            return static_cast<T>(scm_to_uint64(value));
        }
    }
    static SCM cast(T value) {
        if constexpr (std::is_signed_v<T>) return scm_from_int64(static_cast<std::int64_t>(value));
        else return scm_from_uint64(static_cast<std::uint64_t>(value));
    }
};
// Reals: inexact reals when strict; exact integers and rationals convert otherwise.
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(SCM value, bool strict) { return scm_is_real(value) && (!strict || !scm_is_exact(value)); }
    static T load(SCM value) {
        if (!scm_is_real(value)) fail(Kind::wrong_type, "expected a real number, got " + type_name(value));
        const double n = scm_to_double(value);
        if constexpr (!std::is_same_v<T, double> && !std::is_same_v<T, long double>) {
            if (std::isfinite(n) && (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max()))
                fail(Kind::out_of_range, "number outside the range of the C++ type");
        }
        return static_cast<T>(n);
    }
    static SCM cast(T value) { return scm_from_double(static_cast<double>(value)); }
};
inline std::string load_string(SCM value) {
    if (scm_is_symbol(value)) value = scm_symbol_to_string(value);
    if (!scm_is_string(value)) fail(Kind::wrong_type, "expected a string, got " + type_name(value));
    return to_utf8(value);
}
template<> struct Value<std::string> {
    static bool check(SCM value, bool strict) { return scm_is_string(value) || (!strict && scm_is_symbol(value)); }
    static std::string load(SCM value) { return load_string(value); }
    static SCM cast(const std::string& value) { return utf8(value); }
};
template<> struct Value<std::string_view> {
    static bool check(SCM value, bool strict) { return Value<std::string>::check(value, strict); }
    static SCM cast(std::string_view value) { return utf8(std::string(value)); }
};
template<> struct Value<const char*> {
    static bool check(SCM value, bool strict) { return scm_is_false(value) || Value<std::string>::check(value, strict); }
    static SCM cast(const char* value) { if (!value) return SCM_BOOL_F; return utf8(std::string(value)); }
};
template<> struct Value<char*> {
    static SCM cast(const char* value) { return Value<const char*>::cast(value); }
};
// Paths are strings.
template<> struct Value<std::filesystem::path> {
    static bool check(SCM value, bool) { return scm_is_string(value); }
    static std::filesystem::path load(SCM value) { return std::filesystem::path(load_string(value)); }
    static SCM cast(const std::filesystem::path& value) { return utf8(value.string()); }
};
template<> struct Value<std::nullptr_t> {
    static bool check(SCM value, bool) { return scm_is_false(value); }
    static std::nullptr_t load(SCM) { return nullptr; }
    static SCM cast(std::nullptr_t) { return SCM_BOOL_F; }
};
template<> struct Value<std::monostate> {
    static bool check(SCM value, bool) { return scm_is_false(value); }
    static std::monostate load(SCM) { return {}; }
    static SCM cast(std::monostate) { return SCM_BOOL_F; }
};
template<class T> struct Value<std::complex<T>> {
    static bool check(SCM value, bool strict) { return (scm_is_complex(value) && !scm_is_real(value)) || Value<T>::check(value, strict); }
    static std::complex<T> load(SCM value) {
        if (!scm_is_complex(value)) fail(Kind::wrong_type, "expected a complex number, got " + type_name(value));
        return {static_cast<T>(scm_c_real_part(value)), static_cast<T>(scm_c_imag_part(value))};
    }
    static SCM cast(const std::complex<T>& value) {
        return scm_c_make_rectangular(static_cast<double>(value.real()), static_cast<double>(value.imag()));
    }
};

// Enumerations are symbols named after their enumerators; flag enumerations (policy enum_flags)
// are exact integers. Arguments also take the integer value, and flags a list of symbols.
struct EnumInfo {
    bool flags = false;
    std::vector<std::pair<SCM, long long>> members;
};
inline std::map<std::type_index, EnumInfo>& enum_infos() { static std::map<std::type_index, EnumInfo> value; return value; }
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    using N = std::conditional_t<std::is_same_v<U, bool>, int, U>;
    static const EnumInfo* info() {
        const auto found = enum_infos().find(std::type_index(typeid(T)));
        return found == enum_infos().end() ? nullptr : &found->second;
    }
    static bool member(SCM name, long long* out) {
        const auto* e = info();
        if (!e) return false;
        for (const auto& [symbol_value, value] : e->members) if (scm_is_eq(symbol_value, name)) { if (out) *out = value; return true; }
        return false;
    }
    static bool check(SCM value, bool strict) {
        // Lenient: any symbol, so an unknown one raises an error naming the enumerators.
        if (scm_is_symbol(value)) return !strict || member(value, nullptr);
        const auto* e = info();
        if (e && e->flags && is_list(value)) {
            for (SCM item = value; scm_is_pair(item); item = SCM_CDR(item))
                if (!scm_is_symbol(SCM_CAR(item)) || !member(SCM_CAR(item), nullptr)) return false;
            return true;
        }
        return scm_is_exact_integer(value) && (!strict || (e && e->flags));
    }
    static T load(SCM value) {
        long long number = 0;
        if (scm_is_symbol(value)) {
            if (!member(value, &number)) {
                std::string names;
                if (const auto* e = info()) for (const auto& item : e->members) names += (names.empty() ? "" : " ") + to_utf8(scm_symbol_to_string(item.first));
                fail(Kind::invalid_argument, "unknown enumerator " + to_utf8(scm_symbol_to_string(value)) + " (expected one of " + names + ")");
            }
            return static_cast<T>(static_cast<U>(number));
        }
        if (is_list(value) && !scm_is_null(value)) {
            unsigned long long bits = 0;
            for (SCM item = value; scm_is_pair(item); item = SCM_CDR(item)) bits |= static_cast<unsigned long long>(static_cast<U>(load(SCM_CAR(item))));
            return static_cast<T>(static_cast<U>(bits));
        }
        if (!scm_is_exact_integer(value)) fail(Kind::wrong_type, "expected a symbol or an exact integer, got " + type_name(value));
        return static_cast<T>(static_cast<U>(Value<N>::load(value)));
    }
    static SCM cast(T value) {
        const auto number = static_cast<long long>(static_cast<U>(value));
        if (const auto* e = info(); e && !e->flags)
            for (const auto& [name, item] : e->members) if (item == number) return name;
        return Value<N>::cast(static_cast<N>(static_cast<U>(value)));
    }
};

// Exported classes by value: Scheme receives an owned copy.
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(SCM value, bool) { return is_instance<T>(value); }
    static T load(SCM value) { return *native<T>(value); }
    static SCM cast(const T& value) {
        if constexpr (copyable_v<T>) return wrap(new T(value), Ownership::owned, SCM_BOOL_F);
        else fail(Kind::type, "C++ result is not copyable");
    }
    static SCM cast(T&& value) {
        if constexpr (std::is_move_constructible_v<T> && !std::is_abstract_v<T>) return wrap(new T(std::move(value)), Ownership::owned, SCM_BOOL_F);
        else return cast(static_cast<const T&>(value));
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(SCM value, bool) { return scm_is_false(value) || is_instance<T>(value); }
    static T* load(SCM value) { return scm_is_false(value) ? nullptr : native<T>(value); }
    static SCM cast(T* value) { return wrap(value, Ownership::borrowed, SCM_BOOL_F); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(SCM value, bool) { return scm_is_false(value) || is_instance<T>(value); }
    static std::shared_ptr<T> load(SCM value) { return shared<T>(value); }
    static SCM cast(const std::shared_ptr<T>& value) { return wrap_shared(value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(SCM value, bool) { return scm_is_false(value) || is_instance<T>(value); }
    static std::unique_ptr<T, D> load(SCM value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(value);
        else fail(Kind::type, "custom deleters cannot receive Scheme objects");
    }
    static SCM cast(std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return wrap(value.release(), Ownership::owned, SCM_BOOL_F);
        else fail(Kind::type, "custom deleters cannot be returned to Scheme");
    }
};
// std::optional: #f is the empty optional.
template<class T> struct Value<std::optional<T>> {
    static bool check(SCM value, bool strict) { return scm_is_false(value) || Value<T>::check(value, strict); }
    static std::optional<T> load(SCM value) {
        if (scm_is_false(value)) return std::nullopt;
        return Value<T>::load(value);
    }
    static SCM cast(const std::optional<T>& value) {
        if (!value) return SCM_BOOL_F;
        return Value<T>::cast(*value);
    }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static SCM cast(std::reference_wrapper<T> value) {
        if constexpr (bound_v<T>) return wrap(&value.get(), Ownership::borrowed, SCM_BOOL_F);
        else return Value<std::remove_cv_t<T>>::cast(value.get());
    }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- containers
// Sequences: lists out; lists, vectors and (when not strict) uniform vectors in.
template<class C, class E> struct Sequence {
    static bool check(SCM value, bool strict) {
        if (!is_sequence(value, strict)) return false;
        if (is_list(value)) {
            for (SCM item = value; scm_is_pair(item); item = SCM_CDR(item)) if (!Value<E>::check(SCM_CAR(item), strict)) return false;
            return true;
        }
        if (scm_is_vector(value)) {
            const std::size_t size = scm_c_vector_length(value);
            for (std::size_t i = 0; i < size; ++i) if (!Value<E>::check(scm_c_vector_ref(value, i), strict)) return false;
            return true;
        }
        return !strict;
    }
    static C load(SCM value) {
        if (!is_sequence(value, false)) fail(Kind::wrong_type, "expected a list or vector, got " + type_name(value));
        const SCM items = sequence_list(value);
        C result{};
        for (SCM item = items; scm_is_pair(item); item = SCM_CDR(item)) {
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(Value<E>::load(SCM_CAR(item)));
            else result.push_back(Value<E>::load(SCM_CAR(item)));
        }
        scm_remember_upto_here_1(items);
        return result;
    }
    template<class Container> static SCM cast(Container&& value) {
        ListBuilder list;
        for (auto&& item : value) {
            if constexpr (std::is_rvalue_reference_v<Container&&> && !std::is_same_v<E, bool>) list.push(Value<E>::cast(std::move(item)));
            else list.push(Value<E>::cast(item));
        }
        return list.head;
    }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
// Byte vectors are bytevectors (arguments also take a list of integers).
template<class A> struct Value<std::vector<unsigned char, A>> : Sequence<std::vector<unsigned char, A>, unsigned char> {
    using Base = Sequence<std::vector<unsigned char, A>, unsigned char>;
    static bool check(SCM value, bool strict) { return scm_is_bytevector(value) || Base::check(value, strict); }
    static std::vector<unsigned char, A> load(SCM value) {
        if (scm_is_bytevector(value)) {
            const auto* data = reinterpret_cast<const unsigned char*>(SCM_BYTEVECTOR_CONTENTS(value));
            return std::vector<unsigned char, A>(data, data + SCM_BYTEVECTOR_LENGTH(value));
        }
        return Base::load(value);
    }
    static SCM cast(const std::vector<unsigned char, A>& value) {
        const SCM result = scm_c_make_bytevector(value.size());
        if (!value.empty()) std::memcpy(SCM_BYTEVECTOR_CONTENTS(result), value.data(), value.size());
        return result;
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
template<class E, class C, class A> struct Value<std::set<E, C, A>> : Sequence<std::set<E, C, A>, E> {
    static std::set<E, C, A> load(SCM value) {
        if (!is_sequence(value, false)) fail(Kind::wrong_type, "expected a list or vector, got " + type_name(value));
        const SCM items = sequence_list(value);
        std::set<E, C, A> result;
        for (SCM item = items; scm_is_pair(item); item = SCM_CDR(item)) result.insert(Value<E>::load(SCM_CAR(item)));
        scm_remember_upto_here_1(items);
        return result;
    }
};
template<class E, class H, class Q, class A> struct Value<std::unordered_set<E, H, Q, A>> : Sequence<std::unordered_set<E, H, Q, A>, E> {
    static std::unordered_set<E, H, Q, A> load(SCM value) {
        if (!is_sequence(value, false)) fail(Kind::wrong_type, "expected a list or vector, got " + type_name(value));
        const SCM items = sequence_list(value);
        std::unordered_set<E, H, Q, A> result;
        for (SCM item = items; scm_is_pair(item); item = SCM_CDR(item)) result.insert(Value<E>::load(SCM_CAR(item)));
        scm_remember_upto_here_1(items);
        return result;
    }
};
template<class E> struct Value<std::valarray<E>> {
    static bool check(SCM value, bool strict) { return Value<std::vector<E>>::check(value, strict); }
    static std::valarray<E> load(SCM value) { auto v = Value<std::vector<E>>::load(value); return std::valarray<E>(v.data(), v.size()); }
    static SCM cast(const std::valarray<E>& value) { return Value<std::vector<E>>::cast(std::vector<E>(std::begin(value), std::end(value))); }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(SCM value, bool strict) {
        if (!Sequence<std::vector<E>, E>::check(value, strict)) return false;
        return is_list(value) ? scm_ilength(value) == static_cast<long>(N) : !scm_is_vector(value) || scm_c_vector_length(value) == N;
    }
    static std::array<E, N> load(SCM value) {
        auto items = Sequence<std::vector<E>, E>::load(value);
        if (items.size() != N) fail(Kind::invalid_argument, "expected " + std::to_string(N) + " elements, got " + std::to_string(items.size()));
        std::array<E, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = std::move(items[i]);
        return result;
    }
    static SCM cast(const std::array<E, N>& value) { return Sequence<std::vector<E>, E>::cast(value); }
};
// Maps: association lists ((key . value) ...) out; association lists or hash tables in.
inline bool is_hash_table(SCM value) { return scm_is_true(scm_hash_table_p(value)); }
inline SCM map_entries(SCM value) {
    if (is_hash_table(value)) return protect([&]() { return scm_hash_map_to_list(runtime().cons, value); });
    return value;
}
template<class M, class K, class V> struct Mapping {
    static bool check(SCM value, bool strict) {
        if (!is_list(value) && !is_hash_table(value)) return false;
        const SCM entries = map_entries(value);
        for (SCM item = entries; scm_is_pair(item); item = SCM_CDR(item)) {
            const SCM entry = SCM_CAR(item);
            if (!scm_is_pair(entry) || !Value<K>::check(SCM_CAR(entry), strict) || !Value<V>::check(SCM_CDR(entry), strict)) return false;
        }
        scm_remember_upto_here_1(entries);
        return true;
    }
    static M load(SCM value) {
        if (!is_list(value) && !is_hash_table(value)) fail(Kind::wrong_type, "expected an association list or hash table, got " + type_name(value));
        const SCM entries = map_entries(value);
        M result;
        for (SCM item = entries; scm_is_pair(item); item = SCM_CDR(item)) {
            const SCM entry = SCM_CAR(item);
            if (!scm_is_pair(entry)) fail(Kind::wrong_type, "expected (key . value) pairs, got " + type_name(entry));
            result.emplace(Value<K>::load(SCM_CAR(entry)), Value<V>::load(SCM_CDR(entry)));
        }
        scm_remember_upto_here_1(entries);
        return result;
    }
    static SCM cast(const M& value) {
        ListBuilder list;
        for (const auto& [key, item] : value) {
            const SCM k = Value<K>::cast(key);
            list.push(scm_cons(k, Value<V>::cast(item)));
        }
        return list.head;
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};
// Pairs are pairs; tuples are lists.
template<class A, class B> struct Value<std::pair<A, B>> {
    static bool check(SCM value, bool strict) {
        return scm_is_pair(value) && Value<A>::check(SCM_CAR(value), strict) && Value<B>::check(SCM_CDR(value), strict);
    }
    static std::pair<A, B> load(SCM value) {
        if (!scm_is_pair(value)) fail(Kind::wrong_type, "expected a pair, got " + type_name(value));
        return {Value<A>::load(SCM_CAR(value)), Value<B>::load(SCM_CDR(value))};
    }
    static SCM cast(const std::pair<A, B>& value) {
        const SCM first = Value<A>::cast(value.first);
        return scm_cons(first, Value<B>::cast(value.second));
    }
};
template<class... T> struct Value<std::tuple<T...>> {
    static SCM item(SCM value, std::size_t index) {
        for (std::size_t i = 0; i < index; ++i) value = SCM_CDR(value);
        return SCM_CAR(value);
    }
    template<std::size_t... I> static bool check_items(SCM value, bool strict, std::index_sequence<I...>) {
        return (Value<T>::check(item(value, I), strict) && ...);
    }
    static bool check(SCM value, bool strict) {
        return is_list(value) && scm_ilength(value) == static_cast<long>(sizeof...(T)) && check_items(value, strict, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static std::tuple<T...> load_items(SCM value, std::index_sequence<I...>) {
        return std::tuple<T...>{Value<T>::load(item(value, I))...};
    }
    static std::tuple<T...> load(SCM value) {
        if (!is_list(value) || scm_ilength(value) != static_cast<long>(sizeof...(T)))
            fail(Kind::wrong_type, "expected a list of " + std::to_string(sizeof...(T)) + " elements, got " + type_name(value));
        return load_items(value, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static SCM cast_items(const std::tuple<T...>& value, std::index_sequence<I...>) {
        ListBuilder list;
        (list.push(Value<T>::cast(std::get<I>(value))), ...);
        return list.head;
    }
    static SCM cast(const std::tuple<T...>& value) { return cast_items(value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(SCM value, bool strict) { return (Value<T>::check(value, true) || ...) || (!strict && (Value<T>::check(value, false) || ...)); }
    template<class First, class... Rest> static std::variant<T...> load_first(SCM value, bool strict) {
        if (Value<First>::check(value, strict)) return std::variant<T...>(std::in_place_type<First>, Value<First>::load(value));
        if constexpr (sizeof...(Rest) > 0) return load_first<Rest...>(value, strict);
        else fail(Kind::wrong_type, "no variant alternative accepts " + type_name(value));
    }
    static std::variant<T...> load(SCM value) {
        if ((Value<T>::check(value, true) || ...)) return load_first<T...>(value, true);
        return load_first<T...>(value, false);
    }
    static SCM cast(const std::variant<T...>& value) {
        return std::visit([](const auto& item) -> SCM { return Value<std::decay_t<decltype(item)>>::cast(item); }, value);
    }
};
// Durations are real seconds.
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(SCM value, bool) { return scm_is_real(value); }
    static D load(SCM value) { return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(value))); }
    static SCM cast(const D& value) { return Value<double>::cast(std::chrono::duration<double>(value).count()); }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arguments
struct fill_t {}; inline constexpr fill_t fill{};
struct fill_default_t {}; inline constexpr fill_default_t fill_default{};
// Writes a converted container back into the caller's vector or list (same length only).
inline void write_back(SCM original, SCM converted) {
    if (scm_is_vector(original) && is_list(converted) && scm_c_vector_length(original) == static_cast<std::size_t>(scm_ilength(converted))) {
        std::size_t i = 0;
        for (SCM item = converted; scm_is_pair(item); item = SCM_CDR(item), ++i) scm_c_vector_set_x(original, i, SCM_CAR(item));
    } else if (scm_is_pair(original) && is_list(original) && is_list(converted) && scm_ilength(original) == scm_ilength(converted)) {
        SCM target = original;
        for (SCM item = converted; scm_is_pair(item); item = SCM_CDR(item), target = SCM_CDR(target)) {
            const SCM cell = target, value = SCM_CAR(item);
            try { protect([&]() { return scm_set_car_x(cell, value); }); } // literal constants are read-only
            catch (const scheme_error&) { return; }
        }
    }
}
// Converts a Scheme argument for a declared C++ parameter type P and keeps any temporary alive.
// The fill constructors use the C++ default of a parameter the call skipped with #:default.
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(SCM value, bool strict) { return Value<V>::check(value, strict); }
    explicit Param(SCM value) : value_(Value<V>::load(value)) {}
    template<class U> Param(fill_t, U&& value) : value_(static_cast<V>(std::forward<U>(value))) {}
    explicit Param(fill_default_t) : value_{} {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    // Mutable references to converted containers update the caller's vector or list in place.
    void writeback(SCM original) {
        if constexpr (std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>>) {
            if constexpr (std::is_copy_constructible_v<V>) {
                if (scm_is_vector(original) || scm_is_pair(original)) {
                    const SCM converted = Value<V>::cast(value_);
                    write_back(original, converted);
                }
            }
        }
    }
private:
    V value_;
};
// Exported classes are passed by reference to the Scheme-owned object.
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(SCM value, bool) { return is_instance<V>(value); }
    explicit Param(SCM value) : ptr_(native<V>(value)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
    void writeback(SCM) {}
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(SCM value, bool) { return scm_is_false(value) || is_instance<V>(value); }
    explicit Param(SCM value) : ptr_(scm_is_false(value) ? nullptr : native<V>(value)) {}
    Param(fill_t, std::nullptr_t) : ptr_(nullptr) {}
    P get() { return ptr_; }
    void writeback(SCM) {}
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(SCM value, bool strict) { return scm_is_false(value) || Value<std::string>::check(value, strict); }
    explicit Param(SCM value) { if (!scm_is_false(value)) text_ = load_string(value); }
    Param(fill_t, const char* value) { if (value) text_ = value; }
    const char* get() { return text_ ? text_->c_str() : nullptr; }
    void writeback(SCM) {}
private:
    std::optional<std::string> text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(SCM value, bool strict) { return Value<std::string>::check(value, strict); }
    explicit Param(SCM value) : text_(load_string(value)) {}
    Param(fill_t, std::string_view value) : text_(value) {}
    std::string_view get() { return text_; }
    void writeback(SCM) {}
private:
    std::string text_;
};

// The positional arguments of one call. #:default marks a skipped parameter.
struct Args {
    SCM list;
    std::vector<SCM> argv; // copies of the elements of list, which keeps them alive
    bool arity_matched = false;
    explicit Args(SCM rest) : list(rest) {
        for (SCM item = rest; scm_is_pair(item); item = SCM_CDR(item)) argv.push_back(SCM_CAR(item));
    }
    // Maps the arguments onto one overload's parameters. A skipped argument (#:default, here
    // SCM_UNDEFINED) is allowed before the last given one only where the parameter's C++ default
    // can be reproduced.
    bool collect(long count, long required, const bool* fillable, SCM* out, long* used) {
        if (static_cast<long>(argv.size()) > count) return false;
        long last = 0;
        for (long i = 0; i < count; ++i) {
            out[i] = i < static_cast<long>(argv.size()) && !is_default(argv[static_cast<std::size_t>(i)]) ? argv[static_cast<std::size_t>(i)] : SCM_UNDEFINED;
            if (!SCM_UNBNDP(out[i])) last = i + 1;
        }
        if (last < required) return false;
        for (long i = 0; i < last; ++i) if (SCM_UNBNDP(out[i]) && (i < required || !fillable[i])) return false;
        *used = last;
        arity_matched = true;
        return true;
    }
};
[[noreturn]] inline void no_overload(const char* name, const char* signatures, const Args& args, const SCM* self) {
    (void)name; // the error's subr
    std::string received;
    if (self) received = class_name(*self);
    for (const SCM value : args.argv) received += (received.empty() ? "" : " ") + (is_default(value) ? std::string("#:default") : type_name(value));
    fail(args.arity_matched ? Kind::wrong_type : Kind::wrong_args,
         std::string(args.arity_matched ? "no overload accepts (" : "wrong number of arguments (") + received +
             "); supported signatures:\n" + signatures);
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
template<class R> SCM result(R&& value, SCM parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) return SCM_BOOL_F;
        if (!scm_is_false(parent) && is_instance<B>(parent) && instance(parent)->ptr && native<B>(parent) == ptr) return parent;
        if (policy == Return::owned) return wrap(ptr, Ownership::owned, SCM_BOOL_F);
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) return wrap(new B(*ptr), Ownership::owned, SCM_BOOL_F);
            else fail(Kind::type, "C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (!scm_is_false(parent) && is_instance<V>(parent) && instance(parent)->ptr && native<V>(parent) == ptr) return parent;
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) return wrap(new V(value), Ownership::owned, SCM_BOOL_F);
            else fail(Kind::type, "C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V>) {
        return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else if constexpr (std::is_lvalue_reference_v<R>) {
        return Value<V>::cast(static_cast<const V&>(value));
    } else {
        return Value<V>::cast(std::move(value));
    }
}
// Calls a native function (a lambda returning decltype(auto)) and converts its result.
template<class F> SCM call(F&& function, SCM parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); return SCM_UNSPECIFIED; }
    else return result<R>(function(), parent, policy);
}
template <class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail(Kind::type, "native field type cannot be assigned");
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- C++ -> Scheme calls
// Converts arguments passed from C++ to Scheme procedures; references to classes are borrowed.
template<class A> SCM argument(A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) return wrap(const_cast<V*>(&value), Ownership::borrowed, SCM_BOOL_F);
        else return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        return wrap(const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed, SCM_BOOL_F);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else {
        return Value<V>::cast(value);
    }
}
template<class R> R returned(SCM value) {
    if constexpr (std::is_void_v<R>) { (void)value; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(value);
        else {
            if (!Value<V>::check(value, false))
                fail(Kind::wrong_type, "Scheme procedure returned " + type_name(value) + ", which does not convert to the C++ result type");
            return Value<V>::load(value);
        }
    }
}
// Runs fn in Guile mode: on a thread Guile does not know (C++ worker threads) it enters Guile
// first. C++ exceptions are carried across libguile's C frames.
template<class F> void with_guile(F&& fn) {
    struct Data { F* fn; std::exception_ptr error; } data{&fn, nullptr};
    scm_with_guile([](void* raw) -> void* {
        auto* d = static_cast<Data*>(raw);
        try { (*d->fn)(); } catch (...) { d->error = std::current_exception(); }
        return nullptr;
    }, &data);
    if (data.error) std::rethrow_exception(data.error);
}
// Calls procedure with converted arguments and converts its result to R.
template<class R, class... A> R invoke(SCM procedure, A&&... arguments) {
    const auto run = [&]() -> SCM {
        SCM argv[sizeof...(A) + 1] = {argument<A>(std::forward<A>(arguments))..., SCM_BOOL_F};
        const SCM value = protect([&]() { return scm_call_n(procedure, argv, sizeof...(A)); });
        scm_remember_upto_here_1(argv[0]);
        return value;
    };
    if constexpr (std::is_void_v<R>) {
        with_guile([&]() { run(); });
    } else {
        std::optional<std::remove_cv_t<R>> value;
        with_guile([&]() { value.emplace(returned<R>(run())); });
        return std::move(*value);
    }
}
// Procedures (lambdas, gsubrs, generics) become a std::function; #f is empty.
template<class R, class... A> struct Value<std::function<R(A...)>> {
    static bool check(SCM value, bool) { return scm_is_false(value) || scm_is_true(scm_procedure_p(value)); }
    static std::function<R(A...)> load(SCM value) {
        if (scm_is_false(value)) return {};
        if (!check(value, false)) fail(Kind::wrong_type, "expected a procedure, got " + type_name(value));
        SharedRef target(value);
        return [target](A... arguments) -> R { return invoke<R, A...>(target.get(), std::forward<A>(arguments)...); };
    }
    static SCM cast(const std::function<R(A...)>& value);
};
// Native functions returned to Scheme: a Scheme procedure around a handle that owns the
// std::function.
struct Callable {
    std::function<SCM(const std::vector<SCM>&)> call;
    std::size_t arity = 0;
};
inline void delete_callable(void* data) noexcept { delete static_cast<Callable*>(data); }
inline void callable_finalize(SCM handle) {
    auto* object = static_cast<Callable*>(scm_foreign_object_ref(handle, 0));
    if (!object) return;
    scm_foreign_object_set_x(handle, 0, nullptr);
    defer(object, &delete_callable);
}
inline SCM call_native_subr(SCM handle, SCM rest) {
    return guard("native procedure", [&]() -> SCM {
        if (!SCM_STRUCTP(handle) || !scm_is_eq(SCM_STRUCT_VTABLE(handle), runtime().callable_type)) fail(Kind::wrong_type, "expected a native procedure");
        auto* object = static_cast<Callable*>(scm_foreign_object_ref(handle, 0));
        if (!object || !object->call) fail(Kind::runtime, "empty native procedure");
        Args args(rest);
        if (args.argv.size() != object->arity)
            fail(Kind::wrong_args, "wrong number of arguments (given " + std::to_string(args.argv.size()) + ", expected " + std::to_string(object->arity) + ")");
        return object->call(args.argv);
    });
}
template<class R, class... A, std::size_t... I>
SCM call_native(const std::function<R(A...)>& function, const std::vector<SCM>& argv, std::index_sequence<I...>) {
    std::tuple<Param<A>...> params{Param<A>(argv[I])...};
    (void)params; (void)argv;
    return call([&]() -> R { return function(std::get<I>(params).get()...); }, SCM_BOOL_F, Return::copy);
}
template<class R, class... A>
SCM Value<std::function<R(A...)>>::cast(const std::function<R(A...)>& value) {
    if (!value) return SCM_BOOL_F;
    auto* object = new Callable;
    object->arity = sizeof...(A);
    object->call = [value](const std::vector<SCM>& argv) -> SCM { return call_native<R, A...>(value, argv, std::index_sequence_for<A...>{}); };
    const SCM handle = scm_make_foreign_object_1(runtime().callable_type, object);
    return protect([&]() { return scm_call_1(runtime().make_callable, handle); });
}

// ---------------------------------------------------------------- directors
// The public generic of a virtual method, looked up in the module on first use.
inline SCM module_procedure(SCM* cache, const char* name) {
    if (scm_is_false(*cache)) {
        const SCM variable = protect([&]() { return scm_module_variable(runtime().module, symbol(name)); });
        if (scm_is_false(variable)) fail(Kind::runtime, std::string("generic ") + name + " is not defined");
        *cache = permanent(protect([&]() { return scm_variable_ref(variable); }));
    }
    return *cache;
}
// Calls the public generic NAME of the module with self and the converted arguments: the C++
// side of a virtual method a Scheme subclass may specialize.
template<class R, class... A> R invoke_method(SCM* cache, const char* name, SCM self, A&&... arguments) {
    const auto run = [&]() -> SCM {
        const SCM procedure = module_procedure(cache, name);
        SCM argv[sizeof...(A) + 1] = {self, argument<A>(std::forward<A>(arguments))...};
        const SCM value = protect([&]() { return scm_call_n(procedure, argv, sizeof...(A) + 1); });
        scm_remember_upto_here_1(argv[0]);
        return value;
    };
    if constexpr (std::is_void_v<R>) {
        with_guile([&]() { run(); });
    } else {
        std::optional<std::remove_cv_t<R>> value;
        with_guile([&]() { value.emplace(returned<R>(run())); });
        return std::move(*value);
    }
}
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(Kind::not_implemented, std::string("pure virtual method ") + name + " is not implemented by the Scheme subclass");
}
// A Scheme exception from a noexcept override cannot reach C++: report it and terminate.
[[noreturn]] inline void unraisable(const char* name) noexcept {
    std::fprintf(stderr, "rosetta: exception in Scheme override of noexcept %s\n", name);
    std::terminate();
}

// ---------------------------------------------------------------- module helpers
template<class T> std::string stream_text(void* ptr) {
    std::ostringstream stream;
    stream << *static_cast<T*>(ptr);
    return stream.str();
}
inline SCM lookup(const char* name) {
    return protect([&]() { return scm_variable_ref(scm_c_module_lookup(runtime().module, name)); });
}
template<class T> void register_type(TypeInfo& info, const char* name, SCM klass) {
    info.name = name;
    info.klass = permanent(klass);
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* ptr) { delete static_cast<T*>(ptr); };
    if constexpr (copyable_v<T>) info.copy = [](const void* ptr) -> void* { return new T(*static_cast<const T*>(ptr)); };
    registry()[std::type_index(typeid(T))] = &info;
    class_registry()[SCM_UNPACK(klass)] = &info;
}
template<class Derived, class Base> void add_base(TypeInfo& info) {
    info.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* {
        return static_cast<Base*>(static_cast<Derived*>(ptr));
    }});
}
template<class T> void define_enum(bool flags, std::initializer_list<std::pair<const char*, long long>> members) {
    EnumInfo info;
    info.flags = flags;
    for (const auto& [name, value] : members) info.members.emplace_back(permanent(symbol(name)), value);
    enum_infos()[std::type_index(typeid(T))] = std::move(info);
}
inline void define(const char* name, int required, int rest, scm_t_subr function) {
    scm_c_define_gsubr(name, required, 0, rest, function);
}
// The generated class nearest to the class of self in its precedence list.
inline TypeInfo* nearest(SCM self) {
    for (SCM item = scm_class_precedence_list(scm_class_of(self)); scm_is_pair(item); item = SCM_CDR(item)) {
        const auto found = class_registry().find(SCM_UNPACK(SCM_CAR(item)));
        if (found != class_registry().end()) return found->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------- runtime procedures
inline SCM construct_subr(SCM self, SCM rest) {
    return guard("make", [&]() -> SCM {
        if (!is_instance_object(self)) fail(Kind::wrong_type, "not a native object: " + type_name(self));
        auto* info = nearest(self);
        if (!info) fail(Kind::type, class_name(self) + " has no native class");
        if (!info->construct) fail(Kind::type, std::string(info->name) + " cannot be constructed from Scheme");
        return info->construct(self, rest);
    });
}
inline SCM close_subr(SCM self) {
    return guard("close!", [&]() -> SCM {
        if (is_instance_object(self)) {
            if (auto* object = instance(self)) {
                if (object->director && object->ptr && !object->owned) fail(Kind::runtime, "a Scheme subclass object owned by C++ cannot be closed");
                release(object);
            }
        }
        return SCM_UNSPECIFIED;
    });
}
inline SCM closed_subr(SCM self) {
    if (!is_instance_object(self)) return SCM_BOOL_T;
    auto* object = instance(self);
    return scm_from_bool(!object || !object->ptr);
}
inline SCM owned_subr(SCM self) {
    if (!is_instance_object(self)) return SCM_BOOL_F;
    auto* object = instance(self);
    return scm_from_bool(object && object->ptr && (object->owned || object->holder));
}
inline SCM copy_subr(SCM self) {
    return guard("shallow-clone", [&]() -> SCM {
        if (!is_instance_object(self)) fail(Kind::wrong_type, "not a native object: " + type_name(self));
        auto* source = instance(self);
        if (!source || !source->ptr) fail(Kind::runtime, "native " + class_name(self) + " object is closed, moved or uninitialized");
        if (!source->info->copy || source->director) fail(Kind::type, class_name(self) + " is not copyable");
        const SCM result = new_object(source->info->klass);
        auto* target = instance(result);
        target->ptr = source->info->copy(source->ptr);
        target->info = source->info;
        target->owned = true;
        return result;
    });
}
inline SCM describe_subr(SCM self) {
    return guard("write", [&]() -> SCM {
        char address[64];
        auto* object = is_instance_object(self) ? instance(self) : nullptr;
        const bool open = object && object->ptr;
        std::snprintf(address, sizeof(address), "%p", open ? object->ptr : nullptr);
        return utf8("#<" + class_name(self) + (open ? std::string(" ") + address : std::string(" closed")) + ">");
    });
}
inline SCM stream_subr(SCM self) {
    return guard("display", [&]() -> SCM {
        auto* object = is_instance_object(self) ? instance(self) : nullptr;
        if (!object || !object->ptr || !object->info || !object->info->stream) return SCM_BOOL_F;
        return utf8(object->info->stream(object->ptr));
    });
}
inline SCM error_keys_subr() { return runtime().error_keys; }
// The module's runtime: classes, keys and runtime procedures. The Scheme part of the module
// defined <native-object> before loading the extension.
inline void initialize_runtime(const char* prefix) {
    auto& r = runtime();
    r.prefix = prefix;
    r.module = permanent(scm_current_module());
    r.root = permanent(lookup("<native-object>"));
    r.handle_type = permanent(scm_make_foreign_object_type(symbol("native-handle"), scm_list_1(symbol("instance")), &handle_finalize));
    r.callable_type = permanent(scm_make_foreign_object_type(symbol("native-procedure"), scm_list_1(symbol("callable")), &callable_finalize));
    r.allocate_instance = permanent(protect([]() { return scm_c_public_ref("oop goops", "allocate-instance"); }));
    r.raise_exception = permanent(protect([]() { return scm_c_public_ref("guile", "raise-exception"); }));
    r.cons = permanent(protect([]() { return scm_c_public_ref("guile", "cons"); }));
    r.default_keyword = permanent(scm_from_utf8_keyword("default"));
    r.exception_key = permanent(symbol("%exception"));
    ListBuilder keys;
    for (int i = 0; i < static_cast<int>(Kind::count_); ++i) {
        const char* suffix = kind_suffix(static_cast<Kind>(i));
        const std::string name = suffix[0] == '-' ? std::string(prefix) + suffix : std::string(suffix);
        r.keys[i] = permanent(symbol(name.c_str()));
        if (suffix[0] == '-') keys.push(r.keys[i]);
    }
    r.error_keys = permanent(keys.head);
    define("%construct", 1, 1, reinterpret_cast<scm_t_subr>(&construct_subr));
    define("%close!", 1, 0, reinterpret_cast<scm_t_subr>(&close_subr));
    define("%closed?", 1, 0, reinterpret_cast<scm_t_subr>(&closed_subr));
    define("%owned?", 1, 0, reinterpret_cast<scm_t_subr>(&owned_subr));
    define("%copy", 1, 0, reinterpret_cast<scm_t_subr>(&copy_subr));
    define("%describe", 1, 0, reinterpret_cast<scm_t_subr>(&describe_subr));
    define("%stream", 1, 0, reinterpret_cast<scm_t_subr>(&stream_subr));
    define("%error-keys", 0, 0, reinterpret_cast<scm_t_subr>(&error_keys_subr));
    define("%call-native", 2, 0, reinterpret_cast<scm_t_subr>(&call_native_subr));
    r.make_callable = permanent(protect([]() {
        return scm_c_eval_string("(lambda (handle) (lambda args (%call-native handle args)))");
    }));
}
} } // ROSETTA_GG_MODULE::rosetta_gg
)cpp"};

// The Scheme runtime at the top of every generated module, after define-module. @PREFIX@ is the
// module's error prefix. Procedures are referenced through the core: and goops: prefixes, so
// generated definitions that shadow core names (apply, length, ...) cannot break it.
inline constexpr const char* guile_full_runtime_scheme = R"scm(
;;; ------------------------------------------------------------------ runtime
;; Procedures the extension defines (declared for the compiler; load-extension replaces them).
(define %construct #f) (define %close! #f) (define %closed? #f) (define %owned? #f) (define %copy #f)
(define %describe #f) (define %stream #f) (define %error-keys #f) (define %call-native #f)
;; Base class of every wrapped C++ object: slot 0 holds the native handle, slot 1 the object a
;; borrowed result keeps alive.
(goops:define-class <native-object> ()
  (%handle #:init-value #f)
  (%keepalive #:init-value #f))
(core:set-object-property! <native-object> 'documentation
  "Base class of every wrapped C++ object.

(make <class> arg ...) calls the C++ constructor with the positional ARGs; keyword
initargs that follow initialize Scheme slots of a Scheme subclass. (close! obj) destroys
the C++ object now; otherwise it is destroyed after the object is garbage collected.")

;; A documented generic function: an applicable struct whose procedure is the documented
;; procedure (so procedure-documentation finds the docstring) and which forwards define-method
;; to the GOOPS generic it calls.
(goops:define-class <documented-generic> (goops:<applicable-struct-with-setter>)
  (generic #:init-keyword #:generic)
  #:metaclass goops:<applicable-struct-with-setter-class>)
(goops:add-method! goops:add-method!
  (goops:method ((procedure <documented-generic>) (m goops:<method>))
    (goops:add-method! (goops:slot-ref procedure 'generic) m)))
(goops:add-method! core:write
  (goops:method ((procedure <documented-generic>) port)
    (core:format port "#<generic ~a>" (core:procedure-name procedure))))
(define (%no-setter . _args)
  (core:scm-error 'wrong-type-arg #f "no setter for this procedure" '() #f))
(define (%documented procedure generic setter)
  (goops:make <documented-generic> #:procedure procedure #:generic generic
              #:setter (core:or setter %no-setter)))
(define (%generic name)
  (goops:make goops:<generic> #:name name))

;; (make <class> positional ... #:keyword value ...): the positional arguments go to the C++
;; constructor, the keywords to the slots of Scheme subclasses.
(define (%split-initargs initargs)
  (core:let loop ((rest initargs) (positional '()))
    (core:if (core:and (core:pair? rest) (core:keyword? (core:car rest)) (core:not (core:eq? (core:car rest) #:default)))
             (core:values (core:reverse positional) rest)
             (core:if (core:pair? rest)
                      (loop (core:cdr rest) (core:cons (core:car rest) positional))
                      (core:values (core:reverse positional) '())))))
(goops:add-method! goops:initialize
  (goops:method ((self <native-object>) initargs)
    (core:call-with-values (lambda () (%split-initargs initargs))
      (lambda (positional keywords)
        (next-method self keywords)
        (core:apply %construct self positional)))))
(goops:add-method! goops:shallow-clone (goops:method ((self <native-object>)) (%copy self)))
(goops:add-method! core:write (goops:method ((self <native-object>) port) (core:display (%describe self) port)))
(goops:add-method! core:display
  (goops:method ((self <native-object>) port)
    (core:display (core:or (%stream self) (%describe self)) port)))

(define (close! object)
  "Destroy the C++ object of OBJECT now (when Scheme owns it).

Later calls on OBJECT raise @PREFIX@-runtime-error; closing twice does nothing."
  (%close! object))
(define (closed? object)
  "Return #t when the C++ object of OBJECT was closed, moved to C++ or never constructed."
  (%closed? object))
(define (alive? object)
  "Return #t while the C++ object of OBJECT is usable."
  (core:not (%closed? object)))
(define (owned? object)
  "Return #t when Scheme owns (and finally destroys) the C++ object of OBJECT."
  (%owned? object))

;; Errors: C++ exceptions are thrown to keys named @PREFIX@-invalid-argument, ... with Guile's
;; (SUBR MESSAGE IRRITANTS (C++-CLASS)) arguments, so catch, with-exception-handler,
;; exception-message and error? all work.
(define (%exception-key error)
  (core:cond ((core:symbol? error) error)
             ((exn:exception? error) (exn:exception-kind error))
             (core:else #f)))
(define (%exception-args error)
  (core:if (exn:exception? error) (exn:exception-args error) '()))
(define (@PREFIX@-error? error)
  "Return #t when ERROR (an exception object or a catch key) comes from a C++ exception."
  (core:if (core:memq (%exception-key error) (%error-keys)) #t #f))
(define (@PREFIX@-error-kind error)
  "Return the key of ERROR, such as @PREFIX@-invalid-argument, or #f."
  (%exception-key error))
(define (@PREFIX@-error-message error)
  "Return the message of ERROR (an exception object), or #f."
  (core:let ((args (%exception-args error)))
    (core:if (core:and (core:list? args) (core:>= (core:length args) 2) (core:string? (core:cadr args))) (core:cadr args) #f)))
(define (@PREFIX@-error-cpp-class error)
  "Return the C++ exception class of ERROR, such as \"std::invalid_argument\", or #f."
  (core:let ((args (%exception-args error)))
    (core:if (core:and (core:list? args) (core:= (core:length args) 4) (core:pair? (core:list-ref args 3)))
             (core:car (core:list-ref args 3))
             #f)))

;; Enumerations: symbols, with tables of (symbol . value).
(define (%enum->integer table enum value)
  (core:cond ((core:exact-integer? value) value)
             ((core:assq value table) => core:cdr)
             (core:else (core:scm-error '@PREFIX@-invalid-argument (core:symbol->string enum)
                                        "unknown enumerator ~s" (core:list value) #f))))
(define (%integer->enum table value)
  (core:let loop ((rest table))
    (core:cond ((core:null? rest) #f)
               ((core:eqv? (core:cdar rest) value) (core:caar rest))
               (core:else (loop (core:cdr rest))))))
)scm";
} // namespace rosetta::detail
