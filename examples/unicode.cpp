#include "demo.hpp"
#include "unicode_library.hpp"

namespace demo {
namespace unicode = feature_library::unicode;
static_assert(std::numeric_limits<unsigned int>::max() >= 0x10ffff, "Unicode facade requires at least 21-bit unsigned integers");
std::vector<unsigned int> unicode_decode(std::string bytes) {
    const auto points = unicode::decode(bytes);
    return {points.begin(), points.end()};
}
std::string unicode_encode(std::vector<unsigned int> points) {
    std::u32string native;
    native.reserve(points.size());
    for (auto point : points) { unicode::require_scalar(point); native.push_back(static_cast<char32_t>(point)); }
    return unicode::encode(native);
}
std::vector<unsigned int> unicode_utf16(std::string bytes) {
    const auto units = unicode::utf16(unicode::decode(bytes));
    return {units.begin(), units.end()};
}
std::string unicode_from_utf16(std::vector<unsigned int> units) {
    std::u16string native;
    native.reserve(units.size());
    for (auto unit : units) {
        if (unit > 0xffff) throw std::out_of_range("UTF-16 code unit is out of range");
        native.push_back(static_cast<char16_t>(unit));
    }
    return unicode::encode(unicode::from_utf16(native));
}
std::string unicode_char8(std::string bytes) { return unicode::bytes(unicode::char8(bytes)); }
std::string unicode_wide(std::string bytes) { return unicode::encode(unicode::from_wide(unicode::wide(unicode::decode(bytes)))); }
unsigned long long unicode_wide_length(std::string bytes) { return unicode::wide(unicode::decode(bytes)).size(); }
unsigned long long unicode_scalar_count(std::string bytes) { return unicode::decode(bytes).size(); }
int unicode_wchar_bits() { return sizeof(wchar_t) * CHAR_BIT; }
} // namespace demo
