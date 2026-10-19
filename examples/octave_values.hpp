#pragma once
#include <cstdint>
#include <limits>

namespace octave_values {
enum class Mode : unsigned char { add = 1, plus = 1, multiply = 2 };
enum Legacy { negative = -42, positive = 42 };
enum class Signed : std::int64_t { low = std::numeric_limits<std::int64_t>::min(), high = std::numeric_limits<std::int64_t>::max() };
enum class Wide : std::uint64_t { exact = (std::uint64_t(1) << 53) + 1, high = std::numeric_limits<std::uint64_t>::max() };
enum class Logical : bool { off = false, on = true };
using Operation = Mode;
inline Operation echo(Operation value = Mode::add) { return value; }
inline Legacy echo_legacy(Legacy value) { return value; }
inline Signed echo_signed(Signed value) { return value; }
inline Wide echo_wide(Wide value) { return value; }
inline Logical echo_logical(Logical value) { return value; }
inline Mode invalid_result() { return static_cast<Mode>(3); }
class Holder {
public:
    enum class State : int { idle = 0, ready = 42 };
    Mode mode;
    const State state = State::ready;
    explicit Holder(Mode value = Mode::add) : mode(value) {}
    Mode get() const { return mode; }
    void set(Mode value) { mode = value; }
    static State status() { return State::ready; }
};
}
