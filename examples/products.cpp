#include "demo.hpp"
#include <limits>
#include <stdexcept>

namespace demo {
Product product_identity(Product value) { return value; }
TextPair pair_identity(TextPair value) { return value; }
std::tuple<> empty_product(std::tuple<> value) { return value; }
NestedProduct nested_product(const NestedProduct& value) { return value; }
std::vector<Product> product_vector(std::vector<Product> values) { return values; }
std::optional<Product> optional_product(std::optional<Product> value) { return value; }
Product product_failure(Product) { throw std::runtime_error("product failure"); }
std::tuple<std::string, unsigned long long> product_large_result(Product value) {
    return {std::move(std::get<0>(value)), std::numeric_limits<unsigned long long>::max()};
}
std::tuple<std::string_view, int> product_view(const std::string& value) { return {value, 42}; }
Product product_callback(ProductCallback callback, Product value) { return callback(std::move(value)); }
ConstProduct product_const(ConstProduct value) { return value; }
std::optional<SparseProduct> sparse_product(std::optional<SparseProduct> value) { return value; }
std::string product_view_after(std::tuple<std::string_view, int> value, std::function<void()> mutate) {
    mutate();
    return std::string(std::get<0>(value)) + std::to_string(std::get<1>(value));
}
std::vector<std::string> vector_view_after(std::vector<std::string_view> value, std::function<void()> mutate) {
    mutate();
    std::vector<std::string> result;
    for (const auto item : value) result.emplace_back(item);
    return result;
}
} // namespace demo
