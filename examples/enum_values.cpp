#include "demo.hpp"
#include <stdexcept>

namespace demo {
std::vector<Operation> operation_vector(std::vector<Operation> value) { return value; }
std::array<ByteFlags, 3> byte_flag_array(std::array<ByteFlags, 3> value) { return value; }
std::optional<Operation> optional_operation(std::optional<Operation> value) { return value; }
EnumProduct enum_product(EnumProduct value) { return value; }
std::tuple<BoolFlags, WideFlags> enum_widths(std::tuple<BoolFlags, WideFlags> value) { return value; }
Operation enum_callback(std::function<Operation(Operation)> callback, Operation value) { return callback(value); }
EnumProduct enum_product_callback(std::function<EnumProduct(EnumProduct)> callback, EnumProduct value) {
    return callback(std::move(value));
}
std::tuple<Operation, ByteFlags> enum_native_values() { return {static_cast<Operation>(3), ByteFlags::reserved}; }
EnumProduct enum_product_failure(EnumProduct) { throw std::runtime_error("enum product failure"); }
std::tuple<std::string, WideFlags> enum_oversized_result(EnumProduct value) {
    return {std::move(std::get<0>(value)), static_cast<WideFlags>(1ULL << 63)};
}
} // namespace demo
