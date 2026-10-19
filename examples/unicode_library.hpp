#pragma once

#include <bit>
#include <climits>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace feature_library::unicode {
static_assert(CHAR_BIT == 8, "Unicode byte adapter requires 8-bit bytes");
constexpr bool scalar(std::uintmax_t value) { return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff); }
static_assert(scalar(0x10ffff) && !scalar(0x100000000ULL));
inline void require_scalar(std::uintmax_t value) {
    if (!scalar(value)) throw std::invalid_argument("invalid Unicode scalar value");
}
[[noreturn]] inline void invalid_utf8(std::size_t offset) {
    throw std::invalid_argument("invalid UTF-8 sequence at byte " + std::to_string(offset));
}
inline std::u32string decode(std::string_view bytes) {
    std::u32string result;
    result.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size();) {
        const auto start = i;
        const auto first = std::bit_cast<unsigned char>(bytes[i++]);
        std::uint32_t value = 0, minimum = 0;
        unsigned trailing = 0;
        if (first <= 0x7f) value = first;
        else if (first >= 0xc2 && first <= 0xdf) { value = first & 0x1f; trailing = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { value = first & 0x0f; trailing = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { value = first & 0x07; trailing = 3; minimum = 0x10000; }
        else invalid_utf8(start);
        if (trailing > bytes.size() - i) invalid_utf8(start);
        for (unsigned n = 0; n < trailing; ++n) {
            const auto next = std::bit_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80) invalid_utf8(i - 1);
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || !scalar(value)) invalid_utf8(start);
        result.push_back(static_cast<char32_t>(value));
    }
    return result;
}
inline void byte(std::string& result, std::uint32_t value) {
    result.push_back(std::bit_cast<char>(static_cast<unsigned char>(value)));
}
inline std::string encode(std::u32string_view points) {
    std::string result;
    result.reserve(points.size());
    for (char32_t point : points) {
        const auto value = static_cast<std::uintmax_t>(point);
        require_scalar(value);
        if (value <= 0x7f) byte(result, value);
        else if (value <= 0x7ff) { byte(result, 0xc0 | (value >> 6)); byte(result, 0x80 | (value & 0x3f)); }
        else if (value <= 0xffff) {
            byte(result, 0xe0 | (value >> 12)); byte(result, 0x80 | ((value >> 6) & 0x3f)); byte(result, 0x80 | (value & 0x3f));
        } else {
            byte(result, 0xf0 | (value >> 18)); byte(result, 0x80 | ((value >> 12) & 0x3f));
            byte(result, 0x80 | ((value >> 6) & 0x3f)); byte(result, 0x80 | (value & 0x3f));
        }
    }
    return result;
}
inline std::u16string utf16(std::u32string_view points) {
    std::u16string result;
    result.reserve(points.size());
    for (char32_t point : points) {
        auto value = static_cast<std::uintmax_t>(point);
        require_scalar(value);
        if (value <= 0xffff) result.push_back(static_cast<char16_t>(value));
        else {
            value -= 0x10000;
            result.push_back(static_cast<char16_t>(0xd800 | (value >> 10)));
            result.push_back(static_cast<char16_t>(0xdc00 | (value & 0x3ff)));
        }
    }
    return result;
}
inline std::u32string from_utf16(std::u16string_view units) {
    std::u32string result;
    result.reserve(units.size());
    for (std::size_t i = 0; i < units.size(); ++i) {
        std::uintmax_t value = units[i];
        if (value > 0xffff) throw std::out_of_range("UTF-16 code unit is out of range");
        if (value >= 0xd800 && value <= 0xdbff) {
            if (++i == units.size() || units[i] < 0xdc00 || units[i] > 0xdfff)
                throw std::invalid_argument("unpaired UTF-16 high surrogate");
            value = 0x10000 + ((value - 0xd800) << 10) + (static_cast<std::uint32_t>(units[i]) - 0xdc00);
        } else if (value >= 0xdc00 && value <= 0xdfff) throw std::invalid_argument("unpaired UTF-16 low surrogate");
        result.push_back(static_cast<char32_t>(value));
    }
    return result;
}
inline std::u8string char8(std::string_view bytes) {
    (void)decode(bytes);
    std::u8string result;
    result.reserve(bytes.size());
    for (char item : bytes) result.push_back(static_cast<char8_t>(std::bit_cast<unsigned char>(item)));
    return result;
}
inline std::string bytes(std::u8string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char8_t item : value) byte(result, static_cast<unsigned char>(item));
    return result;
}
inline std::wstring wide(std::u32string_view points) {
    std::wstring result;
    if constexpr (sizeof(wchar_t) == 2 && std::numeric_limits<wchar_t>::max() >= 0xffff) {
        for (char16_t unit : utf16(points)) result.push_back(static_cast<wchar_t>(unit));
    } else if constexpr (sizeof(wchar_t) == 4 && std::numeric_limits<wchar_t>::max() >= 0x10ffff) {
        result.reserve(points.size());
        for (char32_t point : points) { require_scalar(point); result.push_back(static_cast<wchar_t>(point)); }
    } else throw std::runtime_error("unsupported wchar_t representation for Unicode adapter");
    return result;
}
inline std::u32string from_wide(std::wstring_view value) {
    if constexpr (sizeof(wchar_t) == 2 && std::numeric_limits<wchar_t>::max() >= 0xffff) {
        std::u16string units;
        units.reserve(value.size());
        for (wchar_t unit : value) units.push_back(static_cast<char16_t>(unit));
        return from_utf16(units);
    } else if constexpr (sizeof(wchar_t) == 4 && std::numeric_limits<wchar_t>::max() >= 0x10ffff) {
        std::u32string points;
        points.reserve(value.size());
        for (wchar_t unit : value) {
            const auto point = static_cast<std::uint32_t>(unit);
            require_scalar(point);
            points.push_back(static_cast<char32_t>(point));
        }
        return points;
    } else throw std::runtime_error("unsupported wchar_t representation for Unicode adapter");
}
} // namespace feature_library::unicode
