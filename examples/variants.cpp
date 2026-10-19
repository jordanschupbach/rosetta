#include "demo.hpp"
#include <limits>
#include <stdexcept>

namespace demo {
Sum variant_identity(Sum value) { return value; }
DuplicateSum duplicate_variant(DuplicateSum value) { return value; }
std::vector<Sum> variant_vector(std::vector<Sum> values) { return values; }
std::optional<Sum> optional_variant(std::optional<Sum> value) { return value; }
std::map<std::string, Sum> variant_map(std::map<std::string, Sum> value) { return value; }
std::unordered_map<DuplicateSum, std::string> variant_keys(std::unordered_map<DuplicateSum, std::string> value) { return value; }
std::map<std::string, std::monostate> monostate_map(std::map<std::string, std::monostate> value) { return value; }
std::monostate monostate_identity(std::monostate value) { return value; }
Sum variant_callback(std::function<Sum(Sum)> callback, Sum value) { return callback(std::move(value)); }
Sum variant_failure(Sum) { throw std::runtime_error("variant failure"); }
std::variant<std::string, WideFlags> variant_oversized_result(std::string value, bool oversized) {
    if (oversized) return static_cast<WideFlags>(1ULL << 63);
    return value;
}
ConstSum variant_exception() {
    ConstSum value(std::in_place_index<0>, 42);
    try { value.emplace<1>(std::numeric_limits<std::size_t>::max(), 'x'); }
    catch (const std::length_error&) {}
    return value;
}
bool variant_exception_is_valueless() { return variant_exception().valueless_by_exception(); }
ConstSum const_variant(ConstSum value) { return value; }
ViewSum variant_view(ViewSum value) { return value; }
std::string variant_view_after(ViewSum value, std::function<void()> mutate) {
    mutate();
    return value.index() == 0 ? std::string(std::get<0>(value)) : std::to_string(std::get<1>(value));
}
} // namespace demo
