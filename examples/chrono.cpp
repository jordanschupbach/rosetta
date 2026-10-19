#include "demo.hpp"
#include "chrono_library.hpp"

namespace demo {
namespace time = feature_library::time;
long long time_convert(long long ticks, std::string from, std::string to, std::string rounding) {
    return time::convert(ticks, from, to, rounding);
}
TimePoint::TimePoint(long long ticks, bool steady) : native_(std::make_shared<time::Point>(ticks, steady)) {}
long long TimePoint::ticks() const { return native_->ticks(); }
bool TimePoint::steady() const { return native_->steady(); }
TimePoint TimePoint::shifted(long long delta) const {
    const auto point = native_->shifted(delta);
    return TimePoint(point.ticks(), point.steady());
}
long long TimePoint::difference(const TimePoint& other) const { return native_->difference(*other.native_); }
bool TimePoint::before(const TimePoint& other) const { return native_->before(*other.native_); }
int TimePoint::live() { return time::Point::alive; }
TimePoint time_now(bool steady) {
    return TimePoint(steady ? time::now<std::chrono::steady_clock>() : time::now<std::chrono::system_clock>(), steady);
}
std::vector<long long> time_clock_info(bool steady) {
    return steady ? time::clock_info<std::chrono::steady_clock>() : time::clock_info<std::chrono::system_clock>();
}
} // namespace demo
