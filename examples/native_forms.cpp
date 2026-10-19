#include "demo.hpp"
#include <atomic>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace feature_library::forms {
class Stationary final {
    const std::string text_;
    mutable unsigned long long inspections_ = 0;
public:
    inline static std::atomic<int> alive{0};
    inline static std::atomic<int> shared_setting{42};
    Stationary(std::string text, bool fail) : text_(std::move(text)) {
        if (fail) throw std::runtime_error("stationary construction failed");
        ++alive;
    }
    Stationary(const Stationary&) = delete;
    Stationary(Stationary&&) = delete;
    Stationary& operator=(const Stationary&) = delete;
    Stationary& operator=(Stationary&&) = delete;
    ~Stationary() noexcept { --alive; }
    [[nodiscard]] decltype(auto) label() const {
        if (inspections_ == std::numeric_limits<unsigned long long>::max()) throw std::overflow_error("inspection counter overflow");
        ++inspections_;
        return (text_); // const reference; the facade copies while the owner is live
    }
    auto inspections() const noexcept -> unsigned long long { return inspections_; }
};
static_assert(!std::is_copy_constructible_v<Stationary> && !std::is_move_constructible_v<Stationary>);
static_assert(!std::is_copy_assignable_v<Stationary> && !std::is_move_assignable_v<Stationary>);
}
namespace {
struct Coordinate { int row; int column; };
static_assert(std::is_aggregate_v<Coordinate> && std::is_standard_layout_v<Coordinate> && std::is_trivial_v<Coordinate>);
struct Empty {};
struct Compact { [[no_unique_address]] Empty marker; int value; };
static int checked_sum(int left, int right) {
    const auto value = static_cast<long long>(left) + right;
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::out_of_range("native form arithmetic overflow");
    return static_cast<int>(value);
}
#if defined(__GNUC__) || defined(__clang__)
#define ROSETTA_FORM_RESTRICT __restrict__
constexpr bool restricted_pointers = true;
#else
#define ROSETTA_FORM_RESTRICT
constexpr bool restricted_pointers = false;
#endif
// The facade supplies separate arrays, preserving the native no-alias contract.
void shift(const int* ROSETTA_FORM_RESTRICT input, int* ROSETTA_FORM_RESTRICT output, int bias) {
    for (std::size_t i = 0; i < 6; ++i) output[i] = checked_sum(input[i], bias);
}
#undef ROSETTA_FORM_RESTRICT
void unpack(const int (&matrix)[2][3], int (&output)[6]) {
    for (std::size_t row = 0; row < 2; ++row)
        for (std::size_t column = 0; column < 3; ++column) output[row * 3 + column] = matrix[row][column];
}
}
namespace demo {
NativeStationary::NativeStationary(std::string text, bool fail)
    : native_(std::make_shared<feature_library::forms::Stationary>(std::move(text), fail)) {}
NativeStationary::NativeStationary(std::shared_ptr<feature_library::forms::Stationary> native) : native_(std::move(native)) {}
std::string NativeStationary::text() const { return native_->label(); }
unsigned long long NativeStationary::inspections() const { return native_->inspections(); }
bool NativeStationary::same(const NativeStationary& other) const { return native_.get() == other.native_.get(); }
NativeStationary NativeStationary::share() const { return NativeStationary(native_); }
const NativeStationary& NativeStationary::readonly() const { return *this; }
int NativeStationary::live() { return feature_library::forms::Stationary::alive.load(); }
int NativeStationary::setting() { return feature_library::forms::Stationary::shared_setting.load(); }
void NativeStationary::set_setting(int value) { feature_library::forms::Stationary::shared_setting.store(value); }
std::map<std::string, bool> native_storage_traits() {
    using State = feature_library::forms::Stationary;
    return {{"copy_constructible", std::is_copy_constructible_v<State>}, {"move_constructible", std::is_move_constructible_v<State>},
        {"copy_assignable", std::is_copy_assignable_v<State>}, {"move_assignable", std::is_move_assignable_v<State>},
        {"aggregate", std::is_aggregate_v<Coordinate>}, {"standard_layout", std::is_standard_layout_v<Coordinate>},
        {"trivial", std::is_trivial_v<Coordinate>}, {"empty_marker", std::is_empty_v<Empty>},
        {"compact_standard_layout", std::is_standard_layout_v<Compact>}, {"restricted_pointers", restricted_pointers}};
}
int native_internal_add(int left, int right) {
    // Local and unnamed types have no externally usable name; adapt in their TU.
    class Local {
    public:
        [[nodiscard]] auto operator()(Coordinate value) const { return checked_sum(value.row, value.column); }
    };
    struct { int first; int second; } unnamed{left, right};
    using Native = decltype(unnamed);
    [[maybe_unused]] const Native copy = unnamed;
    const Coordinate coordinate{.row = unnamed.first, .column = unnamed.second};
    return Local{}(coordinate);
}
std::array<std::array<int, 3>, 2> native_matrix_shift(std::array<std::array<int, 3>, 2> values, int bias) {
    int matrix[2][3]{};
    for (std::size_t row = 0; row < 2; ++row)
        for (std::size_t column = 0; column < 3; ++column) matrix[row][column] = values[row][column];
    int input[6]{}, output[6]{};
    unpack(matrix, input);
    shift(input, output, bias);
    for (std::size_t row = 0; row < 2; ++row)
        for (std::size_t column = 0; column < 3; ++column) values[row][column] = output[row * 3 + column];
    return values;
}
}
