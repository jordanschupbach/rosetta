#pragma once

namespace rosetta::detail {
inline constexpr const char* julia_runtime = R"cpp(
namespace {
namespace rosetta_jl {
// This private C ABI uses fixed-width fields and pointer-sized lengths. No Julia
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
        // workers never enter Julia; closing cannot race an active native call.
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
} // namespace rosetta_jl
} // anonymous namespace
)cpp";

inline constexpr const char* julia_interface_runtime = R"jl(
using Libdl
import Serialization
struct RosettaError <: Exception
    kind::Symbol
    message::String
end
Base.showerror(io::IO, e::RosettaError) = print(io, "Rosetta ", e.kind, ": ", e.message)
struct _Value
    tag::UInt64
    integer::Int64
    unsigned_integer::UInt64
    real::Float64
    data::Ptr{UInt8}
    size::Csize_t
end
struct _Result
    value::_Value
    error::UInt64
end
const _empty = _Value(0, 0, 0, 0, C_NULL, 0)
_pack(x::Bool) = _Value(4, 0, x, 0, C_NULL, 0)
_pack(x::Union{Int8,Int16,Int32,Int64}) = _Value(1, x, 0, 0, C_NULL, 0)
_pack(x::Union{UInt8,UInt16,UInt32,UInt64}) = _Value(2, 0, x, 0, C_NULL, 0)
_pack(x::Float64) = _Value(3, 0, 0, x, C_NULL, 0)
_pack(x::Float32) = _Value(6, 0, 0, x, C_NULL, 0)
_pack(x::String) = _Value(5, 0, 0, 0, pointer(x), sizeof(x))
_pack(x) = throw(RosettaError(:conversion, "expected a machine integer, float, Bool or string"))
_text(v::_Value) = v.size == 0 ? "" : unsafe_string(v.data, v.size)
function _decode(v::_Value)
    v.tag == 0 && return nothing
    v.tag == 1 && return v.integer
    v.tag == 2 && return v.unsigned_integer
    v.tag == 3 && return v.real
    v.tag == 4 && return v.unsigned_integer != 0
    v.tag == 5 && return _text(v)
    v.tag == 6 && return Float32(v.real)
    throw(RosettaError(:conversion, "invalid native result tag"))
end
abstract type _Owner end
Base.copy(::_Owner) = throw(RosettaError(:ownership, "native owners cannot be copied"))
Base.deepcopy_internal(::_Owner, ::IdDict) = throw(RosettaError(:ownership, "native owners cannot be copied"))
Serialization.serialize(::Serialization.AbstractSerializer, ::_Owner) = throw(RosettaError(:ownership, "native owners cannot be serialized"))
)jl";
} // namespace rosetta::detail
