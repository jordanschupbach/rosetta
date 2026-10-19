#pragma once
#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace feature_library::callables {
using Function = std::function<std::string(int)>;
inline int checked_sum(int left, int right) {
    const auto result = static_cast<long long>(left) + right;
    if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
        throw std::out_of_range("native callable overflow");
    return static_cast<int>(result);
}
struct Capture {
    inline static std::atomic<int> alive{0};
    std::string prefix;
    int seed;
    Capture(std::string text, int value) : prefix(std::move(text)), seed(value) { ++alive; }
    Capture(const Capture& other) : prefix(other.prefix), seed(other.seed) { ++alive; }
    Capture(Capture&& other) noexcept : prefix(std::move(other.prefix)), seed(other.seed) { ++alive; }
    ~Capture() { --alive; }
    std::string operator()(int value) {
        const int next = checked_sum(seed, value);
        auto result = prefix + std::to_string(next);
        seed = next; // native result preparation precedes mutation
        return result;
    }
};
struct Target {
    inline static std::atomic<int> alive{0};
    std::string prefix;
    int seed;
    Target(std::string text, int value) : prefix(std::move(text)), seed(value) { ++alive; }
    Target(const Target&) = delete;
    ~Target() { --alive; }
    std::string apply(int value) {
        const int next = checked_sum(seed, value);
        auto result = prefix + std::to_string(next);
        seed = next;
        return result;
    }
};
inline std::string pointer_function(int value) { return std::to_string(value); }
// Actual native callable results, with distinct native copy semantics.
inline Function make_capture(std::string text, int seed) { return Capture(std::move(text), seed); }
inline Function make_bound(std::string text, int seed) {
    return std::bind_front(&Target::apply, std::make_shared<Target>(std::move(text), seed));
}
inline Function make_pointer() { return &pointer_function; }
inline Function make_empty() { return {}; }
class Slot {
    std::shared_ptr<Function> function_;
public:
    inline static std::atomic<int> alive{0};
    explicit Slot(Function function) : function_(std::make_shared<Function>(std::move(function))) { ++alive; }
    Slot(const Slot&) = delete;
    ~Slot() { --alive; }
    bool empty() const noexcept { return !function_ || !*function_; }
    void clear() noexcept { function_.reset(); }
    Function copy() const { return function_ ? *function_ : Function{}; }
    std::string invoke(int value) {
        // Retain the same callable object during self-clear/reentrant callbacks.
        // Copying std::function here would discard a mutable capture's updates.
        auto function = function_;
        if (!function || !*function) throw std::bad_function_call();
        return (*function)(value);
    }
};
}
