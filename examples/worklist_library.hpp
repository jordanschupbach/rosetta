#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <queue>
#include <stack>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace feature_library::worklists {
class State {
    using Stack = std::stack<std::string>;
    using Queue = std::queue<std::string>;
    using Highest = std::priority_queue<std::string>;
    using Lowest = std::priority_queue<std::string, std::vector<std::string>, std::greater<std::string>>;
    using Data = std::variant<Stack, Queue, Highest, Lowest>;
    int order_;
    Data data_;
    static Data make(int order) {
        switch (order) {
        case 1: return Data(std::in_place_index<0>);
        case 2: return Data(std::in_place_index<1>);
        case 3: return Data(std::in_place_index<2>);
        case 4: return Data(std::in_place_index<3>);
        default: throw std::invalid_argument("unknown worklist order");
        }
    }
    template<class Adapter> static const std::string& next(const Adapter& value) {
        if constexpr (std::is_same_v<Adapter, Queue>) return value.front();
        else return value.top();
    }
public:
    inline static std::atomic<int> alive{0};
    explicit State(int order) : order_(order), data_(make(order)) { ++alive; }
    State(const State& other) : order_(other.order_), data_(other.data_) { ++alive; }
    State& operator=(const State&) = delete;
    ~State() { --alive; }
    int order() const { return order_; }
    unsigned long long size() const { return std::visit([](const auto& value) { return static_cast<unsigned long long>(value.size()); }, data_); }
    void push(std::string text) { std::visit([&](auto& value) { value.push(std::move(text)); }, data_); }
    std::optional<std::string> peek() const {
        return std::visit([](const auto& value) -> std::optional<std::string> {
            if (value.empty()) return std::nullopt;
            return next(value);
        }, data_);
    }
    std::optional<std::string> pop() {
        return std::visit([](auto& value) -> std::optional<std::string> {
            if (value.empty()) return std::nullopt;
            std::string text = next(value); // prepare the copy before consuming
            value.pop();
            return text;
        }, data_);
    }
    std::vector<std::string> snapshot() const {
        auto copy = data_;
        std::vector<std::string> result;
        std::visit([&](auto& value) {
            result.reserve(value.size());
            while (!value.empty()) { result.push_back(next(value)); value.pop(); }
        }, copy);
        return result;
    }
};
} // namespace feature_library::worklists
