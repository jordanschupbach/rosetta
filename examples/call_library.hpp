#pragma once

#include <charconv>
#include <cstdarg>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace feature_library::calls {
inline int checked(long long value) {
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::overflow_error("call adapter integer overflow");
    return static_cast<int>(value);
}
inline int choose(int value) { return checked(static_cast<long long>(value) + 1); }
inline double choose(double value) { return value + 0.5; }
inline std::string choose(const std::string& value) { return "string:" + value; }
inline void choose(bool) = delete;
inline int ambiguous(int) { return 1; }
inline int ambiguous(unsigned int) { return 2; }
inline int default_evaluations = 0;
inline int default_factor() {
    if (default_evaluations == std::numeric_limits<int>::max()) throw std::overflow_error("default evaluation counter exhausted");
    ++default_evaluations;
    return 2;
}
inline int scale(int value, int factor = default_factor()) { return checked(static_cast<long long>(value) * factor); }
inline int sum_ints(unsigned int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    long long total = 0;
    for (unsigned int i = 0; i < count; ++i) total += va_arg(arguments, int);
    va_end(arguments);
    return checked(total);
}
inline int sum_list(std::initializer_list<int> values) {
    long long total = 0;
    for (int value : values) total += value;
    return checked(total);
}
inline int parse(const std::string& text) {
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc::result_out_of_range) throw std::out_of_range("constructor integer out of range");
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("constructor requires a complete decimal integer");
    return value;
}
class Object {
    int value_;
    friend int read_secret(const Object& object);
public:
    inline static int alive = 0;
    Object() : Object(0) {}
    explicit Object(int value) : value_(value) { ++alive; }
    explicit Object(const std::string& text) : Object(parse(text)) {}
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    ~Object() noexcept { --alive; }
    int adjust(int amount) & { value_ = checked(static_cast<long long>(value_) + amount); return value_; }
    int adjust(int amount) const & { return checked(static_cast<long long>(value_) + amount); }
    int take() && { const int value = value_; value_ = 0; return value; }
    explicit operator int() const { return value_; }
};
inline int read_secret(const Object& object) { return object.value_; }
} // namespace feature_library::calls
