#pragma once

namespace rosetta::detail {
inline constexpr const char* cabi_runtime = R"cpp(
namespace {
namespace rosetta_abi {
// This private C ABI uses fixed-width fields and pointer-sized lengths. No managed
// objects or C++ exceptions cross the boundary. Result bytes are caller-freed.
struct Value {
    std::uint64_t tag;
    std::int64_t integer;
    std::uint64_t unsigned_integer;
    double real;
    const char* data;
    std::size_t size;
};
struct Result { Value value; std::uint64_t error; };
struct conversion_error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Native { virtual ~Native() = default; };
template<class T> struct Holder final : Native {
    std::unique_ptr<T> value;
    explicit Holder(std::unique_ptr<T> object) : value(std::move(object)) {}
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::unique_ptr<Native>> owners;
    std::uint64_t next = 1;
};
inline Registry& registry() { static Registry value; return value; }
template<class T> std::uint64_t own(std::unique_ptr<T> value) {
    auto& storage = registry();
    if (storage.next == 0) throw std::overflow_error("native owner identifiers exhausted");
    const auto id = storage.next++;
    storage.owners.emplace(id, std::make_unique<Holder<T>>(std::move(value)));
    return id;
}
template<class T> T* object(std::uint64_t id) {
    const auto& owners = registry().owners;
    const auto found = owners.find(id);
    if (found == owners.end()) throw conversion_error("native object is closed");
    const auto owner = dynamic_cast<Holder<T>*>(found->second.get());
    if (!owner) throw conversion_error("foreign native owner type");
    return owner->value.get();
}
[[maybe_unused]] inline void arity(std::size_t count, std::size_t minimum, std::size_t maximum) {
    if (count < minimum || count > maximum) throw conversion_error("incorrect number of positional arguments");
}
template<class T> T from(const Value& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        if (value.tag != 5) throw conversion_error("expected a string");
        return std::string(value.data, value.size);
    } else if constexpr (std::is_same_v<T, bool>) {
        if (value.tag != 4) throw conversion_error("expected a Bool");
        return value.unsigned_integer != 0;
    } else {
        if (value.tag != 1 && value.tag != 2 && value.tag != 3 && value.tag != 6)
            throw conversion_error("expected a real numeric scalar");
        if constexpr (std::is_integral_v<T>) {
            if (value.tag == 1) {
                const auto number = value.integer;
                if constexpr (std::is_signed_v<T>) {
                    if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
                        throw conversion_error("integer outside C++ type range");
                } else if (number < 0 || static_cast<std::uint64_t>(number) > std::numeric_limits<T>::max())
                    throw conversion_error("integer outside C++ type range");
                return static_cast<T>(number);
            }
            if (value.tag == 2) {
                if (value.unsigned_integer > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                    throw conversion_error("integer outside C++ type range");
                return static_cast<T>(value.unsigned_integer);
            }
            const auto number = value.real;
            const double exact = value.tag == 6 ? 16777216.0 : 9007199254740992.0;
            if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > exact)
                throw conversion_error("expected an exact integer; use Int64/UInt64 for large integers");
            if (static_cast<long double>(number) < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
                static_cast<long double>(number) > static_cast<long double>(std::numeric_limits<T>::max()))
                throw conversion_error("integer outside C++ type range");
            return static_cast<T>(number);
        } else {
            const double number = value.tag == 1 ? static_cast<double>(value.integer) :
                value.tag == 2 ? static_cast<double>(value.unsigned_integer) : value.real;
            if (!std::isfinite(number) || number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
                throw conversion_error("expected a finite number in C++ type range");
            return static_cast<T>(number);
        }
    }
}
inline Value bytes(const char* data, std::size_t size) {
    auto* copy = static_cast<char*>(std::malloc(size ? size : 1));
    if (!copy) throw std::bad_alloc();
    if (size) std::memcpy(copy, data, size);
    return {5, 0, 0, 0, copy, size};
}
template<class T> Value to(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) return bytes(value.data(), value.size());
    else if constexpr (std::is_same_v<T, bool>) return {4, 0, value ? 1u : 0u, 0, nullptr, 0};
    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return {1, value, 0, 0, nullptr, 0};
    else if constexpr (std::is_integral_v<T>) return {2, 0, value, 0, nullptr, 0};
    else {
        if (!std::isfinite(value)) throw conversion_error("non-finite C++ result");
        return {std::is_same_v<T, float> ? 6u : 3u, 0, 0, static_cast<double>(value), nullptr, 0};
    }
}
inline void failure(Result* result, std::uint64_t code, const char* message) noexcept {
    result->error = code;
    try { result->value = bytes(message, std::strlen(message)); }
    catch (...) { result->error = 1; result->value = {}; }
}
template<class F> void guard(Result* result, F fn) noexcept {
    *result = {};
    try {
        // Calls and destruction are serialized per generated library. Native
        // workers never enter managed code; closing cannot race an active native call.
        std::lock_guard<std::mutex> lock(registry().mutex);
        result->value = fn();
    }
    catch (const std::bad_alloc&) { failure(result, 1, "native allocation failed"); }
    catch (const std::overflow_error& e) { failure(result, 2, e.what()); }
    catch (const std::out_of_range& e) { failure(result, 3, e.what()); }
    catch (const std::invalid_argument& e) { failure(result, 4, e.what()); }
    catch (const conversion_error& e) { failure(result, 6, e.what()); }
    catch (const std::exception& e) { failure(result, 5, e.what()); }
    catch (...) { failure(result, 5, "unknown C++ exception"); }
}
} // namespace rosetta_abi
} // anonymous namespace
)cpp";

} // namespace rosetta::detail
