#include "demo.hpp"
#include <stdexcept>

namespace demo {
StringMap string_map(StringMap value) { return value; }
std::unordered_map<std::string, Operation> unordered_enum_map(std::unordered_map<std::string, Operation> value) { return value; }
OptionalMap optional_map(OptionalMap value) { return value; }
ComplexMap complex_map(const ComplexMap& value) { return value; }
std::map<double, std::string> number_map(std::map<double, std::string> value) { return value; }
std::unordered_map<Operation, std::string> hashed_enum_map(std::unordered_map<Operation, std::string> value) { return value; }
std::unordered_map<std::optional<int>, std::string> hashed_optional_map(std::unordered_map<std::optional<int>, std::string> value) { return value; }
std::unordered_map<std::vector<bool>, std::string> hashed_bits_map(std::unordered_map<std::vector<bool>, std::string> value) { return value; }
StringMap map_failure(StringMap) { throw std::runtime_error("map failure"); }
std::map<std::string, WideFlags> map_oversized_result(StringMap value) {
    std::map<std::string, WideFlags> result;
    for (const auto& entry : value) result.emplace(entry.first, static_cast<WideFlags>(1ULL << 63));
    return result;
}
StringMap map_callback(std::function<StringMap(StringMap)> callback, StringMap value) { return callback(std::move(value)); }
StringMap map_views_after(std::map<std::string, std::string_view> value, std::function<void()> mutate) {
    mutate();
    StringMap result;
    for (const auto& entry : value) result.emplace(entry.first, entry.second);
    return result;
}
} // namespace demo
