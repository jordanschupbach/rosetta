#pragma once
#include <string>
namespace f_values {
inline unsigned long long unsigned_maximum() { return 18446744073709551615ULL; }
inline std::string defaulted_string(const std::string& value = std::string("a\0b", 3), int count = 3) {
    return std::string(value.data(), static_cast<std::size_t>(count) < value.size() ? static_cast<std::size_t>(count) : value.size());
}
inline int xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx(int a, int b, int c, int d, int e) { return a+b+c+d+e; }
}
