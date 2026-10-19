#pragma once

namespace rosetta::detail {
inline constexpr const char* ruby_runtime = R"cpp(
namespace { namespace rosetta_rb {
struct ruby_jump { int state; };
struct error { VALUE type; const char* message; };
[[noreturn]] inline void fail(VALUE type, const char* message) { throw error{type, message}; }
// A Ruby jump is caught before it can skip any C++ automatic destructor.
// These callbacks contain only Ruby C API calls and trivial stack objects.
template<class F> VALUE checked(F fn) {
    static_assert(std::is_trivially_destructible_v<F>);
    int state = 0;
    VALUE result = rb_protect([](VALUE data) -> VALUE {
        return (*reinterpret_cast<F*>(data))();
    }, reinterpret_cast<VALUE>(&fn), &state);
    if (state) throw ruby_jump{state};
    return result;
}
template<class F> VALUE guard(F fn) {
    static_assert(std::is_trivially_destructible_v<F>);
    int state = 0;
    VALUE type = Qnil;
    char message[1024] = {};
    try { return fn(); }
    catch (const ruby_jump& e) { state = e.state; }
    catch (const error& e) { type = e.type; std::snprintf(message, sizeof(message), "%s", e.message); }
    catch (const std::bad_alloc&) { type = rb_eNoMemError; std::snprintf(message, sizeof(message), "native allocation failed"); }
    catch (const std::overflow_error& e) { type = rb_eRangeError; std::snprintf(message, sizeof(message), "%s", e.what()); }
    catch (const std::out_of_range& e) { type = rb_eIndexError; std::snprintf(message, sizeof(message), "%s", e.what()); }
    catch (const std::invalid_argument& e) { type = rb_eArgError; std::snprintf(message, sizeof(message), "%s", e.what()); }
    catch (const std::exception& e) { type = rb_eRuntimeError; std::snprintf(message, sizeof(message), "%s", e.what()); }
    catch (...) { type = rb_eRuntimeError; std::snprintf(message, sizeof(message), "unknown C++ exception"); }
    // All C++ exceptions and nontrivial locals have been destroyed here.
    if (state) rb_jump_tag(state);
    rb_raise(type, "%s", message);
    return Qnil;
}
[[maybe_unused]] inline int arity(int argc, int minimum, int maximum) {
    if (rb_keyword_given_p()) fail(rb_eArgError, "only positional arguments are supported");
    if (argc < minimum || argc > maximum) fail(rb_eArgError, "incorrect number of positional arguments");
    return argc;
}
template<class T> T from(VALUE value) {
    if constexpr (std::is_same_v<T, bool>) {
        if (value != Qtrue && value != Qfalse) fail(rb_eTypeError, "expected true or false");
        return value == Qtrue;
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!RB_TYPE_P(value, T_STRING)) fail(rb_eTypeError, "expected String");
        return std::string(RSTRING_PTR(value), static_cast<std::size_t>(RSTRING_LEN(value)));
    } else if constexpr (std::is_integral_v<T>) {
        if (!RB_INTEGER_TYPE_P(value)) fail(rb_eTypeError, "expected Integer");
        unsigned long long magnitude = 0;
        int sign = 0;
        checked([&]() -> VALUE {
            sign = rb_integer_pack(value, &magnitude, 1, sizeof(magnitude), 0,
                                   INTEGER_PACK_NATIVE_BYTE_ORDER | INTEGER_PACK_LSWORD_FIRST);
            return Qnil;
        });
        if (sign == 2 || sign == -2) fail(rb_eRangeError, "integer outside C++ type range");
        if constexpr (std::is_signed_v<T>) {
            const auto maximum = static_cast<unsigned long long>(std::numeric_limits<T>::max());
            if (sign < 0) {
                if (magnitude > maximum + 1) fail(rb_eRangeError, "integer outside C++ type range");
                if (magnitude == maximum + 1) return std::numeric_limits<T>::lowest();
                return static_cast<T>(-static_cast<long long>(magnitude));
            }
            if (magnitude > maximum) fail(rb_eRangeError, "integer outside C++ type range");
        } else if (sign < 0 || magnitude > std::numeric_limits<T>::max())
            fail(rb_eRangeError, "integer outside C++ type range");
        return static_cast<T>(magnitude);
    } else {
        double number = 0;
        if (RB_FLOAT_TYPE_P(value)) number = RFLOAT_VALUE(value);
        else if (RB_FIXNUM_P(value)) number = static_cast<double>(FIX2LONG(value));
        else if (RB_TYPE_P(value, T_BIGNUM)) checked([&]() -> VALUE { number = rb_big2dbl(value); return Qnil; });
        else fail(rb_eTypeError, "expected Float or Integer");
        if (!std::isfinite(number)) fail(rb_eRangeError, "expected a finite number");
        if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
            fail(rb_eRangeError, "number outside C++ type range");
        return static_cast<T>(number);
    }
}
template<class T> VALUE to(const T& value) {
    if constexpr (std::is_same_v<T, bool>) return value ? Qtrue : Qfalse;
    else if constexpr (std::is_same_v<T, std::string>) {
        if (value.size() > static_cast<std::size_t>(LONG_MAX)) fail(rb_eRangeError, "string too long for Ruby");
        return checked([&]() { return rb_str_new(value.data(), static_cast<long>(value.size())); });
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
        return checked([&]() { return LL2NUM(value); });
    else if constexpr (std::is_integral_v<T>) return checked([&]() { return ULL2NUM(value); });
    else {
        if (!std::isfinite(value)) fail(rb_eRangeError, "non-finite C++ result");
        return checked([&]() { return rb_float_new(value); });
    }
}
template<class T> struct Object { T* ptr = nullptr; bool initialized = false; };
template<class T> void close(Object<T>* wrapper) noexcept {
    auto* native = wrapper->ptr;
    wrapper->ptr = nullptr;
    delete native;
}
template<class T> void free_object(void* data) noexcept {
    auto* wrapper = static_cast<Object<T>*>(data);
    if (wrapper) { close(wrapper); delete wrapper; }
}
template<class T> std::size_t size_object(const void*) { return sizeof(Object<T>); }
template<class T> const rb_data_type_t* descriptor() {
    static const rb_data_type_t type = {"Rosetta owned object",
        {nullptr, &free_object<T>, &size_object<T>, nullptr, {nullptr}},
        nullptr, nullptr, RUBY_TYPED_FREE_IMMEDIATELY};
    return &type;
}
template<class T> Object<T>* wrapper(VALUE self) {
    void* result = nullptr;
    checked([&]() -> VALUE { result = rb_check_typeddata(self, descriptor<T>()); return Qnil; });
    return static_cast<Object<T>*>(result);
}
template<class T> T* object(VALUE self) {
    auto* result = wrapper<T>(self)->ptr;
    if (!result) fail(rb_eRuntimeError, "native object is closed or uninitialized");
    return result;
}
[[maybe_unused]] inline void writable(VALUE self) {
    checked([&]() -> VALUE { rb_check_frozen(self); return Qnil; });
}
template<class T> VALUE allocate(VALUE klass) {
    return guard([&]() -> VALUE {
        auto data = std::make_unique<Object<T>>();
        VALUE self = checked([&]() { return rb_data_typed_object_wrap(klass, data.get(), descriptor<T>()); });
        data.release();
        return self;
    });
}
template<class T> VALUE close_method(VALUE self) {
    return guard([&]() -> VALUE { close(wrapper<T>(self)); return Qnil; });
}
[[maybe_unused]] inline VALUE uncopyable(VALUE, VALUE) {
    return guard([]() -> VALUE { fail(rb_eTypeError, "native objects cannot be copied or subclassed"); });
}
[[maybe_unused]] inline VALUE unmarshalable(VALUE) {
    return guard([]() -> VALUE { fail(rb_eTypeError, "native objects cannot be marshalled"); });
}
template<class T> VALUE open(int argc, VALUE* argv, VALUE klass) {
    return guard([&]() -> VALUE {
        if (!rb_block_given_p()) fail(rb_eArgError, "open requires a block");
        arity(argc, 0, INT_MAX);
        VALUE self = checked([&]() { return rb_class_new_instance(argc, argv, klass); });
        struct Context { VALUE self; Object<T>* data; } context{self, wrapper<T>(self)};
        VALUE result = checked([&]() {
            return rb_ensure([](VALUE value) -> VALUE {
                return rb_yield(reinterpret_cast<Context*>(value)->self);
            }, reinterpret_cast<VALUE>(&context), [](VALUE value) -> VALUE {
                close(reinterpret_cast<Context*>(value)->data);
                return Qnil;
            }, reinterpret_cast<VALUE>(&context));
        });
        RB_GC_GUARD(self);
        return result;
    });
}
} } // anonymous namespace, rosetta_rb
)cpp";
} // namespace rosetta::detail
