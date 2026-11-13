#pragma once

// Runtime for the JavaScript backend (--language javascript): emitted (as text) into every
// generated source, inside the module's namespace ROSETTA_JS_MODULE, so that split sources of
// one module share it and two modules never accept each other's objects.
//
// It is written against Node-API (node_api.h, version 8), the ABI-stable C API of Node.js, so
// one compiled addon loads into every Node.js release that provides that version.
//
// No JavaScript exception crosses a live C++ frame as a pending exception: conversions throw
// C++ exceptions, and boundary() throws the JavaScript error after the native frames are gone.
// JavaScript called from C++ (callbacks, overrides) that throws comes back as a C++ exception
// (Thrown) carrying the original error value, which boundary() rethrows unchanged.
namespace rosetta::detail {
inline constexpr const char* js_full_runtime_parts[] = {
R"cpp(
namespace ROSETTA_JS_MODULE { namespace rosetta_js {
// ---------------------------------------------------------------- errors
// A conversion or overload failure: thrown as a JavaScript TypeError.
struct TypeError : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] inline void fail(const std::string& message) { throw std::runtime_error(message); }
[[noreturn]] inline void type_fail(const std::string& message) { throw TypeError(message); }
// The argument being converted ("argument 'seed'"), named in conversion errors; null outside conversions.
inline const char*& converting() { static thread_local const char* label = nullptr; return label; }

// Whether the environment (one per main thread or worker) is still running, and its thread:
// JavaScript values kept by C++ are released and called only while it is, and only on it.
struct EnvLife { napi_env env = nullptr; bool alive = true; std::thread::id thread; };

// A JavaScript exception that passed through C++: its value, kept until it is rethrown.
struct Kept;
struct Thrown : std::runtime_error {
    std::shared_ptr<Kept> value;
    Thrown(std::shared_ptr<Kept> kept, const std::string& message) : std::runtime_error(message), value(std::move(kept)) {}
};
[[noreturn]] void rethrow_pending(napi_env env);
// Every Node-API call goes through check: a pending JavaScript exception becomes Thrown.
inline void check(napi_env env, napi_status status) {
    if (status == napi_ok) return;
    if (status == napi_pending_exception) rethrow_pending(env);
    const napi_extended_error_info* info = nullptr;
    napi_get_last_error_info(env, &info);
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (pending) rethrow_pending(env);
    fail(std::string("Node-API call failed: ") + (info && info->error_message ? info->error_message : "unknown error"));
}
inline napi_value undefined(napi_env env) { napi_value v; check(env, napi_get_undefined(env, &v)); return v; }
inline napi_value null(napi_env env) { napi_value v; check(env, napi_get_null(env, &v)); return v; }
inline napi_valuetype type_of(napi_env env, napi_value value) {
    napi_valuetype type = napi_undefined;
    if (value) check(env, napi_typeof(env, value, &type));
    return type;
}
inline bool is_nullish(napi_env env, napi_value value) {
    const auto type = type_of(env, value);
    return type == napi_undefined || type == napi_null;
}
inline bool is_array(napi_env env, napi_value value) { bool result = false; check(env, napi_is_array(env, value, &result)); return result; }
inline napi_value string_value(napi_env env, const std::string& text) {
    napi_value v; check(env, napi_create_string_utf8(env, text.data(), text.size(), &v)); return v;
}
inline std::string text(napi_env env, napi_value value) {
    std::size_t size = 0;
    check(env, napi_get_value_string_utf8(env, value, nullptr, 0, &size));
    std::string result(size, '\0');
    if (size) check(env, napi_get_value_string_utf8(env, value, result.data(), size + 1, &size));
    return result;
}
inline napi_value get(napi_env env, napi_value object, const char* name) {
    napi_value v; check(env, napi_get_named_property(env, object, name, &v)); return v;
}
inline void set(napi_env env, napi_value object, const char* name, napi_value value) {
    check(env, napi_set_named_property(env, object, name, value));
}
inline napi_value global(napi_env env, const char* name) {
    napi_value g; check(env, napi_get_global(env, &g)); return get(env, g, name);
}
inline napi_value call_function(napi_env env, napi_value receiver, napi_value function, std::size_t count, const napi_value* arguments) {
    napi_value result = nullptr;
    check(env, napi_call_function(env, receiver, function, count, arguments, &result));
    return result;
}
inline std::uint32_t array_length(napi_env env, napi_value array) {
    std::uint32_t n = 0; check(env, napi_get_array_length(env, array, &n)); return n;
}
inline napi_value element(napi_env env, napi_value array, std::uint32_t i) {
    napi_value v; check(env, napi_get_element(env, array, i, &v)); return v;
}

// A JavaScript value kept alive by C++ (a strong reference).
struct Kept {
    std::shared_ptr<EnvLife> life;
    napi_ref ref = nullptr;
    Kept(napi_env env, napi_value value, std::shared_ptr<EnvLife> state) : life(std::move(state)) {
        check(env, napi_create_reference(env, value, 1, &ref));
    }
    ~Kept() {
        if (ref && life->alive && std::this_thread::get_id() == life->thread) napi_delete_reference(life->env, ref);
    }
    Kept(const Kept&) = delete;
    Kept& operator=(const Kept&) = delete;
    napi_value value() const {
        napi_value v = nullptr;
        check(life->env, napi_get_reference_value(life->env, ref, &v));
        return v;
    }
    // The environment to call on: only its own thread may run JavaScript.
    napi_env env() const {
        if (!life->alive) fail("the JavaScript environment of a callback has shut down");
        if (std::this_thread::get_id() != life->thread) fail("C++ called a JavaScript function from a thread other than the JavaScript thread");
        return life->env;
    }
};
std::shared_ptr<EnvLife> life(napi_env env);
inline std::string error_message(napi_env env, napi_value error) {
    napi_value message = nullptr;
    bool is_error = false;
    napi_is_error(env, error, &is_error);
    if (is_error && napi_get_named_property(env, error, "message", &message) == napi_ok && type_of(env, message) == napi_string)
        return text(env, message);
    napi_value coerced = nullptr;
    if (napi_coerce_to_string(env, error, &coerced) == napi_ok) return text(env, coerced);
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (pending) { napi_value ignored; napi_get_and_clear_last_exception(env, &ignored); }
    return "JavaScript exception";
}
[[noreturn]] inline void rethrow_pending(napi_env env) {
    napi_value error = nullptr;
    if (napi_get_and_clear_last_exception(env, &error) != napi_ok || !error) fail("JavaScript exception");
    const auto message = error_message(env, error);
    throw Thrown(std::make_shared<Kept>(env, error, life(env)), message);
}

// Opens a handle scope for C++ code that creates JavaScript values outside a native call's
// own scope (callbacks invoked repeatedly from one native call).
struct Scope {
    napi_env env;
    napi_handle_scope scope = nullptr;
    explicit Scope(napi_env e) : env(e) { check(env, napi_open_handle_scope(env, &scope)); }
    ~Scope() { if (scope) napi_close_handle_scope(env, scope); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

template<class F> napi_value boundary(napi_env env, F&& fn) {
    const char* outer_label = converting();
    converting() = nullptr;
    napi_value result = nullptr;
    const auto label = [](const std::exception& error) {
        return converting() ? std::string(converting()) + ": " + error.what() : std::string(error.what());
    };
    try { result = fn(); }
    catch (const Thrown& error) {
        // A JavaScript exception thrown inside a callback: rethrown as it was.
        napi_value value = nullptr;
        if (error.value && error.value->life->alive && napi_get_reference_value(env, error.value->ref, &value) == napi_ok && value)
            napi_throw(env, value);
        else napi_throw_error(env, nullptr, error.what());
    }
    catch (const TypeError& error) { napi_throw_type_error(env, nullptr, label(error).c_str()); }
    catch (const std::out_of_range& error) { napi_throw_range_error(env, nullptr, label(error).c_str()); }
    catch (const std::exception& error) { napi_throw_error(env, nullptr, label(error).c_str()); }
    catch (...) { napi_throw_error(env, nullptr, "unknown C++ exception"); }
    converting() = outer_label;
    return result;
}

// The arguments of a native call.
struct Call {
    napi_env env;
    napi_value self = nullptr;
    napi_value target = nullptr; // new.target (constructors)
    std::vector<napi_value> args;
    Call(napi_env e, napi_callback_info info, bool construct = false) : env(e) {
        std::size_t count = 8;
        args.resize(count);
        check(env, napi_get_cb_info(env, info, &count, args.data(), &self, nullptr));
        if (count > args.size()) {
            args.resize(count);
            check(env, napi_get_cb_info(env, info, &count, args.data(), &self, nullptr));
        }
        args.resize(count);
        if (construct) check(env, napi_get_new_target(env, info, &target));
    }
    int count() const { return static_cast<int>(args.size()); }
    napi_value arg(int i) const { return i < count() ? args[static_cast<std::size_t>(i)] : undefined(env); }
    // Arguments without trailing undefined (omitted arguments).
    int given() const {
        int n = count();
        while (n > 0 && type_of(env, args[static_cast<std::size_t>(n - 1)]) == napi_undefined) --n;
        return n;
    }
};

// ---------------------------------------------------------------- classes
struct TypeInfo;
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct TypeInfo {
    const char* name = nullptr; // JavaScript class name
    std::vector<BaseLink> bases;
    void (*destroy)(void*) = nullptr;
    std::size_t id = 0;         // index of the class in the per-environment tables
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;
inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }
inline std::size_t& class_count() { static std::size_t count = 0; return count; }
template<class T> void register_type(const char* name) {
    auto& info = Info<T>::value;
    info.name = name;
    info.id = class_count()++;
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* p) { delete static_cast<T*>(p); };
    registry()[std::type_index(typeid(T))] = &info;
}
template<class Derived, class Base> void add_base() {
    Info<Derived>::value.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* {
        return static_cast<Base*>(static_cast<Derived*>(ptr));
    }});
}

// Per-environment state: class constructors and prototypes (by TypeInfo::id), helpers.
struct EnvState {
    std::shared_ptr<EnvLife> life;
    std::vector<napi_ref> constructors, prototypes;
    napi_ref object_create = nullptr, set_prototype_of = nullptr, ref_class = nullptr, map_class = nullptr, array_from = nullptr;
};
inline EnvState& state(napi_env env) {
    void* data = nullptr;
    check(env, napi_get_instance_data(env, &data));
    if (!data) fail("native module is not initialized in this JavaScript environment");
    return *static_cast<EnvState*>(data);
}
inline std::shared_ptr<EnvLife> life(napi_env env) { return state(env).life; }
inline napi_value deref(napi_env env, napi_ref ref) {
    napi_value v = nullptr;
    check(env, napi_get_reference_value(env, ref, &v));
    return v;
}

// Type tags tell this module's objects from every other object.
inline const napi_type_tag& object_tag() { static const napi_type_tag tag{ROSETTA_JS_TAG, 0x6f626a6563740001ULL}; return tag; }
inline const napi_type_tag& ref_tag() { static const napi_type_tag tag{ROSETTA_JS_TAG, 0x7265666365000002ULL}; return tag; }
inline const napi_type_tag& callable_tag() { static const napi_type_tag tag{ROSETTA_JS_TAG, 0x63616c6c00000003ULL}; return tag; }
inline bool tagged(napi_env env, napi_value value, const napi_type_tag& tag) {
    const auto type = type_of(env, value);
    if (type != napi_object && type != napi_function) return false;
    bool result = false;
    check(env, napi_check_object_type_tag(env, value, &tag, &result));
    return result;
}

struct Director;
// The native side of a JavaScript object (napi_wrap'd); parent keeps the object a borrowed
// result lives in alive.
struct Holder {
    void* ptr = nullptr;
    TypeInfo* info = nullptr;
    bool owned = false;
    std::shared_ptr<void>* shared = nullptr;
    bool director = false; // a JavaScript subclass instance implemented through a generated director
    napi_ref parent = nullptr;
};
inline void release(Holder* holder) noexcept {
    if (!holder) return;
    if (holder->shared) { delete holder->shared; holder->shared = nullptr; }
    else if (holder->owned && holder->ptr && holder->info && holder->info->destroy) holder->info->destroy(holder->ptr);
    holder->ptr = nullptr;
    holder->owned = false;
}
inline void finalize_holder(napi_env env, void* data, void*) {
    auto* holder = static_cast<Holder*>(data);
    release(holder);
    if (holder->parent) napi_delete_reference(env, holder->parent);
    delete holder;
}
inline Holder* holder_at(napi_env env, napi_value value) {
    if (!value || !tagged(env, value, object_tag())) return nullptr;
    void* data = nullptr;
    if (napi_unwrap(env, value, &data) != napi_ok) return nullptr;
    return static_cast<Holder*>(data);
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
inline std::string type_name(napi_env env, napi_value value) {
    if (auto* holder = holder_at(env, value)) return holder->info && holder->info->name ? holder->info->name : "native object";
    switch (type_of(env, value)) {
    case napi_undefined: return "undefined";
    case napi_null: return "null";
    case napi_boolean: return "boolean";
    case napi_number: return "number";
    case napi_string: return "string";
    case napi_symbol: return "symbol";
    case napi_function: return "function";
    case napi_bigint: return "bigint";
    case napi_external: return "external";
    default: break;
    }
    if (is_array(env, value)) return "Array";
    bool typed = false;
    napi_is_typedarray(env, value, &typed);
    if (typed) return "TypedArray";
    return "object";
}
template<class T> bool is_instance(napi_env env, napi_value value) {
    auto* holder = holder_at(env, value);
    return holder && holder->info && derives(holder->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> T* native(napi_env env, napi_value value) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    auto* holder = holder_at(env, value);
    if (!holder) type_fail(std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + type_name(env, value));
    if (!holder->ptr) fail("native object is closed");
    void* result = cast_path(holder->ptr, holder->info, info);
    if (!result) type_fail(std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + (holder->info && holder->info->name ? holder->info->name : "a native object"));
    return static_cast<U*>(result);
}
// Wraps a JavaScript object (a constructor's `this`, or a new object) around a native pointer.
inline void attach(napi_env env, napi_value object, Holder* holder) {
    const napi_status status = napi_wrap(env, object, holder, finalize_holder, nullptr, nullptr);
    if (status != napi_ok) { release(holder); delete holder; check(env, status); }
    check(env, napi_type_tag_object(env, object, &object_tag()));
}
)cpp",
R"cpp(
// Base of generated director classes: C++ subclasses forwarding virtual calls to the methods
// of a JavaScript subclass. The JavaScript object owns its director (a weak reference back);
// once C++ takes ownership, the director keeps the JavaScript object alive (a strong one).
struct Director {
    std::shared_ptr<EnvLife> rosetta_life;
    napi_ref rosetta_self = nullptr;
    bool rosetta_strong = false;
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    virtual ~Director() {
        if (!rosetta_life || !rosetta_life->alive || !rosetta_self || std::this_thread::get_id() != rosetta_life->thread) return;
        napi_env env = rosetta_life->env;
        if (rosetta_strong) {
            // C++ deleted an object the JavaScript side still refers to: it reads as closed.
            napi_value object = nullptr;
            if (napi_get_reference_value(env, rosetta_self, &object) == napi_ok && object) {
                void* data = nullptr;
                if (napi_unwrap(env, object, &data) == napi_ok && data) {
                    auto* holder = static_cast<Holder*>(data);
                    holder->ptr = nullptr;
                    holder->owned = false;
                }
            }
        }
        napi_delete_reference(env, rosetta_self);
    }
    bool rosetta_has_self() const { return rosetta_life && rosetta_life->alive && rosetta_self; }
    // The JavaScript object implementing this director, or null when it is gone.
    napi_value rosetta_object(napi_env env) const {
        if (!rosetta_has_self()) return nullptr;
        napi_value object = nullptr;
        if (napi_get_reference_value(env, rosetta_self, &object) != napi_ok) return nullptr;
        return object;
    }
};
// A JavaScript override of a virtual method, looked up for one call: present when the object's
// method is not the native class's own.
struct Override {
    std::optional<Scope> scope;
    napi_env env = nullptr;
    napi_value self = nullptr, function = nullptr;
    Override(const Director* director, const char* name, TypeInfo* info) {
        if (!director->rosetta_has_self()) return;
        if (std::this_thread::get_id() != director->rosetta_life->thread)
            fail(std::string("C++ called the JavaScript override of ") + name + " from a thread other than the JavaScript thread");
        env = director->rosetta_life->env;
        scope.emplace(env);
        self = director->rosetta_object(env);
        if (!self) return;
        napi_value method = get(env, self, name);
        if (type_of(env, method) != napi_function) return;
        napi_value native_method = get(env, deref(env, state(env).prototypes.at(info->id)), name);
        bool same = false;
        check(env, napi_strict_equals(env, method, native_method, &same));
        if (!same) function = method;
    }
    explicit operator bool() const { return function != nullptr; }
};
inline bool is_director(napi_env env, napi_value value) {
    auto* holder = holder_at(env, value);
    return holder && holder->director;
}
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(std::string("pure virtual method ") + name + " is not implemented by the JavaScript subclass");
}

enum class Ownership { borrowed, owned };
// A new JavaScript object of the class `info` for ptr; keepalive (or null) stays reachable from it.
inline napi_value push_pointer(napi_env env, void* ptr, TypeInfo* info, Ownership ownership, napi_value keepalive) {
    auto& s = state(env);
    if (info->id >= s.prototypes.size() || !s.prototypes[info->id]) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail(std::string("class ") + (info->name ? info->name : "?") + " is not loaded in this JavaScript environment");
    }
    napi_value prototype = deref(env, s.prototypes[info->id]);
    napi_value object = call_function(env, undefined(env), deref(env, s.object_create), 1, &prototype);
    auto* holder = new Holder{ptr, info, ownership == Ownership::owned, nullptr, false, nullptr};
    if (keepalive) {
        const auto status = napi_create_reference(env, keepalive, 1, &holder->parent);
        if (status != napi_ok) { release(holder); delete holder; check(env, status); }
    }
    attach(env, object, holder);
    return object;
}
template<class T> napi_value wrap(napi_env env, T* value, Ownership ownership, napi_value keepalive) {
    using U = std::remove_cv_t<T>;
    if (!value) return null(env);
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value)) {
            if (napi_value object = director->rosetta_object(env)) {
                if (ownership == Ownership::owned) {
                    // C++ handed back ownership of a JavaScript-implemented object.
                    auto* self = const_cast<Director*>(director);
                    auto* holder = holder_at(env, object);
                    if (holder && holder->ptr && !holder->owned && self->rosetta_strong) {
                        holder->owned = true;
                        std::uint32_t count = 0;
                        check(env, napi_reference_unref(env, self->rosetta_self, &count));
                        self->rosetta_strong = false;
                    }
                }
                return object;
            }
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    if (!info->name) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail("C++ result type is not exported to JavaScript");
    }
    return push_pointer(env, ptr, info, ownership, keepalive);
}
template<class T> napi_value wrap_shared(napi_env env, std::shared_ptr<T> value) {
    if (!value) return null(env);
    using U = std::remove_cv_t<T>;
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value.get());
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()))
            if (napi_value object = director->rosetta_object(env)) return object;
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value.get()));
        }
    }
    if (!info->name) fail("C++ result type is not exported to JavaScript");
    napi_value object = push_pointer(env, ptr, info, Ownership::borrowed, nullptr);
    holder_at(env, object)->shared = new std::shared_ptr<void>(std::const_pointer_cast<U>(value), ptr);
    return object;
}
template<class T> std::shared_ptr<T> shared(napi_env env, napi_value value) {
    if (is_nullish(env, value)) return {};
    T* ptr = native<T>(env, value);
    auto* holder = holder_at(env, value);
    // A borrowed object (or a director, owned by its JavaScript object) stays alive through a reference.
    if ((holder->director && !holder->shared) || (!holder->shared && !holder->owned))
        return std::shared_ptr<T>(std::make_shared<Kept>(env, value, life(env)), ptr);
    if (!holder->shared) {
        auto* info = holder->info;
        holder->shared = new std::shared_ptr<void>(holder->ptr, [info](void* p) { if (info->destroy) info->destroy(p); });
        holder->owned = false;
    }
    return std::shared_ptr<T>(*holder->shared, ptr);
}
template<class T> std::unique_ptr<T> take(napi_env env, napi_value value) {
    if (is_nullish(env, value)) return {};
    T* ptr = native<T>(env, value);
    auto* holder = holder_at(env, value);
    if (holder->shared || !holder->owned) fail("cannot transfer ownership of a shared or borrowed native object");
    if (holder->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail("cannot transfer a derived object without a virtual destructor");
    holder->owned = false;
    bool kept = false;
    if constexpr (std::is_polymorphic_v<T>) {
        if (auto* director = dynamic_cast<Director*>(ptr); director && holder->director && director->rosetta_self) {
            // The C++ owner now keeps the JavaScript implementation alive; the object stays usable.
            std::uint32_t count = 0;
            check(env, napi_reference_ref(env, director->rosetta_self, &count));
            director->rosetta_strong = true;
            kept = true;
        }
    }
    if (!kept) holder->ptr = nullptr;
    return std::unique_ptr<T>(ptr);
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- values
template<class T, class = void> struct Value;

// Integers are JavaScript numbers holding whole values (or BigInts); results outside the
// safe integer range come back as BigInts.
inline constexpr double max_safe = 9007199254740991.0;
template<class T> bool fits(napi_env env, napi_value value) {
    const auto type = type_of(env, value);
    if (type == napi_number) {
        double n = 0;
        check(env, napi_get_value_double(env, value, &n));
        if (!std::isfinite(n) || std::trunc(n) != n) return false;
        const long double wide = n;
        return wide >= static_cast<long double>(std::numeric_limits<T>::lowest()) && wide <= static_cast<long double>(std::numeric_limits<T>::max());
    }
    if (type == napi_bigint) {
        bool lossless = false;
        if constexpr (std::is_unsigned_v<T>) {
            std::uint64_t n = 0;
            check(env, napi_get_value_bigint_uint64(env, value, &n, &lossless));
            return lossless && n <= static_cast<std::uint64_t>(std::numeric_limits<T>::max());
        } else {
            std::int64_t n = 0;
            check(env, napi_get_value_bigint_int64(env, value, &n, &lossless));
            return lossless && n >= static_cast<std::int64_t>(std::numeric_limits<T>::lowest()) && n <= static_cast<std::int64_t>(std::numeric_limits<T>::max());
        }
    }
    return false;
}
template<class T> napi_value push_number(napi_env env, T value) {
    napi_value result;
    if constexpr (std::is_integral_v<T>) {
        if constexpr (sizeof(T) >= 8) {
            if (static_cast<long double>(value) > max_safe || static_cast<long double>(value) < -max_safe) {
                if constexpr (std::is_unsigned_v<T>) check(env, napi_create_bigint_uint64(env, static_cast<std::uint64_t>(value), &result));
                else check(env, napi_create_bigint_int64(env, static_cast<std::int64_t>(value), &result));
                return result;
            }
        }
        if constexpr (std::is_unsigned_v<T>) check(env, napi_create_double(env, static_cast<double>(value), &result));
        else check(env, napi_create_int64(env, static_cast<std::int64_t>(value), &result));
    } else check(env, napi_create_double(env, static_cast<double>(value), &result));
    return result;
}
inline double number(napi_env env, napi_value value) { double n = 0; check(env, napi_get_value_double(env, value, &n)); return n; }

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool check(napi_env env, napi_value value, bool) { return type_of(env, value) == napi_boolean; }
    static bool load(napi_env env, napi_value value) {
        if (type_of(env, value) != napi_boolean) type_fail("expected a boolean, got " + type_name(env, value));
        bool result = false;
        rosetta_js::check(env, napi_get_value_bool(env, value, &result));
        return result;
    }
    static napi_value push(napi_env env, bool value) { napi_value v; rosetta_js::check(env, napi_get_boolean(env, value, &v)); return v; }
};
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(napi_env env, napi_value value, bool) { return fits<T>(env, value); }
    static T load(napi_env env, napi_value value) {
        const auto type = type_of(env, value);
        if (type != napi_number && type != napi_bigint) type_fail("expected an integer, got " + type_name(env, value));
        if (!fits<T>(env, value)) {
            if (type == napi_number && std::trunc(number(env, value)) != number(env, value)) type_fail("expected an integer, got a number with a fraction");
            throw std::out_of_range("integer outside C++ type range");
        }
        if (type == napi_bigint) {
            bool lossless = false;
            if constexpr (std::is_unsigned_v<T>) { std::uint64_t n = 0; rosetta_js::check(env, napi_get_value_bigint_uint64(env, value, &n, &lossless)); return static_cast<T>(n); }
            else { std::int64_t n = 0; rosetta_js::check(env, napi_get_value_bigint_int64(env, value, &n, &lossless)); return static_cast<T>(n); }
        }
        return static_cast<T>(number(env, value));
    }
    static napi_value push(napi_env env, T value) { return push_number(env, value); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(napi_env env, napi_value value, bool) { return type_of(env, value) == napi_number; }
    static T load(napi_env env, napi_value value) {
        if (type_of(env, value) != napi_number) type_fail("expected a number, got " + type_name(env, value));
        const double n = number(env, value);
        if (std::isfinite(n) && std::abs(n) > static_cast<double>(std::numeric_limits<T>::max())) throw std::out_of_range("number outside C++ type range");
        return static_cast<T>(n);
    }
    static napi_value push(napi_env env, T value) { return push_number(env, value); }
};
template<> struct Value<char> {
    static bool check(napi_env env, napi_value value, bool strict) {
        if (type_of(env, value) == napi_string) return text(env, value).size() == 1;
        return !strict && Value<int>::check(env, value, false);
    }
    static char load(napi_env env, napi_value value) {
        if (type_of(env, value) == napi_string) {
            const auto s = text(env, value);
            if (s.size() != 1) type_fail("expected a single character");
            return s[0];
        }
        return static_cast<char>(Value<int>::load(env, value));
    }
    static napi_value push(napi_env env, char value) { return string_value(env, std::string(1, value)); }
};
template<> struct Value<std::string> {
    static bool check(napi_env env, napi_value value, bool) { return type_of(env, value) == napi_string; }
    static std::string load(napi_env env, napi_value value) {
        if (type_of(env, value) != napi_string) type_fail("expected a string, got " + type_name(env, value));
        return text(env, value);
    }
    static napi_value push(napi_env env, const std::string& value) { return string_value(env, value); }
};
template<> struct Value<std::string_view> {
    static bool check(napi_env env, napi_value value, bool strict) { return Value<std::string>::check(env, value, strict); }
    static napi_value push(napi_env env, std::string_view value) { return string_value(env, std::string(value)); }
};
template<> struct Value<const char*> {
    static napi_value push(napi_env env, const char* value) { return value ? string_value(env, value) : null(env); }
};
template<> struct Value<char*> {
    static napi_value push(napi_env env, const char* value) { return Value<const char*>::push(env, value); }
};
template<> struct Value<std::filesystem::path> {
    static bool check(napi_env env, napi_value value, bool) { return type_of(env, value) == napi_string; }
    static std::filesystem::path load(napi_env env, napi_value value) {
        const std::string utf8 = Value<std::string>::load(env, value);
#if defined(__cpp_char8_t)
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
        return std::filesystem::u8path(utf8);
#endif
    }
    static napi_value push(napi_env env, const std::filesystem::path& value) {
        const auto utf8 = value.u8string();
        return string_value(env, std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
    }
};
template<> struct Value<std::nullptr_t> {
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value); }
    static std::nullptr_t load(napi_env, napi_value) { return nullptr; }
    static napi_value push(napi_env env, std::nullptr_t) { return null(env); }
};
template<> struct Value<std::monostate> {
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value); }
    static std::monostate load(napi_env, napi_value) { return {}; }
    static napi_value push(napi_env env, std::monostate) { return null(env); }
};
// Complex numbers are [re, im] arrays (numbers are accepted on input).
template<class T> struct Value<std::complex<T>> {
    static bool check(napi_env env, napi_value value, bool strict) {
        if (is_array(env, value)) return array_length(env, value) == 2;
        return !strict && type_of(env, value) == napi_number;
    }
    static std::complex<T> load(napi_env env, napi_value value) {
        if (type_of(env, value) == napi_number) return {static_cast<T>(number(env, value)), T{}};
        if (!is_array(env, value) || array_length(env, value) != 2) type_fail("expected a number or an [re, im] array");
        return {Value<T>::load(env, element(env, value, 0)), Value<T>::load(env, element(env, value, 1))};
    }
    static napi_value push(napi_env env, const std::complex<T>& value) {
        napi_value array;
        rosetta_js::check(env, napi_create_array_with_length(env, 2, &array));
        rosetta_js::check(env, napi_set_element(env, array, 0, push_number(env, value.real())));
        rosetta_js::check(env, napi_set_element(env, array, 1, push_number(env, value.imag())));
        return array;
    }
};

