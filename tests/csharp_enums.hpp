#pragma once

namespace enum_test {
enum class Mode : int { low = -2, next, zero, answer = 6 * 7, alias = answer };
enum Plain { first, second, third = 10 };
enum class Big : unsigned long long { zero, maximum = 18446744073709551615ULL };
enum class Signed : long long { minimum = (-9223372036854775807LL - 1), next, maximum = 9223372036854775807LL };
enum class Boolean : bool { no = false, yes = true };
using Choice = Mode;
inline Choice echo(Choice value = Mode::answer) { return value; }
inline Plain echo_plain(Plain value) { return value; }
inline Big echo_big(Big value) { return value; }
inline Signed echo_signed(Signed value) { return value; }
inline Boolean echo_bool(Boolean value) { return value; }
inline Mode invalid_result() { return static_cast<Mode>(9); }
struct Holder {
    Mode mode;
    const Mode initial;
    explicit Holder(Mode value = Mode::answer) : mode(value), initial(value) {}
    Mode get() const { return mode; }
    void set(Mode value) { mode = value; }
    static Mode identity(Mode value) { return value; }
};
} // namespace enum_test
