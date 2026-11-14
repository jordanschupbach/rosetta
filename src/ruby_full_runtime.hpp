#pragma once

// Support code emitted into every generated MRI Ruby extension (--ruby-api full). It is split
// into several literals only to stay below compiler limits for one string literal. Everything
// is inline: a split extension repeats it in each source, and the per-module namespace
// ROSETTA_RB_MODULE keeps two extensions in one process apart.
//
// Ruby raises by longjmp, which would skip C++ destructors. Every Ruby API call that may raise
// or run Ruby code goes through protect(), which turns a pending Ruby exception (or other jump)
// into the C++ exception ruby_error. guard() is the outermost frame of every method: it catches
// C++ exceptions after all C++ objects are destroyed and only then raises in Ruby.
namespace rosetta::detail {
inline constexpr const char* ruby_full_runtime_parts[] = {
R"cpp(
namespace ROSETTA_RB_MODULE { namespace rosetta_rb {
// ---------------------------------------------------------------- pinning
// Ruby objects referenced from C++ (callbacks, director objects C++ owns, exceptions in flight)
// are marked through one keeper object; rb_gc_mark also pins them against compaction.
inline std::mutex& pin_mutex() { static std::mutex value; return value; }
inline std::unordered_map<VALUE, std::size_t>& pins() { static std::unordered_map<VALUE, std::size_t> value; return value; }
inline void pin(VALUE value) {
    if (RB_SPECIAL_CONST_P(value)) return;
    std::lock_guard<std::mutex> lock(pin_mutex());
    ++pins()[value];
}
inline void unpin(VALUE value) noexcept {
    if (RB_SPECIAL_CONST_P(value)) return;
    std::lock_guard<std::mutex> lock(pin_mutex());
    auto found = pins().find(value);
    if (found != pins().end() && --found->second == 0) pins().erase(found);
}
inline void mark_pins(void*) {
    std::lock_guard<std::mutex> lock(pin_mutex());
    for (const auto& item : pins()) rb_gc_mark(item.first);
}
inline const rb_data_type_t keeper_type = {"rosetta pins", {mark_pins, nullptr, nullptr, nullptr, {nullptr}}, nullptr, nullptr, 0};
// A Ruby object kept alive by C++; copies share one pin.
class Pinned {
public:
    explicit Pinned(VALUE value) : value_(value) { pin(value_); }
    Pinned(const Pinned&) = delete;
    Pinned& operator=(const Pinned&) = delete;
    ~Pinned() { unpin(value_); }
    VALUE get() const noexcept { return value_; }
private:
    VALUE value_;
};
class SharedRef {
public:
    SharedRef() = default;
    explicit SharedRef(VALUE value) : pinned_(std::make_shared<Pinned>(value)) {}
    VALUE get() const noexcept { return pinned_ ? pinned_->get() : Qnil; }
private:
    std::shared_ptr<Pinned> pinned_;
};

// ---------------------------------------------------------------- errors
// A Ruby exception (or another non-local exit) raised by Ruby code that C++ called.
struct ruby_error {
    int state = 0;
    std::shared_ptr<Pinned> exception; // null for throw/break, which stay in rb_errinfo()
};
// An error raised at the Ruby boundary with this class and message.
struct native_error { VALUE klass; std::string message; };
[[noreturn]] inline void fail(VALUE klass, const std::string& message) { throw native_error{klass, message}; }

inline bool is_exception(VALUE value) {
    return !RB_SPECIAL_CONST_P(value) && RB_TYPE_P(value, T_OBJECT) && RTEST(rb_obj_is_kind_of(value, rb_eException));
}
template<class F> VALUE protect_trampoline(VALUE data) { return (*reinterpret_cast<F*>(data))(); }
// Runs fn (Ruby C API calls only: it must not throw) and converts a Ruby exception into ruby_error.
template<class F> VALUE protect(F fn) {
    int state = 0;
    const VALUE result = rb_protect(&protect_trampoline<F>, reinterpret_cast<VALUE>(&fn), &state);
    if (state) {
        const VALUE error = rb_errinfo();
        ruby_error pending;
        pending.state = state;
        if (is_exception(error)) {
            pending.exception = std::make_shared<Pinned>(error);
            rb_set_errinfo(Qnil);
        }
        throw pending;
    }
    return result;
}

// Exception classes of the generated module: Error is a module every C++-originated exception
// includes, so `rescue Module::Error` catches them all while `rescue ArgumentError` still works.
struct Errors {
    VALUE error = Qnil, argument = Qnil, index = Qnil, key = Qnil, range = Qnil, type = Qnil,
          not_implemented = Qnil, runtime = Qnil, zero_division = Qnil, io = Qnil, frozen = Qnil;
};
inline Errors& errors() { static Errors value; return value; }

inline std::string demangle(const char* name) {
    int status = 0;
    char* text = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = status == 0 && text ? text : name;
    std::free(text);
    for (const char* inline_namespace : {"__cxx11::", "__1::"})
        for (std::size_t i = 0; (i = result.find(inline_namespace, i)) != std::string::npos;) result.erase(i, std::string(inline_namespace).size());
    return result;
}
// Builds an exception object without raising: on failure the pending exception is used instead.
inline VALUE make_exception(VALUE klass, const std::string& message, const std::string& cpp_class) noexcept {
    struct Data { VALUE klass; const std::string* message; const std::string* cpp_class; } data{klass, &message, &cpp_class};
    int state = 0;
    VALUE result = rb_protect([](VALUE raw) -> VALUE {
        auto* d = reinterpret_cast<Data*>(raw);
        VALUE text = rb_utf8_str_new(d->message->data(), static_cast<long>(d->message->size()));
        VALUE exception = rb_exc_new_str(d->klass, text);
        if (!d->cpp_class->empty())
            rb_ivar_set(exception, rb_intern("@cpp_class"), rb_utf8_str_new(d->cpp_class->data(), static_cast<long>(d->cpp_class->size())));
        return exception;
    }, reinterpret_cast<VALUE>(&data), &state);
    if (state) { result = rb_errinfo(); rb_set_errinfo(Qnil); }
    return result;
}
inline VALUE or_default(VALUE klass, VALUE fallback) { return NIL_P(klass) ? fallback : klass; }
// Translates the in-flight C++ exception (inside a catch block).
inline VALUE translate_current() noexcept {
    try { throw; }
    catch (const std::bad_alloc& e) { return make_exception(rb_eNoMemError, "native allocation failed", demangle(typeid(e).name())); }
    catch (const std::invalid_argument& e) { return make_exception(or_default(errors().argument, rb_eArgError), e.what(), demangle(typeid(e).name())); }
    catch (const std::domain_error& e) { return make_exception(or_default(errors().argument, rb_eArgError), e.what(), demangle(typeid(e).name())); }
    catch (const std::length_error& e) { return make_exception(or_default(errors().argument, rb_eArgError), e.what(), demangle(typeid(e).name())); }
    catch (const std::out_of_range& e) { return make_exception(or_default(errors().index, rb_eIndexError), e.what(), demangle(typeid(e).name())); }
    catch (const std::overflow_error& e) { return make_exception(or_default(errors().range, rb_eRangeError), e.what(), demangle(typeid(e).name())); }
    catch (const std::underflow_error& e) { return make_exception(or_default(errors().range, rb_eRangeError), e.what(), demangle(typeid(e).name())); }
    catch (const std::range_error& e) { return make_exception(or_default(errors().range, rb_eRangeError), e.what(), demangle(typeid(e).name())); }
    catch (const std::filesystem::filesystem_error& e) { return make_exception(or_default(errors().io, rb_eIOError), e.what(), demangle(typeid(e).name())); }
    catch (const std::ios_base::failure& e) { return make_exception(or_default(errors().io, rb_eIOError), e.what(), demangle(typeid(e).name())); }
    catch (const std::bad_cast& e) { return make_exception(or_default(errors().type, rb_eTypeError), e.what(), demangle(typeid(e).name())); }
    catch (const std::bad_optional_access& e) { return make_exception(or_default(errors().type, rb_eTypeError), e.what(), demangle(typeid(e).name())); }
    catch (const std::bad_variant_access& e) { return make_exception(or_default(errors().type, rb_eTypeError), e.what(), demangle(typeid(e).name())); }
    catch (const std::bad_function_call& e) { return make_exception(or_default(errors().runtime, rb_eRuntimeError), "empty native callable", demangle(typeid(e).name())); }
    catch (const std::exception& e) { return make_exception(or_default(errors().runtime, rb_eRuntimeError), e.what(), demangle(typeid(e).name())); }
    catch (...) { return make_exception(or_default(errors().runtime, rb_eRuntimeError), "unknown C++ exception", ""); }
}
// The outermost frame of every native method: C++ exceptions become Ruby exceptions once every
// C++ object of the call is destroyed.
// Ruby and C++ buffer standard output separately (fully, when it is not a terminal): guard()
// flushes Ruby's $stdout before C++ runs and C++'s stdout after, so their output stays in order.
inline void flush_ruby_stdout() {
    if (RB_TYPE_P(rb_stdout, T_FILE)) protect([&]() { return rb_io_flush(rb_stdout); });
}
inline void flush_native_stdout() noexcept {
    std::cout.flush();
    std::fflush(stdout);
}
template<class F> VALUE guard(F&& fn) {
    int state = 0;
    volatile VALUE exception = Qnil;
    try {
        flush_ruby_stdout();
        const VALUE result = fn();
        flush_native_stdout();
        return result;
    }
    catch (const ruby_error& e) { state = e.state; exception = e.exception ? e.exception->get() : Qnil; }
    catch (const native_error& e) { exception = make_exception(e.klass, e.message, ""); }
    catch (...) { exception = translate_current(); }
    flush_native_stdout();
    if (NIL_P(exception)) rb_jump_tag(state ? state : 6 /* TAG_RAISE */);
    rb_exc_raise(exception);
    return Qnil;
}

inline std::string class_name(VALUE value) {
    const VALUE klass = rb_obj_class(value);
    const char* name = rb_class2name(klass);
    return name ? name : "Object";
}
inline VALUE symbol(const char* name) { return ID2SYM(rb_intern(name)); }
inline VALUE call_method(VALUE receiver, const char* name, int argc = 0, const VALUE* argv = nullptr) {
    const ID id = rb_intern(name);
    return protect([&]() { return rb_funcallv(receiver, id, argc, argv); });
}
inline bool responds(VALUE value, const char* name) {
    const ID id = rb_intern(name);
    return RTEST(protect([&]() -> VALUE { return rb_respond_to(value, id) ? Qtrue : Qfalse; }));
}
inline VALUE utf8(const std::string& text) {
    if (text.size() > static_cast<std::size_t>(LONG_MAX)) fail(rb_eRangeError, "string too long for Ruby");
    return protect([&]() { return rb_utf8_str_new(text.data(), static_cast<long>(text.size())); });
}
inline void writable(VALUE self) {
    if (RB_OBJ_FROZEN(self)) fail(or_default(errors().frozen, rb_eFrozenError), "can't modify frozen " + class_name(self));
}

// ---------------------------------------------------------------- classes
struct TypeInfo;
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct TypeInfo {
    const char* name = nullptr;
    VALUE klass = Qnil;
    std::vector<BaseLink> bases;
    void (*destroy)(void*) = nullptr;
    void* (*copy)(const void*) = nullptr;
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
// Specialized by generated code for every exported class.
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;
inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }

// Every exported object's data: zero-initialized by Ruby, so plain members only.
struct Instance {
    void* ptr;
    TypeInfo* info;
    std::shared_ptr<void>* holder;
    VALUE keepalive;
    unsigned char owned;
    unsigned char director;
};
inline void release(Instance* object) noexcept {
    void* ptr = object->ptr;
    auto* holder = object->holder;
    const bool owned = object->owned;
    TypeInfo* info = object->info;
    object->ptr = nullptr;
    object->holder = nullptr;
    object->owned = 0;
    if (holder) delete holder;
    else if (owned && ptr && info && info->destroy) info->destroy(ptr);
}
inline void instance_mark(void* data) { rb_gc_mark(static_cast<Instance*>(data)->keepalive); }
inline void instance_free(void* data) {
    auto* object = static_cast<Instance*>(data);
    release(object);
    ruby_xfree(object);
}
inline size_t instance_size(const void*) { return sizeof(Instance); }
inline const rb_data_type_t instance_type = {"rosetta native object", {instance_mark, instance_free, instance_size, nullptr, {nullptr}},
    nullptr, nullptr, RUBY_TYPED_FREE_IMMEDIATELY};
inline VALUE& root_class() { static VALUE value = Qnil; return value; }
inline VALUE& callable_class() { static VALUE value = Qnil; return value; }
inline bool is_instance_object(VALUE value) {
    return !RB_SPECIAL_CONST_P(value) && RB_TYPE_P(value, T_DATA) && RTYPEDDATA_P(value) && RTYPEDDATA_TYPE(value) == &instance_type;
}
inline Instance* instance(VALUE value) { return static_cast<Instance*>(RTYPEDDATA_DATA(value)); }
inline VALUE allocate(VALUE klass) { return rb_data_typed_object_zalloc(klass, sizeof(Instance), &instance_type); }
inline VALUE new_object(VALUE klass) { return protect([&]() { return allocate(klass); }); }

// Base of generated director classes: C++ subclasses forwarding virtual calls to Ruby. When C++
// takes ownership of one, it keeps its Ruby object alive until the C++ object is deleted.
struct Director {
    VALUE rosetta_self = Qnil;
    bool rosetta_owns_self = false;
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    virtual ~Director() {
        if (rosetta_owns_self && !NIL_P(rosetta_self)) {
            instance(rosetta_self)->ptr = nullptr;
            instance(rosetta_self)->owned = 0;
            unpin(rosetta_self);
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
template<class T> bool is_instance(VALUE value) {
    if (!is_instance_object(value)) return false;
    auto* object = instance(value);
    return object->info && derives(object->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> T* native(VALUE value) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    const auto expected = [&] { return std::string("expected ") + (info->name ? info->name : "native object") + ", got " + class_name(value); };
    if (!is_instance_object(value)) fail(rb_eTypeError, expected());
    auto* object = instance(value);
    if (!object->ptr) fail(rb_eRuntimeError, "native " + class_name(value) + " object is closed, moved or uninitialized");
    void* result = object->info ? cast_path(object->ptr, object->info, info) : nullptr;
    if (!result) fail(rb_eTypeError, expected());
    return static_cast<U*>(result);
}
inline bool is_director(VALUE value) { return is_instance_object(value) && instance(value)->director; }

enum class Ownership { borrowed, owned };
template<class T> VALUE wrap(T* value, Ownership ownership, VALUE keepalive) {
    using U = std::remove_cv_t<T>;
    if (!value) return Qnil;
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value); director && !NIL_P(director->rosetta_self)) {
            auto* object = instance(director->rosetta_self);
            if (ownership == Ownership::owned && object->ptr && !object->owned) {
                // C++ handed back ownership of a Ruby-implemented object.
                object->owned = 1;
                if (director->rosetta_owns_self) {
                    const_cast<Director*>(director)->rosetta_owns_self = false;
                    unpin(director->rosetta_self);
                }
            }
            return director->rosetta_self;
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info && !NIL_P(found->second->klass)) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    if (NIL_P(info->klass)) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail(rb_eTypeError, "C++ result type is not exported to Ruby");
    }
    VALUE result;
    try { result = new_object(info->klass); }
    catch (...) { if (ownership == Ownership::owned && info->destroy) info->destroy(ptr); throw; }
    auto* object = instance(result);
    object->ptr = ptr;
    object->info = info;
    object->owned = ownership == Ownership::owned;
    object->keepalive = keepalive;
    return result;
}
template<class T> VALUE wrap_shared(std::shared_ptr<T> value) {
    if (!value) return Qnil;
    if constexpr (std::is_polymorphic_v<std::remove_cv_t<T>>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()); director && !NIL_P(director->rosetta_self))
            return director->rosetta_self;
    }
    VALUE result = wrap(value.get(), Ownership::borrowed, Qnil);
    auto* object = instance(result);
    object->holder = new std::shared_ptr<void>(std::const_pointer_cast<std::remove_cv_t<T>>(value), object->ptr);
    return result;
}
// Shares ownership with Ruby. A uniquely owned plain object becomes shared; a borrowed object or
// a Ruby subclass object stays owned by its Ruby object, which C++ then keeps alive.
template<class T> std::shared_ptr<T> shared(VALUE value) {
    if (NIL_P(value)) return {};
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
        object->owned = 0;
    }
    return std::shared_ptr<T>(*object->holder, ptr);
}
template<class T> std::unique_ptr<T> take(VALUE value) {
    if (NIL_P(value)) return {};
    T* ptr = native<T>(value);
    auto* object = instance(value);
    if (object->holder || !object->owned) fail(rb_eArgError, "cannot transfer ownership of a shared or borrowed native object");
    if (object->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail(rb_eTypeError, "cannot transfer a derived object without a virtual destructor");
    object->owned = 0;
    bool kept = false;
    if constexpr (std::is_polymorphic_v<T>) {
        if (auto* director = dynamic_cast<Director*>(ptr); director && object->director) {
            director->rosetta_owns_self = true; // C++ now keeps the Ruby implementation alive
            pin(value);
            kept = true;
        }
    }
    if (!kept) object->ptr = nullptr;
    return std::unique_ptr<T>(ptr);
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- root class methods
inline VALUE close_method(VALUE self) {
    return guard([&]() -> VALUE {
        if (is_instance_object(self)) {
            if (instance(self)->director && instance(self)->ptr && !instance(self)->owned)
                fail(rb_eRuntimeError, "a Ruby subclass object owned by C++ cannot be closed");
            release(instance(self));
        }
        return Qnil;
    });
}
inline VALUE closed_method(VALUE self) { return is_instance_object(self) && instance(self)->ptr ? Qfalse : Qtrue; }
inline VALUE alive_method(VALUE self) { return is_instance_object(self) && instance(self)->ptr ? Qtrue : Qfalse; }
inline VALUE owned_method(VALUE self) {
    return is_instance_object(self) && instance(self)->ptr && (instance(self)->owned || instance(self)->holder) ? Qtrue : Qfalse;
}
inline VALUE initialize_copy_method(VALUE self, VALUE original) {
    return guard([&]() -> VALUE {
        if (self == original) return self;
        if (!is_instance_object(original) || !instance(original)->ptr)
            fail(rb_eRuntimeError, "native object is closed, moved or uninitialized");
        auto* source = instance(original);
        auto* target = instance(self);
        if (target->ptr) fail(rb_eRuntimeError, "native object already initialized");
        if (!source->info->copy || source->director) fail(rb_eTypeError, class_name(original) + " is not copyable");
        target->ptr = source->info->copy(source->ptr);
        target->info = source->info;
        target->owned = 1;
        return self;
    });
}
inline VALUE inspect_method(VALUE self) {
    return guard([&]() -> VALUE {
        char address[64];
        const bool open = is_instance_object(self) && instance(self)->ptr;
        snprintf(address, sizeof(address), "%p", open ? instance(self)->ptr : nullptr); // ruby.h: ruby_snprintf
        return utf8("#<" + class_name(self) + (open ? std::string(" ") + address : std::string(" (closed)")) + ">");
    });
}
inline VALUE uninitialized_method(int, VALUE*, VALUE self) {
    return guard([&]() -> VALUE { fail(rb_eTypeError, class_name(self) + " cannot be constructed from Ruby"); });
}

// ---------------------------------------------------------------- values
template<class T, class = void> struct Value;   // by-value conversions

inline VALUE& set_class() { static VALUE value = Qnil; return value; }
inline bool is_set(VALUE value) { return !NIL_P(set_class()) && RTEST(rb_obj_is_kind_of(value, set_class())); }
inline VALUE to_array(VALUE value) {
    if (RB_TYPE_P(value, T_ARRAY)) return value;
    VALUE result = call_method(value, "to_a");
    if (!RB_TYPE_P(result, T_ARRAY)) fail(rb_eTypeError, "expected an Array, got " + class_name(value));
    return result;
}
// Arrays, or (when not strict) any Enumerable other than strings, hashes and plain native objects.
inline bool is_sequence(VALUE value, bool strict) {
    if (RB_TYPE_P(value, T_ARRAY)) return true;
    if (strict || RB_SPECIAL_CONST_P(value) || RB_TYPE_P(value, T_STRING) || RB_TYPE_P(value, T_HASH)) return false;
    return RTEST(rb_obj_is_kind_of(value, rb_mEnumerable));
}

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool check(VALUE value, bool) { return value == Qtrue || value == Qfalse; }
    static bool load(VALUE value) {
        if (value != Qtrue && value != Qfalse) fail(rb_eTypeError, "expected true or false, got " + class_name(value));
        return value == Qtrue;
    }
    static VALUE cast(bool value) { return value ? Qtrue : Qfalse; }
};
// char is a one-character String; signed/unsigned char stay integers.
template<> struct Value<char> {
    static bool check(VALUE value, bool) { return RB_TYPE_P(value, T_STRING) && RSTRING_LEN(value) == 1; }
    static char load(VALUE value) {
        if (!RB_TYPE_P(value, T_STRING)) fail(rb_eTypeError, "expected a one-character String, got " + class_name(value));
        if (RSTRING_LEN(value) != 1) fail(rb_eArgError, "expected a single ASCII character");
        return RSTRING_PTR(value)[0];
    }
    static VALUE cast(char value) { return protect([&]() { return rb_utf8_str_new(&value, 1); }); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(VALUE value, bool) { return RB_INTEGER_TYPE_P(value); }
    static T load(VALUE value) {
        if (!RB_INTEGER_TYPE_P(value)) fail(rb_eTypeError, "expected Integer, got " + class_name(value));
        if (RB_FIXNUM_P(value)) {
            const long n = FIX2LONG(value);
            if constexpr (std::is_signed_v<T>) {
                if (n < static_cast<long long>(std::numeric_limits<T>::lowest()) || n > static_cast<long long>(std::numeric_limits<T>::max()))
                    fail(rb_eRangeError, "integer " + std::to_string(n) + " outside the range of the C++ type");
            } else {
                if (n < 0) fail(rb_eRangeError, "negative integer for an unsigned C++ type");
                if (static_cast<unsigned long>(n) > std::numeric_limits<T>::max())
                    fail(rb_eRangeError, "integer " + std::to_string(n) + " outside the range of the C++ type");
            }
            return static_cast<T>(n);
        }
        unsigned long long magnitude = 0;
        int sign = 0;
        protect([&]() -> VALUE {
            sign = rb_integer_pack(value, &magnitude, 1, sizeof(magnitude), 0, INTEGER_PACK_NATIVE_BYTE_ORDER | INTEGER_PACK_LSWORD_FIRST);
            return Qnil;
        });
        if (sign == 2 || sign == -2) fail(rb_eRangeError, "integer outside the range of the C++ type");
        if constexpr (std::is_signed_v<T>) {
            const auto maximum = static_cast<unsigned long long>(std::numeric_limits<T>::max());
            if (sign < 0) {
                if (magnitude > maximum + 1) fail(rb_eRangeError, "integer outside the range of the C++ type");
                if (magnitude == maximum + 1) return std::numeric_limits<T>::lowest();
                return static_cast<T>(-static_cast<long long>(magnitude));
            }
            if (magnitude > maximum) fail(rb_eRangeError, "integer outside the range of the C++ type");
        } else {
            if (sign < 0) fail(rb_eRangeError, "negative integer for an unsigned C++ type");
            if (magnitude > std::numeric_limits<T>::max()) fail(rb_eRangeError, "integer outside the range of the C++ type");
        }
        return static_cast<T>(magnitude);
    }
    static VALUE cast(T value) {
        if constexpr (std::is_signed_v<T>) return protect([&]() { return LL2NUM(static_cast<long long>(value)); });
        else return protect([&]() { return ULL2NUM(static_cast<unsigned long long>(value)); });
    }
};
inline bool is_real(VALUE value) {
    return RB_FLOAT_TYPE_P(value) || RB_INTEGER_TYPE_P(value) || (!RB_SPECIAL_CONST_P(value) && RTEST(rb_obj_is_kind_of(value, rb_cRational)));
}
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(VALUE value, bool strict) { return RB_FLOAT_TYPE_P(value) || (!strict && is_real(value)); }
    static T load(VALUE value) {
        double n;
        if (RB_FLOAT_TYPE_P(value)) n = RFLOAT_VALUE(value);
        else if (RB_FIXNUM_P(value)) n = static_cast<double>(FIX2LONG(value));
        else if (is_real(value)) protect([&]() -> VALUE { n = rb_num2dbl(value); return Qnil; });
        else fail(rb_eTypeError, "expected Float, got " + class_name(value));
        if constexpr (!std::is_same_v<T, double> && !std::is_same_v<T, long double>) {
            if (std::isfinite(n) && (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max()))
                fail(rb_eRangeError, "number outside the range of the C++ type");
        }
        return static_cast<T>(n);
    }
    static VALUE cast(T value) { return protect([&]() { return rb_float_new(static_cast<double>(value)); }); }
};
inline std::string load_string(VALUE value) {
    if (RB_SYMBOL_P(value)) value = rb_sym2str(value);
    if (!RB_TYPE_P(value, T_STRING)) fail(rb_eTypeError, "expected String, got " + class_name(value));
    // Text in another encoding is converted to UTF-8; binary strings pass through as bytes.
    rb_encoding* encoding = rb_enc_get(value);
    if (encoding != rb_utf8_encoding() && encoding != rb_usascii_encoding() && encoding != rb_ascii8bit_encoding())
        value = protect([&]() { return rb_str_conv_enc(value, encoding, rb_utf8_encoding()); });
    return std::string(RSTRING_PTR(value), static_cast<std::size_t>(RSTRING_LEN(value)));
}
template<> struct Value<std::string> {
    static bool check(VALUE value, bool strict) { return RB_TYPE_P(value, T_STRING) || (!strict && RB_SYMBOL_P(value)); }
    static std::string load(VALUE value) { return load_string(value); }
    static VALUE cast(const std::string& value) { return utf8(value); }
};
template<> struct Value<std::string_view> {
    static bool check(VALUE value, bool strict) { return Value<std::string>::check(value, strict); }
    static VALUE cast(std::string_view value) { return utf8(std::string(value)); }
};
template<> struct Value<const char*> {
    static bool check(VALUE value, bool strict) { return NIL_P(value) || Value<std::string>::check(value, strict); }
    static VALUE cast(const char* value) { if (!value) return Qnil; return utf8(std::string(value)); }
};
template<> struct Value<char*> {
    static VALUE cast(const char* value) { return Value<const char*>::cast(value); }
};
// Paths take a String or anything with to_path (Pathname, File) and come back as Strings.
template<> struct Value<std::filesystem::path> {
    static bool check(VALUE value, bool) {
        return RB_TYPE_P(value, T_STRING) || (!RB_SPECIAL_CONST_P(value) && !is_instance_object(value) && responds(value, "to_path"));
    }
    static std::filesystem::path load(VALUE value) {
        if (!RB_TYPE_P(value, T_STRING)) value = call_method(value, "to_path");
        return std::filesystem::path(load_string(value));
    }
    static VALUE cast(const std::filesystem::path& value) { return utf8(value.string()); }
};
template<> struct Value<std::nullptr_t> {
    static bool check(VALUE value, bool) { return NIL_P(value); }
    static std::nullptr_t load(VALUE) { return nullptr; }
    static VALUE cast(std::nullptr_t) { return Qnil; }
};
template<> struct Value<std::monostate> {
    static bool check(VALUE value, bool) { return NIL_P(value); }
    static std::monostate load(VALUE) { return {}; }
    static VALUE cast(std::monostate) { return Qnil; }
};
template<class T> struct Value<std::complex<T>> {
    static bool is_complex(VALUE value) { return !RB_SPECIAL_CONST_P(value) && RB_TYPE_P(value, T_COMPLEX); }
    static bool check(VALUE value, bool strict) { return is_complex(value) || Value<T>::check(value, strict); }
    static std::complex<T> load(VALUE value) {
        if (is_complex(value)) return {Value<T>::load(rb_complex_real(value)), Value<T>::load(rb_complex_imag(value))};
        return {Value<T>::load(value), T{}};
    }
    static VALUE cast(const std::complex<T>& value) {
        const double re = static_cast<double>(value.real()), im = static_cast<double>(value.imag());
        return protect([&]() { return rb_complex_new(rb_float_new(re), rb_float_new(im)); });
    }
};

// Enumerations are Symbols named after their enumerators; flag enumerations (policy enum_flags)
// are Integers. Arguments also take the Integer value, and flags an Array of Symbols.
struct EnumInfo {
    VALUE module = Qnil;
    bool flags = false;
    std::vector<std::pair<ID, long long>> members;
};
inline std::map<std::type_index, EnumInfo>& enum_infos() { static std::map<std::type_index, EnumInfo> value; return value; }
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    using N = std::conditional_t<std::is_same_v<U, bool>, int, U>;
    static const EnumInfo* info() {
        const auto found = enum_infos().find(std::type_index(typeid(T)));
        return found == enum_infos().end() ? nullptr : &found->second;
    }
    static bool member(ID id, long long* out) {
        const auto* e = info();
        if (!e) return false;
        for (const auto& [name, value] : e->members) if (name == id) { if (out) *out = value; return true; }
        return false;
    }
    static bool check(VALUE value, bool strict) {
        // Lenient: any Symbol, so an unknown one raises ArgumentError naming the enumerators.
        if (RB_SYMBOL_P(value)) return !strict || member(rb_sym2id(value), nullptr);
        const auto* e = info();
        if (e && e->flags && RB_TYPE_P(value, T_ARRAY)) {
            for (long i = 0; i < RARRAY_LEN(value); ++i) {
                const VALUE item = rb_ary_entry(value, i);
                if (!RB_SYMBOL_P(item) || !member(rb_sym2id(item), nullptr)) return false;
            }
            return true;
        }
        return RB_INTEGER_TYPE_P(value) && (!strict || (e && e->flags));
    }
    static T load(VALUE value) {
        long long number = 0;
        if (RB_SYMBOL_P(value)) {
            if (!member(rb_sym2id(value), &number)) {
                std::string names;
                if (const auto* e = info()) for (const auto& item : e->members) names += (names.empty() ? ":" : ", :") + std::string(rb_id2name(item.first));
                fail(rb_eArgError, "unknown enumerator :" + std::string(rb_id2name(rb_sym2id(value))) + " (expected one of " + names + ")");
            }
            return static_cast<T>(static_cast<U>(number));
        }
        if (RB_TYPE_P(value, T_ARRAY)) {
            unsigned long long bits = 0;
            for (long i = 0; i < RARRAY_LEN(value); ++i) bits |= static_cast<unsigned long long>(static_cast<U>(load(rb_ary_entry(value, i))));
            return static_cast<T>(static_cast<U>(bits));
        }
        if (!RB_INTEGER_TYPE_P(value)) fail(rb_eTypeError, "expected a Symbol or Integer, got " + class_name(value));
        return static_cast<T>(static_cast<U>(Value<N>::load(value)));
    }
    static VALUE cast(T value) {
        const auto number = static_cast<long long>(static_cast<U>(value));
        if (const auto* e = info(); e && !e->flags)
            for (const auto& [name, item] : e->members) if (item == number) return ID2SYM(name);
        return Value<N>::cast(static_cast<N>(static_cast<U>(value)));
    }
};

// Exported classes by value: Ruby receives an owned copy.
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(VALUE value, bool) { return is_instance<T>(value); }
    static T load(VALUE value) { return *native<T>(value); }
    static VALUE cast(const T& value) {
        if constexpr (copyable_v<T>) return wrap(new T(value), Ownership::owned, Qnil);
        else fail(rb_eTypeError, "C++ result is not copyable");
    }
    static VALUE cast(T&& value) {
        if constexpr (std::is_move_constructible_v<T> && !std::is_abstract_v<T>) return wrap(new T(std::move(value)), Ownership::owned, Qnil);
        else return cast(static_cast<const T&>(value));
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(VALUE value, bool) { return NIL_P(value) || is_instance<T>(value); }
    static T* load(VALUE value) { return NIL_P(value) ? nullptr : native<T>(value); }
    static VALUE cast(T* value) { return wrap(value, Ownership::borrowed, Qnil); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(VALUE value, bool) { return NIL_P(value) || is_instance<T>(value); }
    static std::shared_ptr<T> load(VALUE value) { return shared<T>(value); }
    static VALUE cast(const std::shared_ptr<T>& value) { return wrap_shared(value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(VALUE value, bool) { return NIL_P(value) || is_instance<T>(value); }
    static std::unique_ptr<T, D> load(VALUE value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(value);
        else fail(rb_eTypeError, "custom deleters cannot receive Ruby objects");
    }
    static VALUE cast(std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return wrap(value.release(), Ownership::owned, Qnil);
        else fail(rb_eTypeError, "custom deleters cannot be returned to Ruby");
    }
};
template<class T> struct Value<std::optional<T>> {
    static bool check(VALUE value, bool strict) { return NIL_P(value) || Value<T>::check(value, strict); }
    static std::optional<T> load(VALUE value) {
        if (NIL_P(value)) return std::nullopt;
        return Value<T>::load(value);
    }
    static VALUE cast(const std::optional<T>& value) {
        if (!value) return Qnil;
        return Value<T>::cast(*value);
    }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static VALUE cast(std::reference_wrapper<T> value) {
        if constexpr (bound_v<T>) return wrap(&value.get(), Ownership::borrowed, Qnil);
        else return Value<std::remove_cv_t<T>>::cast(value.get());
    }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- containers
inline VALUE new_array(std::size_t size) { return protect([&]() { return rb_ary_new_capa(static_cast<long>(size)); }); }
inline void push(VALUE array, VALUE item) { protect([&]() { return rb_ary_push(array, item); }); }
// Sequences: Arrays in (or any Enumerable when not strict), Arrays out.
template<class C, class E> struct Sequence {
    static bool check(VALUE value, bool strict) {
        if (!is_sequence(value, strict)) return false;
        if (!RB_TYPE_P(value, T_ARRAY)) return !strict;
        for (long i = 0; i < RARRAY_LEN(value); ++i) if (!Value<E>::check(rb_ary_entry(value, i), strict)) return false;
        return true;
    }
    static C load(VALUE value) {
        if (!is_sequence(value, false)) fail(rb_eTypeError, "expected an Array, got " + class_name(value));
        volatile VALUE array = to_array(value);
        const long size = RARRAY_LEN(array);
        C result{};
        if constexpr (std::is_same_v<C, std::vector<E>>) result.reserve(static_cast<std::size_t>(size));
        for (long i = 0; i < size; ++i) {
            const VALUE item = rb_ary_entry(array, i);
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(Value<E>::load(item));
            else result.push_back(Value<E>::load(item));
        }
        return result;
    }
    template<class Container> static VALUE cast(Container&& value) {
        volatile VALUE result = new_array(value.size());
        for (auto&& item : value) {
            if constexpr (std::is_rvalue_reference_v<Container&&> && !std::is_same_v<E, bool>) push(result, Value<E>::cast(std::move(item)));
            else push(result, Value<E>::cast(item));
        }
        return result;
    }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
// Byte vectors also take a (binary) String.
template<class A> struct Value<std::vector<unsigned char, A>> : Sequence<std::vector<unsigned char, A>, unsigned char> {
    using Base = Sequence<std::vector<unsigned char, A>, unsigned char>;
    static bool check(VALUE value, bool strict) { return RB_TYPE_P(value, T_STRING) || Base::check(value, strict); }
    static std::vector<unsigned char, A> load(VALUE value) {
        if (RB_TYPE_P(value, T_STRING)) {
            const auto* data = reinterpret_cast<const unsigned char*>(RSTRING_PTR(value));
            return std::vector<unsigned char, A>(data, data + RSTRING_LEN(value));
        }
        return Base::load(value);
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
// Sets: Set out; Set, Array or (when not strict) any Enumerable in.
template<class S, class E> struct SetValue {
    static bool check(VALUE value, bool strict) {
        if (is_set(value)) {
            if (strict) {
                volatile VALUE array = to_array(value);
                for (long i = 0; i < RARRAY_LEN(array); ++i) if (!Value<E>::check(rb_ary_entry(array, i), strict)) return false;
            }
            return true;
        }
        return Sequence<S, E>::check(value, strict);
    }
    static S load(VALUE value) {
        volatile VALUE array = to_array(value);
        S result;
        for (long i = 0; i < RARRAY_LEN(array); ++i) result.insert(Value<E>::load(rb_ary_entry(array, i)));
        return result;
    }
    static VALUE cast(const S& value) {
        volatile VALUE array = new_array(value.size());
        for (const auto& item : value) push(array, Value<E>::cast(item));
        if (NIL_P(set_class())) return array;
        const VALUE argument = array;
        return protect([&]() { return rb_funcallv(set_class(), rb_intern("new"), 1, &argument); });
    }
};
template<class E, class C, class A> struct Value<std::set<E, C, A>> : SetValue<std::set<E, C, A>, E> {};
template<class E, class H, class Q, class A> struct Value<std::unordered_set<E, H, Q, A>> : SetValue<std::unordered_set<E, H, Q, A>, E> {};
template<class E> struct Value<std::valarray<E>> {
    static bool check(VALUE value, bool strict) { return Value<std::vector<E>>::check(value, strict); }
    static std::valarray<E> load(VALUE value) { auto v = Value<std::vector<E>>::load(value); return std::valarray<E>(v.data(), v.size()); }
    static VALUE cast(const std::valarray<E>& value) { return Value<std::vector<E>>::cast(std::vector<E>(std::begin(value), std::end(value))); }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(VALUE value, bool strict) {
        if (!RB_TYPE_P(value, T_ARRAY) || static_cast<std::size_t>(RARRAY_LEN(value)) != N) return false;
        for (long i = 0; i < RARRAY_LEN(value); ++i) if (!Value<E>::check(rb_ary_entry(value, i), strict)) return false;
        return true;
    }
    static std::array<E, N> load(VALUE value) {
        if (!RB_TYPE_P(value, T_ARRAY)) fail(rb_eTypeError, "expected an Array, got " + class_name(value));
        if (static_cast<std::size_t>(RARRAY_LEN(value)) != N) fail(rb_eArgError, "expected an Array of length " + std::to_string(N));
        std::array<E, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = Value<E>::load(rb_ary_entry(value, static_cast<long>(i)));
        return result;
    }
    static VALUE cast(const std::array<E, N>& value) { return Sequence<std::vector<E>, E>::cast(value); }
};
// Hash entries as an Array of [key, value] pairs (hash iteration callbacks cannot throw).
inline VALUE hash_pairs(VALUE hash) { return call_method(hash, "to_a"); }
template<class M, class K, class V> struct Mapping {
    static bool check(VALUE value, bool strict) {
        if (!RB_TYPE_P(value, T_HASH)) return false;
        volatile VALUE pairs = hash_pairs(value);
        for (long i = 0; i < RARRAY_LEN(pairs); ++i) {
            const VALUE pair = rb_ary_entry(pairs, i);
            if (!Value<K>::check(rb_ary_entry(pair, 0), strict) || !Value<V>::check(rb_ary_entry(pair, 1), strict)) return false;
        }
        return true;
    }
    static M load(VALUE value) {
        if (!RB_TYPE_P(value, T_HASH)) fail(rb_eTypeError, "expected a Hash, got " + class_name(value));
        volatile VALUE pairs = hash_pairs(value);
        M result;
        for (long i = 0; i < RARRAY_LEN(pairs); ++i) {
            const VALUE pair = rb_ary_entry(pairs, i);
            result.emplace(Value<K>::load(rb_ary_entry(pair, 0)), Value<V>::load(rb_ary_entry(pair, 1)));
        }
        return result;
    }
    static VALUE cast(const M& value) {
        volatile VALUE result = protect([&]() { return rb_hash_new(); });
        for (const auto& [key, item] : value) {
            volatile VALUE k = Value<K>::cast(key);
            const VALUE v = Value<V>::cast(item);
            const VALUE hash = result, kk = k;
            protect([&]() { return rb_hash_aset(hash, kk, v); });
        }
        return result;
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};
// Pairs and tuples are Arrays.
template<class A, class B> struct Value<std::pair<A, B>> {
    static bool check(VALUE value, bool strict) {
        return RB_TYPE_P(value, T_ARRAY) && RARRAY_LEN(value) == 2 && Value<A>::check(rb_ary_entry(value, 0), strict) &&
            Value<B>::check(rb_ary_entry(value, 1), strict);
    }
    static std::pair<A, B> load(VALUE value) {
        if (!RB_TYPE_P(value, T_ARRAY) || RARRAY_LEN(value) != 2) fail(rb_eTypeError, "expected a two-element Array, got " + class_name(value));
        return {Value<A>::load(rb_ary_entry(value, 0)), Value<B>::load(rb_ary_entry(value, 1))};
    }
    static VALUE cast(const std::pair<A, B>& value) {
        volatile VALUE result = new_array(2);
        push(result, Value<A>::cast(value.first));
        push(result, Value<B>::cast(value.second));
        return result;
    }
};
template<class... T> struct Value<std::tuple<T...>> {
    template<std::size_t... I> static bool check_items(VALUE value, bool strict, std::index_sequence<I...>) {
        return (Value<T>::check(rb_ary_entry(value, static_cast<long>(I)), strict) && ...);
    }
    static bool check(VALUE value, bool strict) {
        return RB_TYPE_P(value, T_ARRAY) && RARRAY_LEN(value) == static_cast<long>(sizeof...(T)) &&
            check_items(value, strict, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static std::tuple<T...> load_items(VALUE value, std::index_sequence<I...>) {
        return std::tuple<T...>{Value<T>::load(rb_ary_entry(value, static_cast<long>(I)))...};
    }
    static std::tuple<T...> load(VALUE value) {
        if (!RB_TYPE_P(value, T_ARRAY) || RARRAY_LEN(value) != static_cast<long>(sizeof...(T)))
            fail(rb_eTypeError, "expected an Array of " + std::to_string(sizeof...(T)) + " elements, got " + class_name(value));
        return load_items(value, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static VALUE cast_items(const std::tuple<T...>& value, std::index_sequence<I...>) {
        volatile VALUE result = new_array(sizeof...(T));
        (push(result, Value<T>::cast(std::get<I>(value))), ...);
        return result;
    }
    static VALUE cast(const std::tuple<T...>& value) { return cast_items(value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(VALUE value, bool strict) { return (Value<T>::check(value, true) || ...) || (!strict && (Value<T>::check(value, false) || ...)); }
    template<class First, class... Rest> static std::variant<T...> load_first(VALUE value, bool strict) {
        if (Value<First>::check(value, strict)) return std::variant<T...>(std::in_place_type<First>, Value<First>::load(value));
        if constexpr (sizeof...(Rest) > 0) return load_first<Rest...>(value, strict);
        else fail(rb_eTypeError, "no variant alternative accepts " + class_name(value));
    }
    static std::variant<T...> load(VALUE value) {
        if ((Value<T>::check(value, true) || ...)) return load_first<T...>(value, true);
        return load_first<T...>(value, false);
    }
    static VALUE cast(const std::variant<T...>& value) {
        return std::visit([](const auto& item) -> VALUE { return Value<std::decay_t<decltype(item)>>::cast(item); }, value);
    }
};
// Durations are Float seconds.
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(VALUE value, bool strict) { return Value<double>::check(value, strict); }
    static D load(VALUE value) { return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(value))); }
    static VALUE cast(const D& value) { return Value<double>::cast(std::chrono::duration<double>(value).count()); }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arguments
struct fill_t {}; inline constexpr fill_t fill{};
struct fill_default_t {}; inline constexpr fill_default_t fill_default{};
// Converts a Ruby argument for a declared C++ parameter type P and keeps any temporary alive.
// The fill constructors use the C++ default of a parameter a keyword call skipped.
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(VALUE value, bool strict) { return Value<V>::check(value, strict); }
    explicit Param(VALUE value) : value_(Value<V>::load(value)) {}
    template<class U> Param(fill_t, U&& value) : value_(static_cast<V>(std::forward<U>(value))) {}
    explicit Param(fill_default_t) : value_{} {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    // Mutable references to converted values update the caller's Array or Hash in place.
    void writeback(VALUE original) {
        if constexpr (std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>>) {
            if constexpr (std::is_copy_constructible_v<V>) {
                if (RB_TYPE_P(original, T_ARRAY) || RB_TYPE_P(original, T_HASH)) {
                    writable(original);
                    const VALUE converted = Value<V>::cast(value_);
                    if (RB_TYPE_P(original, T_ARRAY) && RB_TYPE_P(converted, T_ARRAY)) protect([&]() { return rb_ary_replace(original, converted); });
                    else if (RB_TYPE_P(original, T_HASH) && RB_TYPE_P(converted, T_HASH)) protect([&]() { return rb_funcall(original, rb_intern("replace"), 1, converted); });
                }
            }
        }
    }
private:
    V value_;
};
// Exported classes are passed by reference to the Ruby-owned object.
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(VALUE value, bool) { return is_instance<V>(value); }
    explicit Param(VALUE value) : ptr_(native<V>(value)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
    void writeback(VALUE) {}
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(VALUE value, bool) { return NIL_P(value) || is_instance<V>(value); }
    explicit Param(VALUE value) : ptr_(NIL_P(value) ? nullptr : native<V>(value)) {}
    Param(fill_t, std::nullptr_t) : ptr_(nullptr) {}
    P get() { return ptr_; }
    void writeback(VALUE) {}
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(VALUE value, bool strict) { return NIL_P(value) || Value<std::string>::check(value, strict); }
    explicit Param(VALUE value) { if (!NIL_P(value)) text_ = load_string(value); }
    Param(fill_t, const char* value) { if (value) text_ = value; }
    const char* get() { return text_ ? text_->c_str() : nullptr; }
    void writeback(VALUE) {}
private:
    std::optional<std::string> text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(VALUE value, bool strict) { return Value<std::string>::check(value, strict); }
    explicit Param(VALUE value) : text_(load_string(value)) {}
    Param(fill_t, std::string_view value) : text_(value) {}
    std::string_view get() { return text_; }
    void writeback(VALUE) {}
private:
    std::string text_;
};

// The arguments of one call: positional values, keywords (a Hash) and the block as a Proc.
struct Args {
    int argc = 0;
    const VALUE* argv = nullptr;
    VALUE keywords = Qnil;
    VALUE block = Qnil;
    bool arity_matched = false;
    // Must run in the method's own frame (keyword and block flags belong to it).
    Args(int count, const VALUE* values) : argc(count), argv(values) {
        if (argc > 0 && rb_keyword_given_p() && RB_TYPE_P(argv[argc - 1], T_HASH)) keywords = argv[--argc];
        if (rb_block_given_p()) block = protect([]() { return rb_block_proc(); });
    }
    // Maps the arguments onto one overload's parameters. A missing argument (Qundef) is allowed
    // before the last given one only where the parameter's C++ default can be reproduced.
    bool collect(const char* const* names, const bool* fillable, long count, long required, long callback, VALUE* out, long* used) {
        for (long i = 0; i < count; ++i) out[i] = Qundef;
        const bool use_block = !NIL_P(block) && callback >= 0;
        if (!NIL_P(block) && callback < 0) return false;
        long next = 0;
        for (int i = 0; i < argc; ++i) {
            if (use_block && next == callback) ++next;
            if (next >= count) return false;
            out[next++] = argv[i];
        }
        if (use_block) {
            if (out[callback] != Qundef) return false;
            out[callback] = block;
        }
        if (!NIL_P(keywords) && RHASH_SIZE(keywords) > 0) {
            long matched = 0;
            for (long i = 0; i < count; ++i) {
                if (!names[i] || !names[i][0]) continue;
                const VALUE value = rb_hash_lookup2(keywords, ID2SYM(rb_intern(names[i])), Qundef);
                if (value == Qundef) continue;
                if (out[i] != Qundef) return false;
                out[i] = value;
                ++matched;
            }
            if (static_cast<size_t>(matched) != RHASH_SIZE(keywords)) return false;
        }
        long last = 0;
        for (long i = 0; i < count; ++i) if (out[i] != Qundef) last = i + 1;
        if (last < required) return false;
        for (long i = 0; i < last; ++i) if (out[i] == Qundef && (i < required || !fillable[i])) return false;
        *used = last;
        arity_matched = true;
        return true;
    }
};
[[noreturn]] inline void no_overload(const char* name, const char* signatures, const Args& args) {
    std::string received;
    for (int i = 0; i < args.argc; ++i) received += (i ? ", " : "") + class_name(args.argv[i]);
    if (!NIL_P(args.keywords)) {
        volatile VALUE pairs = hash_pairs(args.keywords);
        for (long i = 0; i < RARRAY_LEN(pairs); ++i) {
            const VALUE pair = rb_ary_entry(pairs, i);
            const VALUE key = rb_ary_entry(pair, 0);
            received += std::string(received.empty() ? "" : ", ") + (RB_SYMBOL_P(key) ? rb_id2name(rb_sym2id(key)) : "?") + ": " +
                class_name(rb_ary_entry(pair, 1));
        }
    }
    if (!NIL_P(args.block)) received += std::string(received.empty() ? "" : ", ") + "&block";
    fail(args.arity_matched ? rb_eTypeError : rb_eArgError,
         std::string(name) + (args.arity_matched ? ": no overload accepts (" : ": wrong number of arguments or unknown keywords (") + received +
             "); supported signatures:\n" + signatures);
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
template<class R> VALUE result(R&& value, VALUE parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) return Qnil;
        if (!NIL_P(parent) && is_instance<B>(parent) && instance(parent)->ptr && native<B>(parent) == ptr) return parent;
        if (policy == Return::owned) return wrap(ptr, Ownership::owned, Qnil);
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) return wrap(new B(*ptr), Ownership::owned, Qnil);
            else fail(rb_eTypeError, "C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (!NIL_P(parent) && is_instance<V>(parent) && instance(parent)->ptr && native<V>(parent) == ptr) return parent;
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) return wrap(new V(value), Ownership::owned, Qnil);
            else fail(rb_eTypeError, "C++ result is not copyable");
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
template<class F> VALUE call(F&& function, VALUE parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); return Qnil; }
    else return result<R>(function(), parent, policy);
}
template <class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail(rb_eNoMethodError, "native field type cannot be assigned");
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- C++ -> Ruby calls
// Converts arguments passed from C++ to Ruby callbacks; references to classes are borrowed.
template<class A> VALUE argument(A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) return wrap(const_cast<V*>(&value), Ownership::borrowed, Qnil);
        else return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        return wrap(const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed, Qnil);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else {
        return Value<V>::cast(value);
    }
}
template<class R> R returned(VALUE value) {
    if constexpr (std::is_void_v<R>) { (void)value; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_lvalue_reference_v<R> && bound_v<V>) return *native<V>(value);
        else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(value);
        else {
            if (!Value<V>::check(value, false))
                fail(rb_eTypeError, "Ruby callback returned " + class_name(value) + ", which does not convert to the C++ result type");
            return Value<V>::load(value);
        }
    }
}
inline void require_ruby_thread() {
    if (!ruby_native_thread_p()) throw std::runtime_error("a Ruby callback was called from a thread Ruby does not know");
}
// Calls receiver.name(arguments...) and converts the result to R.
template<class R, class... A> R invoke_ruby(VALUE receiver, ID name, A&&... arguments) {
    require_ruby_thread();
    VALUE argv[sizeof...(A) + 1] = {argument<A>(std::forward<A>(arguments))..., Qnil};
    const VALUE value = protect([&]() { return rb_funcallv(receiver, name, static_cast<int>(sizeof...(A)), argv); });
    RB_GC_GUARD(argv[0]);
    if constexpr (std::is_void_v<R>) { (void)value; return; }
    else return returned<R>(value);
}
// Anything with #call (Proc, lambda, Method, a block) becomes a std::function; nil is empty.
template<class R, class... A> struct Value<std::function<R(A...)>> {
    static bool check(VALUE value, bool) {
        if (NIL_P(value)) return true;
        if (RB_SPECIAL_CONST_P(value)) return false;
        if (RTEST(rb_obj_is_proc(value)) || RTEST(rb_obj_is_method(value))) return true;
        return (!is_instance_object(value) || rb_obj_class(value) == callable_class()) && responds(value, "call");
    }
    static std::function<R(A...)> load(VALUE value) {
        if (NIL_P(value)) return {};
        if (!check(value, false)) fail(rb_eTypeError, "expected a Proc or an object with #call, got " + class_name(value));
        SharedRef target(value);
        return [target](A... arguments) -> R {
            static const ID call_id = rb_intern("call");
            return invoke_ruby<R, A...>(target.get(), call_id, std::forward<A>(arguments)...);
        };
    }
    static VALUE cast(const std::function<R(A...)>& value);
};
// Native callables returned to Ruby: NativeCallable objects with #call and #to_proc.
struct Callable {
    std::function<VALUE(int, const VALUE*)>* call;
    int arity;
};
inline void callable_free(void* data) {
    auto* object = static_cast<Callable*>(data);
    delete object->call;
    ruby_xfree(object);
}
inline size_t callable_size(const void*) { return sizeof(Callable); }
inline const rb_data_type_t callable_type = {"rosetta native callable", {nullptr, callable_free, callable_size, nullptr, {nullptr}},
    nullptr, nullptr, RUBY_TYPED_FREE_IMMEDIATELY};
inline VALUE callable_allocate(VALUE klass) { return rb_data_typed_object_zalloc(klass, sizeof(Callable), &callable_type); }
inline Callable* callable(VALUE self) {
    if (RB_SPECIAL_CONST_P(self) || !RB_TYPE_P(self, T_DATA) || !RTYPEDDATA_P(self) || RTYPEDDATA_TYPE(self) != &callable_type)
        fail(rb_eTypeError, "expected a native callable");
    auto* object = static_cast<Callable*>(RTYPEDDATA_DATA(self));
    if (!object->call || !*object->call) fail(rb_eRuntimeError, "empty native callable");
    return object;
}
inline VALUE callable_call(int argc, VALUE* argv, VALUE self) {
    return guard([&]() -> VALUE {
        if (rb_keyword_given_p()) fail(rb_eArgError, "native callables take positional arguments only");
        auto* object = callable(self);
        if (argc != object->arity)
            fail(rb_eArgError, "wrong number of arguments (given " + std::to_string(argc) + ", expected " + std::to_string(object->arity) + ")");
        return (*object->call)(argc, argv);
    });
}
inline VALUE callable_arity(VALUE self) { return guard([&]() -> VALUE { return INT2NUM(callable(self)->arity); }); }
inline VALUE callable_to_proc(VALUE self) {
    return guard([&]() -> VALUE {
        const VALUE method = protect([&]() { return rb_obj_method(self, ID2SYM(rb_intern("call"))); });
        return call_method(method, "to_proc");
    });
}
template<class R, class... A, std::size_t... I>
VALUE call_native(const std::function<R(A...)>& function, const VALUE* argv, std::index_sequence<I...>) {
    std::tuple<Param<A>...> params{Param<A>(argv[I])...};
    (void)params; (void)argv;
    return call([&]() -> R { return function(std::get<I>(params).get()...); }, Qnil, Return::copy);
}
template<class R, class... A>
VALUE Value<std::function<R(A...)>>::cast(const std::function<R(A...)>& value) {
    if (!value) return Qnil;
    if (NIL_P(callable_class())) fail(rb_eRuntimeError, "native callable class is not initialized");
    const VALUE result = protect([&]() { return callable_allocate(callable_class()); });
    auto* object = static_cast<Callable*>(RTYPEDDATA_DATA(result));
    object->arity = static_cast<int>(sizeof...(A));
    object->call = new std::function<VALUE(int, const VALUE*)>([value](int, const VALUE* argv) -> VALUE {
        return call_native<R, A...>(value, argv, std::index_sequence_for<A...>{});
    });
    return result;
}

// ---------------------------------------------------------------- directors
inline std::set<VALUE>& native_classes() { static std::set<VALUE> value; return value; }
// Whether self's method `name` comes from Ruby code rather than from a native class.
inline bool overridden(VALUE self, const char* name) {
    if (NIL_P(self)) return false;
    require_ruby_thread();
    const VALUE name_symbol = ID2SYM(rb_intern(name));
    const VALUE owner = protect([&]() -> VALUE {
        const VALUE method = rb_obj_method(self, name_symbol);
        return rb_funcall(method, rb_intern("owner"), 0);
    });
    return native_classes().count(owner) == 0;
}
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(or_default(errors().not_implemented, rb_eNotImpError), std::string("pure virtual method ") + name + " is not implemented by the Ruby subclass");
}
// A Ruby exception from a noexcept override cannot reach C++: report it and terminate.
[[noreturn]] inline void unraisable(const char* name) noexcept {
    std::fprintf(stderr, "rosetta: exception in Ruby override of noexcept %s\n", name);
    std::terminate();
}

// ---------------------------------------------------------------- operators and protocols
// <=> from < and ==, so Comparable and sort work.
inline VALUE spaceship_method(VALUE self, VALUE other) {
    return guard([&]() -> VALUE {
        if (RTEST(call_method(self, "==", 1, &other))) return INT2FIX(0);
        const VALUE less = protect([&]() -> VALUE {
            int state = 0;
            struct D { VALUE self, other; } d{self, other};
            VALUE r = rb_protect([](VALUE raw) -> VALUE { auto* p = reinterpret_cast<D*>(raw); return rb_funcall(p->self, rb_intern("<"), 1, p->other); },
                                 reinterpret_cast<VALUE>(&d), &state);
            if (state) { rb_set_errinfo(Qnil); return Qundef; }
            return r;
        });
        if (less == Qundef) return Qnil;
        if (RTEST(less)) return INT2FIX(-1);
        return INT2FIX(1);
    });
}
// each over size and [] (or at), so Enumerable works.
inline VALUE each_by_index(VALUE self, const char* size_name, const char* item_name) {
    return guard([&]() -> VALUE {
        if (!rb_block_given_p()) {
            const VALUE method = symbol("each");
            return protect([&]() { return rb_enumeratorize_with_size(self, method, 0, nullptr, nullptr); });
        }
        const long size = Value<long>::load(call_method(self, size_name));
        for (long i = 0; i < size; ++i) {
            const VALUE index = LONG2NUM(i);
            const VALUE item = call_method(self, item_name, 1, &index);
            protect([&]() { return rb_yield(item); });
        }
        return self;
    });
}
template<class T, class = void> struct hashable : std::false_type {};
template<class T> struct hashable<T, std::void_t<decltype(std::hash<T>{}(std::declval<const T&>()))>> : std::true_type {};
template<class T, class = void> struct equality : std::false_type {};
template<class T> struct equality<T, std::void_t<decltype(std::declval<const T&>() == std::declval<const T&>())>> : std::true_type {};
template<class T> VALUE hash_method(VALUE self) {
    return guard([&]() -> VALUE {
        if constexpr (hashable<T>::value) return Value<std::size_t>::cast(std::hash<T>{}(*native<T>(self)));
        else return Qnil;
    });
}
template<class T> VALUE eql_method(VALUE self, VALUE other) {
    return guard([&]() -> VALUE {
        if constexpr (equality<T>::value) {
            if (!is_instance<T>(other) || rb_obj_class(self) != rb_obj_class(other)) return Qfalse;
            return *native<T>(self) == *native<T>(other) ? Qtrue : Qfalse;
        } else return Qfalse;
    });
}
// hash and eql? for classes with std::hash and ==, so they work as Hash keys.
template<class T> void define_hash(VALUE klass) {
    if constexpr (hashable<T>::value && equality<T>::value) {
        rb_define_method(klass, "hash", reinterpret_cast<VALUE (*)(ANYARGS)>(&hash_method<T>), 0);
        rb_define_method(klass, "eql?", reinterpret_cast<VALUE (*)(ANYARGS)>(&eql_method<T>), 1);
    } else (void)klass;
}
// to_s through operator<<(std::ostream&, const T&).
template<class T> VALUE stream_method(VALUE self) {
    return guard([&]() -> VALUE {
        std::ostringstream stream;
        stream << *native<T>(self);
        return utf8(stream.str());
    });
}

// ---------------------------------------------------------------- module helpers
template<class T> void register_type(TypeInfo& info, const char* name, VALUE klass) {
    info.name = name;
    info.klass = klass;
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* ptr) { delete static_cast<T*>(ptr); };
    if constexpr (copyable_v<T>) info.copy = [](const void* ptr) -> void* { return new T(*static_cast<const T*>(ptr)); };
    registry()[std::type_index(typeid(T))] = &info;
    native_classes().insert(klass);
    rb_gc_register_mark_object(klass);
}
template<class Derived, class Base> void add_base(TypeInfo& info) {
    info.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* {
        return static_cast<Base*>(static_cast<Derived*>(ptr));
    }});
}
inline VALUE enum_values(VALUE self) { return rb_ivar_get(self, rb_intern("@__values")); }
inline VALUE enum_to_i(VALUE self, VALUE name) {
    const VALUE values = rb_ivar_get(self, rb_intern("@__values"));
    const VALUE found = RB_TYPE_P(values, T_HASH) ? rb_hash_lookup2(values, name, Qundef) : Qundef;
    if (found == Qundef) rb_raise(rb_eArgError, "unknown enumerator");
    return found;
}
inline VALUE enum_from_i(VALUE self, VALUE number) {
    const VALUE values = rb_ivar_get(self, rb_intern("@__values"));
    return rb_funcall(values, rb_intern("key"), 1, number);
}
inline VALUE enum_names(VALUE self) {
    return rb_funcall(rb_ivar_get(self, rb_intern("@__values")), rb_intern("keys"), 0);
}
template<class T> void define_enum(VALUE module, bool flags, std::initializer_list<std::tuple<const char*, const char*, long long>> members) {
    EnumInfo info;
    info.module = module;
    info.flags = flags;
    const VALUE values = rb_hash_new();
    for (const auto& [name, constant, value] : members) {
        const ID id = rb_intern(name);
        info.members.emplace_back(id, value);
        const VALUE number = Value<std::underlying_type_t<T>>::cast(static_cast<std::underlying_type_t<T>>(value));
        rb_hash_aset(values, ID2SYM(id), number);
        if (constant) rb_define_const(module, constant, flags ? number : ID2SYM(id));
    }
    rb_obj_freeze(values);
    rb_ivar_set(module, rb_intern("@__values"), values);
    rb_define_singleton_method(module, "to_h", reinterpret_cast<VALUE (*)(ANYARGS)>(&enum_values), 0);
    rb_define_singleton_method(module, "values", reinterpret_cast<VALUE (*)(ANYARGS)>(&enum_names), 0);
    rb_define_singleton_method(module, "to_i", reinterpret_cast<VALUE (*)(ANYARGS)>(&enum_to_i), 1);
    rb_define_singleton_method(module, "from_i", reinterpret_cast<VALUE (*)(ANYARGS)>(&enum_from_i), 1);
    enum_infos()[std::type_index(typeid(T))] = std::move(info);
}
inline VALUE error_cpp_class(VALUE self) { return rb_attr_get(self, rb_intern("@cpp_class")); }
// The module's runtime: NativeObject, NativeCallable and the exception classes.
inline void initialize_runtime(VALUE module) {
    static VALUE keeper = Qnil;
    if (NIL_P(keeper)) {
        // A non-null data pointer: Ruby does not call the mark function of an empty TypedData.
        keeper = rb_data_typed_object_wrap(0, static_cast<void*>(&pins()), &keeper_type);
        rb_gc_register_mark_object(keeper);
    }
    VALUE root = rb_define_class_under(module, "NativeObject", rb_cObject);
    rb_define_alloc_func(root, allocate);
    rb_define_method(root, "initialize", reinterpret_cast<VALUE (*)(ANYARGS)>(&uninitialized_method), -1);
    rb_define_method(root, "close", reinterpret_cast<VALUE (*)(ANYARGS)>(&close_method), 0);
    rb_define_method(root, "closed?", reinterpret_cast<VALUE (*)(ANYARGS)>(&closed_method), 0);
    rb_define_method(root, "alive?", reinterpret_cast<VALUE (*)(ANYARGS)>(&alive_method), 0);
    rb_define_method(root, "owned?", reinterpret_cast<VALUE (*)(ANYARGS)>(&owned_method), 0);
    rb_define_method(root, "initialize_copy", reinterpret_cast<VALUE (*)(ANYARGS)>(&initialize_copy_method), 1);
    rb_define_method(root, "inspect", reinterpret_cast<VALUE (*)(ANYARGS)>(&inspect_method), 0);
    rb_gc_register_mark_object(root);
    root_class() = root;
    native_classes().insert(root);
    native_classes().insert(rb_cObject);
    native_classes().insert(rb_mKernel);
    native_classes().insert(rb_cBasicObject);
    VALUE callable_klass = rb_define_class_under(module, "NativeCallable", rb_cObject);
    rb_define_alloc_func(callable_klass, callable_allocate);
    rb_undef_method(rb_singleton_class(callable_klass), "new");
    rb_define_method(callable_klass, "call", reinterpret_cast<VALUE (*)(ANYARGS)>(&callable_call), -1);
    rb_define_method(callable_klass, "[]", reinterpret_cast<VALUE (*)(ANYARGS)>(&callable_call), -1);
    rb_define_method(callable_klass, "===", reinterpret_cast<VALUE (*)(ANYARGS)>(&callable_call), -1);
    rb_define_method(callable_klass, "yield", reinterpret_cast<VALUE (*)(ANYARGS)>(&callable_call), -1);
    rb_define_method(callable_klass, "arity", reinterpret_cast<VALUE (*)(ANYARGS)>(&callable_arity), 0);
    rb_define_method(callable_klass, "to_proc", reinterpret_cast<VALUE (*)(ANYARGS)>(&callable_to_proc), 0);
    rb_gc_register_mark_object(callable_klass);
    callable_class() = callable_klass;
    auto& e = errors();
    e.error = rb_define_module_under(module, "Error");
    rb_define_method(e.error, "cpp_class", reinterpret_cast<VALUE (*)(ANYARGS)>(&error_cpp_class), 0);
    const auto define_error = [&](const char* name, VALUE base) {
        VALUE klass = rb_define_class_under(module, name, base);
        rb_include_module(klass, e.error);
        rb_gc_register_mark_object(klass);
        return klass;
    };
    e.argument = define_error("ArgumentError", rb_eArgError);
    e.index = define_error("IndexError", rb_eIndexError);
    e.key = define_error("KeyError", rb_eKeyError);
    e.range = define_error("RangeError", rb_eRangeError);
    e.type = define_error("TypeError", rb_eTypeError);
    e.not_implemented = define_error("NotImplementedError", rb_eNotImpError);
    e.runtime = define_error("RuntimeError", rb_eRuntimeError);
    e.zero_division = define_error("ZeroDivisionError", rb_eZeroDivError);
    e.io = define_error("IOError", rb_eIOError);
    e.frozen = rb_eFrozenError;
    int state = 0;
    VALUE set = rb_protect([](VALUE) -> VALUE { return rb_const_get(rb_cObject, rb_intern("Set")); }, Qnil, &state);
    if (state) { rb_set_errinfo(Qnil); set = Qnil; }
    set_class() = set;
}
} } // ROSETTA_RB_MODULE::rosetta_rb
)cpp"};
} // namespace rosetta::detail
