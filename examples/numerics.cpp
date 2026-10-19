#include "demo.hpp"
#include "numeric_library.hpp"

namespace demo {
namespace numeric = feature_library::numeric;
std::string u64_decimal(std::string text) { return std::to_string(numeric::u64(text)); }
std::string u64_add(std::string left, std::string right) { return numeric::u64_add(left, right); }
bool int128_available() { return numeric::has_int128; }
std::string signed128_decimal(std::string text) { return numeric::signed_identity(text); }
std::string unsigned128_decimal(std::string text) { return numeric::unsigned_identity(text); }
std::string signed128_add(std::string left, std::string right) { return numeric::signed_add(left, right); }
std::string unsigned128_add(std::string left, std::string right) { return numeric::unsigned_add(left, right); }
std::string signed128_multiply(std::string left, std::string right) { return numeric::signed_multiply(left, right); }
std::string unsigned128_multiply(std::string left, std::string right) { return numeric::unsigned_multiply(left, right); }
std::string long_double_decimal(std::string text) { return numeric::format(numeric::parse_float(text)); }
std::string long_double_add(std::string left, std::string right) { return numeric::float_add(left, right); }
std::string long_double_multiply(std::string left, std::string right) { return numeric::float_multiply(left, right); }
int long_double_compare(std::string left, std::string right) {
    const auto a = numeric::parse_float(left), b = numeric::parse_float(right);
    return a < b ? -1 : a > b ? 1 : 0;
}
bool long_double_signbit(std::string text) { return std::signbit(numeric::parse_float(text)); }
int long_double_precision() { return std::numeric_limits<long double>::digits; }
std::vector<std::string> long_double_bounds() {
    return {numeric::format(std::numeric_limits<long double>::lowest()), numeric::format(std::numeric_limits<long double>::max()),
        numeric::format(std::numeric_limits<long double>::min()), numeric::format(std::numeric_limits<long double>::denorm_min())};
}
} // namespace demo
