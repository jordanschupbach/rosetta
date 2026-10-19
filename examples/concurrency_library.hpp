#pragma once

#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace feature_library::concurrency {
inline thread_local int local_value = 0;

// All started threads join before captured storage can unwind, including a
// failure while creating a later thread. Workers never enter a target runtime.
class JoinedThreads {
    std::vector<std::thread> threads_;
public:
    explicit JoinedThreads(std::size_t capacity) { threads_.reserve(capacity); }
    JoinedThreads(const JoinedThreads&) = delete;
    JoinedThreads& operator=(const JoinedThreads&) = delete;
    template<class Function> void start(Function&& function) { threads_.emplace_back(std::forward<Function>(function)); }
    void join() { for (auto& thread : threads_) if (thread.joinable()) thread.join(); }
    ~JoinedThreads() noexcept { join(); }
};

inline std::tuple<int, int, int, int> probe(int value) {
    const int before = local_value;
    int initial = -1, changed = -1;
    JoinedThreads threads(1);
    threads.start([&] { initial = local_value; local_value = value; changed = local_value; });
    threads.join();
    return {before, initial, changed, local_value};
}

class State {
    std::atomic<int> counter_;
    mutable std::mutex register_mutex_;
    volatile unsigned int register_;
public:
    inline static std::atomic<int> alive{0};
    inline static std::atomic<int> workers{0};
    State(int counter, unsigned int value) : counter_(counter), register_(value) { ++alive; }
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() { --alive; }
    int load() const { return counter_.load(); }
    void store(int value) { counter_.store(value); }
    std::tuple<bool, int> compare_exchange(int expected, int desired) {
        const bool changed = counter_.compare_exchange_strong(expected, desired);
        return {changed, expected};
    }
    int add(int delta) {
        int previous = counter_.load();
        for (;;) {
            // Test before adding: do not rely on a wider native integer type.
            if ((delta > 0 && previous > std::numeric_limits<int>::max() - delta) ||
                (delta < 0 && previous < std::numeric_limits<int>::min() - delta))
                throw std::overflow_error("atomic counter overflow");
            const int next = previous + delta;
            if (counter_.compare_exchange_weak(previous, next)) return previous;
        }
    }
    unsigned int register_load() const {
        std::lock_guard<std::mutex> lock(register_mutex_);
        return register_;
    }
    void register_store(unsigned int value) {
        std::lock_guard<std::mutex> lock(register_mutex_);
        register_ = value;
    }
    unsigned int register_exchange(unsigned int value) {
        std::lock_guard<std::mutex> lock(register_mutex_);
        const unsigned int previous = register_;
        register_ = value;
        return previous;
    }
    std::vector<int> parallel_add(int count, int iterations, int delta) {
        if (count < 1 || count > 16) throw std::out_of_range("worker count must be between 1 and 16");
        if (iterations < 0 || iterations > 10000) throw std::out_of_range("iterations must be between 0 and 10000");
        std::vector<int> completed(static_cast<std::size_t>(count), 0);
        std::atomic<bool> stop{false};
        std::mutex error_mutex;
        std::exception_ptr error;
        {
            JoinedThreads threads(static_cast<std::size_t>(count));
            try {
                for (int i = 0; i < count; ++i) threads.start([&, i] {
                    struct WorkerScope { WorkerScope() { ++workers; } ~WorkerScope() { --workers; } } worker_scope;
                    local_value = 0;
                    try {
                        for (int step = 0; step < iterations && !stop.load(); ++step) {
                            add(delta);
                            ++local_value;
                        }
                        completed[static_cast<std::size_t>(i)] = local_value;
                    } catch (...) {
                        const auto failure = std::current_exception();
                        std::lock_guard<std::mutex> lock(error_mutex);
                        if (!error) error = failure;
                        stop.store(true);
                    }
                });
            } catch (...) { stop.store(true); throw; }
        }
        if (error) std::rethrow_exception(error);
        return completed;
    }
};
} // namespace feature_library::concurrency
