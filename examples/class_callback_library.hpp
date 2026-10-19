#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace feature_library::class_callbacks {
struct Record {
    inline static std::atomic<int> alive{0};
    std::string text;
    int number;
    Record(std::string value, int integer) : text(std::move(value)), number(integer) { ++alive; }
    Record(const Record& other) : text(other.text), number(other.number) { ++alive; }
    Record(Record&& other) noexcept : text(std::move(other.text)), number(other.number) { ++alive; }
    Record& operator=(const Record&) = delete;
    Record& operator=(Record&&) = delete;
    ~Record() { --alive; }
};
using Transform = std::function<Record(Record)>;
inline Record apply(Record input, const Transform& callback) {
    if (!callback) throw std::invalid_argument("class callback is required");
    return callback(std::move(input));
}
class Frame {
    struct Invocation {
        Record input;
        std::optional<Record> result;
        explicit Invocation(Record record) : input(std::move(record)) {}
    };
    Invocation* current_ = nullptr;
    int depth_ = 0;
    struct Scope {
        Frame& frame;
        Invocation* previous;
        Scope(Frame& owner, Invocation& invocation) : frame(owner), previous(owner.current_) {
            if (frame.depth_ >= 64) throw std::runtime_error("class callback nesting limit");
            frame.current_ = &invocation; ++frame.depth_;
        }
        ~Scope() { frame.current_ = previous; --frame.depth_; }
    };
    Invocation& current() const {
        if (!current_) throw std::runtime_error("class callback frame is inactive");
        return *current_;
    }
public:
    inline static std::atomic<int> alive{0};
    Frame() { ++alive; }
    Frame(const Frame&) = delete;
    ~Frame() { --alive; }
    int depth() const noexcept { return depth_; }
    Record argument() const { return current().input; }
    void set_result(const Record& value) {
        auto& invocation = current();
        Record prepared(value); // Preserve an earlier result if copying fails.
        invocation.result.emplace(std::move(prepared));
    }
    Record transform(Record input, const std::function<void()>& trigger) {
        if (!trigger) throw std::invalid_argument("class callback is required");
        Transform callback = [this, &trigger](Record value) -> Record {
            Invocation invocation(std::move(value));
            Scope scope(*this, invocation);
            trigger();
            if (!invocation.result) throw std::runtime_error("class callback did not set a result");
            return std::move(*invocation.result);
        };
        return feature_library::class_callbacks::apply(std::move(input), callback);
    }
};
}
