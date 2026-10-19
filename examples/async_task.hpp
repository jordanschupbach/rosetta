#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <limits>

namespace feature_library {

class TaskCancelled : public std::exception {
public:
    const char* what() const noexcept override { return "async task cancelled"; }
};

// Native task/future adapter. Workers publish only C++ values and exception_ptr;
// dispatch is the explicit owner-thread boundary for arbitrary host callbacks.
class AsyncTask {
    struct State {
        std::mutex mutex;
        std::condition_variable wake;
        bool cancel_requested = false;
        int phase = 0; // running, succeeded, failed, cancelled
        std::promise<int> promise;
        std::shared_future<int> future = promise.get_future().share();
    };
    std::shared_ptr<State> state_;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::thread worker_;
    std::function<void(int, int)> callback_;
    bool delivered_ = false;

    void owner_thread() const {
        if (std::this_thread::get_id() != owner_) throw std::runtime_error("task dispatch requires the owning thread");
    }
    static void run(const std::shared_ptr<State>& state, int value, int delay_ms, bool fail) {
        ++active_workers;
        // Wait/computation failures become future exceptions. The valid promise
        // is completed exactly once before publishing a terminal phase.
        std::unique_lock<std::mutex> lock(state->mutex);
        try {
            state->wake.wait_for(lock, std::chrono::milliseconds(delay_ms), [&] { return state->cancel_requested; });
            if (state->cancel_requested) throw TaskCancelled();
            if (fail) throw std::runtime_error("async worker failure");
            const auto result = static_cast<long long>(value) * 2;
            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
                throw std::overflow_error("async result overflow");
            state->promise.set_value(static_cast<int>(result));
            state->phase = 1;
        } catch (const TaskCancelled&) {
            state->promise.set_exception(std::current_exception());
            state->phase = 3;
        } catch (...) {
            state->promise.set_exception(std::current_exception());
            state->phase = 2;
        }
        --active_workers;
    }
public:
    inline static std::atomic<int> alive{0};
    inline static std::atomic<int> active_workers{0};
    AsyncTask(int value, int delay_ms, bool fail) : state_(std::make_shared<State>()) {
        if (delay_ms < 0 || delay_ms > 60000) throw std::out_of_range("task delay must be between 0 and 60000 ms");
        worker_ = std::thread([state = state_, value, delay_ms, fail] { run(state, value, delay_ms, fail); });
        ++alive;
    }
    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;
    ~AsyncTask() noexcept {
        cancel();
        if (worker_.joinable()) worker_.join();
        --alive;
    }
    int state() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->phase; }
    bool ready() const { return state() != 0; }
    bool wait(int timeout_ms) const {
        if (timeout_ms < 0 || timeout_ms > 60000) throw std::out_of_range("task timeout must be between 0 and 60000 ms");
        if (state_->future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) return false;
        // Promise readiness can precede the phase update by a few instructions.
        return ready();
    }
    bool cancel() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->phase != 0 || state_->cancel_requested) return false;
        state_->cancel_requested = true;
        state_->wake.notify_one();
        return true;
    }
    int result() const {
        if (!ready()) throw std::runtime_error("async result is not ready");
        return state_->future.get(); // repeatable success or native exception
    }
    void on_complete(std::function<void(int, int)> callback) {
        owner_thread();
        if (callback && delivered_) throw std::runtime_error("task completion was already dispatched");
        callback_ = std::move(callback);
    }
    bool dispatch() {
        owner_thread();
        const int phase = state();
        if (phase == 0 || delivered_ || !callback_) return false;
        const int value = phase == 1 ? result() : 0;
        std::function<void(int, int)> callback;
        callback.swap(callback_);
        delivered_ = true; // reentrancy and exceptions never cause redelivery
        callback(phase, value);
        return true;
    }
};
} // namespace feature_library
