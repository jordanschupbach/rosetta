#pragma once

#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) && !_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT
#define FEATURE_LIBRARY_DOUBLE_FROM_CHARS 0
#include <cctype>
#include <cerrno>
#include <clocale>
#include <xlocale.h>
#else
#define FEATURE_LIBRARY_DOUBLE_FROM_CHARS 1
#endif
#include <vector>

namespace feature_library::numeric {
template<class UInt> UInt decimal_magnitude(std::string_view text, UInt limit) {
    if (text.empty()) throw std::invalid_argument("expected decimal digits");
    UInt value = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9') throw std::invalid_argument("expected complete decimal digits");
        const auto next = static_cast<UInt>(digit - '0');
        if (next > limit || value > (limit - next) / 10) throw std::out_of_range("decimal integer out of range");
        value = value * 10 + next;
    }
    return value;
}
inline std::uint64_t u64(std::string_view text) { return decimal_magnitude(text, std::numeric_limits<std::uint64_t>::max()); }
inline std::string u64_add(std::string_view left, std::string_view right) {
    const auto a = u64(left), b = u64(right);
    if (a > std::numeric_limits<std::uint64_t>::max() - b) throw std::overflow_error("unsigned 64-bit addition overflow");
    return std::to_string(a + b);
}

#if !defined(ROSETTA_DISABLE_INT128) && defined(__SIZEOF_INT128__) && __SIZEOF_INT128__ == 16 && CHAR_BIT == 8
inline constexpr bool has_int128 = true;
__extension__ typedef unsigned __int128 UInt128;
__extension__ typedef __int128 Int128;
inline constexpr UInt128 sign_bit = UInt128{1} << 127;
inline constexpr UInt128 unsigned_max = ~UInt128{0};
inline constexpr Int128 signed_max = static_cast<Int128>(sign_bit - 1);
inline constexpr Int128 signed_min = -signed_max - 1;
inline UInt128 magnitude(Int128 value) {
    return value < 0 ? static_cast<UInt128>(-(value + 1)) + 1 : static_cast<UInt128>(value);
}
inline Int128 from_magnitude(UInt128 value, bool negative) {
    if (negative && value == sign_bit) return signed_min;
    const auto positive = static_cast<Int128>(value); // caller checked the sign-specific limit
    return negative ? -positive : positive;
}
inline Int128 i128(std::string_view text) {
    const bool negative = !text.empty() && text.front() == '-';
    if (negative) text.remove_prefix(1);
    return from_magnitude(decimal_magnitude(text, negative ? sign_bit : sign_bit - 1), negative);
}
inline UInt128 u128(std::string_view text) { return decimal_magnitude(text, unsigned_max); }
inline std::string decimal(UInt128 value) {
    char digits[40];
    auto* end = digits + sizeof(digits);
    auto* begin = end;
    do { *--begin = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    return {begin, end};
}
inline std::string decimal(Int128 value) { return value < 0 ? "-" + decimal(magnitude(value)) : decimal(static_cast<UInt128>(value)); }
inline std::string signed_identity(std::string_view text) { return decimal(i128(text)); }
inline std::string unsigned_identity(std::string_view text) { return decimal(u128(text)); }
inline std::string signed_add(std::string_view left, std::string_view right) {
    const auto a = i128(left), b = i128(right);
    if ((b > 0 && a > signed_max - b) || (b < 0 && a < signed_min - b))
        throw std::overflow_error("signed 128-bit addition overflow");
    return decimal(a + b);
}
inline std::string unsigned_add(std::string_view left, std::string_view right) {
    const auto a = u128(left), b = u128(right);
    if (a > unsigned_max - b) throw std::overflow_error("unsigned 128-bit addition overflow");
    return decimal(a + b);
}
inline std::string signed_multiply(std::string_view left, std::string_view right) {
    const auto a = i128(left), b = i128(right);
    const bool negative = (a < 0) != (b < 0);
    const auto ma = magnitude(a), mb = magnitude(b), limit = negative ? sign_bit : sign_bit - 1;
    if (mb && ma > limit / mb) throw std::overflow_error("signed 128-bit multiplication overflow");
    return decimal(from_magnitude(ma * mb, negative));
}
inline std::string unsigned_multiply(std::string_view left, std::string_view right) {
    const auto a = u128(left), b = u128(right);
    if (b && a > unsigned_max / b) throw std::overflow_error("unsigned 128-bit multiplication overflow");
    return decimal(a * b);
}
#else
inline constexpr bool has_int128 = false;
[[noreturn]] inline void unavailable() { throw std::runtime_error("128-bit integers are unavailable in this build"); }
inline std::string signed_identity(std::string_view) { unavailable(); }
inline std::string unsigned_identity(std::string_view) { unavailable(); }
inline std::string signed_add(std::string_view, std::string_view) { unavailable(); }
inline std::string unsigned_add(std::string_view, std::string_view) { unavailable(); }
inline std::string signed_multiply(std::string_view, std::string_view) { unavailable(); }
inline std::string unsigned_multiply(std::string_view, std::string_view) { unavailable(); }
#endif

namespace detail {
template <class T, class = void> struct has_from_chars : std::false_type {};
template <class T>
struct has_from_chars<T, std::void_t<decltype(std::from_chars(std::declval<const char*>(),
    std::declval<const char*>(), std::declval<T&>(), std::chars_format::general))>> : std::true_type {};

#if FEATURE_LIBRARY_DOUBLE_FROM_CHARS
inline std::from_chars_result from_chars_double(const char* first, const char* last, double& value) {
    return std::from_chars(first, last, value, std::chars_format::general);
}
#else
// Older macOS deployment targets lack floating-point from_chars in libc++.
// Emulate its general-format contract with locale-independent strtod_l.
inline std::from_chars_result from_chars_double(const char* first, const char* last, double& value) {
    const std::string text(first, last);
    const char* digits = text.c_str() + (!text.empty() && text[0] == '-' ? 1 : 0);
    if (*digits == '+' || std::isspace(static_cast<unsigned char>(*digits)))
        return {first, std::errc::invalid_argument};
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) { // general format never reads hex
        value = text[0] == '-' ? -0.0 : 0.0;
        return {first + (digits - text.c_str()) + 1, std::errc{}};
    }
    static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
    char* end = nullptr;
    errno = 0;
    const double parsed = strtod_l(text.c_str(), &end, c_locale);
    if (end == text.c_str()) return {first, std::errc::invalid_argument};
    const auto ptr = first + (end - text.c_str());
    if (errno == ERANGE && (std::isinf(parsed) || parsed == 0)) return {ptr, std::errc::result_out_of_range};
    value = parsed;
    return {ptr, std::errc{}};
}
#endif

// libc++ (macOS) has no long double from_chars. Where long double has the same
// format as double (e.g. Apple arm64), parsing through double is exact.
template <class T>
std::from_chars_result from_chars_long_double(const char* first, const char* last, T& value) {
    if constexpr (has_from_chars<T>::value) {
        return std::from_chars(first, last, value, std::chars_format::general);
    } else {
        static_assert(std::numeric_limits<T>::digits == std::numeric_limits<double>::digits &&
                std::numeric_limits<T>::max_exponent == std::numeric_limits<double>::max_exponent,
            "this standard library lacks long double from_chars and long double is wider than double");
        double parsed = 0;
        const auto result = from_chars_double(first, last, parsed);
        if (result.ec == std::errc{}) value = parsed;
        return result;
    }
}
} // namespace detail