// Enumerations cross as numbers; enumerator names (strings) are accepted on input.
template<class E> struct EnumTable { static inline std::vector<std::pair<std::string, long long>> entries; };
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    static bool check(napi_env env, napi_value value, bool strict) {
        if (type_of(env, value) == napi_string) {
            // Loosely any name matches, so that a sole overload reports the unknown enumerator.
            if (!strict) return true;
            const auto name = text(env, value);
            for (const auto& entry : EnumTable<T>::entries) if (entry.first == name) return true;
            return false;
        }
        if (!Value<U>::check(env, value, false)) return false;
        if (!strict || EnumTable<T>::entries.empty()) return true;
        const auto n = static_cast<long long>(Value<U>::load(env, value));
        for (const auto& entry : EnumTable<T>::entries) if (entry.second == n) return true;
        return false;
    }
    static T load(napi_env env, napi_value value) {
        if (type_of(env, value) == napi_string) {
            const auto name = text(env, value);
            for (const auto& entry : EnumTable<T>::entries) if (entry.first == name) return static_cast<T>(entry.second);
            std::string names;
            for (const auto& entry : EnumTable<T>::entries) names += (names.empty() ? "" : ", ") + entry.first;
            type_fail("unknown enumerator '" + name + "' (expected one of " + names + ")");
        }
        return static_cast<T>(Value<U>::load(env, value));
    }
    static napi_value push(napi_env env, T value) { return push_number(env, static_cast<U>(value)); }
};

