#include "demo.hpp"
#include <limits>
#include <stdexcept>

namespace demo {
static int nested_owners = 0, nested_items = 0;
NestedOwner::NestedOwner(Count value, std::string label)
    : item_(std::make_unique<Item>(value, std::move(label))), mode_(Mode::ready) { ++nested_owners; }
NestedOwner::~NestedOwner() noexcept { --nested_owners; }
NestedOwner::Item& NestedOwner::item() { return *item_; }
const NestedOwner::Item& NestedOwner::readonly_item() const { return *item_; }
NestedOwner::Mode NestedOwner::mode() const { return mode_; }
void NestedOwner::set_mode(Mode value) { mode_ = value; }
int NestedOwner::live() { return nested_owners; }

NestedOwner::Item::Item(Value value, std::string name) : label(std::move(name)), value_(value) {
    if (value < 0) throw std::invalid_argument("negative nested value");
    ++nested_items;
}
NestedOwner::Item::Item(const Item& other) : label(other.label), value_(other.value_), state_(other.state_) { ++nested_items; }
NestedOwner::Item::Item(Item&& other) noexcept : label(std::move(other.label)), value_(other.value_), state_(other.state_) { ++nested_items; }
NestedOwner::Item::~Item() noexcept { --nested_items; }
NestedOwner::Item::Value NestedOwner::Item::value() const { return value_; }
void NestedOwner::Item::set_value(Value value) { value_ = value; }
NestedOwner::Item::Value NestedOwner::Item::adjusted_impl(Value offset) const {
    const long long result = static_cast<long long>(value_) + offset;
    if (result < std::numeric_limits<Value>::min() || result > std::numeric_limits<Value>::max())
        throw std::overflow_error("nested value overflow");
    return static_cast<Value>(result);
}
NestedOwner::Item::State NestedOwner::Item::state() const { return state_; }
void NestedOwner::Item::set_state(State value) { state_ = value; }
NestedOwner::Item::Value NestedOwner::Item::with_callback(IntCallback callback) { return callback(value_); }
int NestedOwner::Item::live() { return nested_items; }
std::vector<NestedOwner::Mode> nested_modes(std::vector<NestedOwner::Mode> values) { return values; }
NestedOwner::Mode nested_mode_callback(std::function<NestedOwner::Mode(NestedOwner::Mode)> callback, NestedOwner::Mode value) {
    return callback(value);
}
} // namespace demo
