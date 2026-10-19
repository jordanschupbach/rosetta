#include "demo.hpp"
#include <stdexcept>

namespace demo {
std::deque<std::string> deque_values(std::deque<std::string> value) { return value; }
std::list<Operation> list_operations(std::list<Operation> value) { return value; }
std::forward_list<Sum> forward_values(std::forward_list<Sum> value) { return value; }
std::set<std::string> ordered_set(std::set<std::string> value) { return value; }
std::unordered_set<Operation> hashed_set(std::unordered_set<Operation> value) { return value; }
std::multiset<std::string> multi_set(std::multiset<std::string> value) { return value; }
std::unordered_multiset<std::string> hashed_multi_set(std::unordered_multiset<std::string> value) { return value; }
std::set<std::optional<std::string>> optional_set(std::set<std::optional<std::string>> value) { return value; }
std::unordered_set<std::optional<int>> optional_hash_set(std::unordered_set<std::optional<int>> value) { return value; }
std::multiset<std::monostate> monostate_multi_set(std::multiset<std::monostate> value) { return value; }
std::set<std::vector<int>> nested_set(std::set<std::vector<int>> value) { return value; }
std::multimap<std::string, std::optional<std::string>> multi_map(std::multimap<std::string, std::optional<std::string>> value) { return value; }
std::unordered_multimap<std::string, std::string> hashed_multi_map(std::unordered_multimap<std::string, std::string> value) { return value; }
std::list<std::string> sequence_callback(std::function<std::list<std::string>(std::list<std::string>)> callback, std::list<std::string> value) { return callback(std::move(value)); }
std::set<std::string> set_callback(std::function<std::set<std::string>(std::set<std::string>)> callback, std::set<std::string> value) { return callback(std::move(value)); }
std::list<std::string> sequence_failure(std::list<std::string>) { throw std::runtime_error("sequence failure"); }
std::deque<std::pair<std::string, WideFlags>> sequence_oversized_result(std::deque<std::string> value) {
    std::deque<std::pair<std::string, WideFlags>> result;
    for (auto& text : value) result.emplace_back(std::move(text), static_cast<WideFlags>(1ULL << 63));
    return result;
}
std::deque<std::string_view> sequence_view(std::deque<std::string_view> value) { return value; }
std::vector<std::string> set_views_after(std::set<std::string_view> value, std::function<void()> mutate) {
    mutate();
    std::vector<std::string> result;
    for (auto text : value) result.emplace_back(text);
    return result;
}
} // namespace demo
