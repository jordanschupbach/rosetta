#pragma once

#include <coroutine>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace feature_library::coroutines {
class Sequence {
public:
    inline static unsigned long long frames = 0, bodies = 0, allocations = 0, releases = 0;
    struct promise_type {
        std::optional<std::string> current;
        std::exception_ptr error;
        promise_type() { ++frames; }
        ~promise_type() noexcept { --frames; }
        static void* operator new(std::size_t size) {
            void* memory = ::operator new(size);
            ++allocations;
            return memory;
        }
        static void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); ++releases; }
        Sequence get_return_object() noexcept { return Sequence(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        std::suspend_always yield_value(std::string value) noexcept { current.emplace(std::move(value)); return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() noexcept { error = std::current_exception(); }
    };
private:
    std::coroutine_handle<promise_type> handle_;
    bool cancelled_ = false;
    explicit Sequence(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
public:
    Sequence(Sequence&& other) noexcept : handle_(std::exchange(other.handle_, {})), cancelled_(other.cancelled_) {}
    Sequence(const Sequence&) = delete;
    Sequence& operator=(const Sequence&) = delete;
    ~Sequence() noexcept { if (handle_) handle_.destroy(); }
    bool done() const { return !handle_ || handle_.done(); }
    bool cancelled() const { return cancelled_; }
    bool advance() {
        if (!handle_) return false;
        if (!handle_.done()) {
            handle_.promise().current.reset();
            handle_.resume();
        }
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
        return !handle_.done();
    }
    std::string value() const {
        if (!handle_) throw std::runtime_error("generator is cancelled");
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
        if (!handle_.promise().current) throw std::runtime_error("generator has no current value");
        return *handle_.promise().current;
    }
    bool cancel() noexcept {
        if (done()) return false;
        cancelled_ = true;
        handle_.destroy();
        handle_ = {};
        return true;
    }
};
struct BodyScope {
    BodyScope() { ++Sequence::bodies; }
    ~BodyScope() noexcept { --Sequence::bodies; }
};
inline Sequence sequence(int start, int count, std::string prefix, int fail_after) {
    BodyScope scope;
    for (int offset = 0; ; ++offset) {
        if (offset == fail_after) throw std::runtime_error("native generator failure");
        if (offset == count) co_return;
        const long long value = static_cast<long long>(start) + offset;
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            throw std::overflow_error("generator integer overflow");
        co_yield prefix + std::to_string(value);
    }
}
inline Sequence create(int start, int count, std::string prefix, int fail_after) {
    if (count < 0) throw std::out_of_range("generator count must be nonnegative");
    if (fail_after < -1 || fail_after > count) throw std::out_of_range("generator failure position is out of range");
    return sequence(start, count, std::move(prefix), fail_after);
}
} // namespace feature_library::coroutines
