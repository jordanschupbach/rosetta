#include "demo.hpp"
#include <type_traits>

namespace demo {
Feature combine_features(Feature left, Feature right) {
    using Bits = std::underlying_type_t<Feature>;
    return static_cast<Feature>(static_cast<Bits>(left) | static_cast<Bits>(right));
}
bool has_features(Feature value, Feature required) {
    using Bits = std::underlying_type_t<Feature>;
    return (static_cast<Bits>(value) & static_cast<Bits>(required)) == static_cast<Bits>(required);
}
ByteFlags byte_flags(ByteFlags value) { return value; }
SignedFlags signed_flags(SignedFlags value) { return value; }
WideFlags wide_flags(WideFlags value) { return value; }
BoolFlags bool_flags(BoolFlags value) { return value; }
std::string flags_text(std::string value, ByteFlags flags) {
    return value + std::to_string(static_cast<unsigned char>(flags));
}
} // namespace demo
