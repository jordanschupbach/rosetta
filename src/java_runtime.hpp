#pragma once
namespace rosetta::detail {
inline constexpr const char* java_runtime = R"cpp(
namespace { namespace rosetta_java {
struct pending_exception {};
struct conversion_error : std::runtime_error { using std::runtime_error::runtime_error; };
inline void check(JNIEnv* env) { if (env->ExceptionCheck()) throw pending_exception{}; }
class Local {
    JNIEnv* env;
    jobject value;
public:
    Local(JNIEnv* e, jobject v, bool nullable = false) : env(e), value(v) {
        check(env);
        if (!value && !nullable) throw std::bad_alloc();
    }
    ~Local() { if (value) env->DeleteLocalRef(value); }
    Local(const Local&) = delete;
    Local& operator=(const Local&) = delete;
    jobject get() const { return value; }
    jclass klass() const { return static_cast<jclass>(value); }
};
inline void exact(JNIEnv* env, jobject value, const char* name) {
    if (!value) throw conversion_error("null is not a native scalar or byte string");
    Local actual(env, env->GetObjectClass(value));
    Local expected(env, env->FindClass(name));
    if (!env->IsSameObject(actual.get(), expected.get())) throw conversion_error("unexpected Java argument type");
}
inline jmethodID method(JNIEnv* env, jclass type, const char* name, const char* signature) {
    auto id = env->GetMethodID(type, name, signature); check(env);
    if (!id) throw std::bad_alloc();
    return id;
}
template<class T> T from(JNIEnv* env, jobjectArray args, jsize index) {
    Local value(env, env->GetObjectArrayElement(args, index), true);
    if constexpr (std::is_same_v<T, std::string>) {
        exact(env, value.get(), "[B");
        const auto array = static_cast<jbyteArray>(value.get());
        const auto size = env->GetArrayLength(array); check(env);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (size) { env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(bytes.data())); check(env); }
        return bytes;
    } else if constexpr (std::is_same_v<T, bool>) {
        exact(env, value.get(), "java/lang/Boolean");
        Local type(env, env->GetObjectClass(value.get()));
        auto result = env->CallBooleanMethod(value.get(), method(env, type.klass(), "booleanValue", "()Z"));
        check(env); return result != JNI_FALSE;
    } else if constexpr (std::is_integral_v<T> && (std::is_signed_v<T> || std::is_same_v<T, char>)) {
        exact(env, value.get(), "java/lang/Long");
        Local type(env, env->GetObjectClass(value.get()));
        auto number = env->CallLongMethod(value.get(), method(env, type.klass(), "longValue", "()J")); check(env);
        if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
            throw conversion_error("integer outside C++ type range");
        return static_cast<T>(number);
    } else if constexpr (std::is_integral_v<T>) {
        // BigInteger is subclassable: accept its exact class so user overrides
        // cannot reenter native code while the registry lock is held.
        exact(env, value.get(), "java/math/BigInteger");
        Local type(env, env->GetObjectClass(value.get()));
        auto sign = env->CallIntMethod(value.get(), method(env, type.klass(), "signum", "()I")); check(env);
        auto bits = env->CallIntMethod(value.get(), method(env, type.klass(), "bitLength", "()I")); check(env);
        if (sign < 0 || bits > 64) throw conversion_error("integer outside C++ type range");
        auto signed_bits = env->CallLongMethod(value.get(), method(env, type.klass(), "longValue", "()J")); check(env);
        const auto number = static_cast<std::uint64_t>(signed_bits);
        if (number > std::numeric_limits<T>::max()) throw conversion_error("integer outside C++ type range");
        return static_cast<T>(number);
    } else {
        constexpr bool single = std::is_same_v<T, float>;
        exact(env, value.get(), single ? "java/lang/Float" : "java/lang/Double");
        Local type(env, env->GetObjectClass(value.get()));
        double number;
        if constexpr (single) number = env->CallFloatMethod(value.get(), method(env, type.klass(), "floatValue", "()F"));
        else number = env->CallDoubleMethod(value.get(), method(env, type.klass(), "doubleValue", "()D"));
        check(env);
        if (!std::isfinite(number) || number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
            throw conversion_error("expected a finite number in C++ type range");
        return static_cast<T>(number);
    }
}
inline jobject box(JNIEnv* env, const char* name, const char* signature, jvalue value) {
    Local type(env, env->FindClass(name));
    auto factory = env->GetStaticMethodID(type.klass(), "valueOf", signature); check(env);
    if (!factory) throw std::bad_alloc();
    auto result = env->CallStaticObjectMethodA(type.klass(), factory, &value); check(env);
    if (!result) throw std::bad_alloc();
    return result;
}
template<class T> jobject to(JNIEnv* env, const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) throw std::overflow_error("Java byte array size limit");
        auto result = env->NewByteArray(static_cast<jsize>(value.size())); check(env);
        if (!result) throw std::bad_alloc();
        if (!value.empty()) {
            env->SetByteArrayRegion(result, 0, static_cast<jsize>(value.size()), reinterpret_cast<const jbyte*>(value.data())); check(env);
        }
        return result;
    } else if constexpr (std::is_same_v<T, bool>) {
        jvalue item{}; item.z = value ? JNI_TRUE : JNI_FALSE;
        return box(env, "java/lang/Boolean", "(Z)Ljava/lang/Boolean;", item);
    } else if constexpr (std::is_integral_v<T> && (std::is_signed_v<T> || std::is_same_v<T, char>)) {
        jvalue item{}; item.j = static_cast<jlong>(value);
        return box(env, "java/lang/Long", "(J)Ljava/lang/Long;", item);
    } else if constexpr (std::is_integral_v<T>) {
        std::uint64_t number = value;
        jbyte bytes[8];
        for (int i = 7; i >= 0; --i) { const unsigned char part = static_cast<unsigned char>(number & 255); std::memcpy(&bytes[i], &part, 1); number >>= 8; }
        Local array(env, env->NewByteArray(8));
        env->SetByteArrayRegion(static_cast<jbyteArray>(array.get()), 0, 8, bytes); check(env);
        Local type(env, env->FindClass("java/math/BigInteger"));
        auto result = env->NewObject(type.klass(), method(env, type.klass(), "<init>", "(I[B)V"), 1, array.get()); check(env);
        if (!result) throw std::bad_alloc();
        return result;
    } else {
        if (!std::isfinite(value)) throw conversion_error("non-finite C++ result");
        jvalue item{};
        if constexpr (std::is_same_v<T, float>) { item.f = value; return box(env, "java/lang/Float", "(F)Ljava/lang/Float;", item); }
        else { item.d = value; return box(env, "java/lang/Double", "(D)Ljava/lang/Double;", item); }
    }
}
struct Native { virtual ~Native() = default; };
template<class T> struct Holder final : Native {
    std::unique_ptr<T> value;
    explicit Holder(std::unique_ptr<T> object) : value(std::move(object)) {}
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<jlong, std::unique_ptr<Native>> owners;
    std::uint64_t next = 1;
};
inline Registry& registry() { static Registry value; return value; }
template<class T> jobject own(JNIEnv* env, std::unique_ptr<T> value) {
    auto& storage = registry();
    if (storage.next > static_cast<std::uint64_t>(std::numeric_limits<jlong>::max())) throw std::overflow_error("native owner identifiers exhausted");
    auto id = static_cast<jlong>(storage.next++);
    storage.owners.emplace(id, std::make_unique<Holder<T>>(std::move(value)));
    try { return to(env, id); }
    catch (...) { storage.owners.erase(id); throw; }
}
template<class T> T* object(jlong id) {
    const auto& owners = registry().owners;
    auto found = owners.find(id);
    if (found == owners.end()) throw conversion_error("native object is closed");
    auto* owner = dynamic_cast<Holder<T>*>(found->second.get());
    if (!owner) throw conversion_error("foreign native owner type");
    return owner->value.get();
}
inline void arity(jsize count, jsize expected) {
    if (count != expected) throw conversion_error("incorrect number of positional arguments");
}
inline void report(JNIEnv* env, const char* name, const char* message) noexcept {
    if (env->ExceptionCheck()) return;
    try {
        // ThrowNew consumes modified UTF-8. Native diagnostics are bytes, so
        // restrict them to ASCII instead of misinterpreting arbitrary input.
        std::string safe;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(message); *p; ++p)
            safe += *p < 128 ? static_cast<char>(*p) : '?';
        auto type = env->FindClass(name);
        if (!env->ExceptionCheck() && type) env->ThrowNew(type, safe.c_str());
        if (type) env->DeleteLocalRef(type);
    } catch (...) {
        auto type = env->FindClass("java/lang/OutOfMemoryError");
        if (!env->ExceptionCheck() && type) env->ThrowNew(type, "native allocation failed");
        if (type) env->DeleteLocalRef(type);
    }
}
template<class F> jobject guard(JNIEnv* env, F fn) noexcept {
    try { std::lock_guard<std::mutex> lock(registry().mutex); return fn(); }
    catch (const pending_exception&) { return nullptr; }
    catch (const std::bad_alloc&) { report(env, "java/lang/OutOfMemoryError", "native allocation failed"); }
    catch (const std::overflow_error& e) { report(env, "java/lang/ArithmeticException", e.what()); }
    catch (const std::out_of_range& e) { report(env, "java/lang/IndexOutOfBoundsException", e.what()); }
    catch (const std::invalid_argument& e) { report(env, "java/lang/IllegalArgumentException", e.what()); }
    catch (const conversion_error& e) { report(env, "java/lang/IllegalArgumentException", e.what()); }
    catch (const std::exception& e) { report(env, "java/lang/RuntimeException", e.what()); }
    catch (...) { report(env, "java/lang/RuntimeException", "unknown C++ exception"); }
    return nullptr;
}
} } // anonymous namespace, rosetta_java
)cpp";
} // namespace rosetta::detail