// Exported classes by value: copies in, owned copies out.
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(napi_env env, napi_value value, bool) { return is_instance<T>(env, value); }
    static T load(napi_env env, napi_value value) {
        if constexpr (copyable_v<T>) return *native<T>(env, value);
        else { (void)env; (void)value; fail("C++ class is not copyable"); }
    }
    static napi_value push(napi_env env, T&& value) {
        if constexpr (std::is_move_constructible_v<T>) return wrap(env, new T(std::move(value)), Ownership::owned, nullptr);
        else { (void)value; fail("C++ class is not movable"); }
    }
    static napi_value push(napi_env env, const T& value) {
        if constexpr (copyable_v<T>) return wrap(env, new T(value), Ownership::owned, nullptr);
        else { (void)value; fail("C++ class is not copyable"); }
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value) || is_instance<T>(env, value); }
    static T* load(napi_env env, napi_value value) { return is_nullish(env, value) ? nullptr : native<T>(env, value); }
    static napi_value push(napi_env env, T* value) { return wrap(env, value, Ownership::borrowed, nullptr); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value) || is_instance<T>(env, value); }
    static std::shared_ptr<T> load(napi_env env, napi_value value) { return shared<T>(env, value); }
    static napi_value push(napi_env env, const std::shared_ptr<T>& value) { return wrap_shared(env, value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value) || is_instance<T>(env, value); }
    static std::unique_ptr<T, D> load(napi_env env, napi_value value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(env, value);
        else fail("unique_ptr with a custom deleter cannot take ownership");
    }
    static napi_value push(napi_env env, std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return wrap(env, value.release(), Ownership::owned, nullptr);
        else fail("unique_ptr with a custom deleter cannot be returned");
    }
};
template<class T> struct Value<std::optional<T>> {
    static bool check(napi_env env, napi_value value, bool strict) { return is_nullish(env, value) || Value<T>::check(env, value, strict); }
    static std::optional<T> load(napi_env env, napi_value value) {
        if (is_nullish(env, value)) return std::nullopt;
        return Value<T>::load(env, value);
    }
    static napi_value push(napi_env env, const std::optional<T>& value) { return value ? Value<T>::push(env, *value) : null(env); }
    static napi_value push(napi_env env, std::optional<T>&& value) { return value ? Value<T>::push(env, std::move(*value)) : null(env); }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static napi_value push(napi_env env, std::reference_wrapper<T> value) {
        using U = std::remove_cv_t<T>;
        if constexpr (bound_v<U>) return wrap(env, const_cast<U*>(&value.get()), Ownership::borrowed, nullptr);
        else return Value<U>::push(env, value.get());
    }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arrays and maps
// Sequences are JavaScript arrays; numeric sequences also accept typed arrays (Float64Array,
// Int32Array, Uint8Array and Buffer, ...).
template<class E> inline constexpr bool numeric_v = std::is_arithmetic_v<E> && !std::is_same_v<E, bool> && !std::is_same_v<E, char>;
struct TypedView { napi_typedarray_type type = napi_int8_array; std::size_t length = 0; void* data = nullptr; };
inline bool typed_view(napi_env env, napi_value value, TypedView& view) {
    bool typed = false;
    check(env, napi_is_typedarray(env, value, &typed));
    if (!typed) return false;
    napi_value buffer;
    std::size_t offset = 0;
    check(env, napi_get_typedarray_info(env, value, &view.type, &view.length, &view.data, &buffer, &offset));
    return true;
}
template<class E> bool typed_fits(const TypedView& view) {
    if constexpr (std::is_floating_point_v<E>) return true;
    else return view.type != napi_float32_array && view.type != napi_float64_array;
}
template<class E> E typed_element(const TypedView& view, std::size_t i) {
    switch (view.type) {
    case napi_int8_array: return static_cast<E>(static_cast<const std::int8_t*>(view.data)[i]);
    case napi_uint8_array: case napi_uint8_clamped_array: return static_cast<E>(static_cast<const std::uint8_t*>(view.data)[i]);
    case napi_int16_array: return static_cast<E>(static_cast<const std::int16_t*>(view.data)[i]);
    case napi_uint16_array: return static_cast<E>(static_cast<const std::uint16_t*>(view.data)[i]);
    case napi_int32_array: return static_cast<E>(static_cast<const std::int32_t*>(view.data)[i]);
    case napi_uint32_array: return static_cast<E>(static_cast<const std::uint32_t*>(view.data)[i]);
    case napi_float32_array: return static_cast<E>(static_cast<const float*>(view.data)[i]);
    case napi_float64_array: return static_cast<E>(static_cast<const double*>(view.data)[i]);
    case napi_bigint64_array: return static_cast<E>(static_cast<const std::int64_t*>(view.data)[i]);
    case napi_biguint64_array: return static_cast<E>(static_cast<const std::uint64_t*>(view.data)[i]);
    default: fail("unsupported typed array");
    }
}
template<class E> bool check_elements(napi_env env, napi_value value, bool strict) {
    if constexpr (numeric_v<E>) {
        TypedView view;
        if (typed_view(env, value, view)) return typed_fits<E>(view);
    }
    if (!is_array(env, value)) return false;
    const auto n = array_length(env, value);
    for (std::uint32_t i = 0; i < n; ++i)
        if (!Value<E>::check(env, element(env, value, i), strict)) return false;
    return true;
}
template<class E, class Add> void load_elements(napi_env env, napi_value value, Add&& add) {
    if constexpr (numeric_v<E>) {
        TypedView view;
        if (typed_view(env, value, view)) {
            if (!typed_fits<E>(view)) type_fail("expected an integer typed array");
            for (std::size_t i = 0; i < view.length; ++i) add(typed_element<E>(view, i));
            return;
        }
    }
    if (!is_array(env, value)) type_fail("expected an array, got " + type_name(env, value));
    const auto n = array_length(env, value);
    for (std::uint32_t i = 0; i < n; ++i) add(Value<E>::load(env, element(env, value, i)));
}
template<class E, class Range> napi_value push_elements(napi_env env, const Range& range, std::size_t size) {
    napi_value array;
    check(env, napi_create_array_with_length(env, size, &array));
    std::uint32_t i = 0;
    for (const auto& item : range) check(env, napi_set_element(env, array, i++, Value<E>::push(env, item)));
    return array;
}
template<class C, class E> struct Sequence {
    static bool check(napi_env env, napi_value value, bool strict) { return check_elements<E>(env, value, strict); }
    static C load(napi_env env, napi_value value) {
        C result;
        if constexpr (std::is_same_v<C, std::vector<E>>) if (is_array(env, value)) result.reserve(array_length(env, value));
        load_elements<E>(env, value, [&](E&& item) {
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(std::move(item));
            else result.push_back(std::move(item));
        });
        return result;
    }
    static napi_value push(napi_env env, const C& value) { return push_elements<E>(env, value, value.size()); }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
template<class A> struct Value<std::vector<bool, A>> {
    static bool check(napi_env env, napi_value value, bool strict) { return check_elements<bool>(env, value, strict); }
    static std::vector<bool, A> load(napi_env env, napi_value value) {
        std::vector<bool, A> result;
        load_elements<bool>(env, value, [&](bool item) { result.push_back(item); });
        return result;
    }
    static napi_value push(napi_env env, const std::vector<bool, A>& value) {
        napi_value array;
        rosetta_js::check(env, napi_create_array_with_length(env, value.size(), &array));
        for (std::size_t i = 0; i < value.size(); ++i)
            rosetta_js::check(env, napi_set_element(env, array, static_cast<std::uint32_t>(i), Value<bool>::push(env, value[i])));
        return array;
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
template<class E> struct Value<std::set<E>> : Sequence<std::set<E>, E> {};
template<class E> struct Value<std::unordered_set<E>> : Sequence<std::unordered_set<E>, E> {};
template<class E> struct Value<std::valarray<E>> {
    static bool check(napi_env env, napi_value value, bool strict) { return check_elements<E>(env, value, strict); }
    static std::valarray<E> load(napi_env env, napi_value value) {
        const auto items = Value<std::vector<E>>::load(env, value);
        return std::valarray<E>(items.data(), items.size());
    }
    static napi_value push(napi_env env, const std::valarray<E>& value) {
        return push_elements<E>(env, std::vector<E>(std::begin(value), std::end(value)), value.size());
    }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(napi_env env, napi_value value, bool strict) {
        TypedView view;
        if constexpr (numeric_v<E>) if (typed_view(env, value, view)) return view.length == N && typed_fits<E>(view);
        return is_array(env, value) && array_length(env, value) == N && check_elements<E>(env, value, strict);
    }
    static std::array<E, N> load(napi_env env, napi_value value) {
        std::array<E, N> result{};
        std::size_t i = 0;
        load_elements<E>(env, value, [&](E&& item) {
            if (i >= N) type_fail("expected an array of " + std::to_string(N) + " elements");
            result[i++] = std::move(item);
        });
        if (i != N) type_fail("expected an array of " + std::to_string(N) + " elements");
        return result;
    }
    static napi_value push(napi_env env, const std::array<E, N>& value) { return push_elements<E>(env, value, N); }
};
// Maps with string keys are plain objects; other maps are JavaScript Maps. Both are accepted
// on input whatever the key type (object keys convert to numbers for numeric keys).
template<class K> inline constexpr bool string_key_v = std::is_same_v<K, std::string> || std::is_same_v<K, std::string_view>;
inline bool is_map(napi_env env, napi_value value) {
    if (type_of(env, value) != napi_object) return false;
    bool result = false;
    check(env, napi_instanceof(env, value, deref(env, state(env).map_class), &result));
    return result;
}
inline bool plain_object(napi_env env, napi_value value) {
    return type_of(env, value) == napi_object && !is_array(env, value) && !holder_at(env, value) && !tagged(env, value, ref_tag());
}
template<class K> napi_value object_key(napi_env env, napi_value key) {
    if constexpr (std::is_arithmetic_v<K> && !std::is_same_v<K, bool>) {
        napi_value converted;
        check(env, napi_coerce_to_number(env, key, &converted));
        return converted;
    } else return key;
}
// Calls visit(key, value) for each entry of a Map or a plain object; stops when it returns false.
template<class K, class F> bool each_entry(napi_env env, napi_value value, F&& visit) {
    if (is_map(env, value)) {
        napi_value entries = call_function(env, undefined(env), deref(env, state(env).array_from), 1, &value);
        const auto n = array_length(env, entries);
        for (std::uint32_t i = 0; i < n; ++i) {
            napi_value entry = element(env, entries, i);
            if (!visit(element(env, entry, 0), element(env, entry, 1))) return false;
        }
        return true;
    }
    napi_value keys;
    check(env, napi_get_property_names(env, value, &keys));
    const auto n = array_length(env, keys);
    for (std::uint32_t i = 0; i < n; ++i) {
        napi_value key = element(env, keys, i), item;
        check(env, napi_get_property(env, value, key, &item));
        if (!visit(object_key<K>(env, key), item)) return false;
    }
    return true;
}
template<class M, class K, class V> struct Mapping {
    static bool check(napi_env env, napi_value value, bool strict) {
        if (!is_map(env, value) && !plain_object(env, value)) return false;
        return each_entry<K>(env, value, [&](napi_value key, napi_value item) {
            return Value<K>::check(env, key, strict) && Value<V>::check(env, item, strict);
        });
    }
    static M load(napi_env env, napi_value value) {
        if (!is_map(env, value) && !plain_object(env, value)) type_fail("expected an object or a Map, got " + type_name(env, value));
        M result;
        each_entry<K>(env, value, [&](napi_value key, napi_value item) {
            K k = Value<K>::load(env, key);
            result.emplace(std::move(k), Value<V>::load(env, item));
            return true;
        });
        return result;
    }
    static napi_value push(napi_env env, const M& value) {
        if constexpr (string_key_v<K>) {
            napi_value object;
            rosetta_js::check(env, napi_create_object(env, &object));
            for (const auto& item : value) rosetta_js::check(env, napi_set_property(env, object, Value<K>::push(env, item.first), Value<V>::push(env, item.second)));
            return object;
        } else {
            napi_value map;
            rosetta_js::check(env, napi_new_instance(env, deref(env, state(env).map_class), 0, nullptr, &map));
            napi_value setter = get(env, map, "set");
            for (const auto& item : value) {
                napi_value pair[2] = {Value<K>::push(env, item.first), Value<V>::push(env, item.second)};
                call_function(env, map, setter, 2, pair);
            }
            return map;
        }
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};

// Pairs and tuples are arrays ([a, b]); pairs also accept {first: a, second: b}.
template<class... T> struct Product {
    static constexpr std::size_t N = sizeof...(T);
    template<std::size_t... I> static bool check_each(napi_env env, napi_value value, bool strict, std::index_sequence<I...>) {
        if (!is_array(env, value) || array_length(env, value) != N) return false;
        bool ok = true;
        ((ok = ok && Value<T>::check(env, element(env, value, static_cast<std::uint32_t>(I)), strict)), ...);
        return ok;
    }
    template<class Tuple, std::size_t... I> static Tuple load_each(napi_env env, napi_value value, std::index_sequence<I...>) {
        if (!is_array(env, value) || array_length(env, value) != N) type_fail("expected an array of " + std::to_string(N) + " elements");
        return Tuple(Value<T>::load(env, element(env, value, static_cast<std::uint32_t>(I)))...);
    }
    template<class Tuple, std::size_t... I> static napi_value push_each(napi_env env, const Tuple& value, std::index_sequence<I...>) {
        napi_value array;
        check(env, napi_create_array_with_length(env, N, &array));
        (check(env, napi_set_element(env, array, static_cast<std::uint32_t>(I), Value<T>::push(env, std::get<I>(value)))), ...);
        return array;
    }
};
template<class A, class B> struct Value<std::pair<A, B>> {
    using P = Product<A, B>;
    static bool named(napi_env env, napi_value value) {
        if (!plain_object(env, value)) return false;
        bool first = false, second = false;
        rosetta_js::check(env, napi_has_named_property(env, value, "first", &first));
        rosetta_js::check(env, napi_has_named_property(env, value, "second", &second));
        return first && second;
    }
    static bool check(napi_env env, napi_value value, bool strict) {
        if (named(env, value)) return Value<A>::check(env, get(env, value, "first"), strict) && Value<B>::check(env, get(env, value, "second"), strict);
        return P::check_each(env, value, strict, std::index_sequence_for<A, B>{});
    }
    static std::pair<A, B> load(napi_env env, napi_value value) {
        if (named(env, value)) return {Value<A>::load(env, get(env, value, "first")), Value<B>::load(env, get(env, value, "second"))};
        return P::template load_each<std::pair<A, B>>(env, value, std::index_sequence_for<A, B>{});
    }
    static napi_value push(napi_env env, const std::pair<A, B>& value) { return P::push_each(env, value, std::index_sequence_for<A, B>{}); }
};
template<class... T> struct Value<std::tuple<T...>> {
    using P = Product<T...>;
    static bool check(napi_env env, napi_value value, bool strict) { return P::check_each(env, value, strict, std::index_sequence_for<T...>{}); }
    static std::tuple<T...> load(napi_env env, napi_value value) { return P::template load_each<std::tuple<T...>>(env, value, std::index_sequence_for<T...>{}); }
    static napi_value push(napi_env env, const std::tuple<T...>& value) { return P::push_each(env, value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(napi_env env, napi_value value, bool strict) { return (Value<T>::check(env, value, strict) || ...); }
    static std::variant<T...> load(napi_env env, napi_value value) {
        std::optional<std::variant<T...>> result;
        for (const bool strict : {true, false}) {
            ((!result && Value<T>::check(env, value, strict) ? (void)result.emplace(std::in_place_type<T>, Value<T>::load(env, value)) : (void)0), ...);
            if (result) return std::move(*result);
        }
        type_fail("value matches no alternative of the C++ variant");
    }
    static napi_value push(napi_env env, const std::variant<T...>& value) {
        return std::visit([env](const auto& item) { return Value<std::remove_cv_t<std::remove_reference_t<decltype(item)>>>::push(env, item); }, value);
    }
};
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(napi_env env, napi_value value, bool) { return type_of(env, value) == napi_number; }
    static D load(napi_env env, napi_value value) { return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(env, value))); }
    static napi_value push(napi_env env, const D& value) { return push_number(env, std::chrono::duration<double>(value).count()); }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arguments
// A Ref cell (ref(value)): an object whose `value` a mutable reference parameter reads and updates.
inline bool is_ref(napi_env env, napi_value value) { return tagged(env, value, ref_tag()); }
// The value a parameter converts from: a Ref cell's value, or the argument.
inline napi_value source(napi_env env, napi_value value, bool out) {
    return out && is_ref(env, value) ? get(env, value, "value") : value;
}
template<class V, class = void> struct pushable : std::false_type {};
template<class V> struct pushable<V, std::void_t<decltype(Value<V>::push(std::declval<napi_env>(), std::declval<const V&>()))>> : std::true_type {};
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static constexpr bool out = std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>>;
    static bool check(napi_env env, napi_value value, bool strict) { return Value<V>::check(env, source(env, value, out), strict); }
    Param(napi_env env, napi_value value) : value_(Value<V>::load(env, source(env, value, out))) {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    // A mutable reference parameter passed a Ref cell stores the updated value back in it.
    void writeback(napi_env env, napi_value value) {
        if constexpr (out && pushable<V>::value) {
            if (is_ref(env, value)) set(env, value, "value", Value<V>::push(env, value_));
        } else { (void)env; (void)value; }
    }
private:
    V value_;
};
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(napi_env env, napi_value value, bool) { return is_instance<V>(env, value); }
    Param(napi_env env, napi_value value) : ptr_(native<V>(env, value)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value) || is_instance<V>(env, value); }
    Param(napi_env env, napi_value value) : ptr_(is_nullish(env, value) ? nullptr : native<V>(env, value)) {}
    P get() { return ptr_; }
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(napi_env env, napi_value value, bool strict) { return is_nullish(env, value) || Value<std::string>::check(env, value, strict); }
    Param(napi_env env, napi_value value) { if (!is_nullish(env, value)) text_ = Value<std::string>::load(env, value); }
    const char* get() { return text_ ? text_->c_str() : nullptr; }
private:
    std::optional<std::string> text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(napi_env env, napi_value value, bool strict) { return Value<std::string>::check(env, value, strict); }
    Param(napi_env env, napi_value value) : text_(Value<std::string>::load(env, value)) {}
    std::string_view get() { return text_; }
private:
    std::string text_;
};

[[noreturn]] inline void no_overload(const Call& call, const char* name, const char* signatures) {
    std::string received;
    for (int i = 0; i < call.count(); ++i) {
        napi_value value = call.arg(i);
        std::string type = type_name(call.env, value);
        if (type == "Array") type += "(" + std::to_string(array_length(call.env, value)) + ")";
        received += std::string(i ? ", " : "") + type;
    }
    type_fail(std::string(name) + "(): incompatible arguments (" + received + "); supported signatures:\n" + signatures);
}
[[noreturn]] inline void wrong_receiver(const char* expected, const char* method) {
    type_fail(std::string(method) + ": `this` is not a " + expected + " object; call it as object." + method + "(...)");
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
inline bool same_object(napi_env env, napi_value parent, void* ptr, TypeInfo* info) {
    auto* holder = parent ? holder_at(env, parent) : nullptr;
    return holder && holder->ptr && cast_path(holder->ptr, holder->info, info) == ptr;
}
// Converts a C++ result; parent (or null) is the object a borrowed result lives in.
template<class R> napi_value result(napi_env env, R&& value, napi_value parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) return null(env);
        if (same_object(env, parent, ptr, &Info<B>::value)) return parent;
        if (policy == Return::owned) return wrap(env, ptr, Ownership::owned, nullptr);
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) return wrap(env, new B(*ptr), Ownership::owned, nullptr);
            else fail("C++ result is not copyable");
        }
        return wrap(env, ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (same_object(env, parent, ptr, &Info<V>::value)) return parent;
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) return wrap(env, new V(value), Ownership::owned, nullptr);
            else fail("C++ result is not copyable");
        }
        return wrap(env, ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V>) {
        return Value<V>::push(env, std::move(value));
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::push(env, value);
    } else if constexpr (std::is_lvalue_reference_v<R>) {
        return Value<V>::push(env, static_cast<const V&>(value));
    } else {
        return Value<V>::push(env, std::move(value));
    }
}
template<class F> napi_value call(napi_env env, F&& function, napi_value parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); return undefined(env); }
    else return result<R>(env, function(), parent, policy);
}
template<class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail("field type is not assignable");
}

