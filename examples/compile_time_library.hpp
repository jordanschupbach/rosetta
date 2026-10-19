#pragma once

#include <array>
#include <concepts>
#include <compare>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <utility>

#ifndef ROSETTA_DEMO_BIAS
#define ROSETTA_DEMO_BIAS 2
#endif
#define ROSETTA_DEMO_APPLY_BIAS(value) (::feature_library::compile_time::add_bias<ROSETTA_DEMO_BIAS>(value))

namespace feature_library::compile_time {

constexpr int checked(long long value) {
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::overflow_error("compile-time adapter integer overflow");
    return static_cast<int>(value);
}
constexpr int answer = 6 * 7;
consteval int immediate_square(int value) { return checked(static_cast<long long>(value) * value); }
inline constinit int initialized = immediate_square(6) + 6;

template<int Bias> constexpr int add_bias(int value) { return checked(static_cast<long long>(value) + Bias); }
template<std::size_t N> constexpr std::array<int, N> pattern(int start) {
    static_assert(N <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    std::array<int, N> result{};
    for (std::size_t i = 0; i < N; ++i) result[i] = checked(static_cast<long long>(start) + static_cast<long long>(i));
    return result;
}
template<class T> struct Category { static constexpr int value = 0; };
template<> struct Category<int> { static constexpr int value = 1; };
template<class T> struct Category<T*> { static constexpr int value = 2; };
template<class T> inline constexpr bool pointer_category = Category<T>::value == 2;

template<class T> struct Box {
    using value_type = T;
    T value;
    explicit constexpr Box(T item) : value(std::move(item)) {}
};
template<class T> Box(T) -> Box<T>;
template<template<class> class Container, class T> struct Envelope {
    Container<T> item;
    // Dependent names are instantiated in native C++, not resolved by Lua.
    using value_type = typename Container<T>::value_type;
    value_type get() const { return item.value; }
};
template<class T> using AliasBox = Envelope<Box, T>;

template<std::integral T> requires (sizeof(T) <= sizeof(int))
T constrained_sum(const std::vector<T>& values) {
    long long total = 0;
    for (T value : values) {
        total += value;
        if (total < std::numeric_limits<T>::min() || total > std::numeric_limits<T>::max())
            throw std::overflow_error("constrained sum overflow");
    }
    return static_cast<T>(total);
}
template<class T> concept CanSum = requires(const std::vector<T>& values) { constrained_sum(values); };
template<class T, std::enable_if_t<std::is_integral_v<T> && sizeof(T) <= sizeof(int), int> = 0>
int sfinae_double(T value) { return checked(static_cast<long long>(value) * 2); }

consteval int operator""_answer(unsigned long long value) {
    if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
        throw "answer literal is out of range";
    return static_cast<int>(value);
}
struct OrderedInt { int value; auto operator<=>(const OrderedInt&) const = default; };

static_assert(immediate_square(6) + 6 == answer);
static_assert(pattern<3>(40)[2] == answer);
static_assert(Category<int>::value == 1 && pointer_category<const int*>);
static_assert(CanSum<int> && !CanSum<double> && !CanSum<std::string>);
static_assert(std::same_as<decltype(Box(42)), Box<int>>);
static_assert(42_answer == answer);
} // namespace feature_library::compile_time
