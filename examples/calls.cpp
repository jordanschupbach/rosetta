#include "demo.hpp"
#include "call_library.hpp"

namespace demo {
namespace calls = feature_library::calls;
int overload_integer(int value) {
    const auto function = static_cast<int (*)(int)>(&calls::choose);
    return function(value);
}
double overload_real(double value) {
    const auto function = static_cast<double (*)(double)>(&calls::choose);
    return function(value);
}
std::string overload_text(std::string value) {
    const auto function = static_cast<std::string (*)(const std::string&)>(&calls::choose);
    return function(value);
}
int signed_choice(int value) { return static_cast<int (*)(int)>(&calls::ambiguous)(value); }
int unsigned_choice(unsigned int value) { return static_cast<int (*)(unsigned int)>(&calls::ambiguous)(value); }
int call_int_min() { return std::numeric_limits<int>::min(); }
int call_int_max() { return std::numeric_limits<int>::max(); }
unsigned int call_uint_max() { return std::numeric_limits<unsigned int>::max(); }
int default_scale(int value) { return calls::scale(value); }
int explicit_scale(int value, int factor) { return calls::scale(value, factor); }
int default_factor_calls() { return calls::default_evaluations; }
int variadic_sum(std::vector<int> values) {
    // Each selected call supplies exactly the count and promoted types expected
    // by this C-style API; arbitrary runtime varargs need a separate adapter.
    switch (values.size()) {
    case 0: return calls::sum_ints(0);
    case 1: return calls::sum_ints(1, values[0]);
    case 2: return calls::sum_ints(2, values[0], values[1]);
    case 3: return calls::sum_ints(3, values[0], values[1], values[2]);
    default: throw std::invalid_argument("variadic adapter supports at most 3 integers");
    }
}
int initializer_sum(std::array<int, 3> values) { return calls::sum_list({values[0], values[1], values[2]}); }
CallObject::CallObject(std::unique_ptr<calls::Object> native) : native_(std::move(native)) {
    if (!native_) throw std::invalid_argument("call object requires an owner");
}
CallObject::~CallObject() noexcept = default;
int CallObject::adjust_mutable(int amount) {
    const auto method = static_cast<int (calls::Object::*)(int) &>(&calls::Object::adjust);
    return ((*native_).*method)(amount);
}
int CallObject::adjust_readonly(int amount) const {
    const auto method = static_cast<int (calls::Object::*)(int) const &>(&calls::Object::adjust);
    const auto& object = *native_;
    return (object.*method)(amount);
}
int CallObject::take() { return std::move(*native_).take(); }
int CallObject::as_integer() const { return static_cast<int>(*native_); }
int CallObject::secret() const { return calls::read_secret(*native_); }
const CallObject& CallObject::readonly() const { return *this; }
int CallObject::live() { return calls::Object::alive; }
std::unique_ptr<CallObject> make_call_default() { return std::make_unique<CallObject>(std::make_unique<calls::Object>()); }
std::unique_ptr<CallObject> make_call_number(int value) { return std::make_unique<CallObject>(std::make_unique<calls::Object>(value)); }
std::unique_ptr<CallObject> make_call_text(std::string value) { return std::make_unique<CallObject>(std::make_unique<calls::Object>(value)); }
} // namespace demo
