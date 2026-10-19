#include "demo.hpp"
#include "compile_time_library.hpp"
#include <compare>

namespace demo {
namespace ct = feature_library::compile_time;
int constexpr_answer() { return ct::answer; }
int immediate_answer() { constexpr int value = ct::immediate_square(6) + 6; return value; }
int initialized_constant() { return ct::initialized; }
bool trait_is_integral() { return std::is_integral_v<int>; }
bool template_variable_pointer() { return ct::pointer_category<const int*>; }
int template_classify_int() { return ct::Category<int>::value; }
int template_classify_pointer() { return ct::Category<const int*>::value; }
int template_add_bias(int value) { return ct::add_bias<2>(value); }
std::array<int, 3> template_pattern(int start) { return ct::pattern<3>(start); }
int template_template_box(int value) { return ct::Envelope<ct::Box, int>{ct::Box(value)}.get(); }
std::string template_alias_text(std::string value) { return ct::AliasBox<std::string>{ct::Box(std::move(value))}.get(); }
int deduced_box(int value) { auto box = ct::Box(value); return box.value; }
int constrained_sum(std::vector<int> values) { return ct::constrained_sum(values); }
int sfinae_double(int value) { return ct::sfinae_double(value); }
int macro_bias() { return ROSETTA_DEMO_BIAS; }
int macro_apply(int value) { return ROSETTA_DEMO_APPLY_BIAS(value); }
bool concepts_available() {
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
    return true;
#else
    return false;
#endif
}
int literal_answer() { using ct::operator""_answer; return 42_answer; }
int compare_ints(int left, int right) {
    const auto result = ct::OrderedInt{left} <=> ct::OrderedInt{right};
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}
} // namespace demo