inline long double parse_float(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("expected a complete decimal long double");
    long double value = 0;
    const auto result = detail::from_chars_long_double(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc::result_out_of_range) throw std::out_of_range("long double value is out of range");
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("expected a complete decimal long double");
    if (!std::isfinite(value)) throw std::invalid_argument("finite long double required");
    return value;
}
inline std::string format(long double value) {
    if (!std::isfinite(value)) throw std::overflow_error("long double arithmetic overflow");
    std::string result(static_cast<std::size_t>(std::numeric_limits<long double>::max_digits10) + 64, '\0');
    const auto printed = std::to_chars(result.data(), result.data() + result.size(), value,
        std::chars_format::scientific, std::numeric_limits<long double>::max_digits10 - 1);
    if (printed.ec != std::errc{}) throw std::runtime_error("long double formatting failed");
    result.resize(static_cast<std::size_t>(printed.ptr - result.data()));
    return result;
}
inline std::string float_add(std::string_view left, std::string_view right) {
    const auto a = parse_float(left), b = parse_float(right);
    const auto maximum = std::numeric_limits<long double>::max();
    if ((b > 0 && a > maximum - b) || (b < 0 && a < -maximum - b))
        throw std::overflow_error("long double arithmetic overflow");
    return format(a + b);
}
inline std::string float_multiply(std::string_view left, std::string_view right) {
    const auto a = parse_float(left), b = parse_float(right);
    if (std::fabs(b) > 1 && std::fabs(a) > std::numeric_limits<long double>::max() / std::fabs(b))
        throw std::overflow_error("long double arithmetic overflow");
    return format(a * b);
}
} // namespace feature_library::numeric