// ---------------------------------------------------------------- C++ -> JavaScript calls
template<class A> napi_value argument(napi_env env, A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) return wrap(env, const_cast<V*>(&value), Ownership::borrowed, nullptr);
        else return Value<V>::push(env, std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        return wrap(env, const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed, nullptr);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::push(env, value);
    } else {
        return Value<V>::push(env, value);
    }
}
template<class R> R returned(napi_env env, napi_value value) {
    if constexpr (std::is_void_v<R>) { (void)env; (void)value; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_lvalue_reference_v<R> && bound_v<V>) return *native<V>(env, value);
        else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(env, value);
        else {
            if (!Value<V>::check(env, value, false))
                type_fail("JavaScript callback returned " + type_name(env, value) + ", which does not convert to the C++ result type");
            return Value<V>::load(env, value);
        }
    }
}
template<class R, class... A> R invoke(napi_env env, napi_value receiver, napi_value function, A&&... arguments) {
    napi_value argv[sizeof...(A) + 1] = {argument<A>(env, std::forward<A>(arguments))..., nullptr};
    napi_value value = call_function(env, receiver, function, sizeof...(A), argv);
    return returned<R>(env, value);
}
template<class R, class... A> R invoke_js(const Kept& function, A&&... arguments) {
    napi_env env = function.env();
    Scope scope(env);
    return invoke<R>(env, undefined(env), function.value(), std::forward<A>(arguments)...);
}
// A director's override: `this` is the JavaScript object.
template<class R, class... A> R invoke_override(const Override& found, A&&... arguments) {
    return invoke<R>(found.env, found.self, found.function, std::forward<A>(arguments)...);
}
// A native std::function returned to JavaScript: a function wrapping it.
struct Callable {
    std::function<napi_value(const Call&)> call;
    std::shared_ptr<void> original; // the std::function, given back unchanged when JavaScript passes the function to C++
    std::type_index type;
};
template<class R, class... A, std::size_t... I>
napi_value invoke_native(const Call& call, const std::function<R(A...)>& function, std::index_sequence<I...>) {
    if (call.count() != static_cast<int>(sizeof...(A))) type_fail("wrong number of arguments for native callable");
    std::tuple<Param<A>...> params{Param<A>(call.env, call.arg(static_cast<int>(I)))...};
    return rosetta_js::call(call.env, [&]() -> decltype(auto) { return function(std::get<I>(params).get()...); }, nullptr, Return::copy);
}
inline napi_value callable_call(napi_env env, napi_callback_info info) {
    return boundary(env, [&] {
        Call call(env, info);
        void* data = nullptr;
        check(env, napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data));
        auto* callable = static_cast<Callable*>(data);
        if (!callable || !callable->call) fail("native callable is no longer available");
        return callable->call(call);
    });
}
template<class R, class... A> struct Value<std::function<R(A...)>> {
    using F = std::function<R(A...)>;
    static bool check(napi_env env, napi_value value, bool) { return is_nullish(env, value) || type_of(env, value) == napi_function; }
    static F load(napi_env env, napi_value value) {
        if (is_nullish(env, value)) return {};
        if (type_of(env, value) != napi_function) type_fail("expected a function, got " + type_name(env, value));
        if (tagged(env, value, callable_tag())) {
            void* data = nullptr;
            rosetta_js::check(env, napi_unwrap(env, value, &data));
            auto* callable = static_cast<Callable*>(data);
            if (callable && callable->type == std::type_index(typeid(F))) return *static_cast<F*>(callable->original.get());
        }
        auto target = std::make_shared<Kept>(env, value, life(env));
        return [target](A... arguments) -> R { return invoke_js<R, A...>(*target, std::forward<A>(arguments)...); };
    }
    static napi_value push(napi_env env, const F& value) {
        if (!value) return null(env);
        auto original = std::make_shared<F>(value);
        auto* callable = new Callable{[original](const Call& call) { return invoke_native(call, *original, std::index_sequence_for<A...>{}); },
                                      original, std::type_index(typeid(F))};
        napi_value function;
        const auto status = napi_create_function(env, "native", NAPI_AUTO_LENGTH, callable_call, callable, &function);
        if (status != napi_ok) { delete callable; rosetta_js::check(env, status); }
        rosetta_js::check(env, napi_wrap(env, function, callable, [](napi_env, void* data, void*) { delete static_cast<Callable*>(data); }, nullptr, nullptr));
        rosetta_js::check(env, napi_type_tag_object(env, function, &callable_tag()));
        return function;
    }
};
} } // ROSETTA_JS_MODULE::rosetta_js
)cpp",
R"cpp(
namespace ROSETTA_JS_MODULE { namespace rosetta_js {
// ---------------------------------------------------------------- module setup
// Whether a constructor call comes from a JavaScript subclass (`class Mine extends Base`).
inline bool subclass_call(const Call& call, TypeInfo* info) {
    if (type_of(call.env, call.target) == napi_undefined)
        type_fail(std::string("Class constructor ") + (info->name ? info->name : "") + " cannot be invoked without 'new'");
    bool same = false;
    check(call.env, napi_strict_equals(call.env, call.target, deref(call.env, state(call.env).constructors.at(info->id)), &same));
    return !same;
}
// A constructor's `this` takes ownership of a new native object (director: its JavaScript implementation).
template<class T> napi_value adopt(const Call& call, T* created, Director* director) {
    std::unique_ptr<T> guard(created);
    auto* holder = new Holder{created, &Info<T>::value, true, nullptr, director != nullptr, nullptr};
    guard.release();
    attach(call.env, call.self, holder);
    if (director) {
        director->rosetta_life = life(call.env);
        check(call.env, napi_create_reference(call.env, call.self, 0, &director->rosetta_self));
    }
    return call.self;
}
inline napi_value refuse(napi_env env, napi_callback_info info) {
    return boundary(env, [&]() -> napi_value {
        void* data = nullptr;
        check(env, napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data));
        type_fail(static_cast<const char*>(data));
    });
}
// object.close() (and object[Symbol.dispose]()): releases the native object now.
inline napi_value object_close(napi_env env, napi_callback_info info) {
    return boundary(env, [&] {
        Call call(env, info);
        if (auto* holder = holder_at(env, call.self)) {
            if (holder->director && !holder->owned && holder->ptr) holder->ptr = nullptr; // C++ owns it: only detach
            else release(holder);
        }
        return undefined(env);
    });
}
// The default toString(): "ClassName" (or "ClassName (closed)").
inline napi_value object_to_string(napi_env env, napi_callback_info info) {
    return boundary(env, [&] {
        Call call(env, info);
        auto* holder = holder_at(env, call.self);
        std::string name = holder && holder->info && holder->info->name ? holder->info->name : "object";
        if (holder && !holder->ptr) name += " (closed)";
        return string_value(env, "[object " + name + "]");
    });
}
// toString() and util.inspect for classes with a to_string() method: calls this.to_string()
// (so a JavaScript subclass's override applies), ignoring inspect's arguments.
inline napi_value to_string_method(napi_env env, napi_callback_info info) {
    return boundary(env, [&] {
        Call call(env, info);
        return call_function(env, call.self, get(env, call.self, "to_string"), 0, nullptr);
    });
}
inline napi_value ref_constructor(napi_env env, napi_callback_info info) {
    return boundary(env, [&] {
        Call call(env, info, true);
        if (type_of(env, call.target) == napi_undefined) type_fail("Class constructor Ref cannot be invoked without 'new'");
        set(env, call.self, "value", call.arg(0));
        check(env, napi_type_tag_object(env, call.self, &ref_tag()));
        return call.self;
    });
}
inline napi_value make_ref(napi_env env, napi_callback_info info) {
    return boundary(env, [&] {
        Call call(env, info);
        napi_value value = call.arg(0), cell;
        check(env, napi_new_instance(env, deref(env, state(env).ref_class), 1, &value, &cell));
        return cell;
    });
}
inline void finalize_state(napi_env, void* data, void*) {
    auto* s = static_cast<EnvState*>(data);
    s->life->alive = false;
    delete s;
}
// Per-environment setup: the state, its shutdown hook, and the helpers the runtime calls.
inline EnvState& open_env(napi_env env) {
    auto* s = new EnvState;
    s->life = std::make_shared<EnvLife>();
    s->life->env = env;
    s->life->thread = std::this_thread::get_id();
    s->constructors.assign(class_count(), nullptr);
    s->prototypes.assign(class_count(), nullptr);
    const auto status = napi_set_instance_data(env, s, finalize_state, nullptr);
    if (status != napi_ok) { delete s; check(env, status); }
    auto* hook = new std::shared_ptr<EnvLife>(s->life);
    check(env, napi_add_env_cleanup_hook(env, [](void* data) {
        auto* kept = static_cast<std::shared_ptr<EnvLife>*>(data);
        (*kept)->alive = false;
        delete kept;
    }, hook));
    napi_value object = global(env, "Object"), array = global(env, "Array");
    check(env, napi_create_reference(env, get(env, object, "create"), 1, &s->object_create));
    check(env, napi_create_reference(env, get(env, object, "setPrototypeOf"), 1, &s->set_prototype_of));
    check(env, napi_create_reference(env, global(env, "Map"), 1, &s->map_class));
    check(env, napi_create_reference(env, get(env, array, "from"), 1, &s->array_from));
    return *s;
}
inline void set_prototype_of(napi_env env, napi_value object, napi_value prototype) {
    napi_value arguments[2] = {object, prototype};
    call_function(env, undefined(env), deref(env, state(env).set_prototype_of), 2, arguments);
}
inline napi_value symbol(napi_env env, const char* name, bool registered) {
    if (registered) {
        napi_value key = string_value(env, name), function = get(env, global(env, "Symbol"), "for");
        return call_function(env, undefined(env), function, 1, &key);
    }
    return get(env, global(env, "Symbol"), name);
}
// An enumeration object: name -> value and value -> name (like a TypeScript enum), frozen.
inline napi_value enum_object(napi_env env, const std::vector<std::pair<std::string, napi_value>>& entries) {
    napi_value object;
    check(env, napi_create_object(env, &object));
    for (const auto& [name, value] : entries) {
        set(env, object, name.c_str(), value);
        napi_value key;
        check(env, napi_coerce_to_string(env, value, &key));
        bool taken = false;
        check(env, napi_has_property(env, object, key, &taken));
        if (!taken) check(env, napi_set_property(env, object, key, string_value(env, name)));
    }
    check(env, napi_object_freeze(env, object));
    return object;
}
// A class: static members on the constructor; instance members on the prototype through
// napi_define_properties, whose functions (unlike napi_define_class's) carry no V8 receiver
// signature, so base-class methods accept derived objects and objects made by Object.create.
inline napi_value define_class(napi_env env, const char* name, napi_callback constructor, void* data,
                               const std::vector<napi_property_descriptor>& properties) {
    std::vector<napi_property_descriptor> statics, instance;
    for (const auto& property : properties) ((property.attributes & napi_static) ? statics : instance).push_back(property);
    napi_value cls;
    check(env, napi_define_class(env, name, NAPI_AUTO_LENGTH, constructor, data, statics.size(), statics.data(), &cls));
    if (!instance.empty()) check(env, napi_define_properties(env, get(env, cls, "prototype"), instance.size(), instance.data()));
    return cls;
}
inline napi_property_descriptor method(const char* name, napi_callback callback, napi_property_attributes attributes = napi_default_method) {
    return {name, nullptr, callback, nullptr, nullptr, nullptr, attributes, nullptr};
}
inline napi_property_descriptor accessor(const char* name, napi_callback getter, napi_callback setter) {
    return {name, nullptr, nullptr, getter, setter, nullptr, static_cast<napi_property_attributes>(napi_enumerable | napi_configurable), nullptr};
}
} } // ROSETTA_JS_MODULE::rosetta_js
)cpp"};
} // namespace rosetta::detail
