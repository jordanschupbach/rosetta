#pragma once
#include <atomic>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace feature_library::awaitables {
enum class Phase { pending, succeeded, failed, cancelled };
class Executor;
struct Control : std::enable_shared_from_this<Control> {
    inline static std::atomic<int> alive{0};
    std::shared_ptr<Executor> executor;
    std::coroutine_handle<> handle{};
    Phase phase = Phase::pending;
    bool queued = false, running = false;
    std::string output;
    std::exception_ptr error;
    explicit Control(std::shared_ptr<Executor> owner) : executor(std::move(owner)) { ++alive; }
    ~Control() { if (handle) handle.destroy(); --alive; }
    bool cancel();
    std::string result() const;
    Phase state() const;
};
class Executor : public std::enable_shared_from_this<Executor> {
    const std::thread::id owner_ = std::this_thread::get_id();
    std::deque<std::weak_ptr<Control>> queue_;
    bool dispatching_ = false;
public:
    inline static std::atomic<int> alive{0}, frames{0}, bodies{0};
    inline static std::atomic<unsigned long long> allocations{0}, releases{0};
    Executor() { ++alive; }
    Executor(const Executor&) = delete;
    ~Executor() { --alive; }
    void owner_thread() const {
        if (std::this_thread::get_id() != owner_) throw std::runtime_error("awaitable executor requires the owning thread");
    }
    void post(Control& control) {
        owner_thread();
        if (control.queued || control.phase != Phase::pending) throw std::logic_error("invalid awaitable scheduling state");
        queue_.push_back(control.weak_from_this());
        control.queued = true;
    }
    std::shared_ptr<Control> submit(int seed, int steps, std::string prefix, bool fail, std::function<int(int)> callback);
    bool run_one() {
        owner_thread();
        if (dispatching_) throw std::runtime_error("awaitable executor dispatch is already active");
        while (!queue_.empty()) {
            auto control = queue_.front().lock();
            queue_.pop_front();
            if (!control || !control->queued || !control->handle || control->phase != Phase::pending) continue;
            control->queued = false;
            struct Running {
                bool& dispatching; bool& running;
                Running(bool& d, bool& r) : dispatching(d), running(r) { dispatching = true; running = true; }
                ~Running() { running = false; dispatching = false; }
            } guard(dispatching_,control->running);
            control->handle.resume();
            if (control->handle.done()) {
                auto completed = std::exchange(control->handle,{});
                completed.destroy(); // release captures/parameters at completion
            }
            return true;
        }
        return false;
    }
    int pending() const {
        owner_thread();
        std::size_t count = 0;
        for (const auto& item : queue_) {
            auto control = item.lock();
            if (control && control->queued && control->phase == Phase::pending) ++count;
        }
        if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::overflow_error("too many awaitable tasks");
        return static_cast<int>(count);
    }
};
struct Work {
    struct promise_type {
        Control* control = nullptr;
        promise_type() { ++Executor::frames; }
        ~promise_type() { --Executor::frames; }
        static void* operator new(std::size_t size) {
            void* memory = ::operator new(size); ++Executor::allocations; return memory;
        }
        static void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); ++Executor::releases; }
        Work get_return_object() noexcept { return Work{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_value(std::string result) noexcept { control->output = std::move(result); control->phase = Phase::succeeded; }
        void unhandled_exception() noexcept { control->error = std::current_exception(); control->phase = Phase::failed; }
    };
    std::coroutine_handle<promise_type> handle;
    explicit Work(std::coroutine_handle<promise_type> value) noexcept : handle(value) {}
    Work(Work&& other) noexcept : handle(std::exchange(other.handle,{})) {}
    Work(const Work&) = delete;
    ~Work() { if (handle) handle.destroy(); }
};
struct Tick {
    Control* control;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const { control->executor->post(*control); }
    void await_resume() const noexcept {}
};
inline Work compute(Control* control, int seed, int steps, std::string prefix, bool fail, std::function<int(int)> callback) {
    struct Body { Body() { ++Executor::bodies; } ~Body() { --Executor::bodies; } } body;
    for (int step = 0; step < steps; ++step) {
        co_await Tick{control};
        if (callback) seed = callback(seed);
        else {
            if (seed == std::numeric_limits<int>::max()) throw std::out_of_range("awaitable integer overflow");
            ++seed;
        }
    }
    if (fail) throw std::runtime_error("native awaitable failure");
    co_return prefix + std::to_string(seed);
}
inline std::shared_ptr<Control> Executor::submit(int seed, int steps, std::string prefix, bool fail, std::function<int(int)> callback) {
    owner_thread();
    if (steps < 0) throw std::out_of_range("awaitable step count must be nonnegative");
    auto control = std::make_shared<Control>(shared_from_this());
    auto work = compute(control.get(),seed,steps,std::move(prefix),fail,std::move(callback));
    work.handle.promise().control = control.get();
    control->handle = std::exchange(work.handle,{});
    post(*control);
    return control;
}
inline bool Control::cancel() {
    executor->owner_thread();
    if (running) throw std::runtime_error("cannot cancel a running awaitable");
    if (phase != Phase::pending) return false;
    queued = false; phase = Phase::cancelled;
    auto cancelled = std::exchange(handle,{});
    if (cancelled) cancelled.destroy();
    return true;
}
inline std::string Control::result() const {
    executor->owner_thread();
    if (phase == Phase::failed) std::rethrow_exception(error);
    if (phase == Phase::cancelled) throw std::runtime_error("awaitable task cancelled");
    if (phase != Phase::succeeded) throw std::runtime_error("awaitable result is not ready");
    return output;
}
inline Phase Control::state() const { executor->owner_thread(); return phase; }
}
