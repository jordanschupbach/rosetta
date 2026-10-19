#pragma once

#include <chrono>
#include <limits>
#include <ratio>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace feature_library::time {
using Tick = long long;
using Milliseconds = std::chrono::duration<Tick, std::milli>;
enum class Rounding { truncate, floor, ceil, nearest, exact };
inline Rounding rounding(std::string_view name) {
    if (name == "truncate") return Rounding::truncate;
    if (name == "floor") return Rounding::floor;
    if (name == "ceil") return Rounding::ceil;
    if (name == "nearest") return Rounding::nearest;
    if (name == "exact") return Rounding::exact;
    throw std::invalid_argument("unknown time rounding mode");
}

template<class Function> decltype(auto) unit(std::string_view name, Function&& function) {
    if (name == "ns") return function(std::chrono::duration<Tick, std::nano>{});
    if (name == "us") return function(std::chrono::duration<Tick, std::micro>{});
    if (name == "ms") return function(Milliseconds{});
    if (name == "s") return function(std::chrono::duration<Tick>{});
    if (name == "min") return function(std::chrono::duration<Tick, std::ratio<60>>{});
    if (name == "h") return function(std::chrono::duration<Tick, std::ratio<3600>>{});
    throw std::invalid_argument("unknown time unit");
}

template<class From, class To> Tick cast(Tick ticks, Rounding mode) {
    using Ratio = std::ratio_divide<typename From::period, typename To::period>;
    static_assert(Ratio::num == 1 || Ratio::den == 1, "time adapter requires nested integral units");
    static_assert(std::is_same_v<typename From::rep, Tick> && std::is_same_v<typename To::rep, Tick>);
    if constexpr (Ratio::num != 1) {
        if (ticks > std::numeric_limits<Tick>::max() / Ratio::num || ticks < std::numeric_limits<Tick>::min() / Ratio::num)
            throw std::overflow_error("time conversion overflow");
    }
    const Tick result = std::chrono::duration_cast<To>(From(ticks)).count();
    if constexpr (Ratio::den == 1) return result;
    // Rounding via source remainders avoids reconstructing an out-of-range
    // source duration at signed extrema. No floating-point conversion occurs.
    const Tick remainder = ticks % Ratio::den;
    if (mode == Rounding::exact && remainder != 0) throw std::invalid_argument("inexact time conversion");
    if (mode == Rounding::floor && remainder < 0) return result - 1;
    if (mode == Rounding::ceil && remainder > 0) return result + 1;
    if (mode == Rounding::nearest) {
        const Tick magnitude = remainder < 0 ? -remainder : remainder; // |remainder| < positive denominator
        if (magnitude > Ratio::den / 2 || (Ratio::den % 2 == 0 && magnitude == Ratio::den / 2 && result % 2 != 0))
            return result + (remainder < 0 ? -1 : 1); // ties to even
    }
    return result;
}

inline Tick convert(Tick ticks, std::string_view from, std::string_view to, std::string_view mode) {
    const auto selected = rounding(mode);
    return unit(from, [&](auto source) {
        return unit(to, [&](auto target) { return cast<decltype(source), decltype(target)>(ticks, selected); });
    });
}
inline void check_add(Tick a, Tick b) {
    if ((b > 0 && a > std::numeric_limits<Tick>::max() - b) || (b < 0 && a < std::numeric_limits<Tick>::min() - b))
        throw std::overflow_error("time arithmetic overflow");
}
inline void check_subtract(Tick a, Tick b) {
    if ((b > 0 && a < std::numeric_limits<Tick>::min() + b) || (b < 0 && a > std::numeric_limits<Tick>::max() + b))
        throw std::overflow_error("time arithmetic overflow");
}

class Point {
    using System = std::chrono::time_point<std::chrono::system_clock, Milliseconds>;
    using Steady = std::chrono::time_point<std::chrono::steady_clock, Milliseconds>;
    std::variant<System, Steady> point_;
    void same_clock(const Point& other) const {
        if (steady() != other.steady()) throw std::invalid_argument("time points use different clocks");
    }
public:
    inline static int alive = 0;
    Point(Tick ticks, bool steady) : point_(System(Milliseconds(ticks))) {
        if (steady) point_ = Steady(Milliseconds(ticks));
        ++alive;
    }
    Point(const Point&) = delete;
    Point& operator=(const Point&) = delete;
    ~Point() noexcept { --alive; }
    bool steady() const { return point_.index() == 1; }
    Tick ticks() const { return std::visit([](const auto& point) { return point.time_since_epoch().count(); }, point_); }
    Point shifted(Tick delta) const {
        check_add(ticks(), delta);
        return std::visit([&](const auto& point) { return Point((point + Milliseconds(delta)).time_since_epoch().count(), steady()); }, point_);
    }
    Tick difference(const Point& other) const {
        same_clock(other);
        check_subtract(ticks(), other.ticks());
        if (steady()) return (std::get<Steady>(point_) - std::get<Steady>(other.point_)).count();
        return (std::get<System>(point_) - std::get<System>(other.point_)).count();
    }
    bool before(const Point& other) const {
        same_clock(other);
        if (steady()) return std::get<Steady>(point_) < std::get<Steady>(other.point_);
        return std::get<System>(point_) < std::get<System>(other.point_);
    }
};

template<class Clock> Tick now() {
    using Rep = typename Clock::rep;
    static_assert(std::is_integral_v<Rep> && std::numeric_limits<Rep>::digits <= std::numeric_limits<Tick>::digits,
        "time adapter requires an integral clock representation that fits signed long long");
    using Duration = std::chrono::duration<Tick, typename Clock::period>;
    return cast<Duration, Milliseconds>(static_cast<Tick>(Clock::now().time_since_epoch().count()), Rounding::floor);
}
template<class Clock> std::vector<Tick> clock_info() {
    return {Clock::period::num, Clock::period::den, Clock::is_steady ? 1 : 0};
}
} // namespace feature_library::time
