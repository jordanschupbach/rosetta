#pragma once
// Names Haskell reserves or already uses (--language haskell): functions, methods, fields,
// parameters and enumerators named like Haskell keywords; classes named like Prelude types
// (Maybe, Either, IO, String) and the runtime's own types (Handle, Value, CppError); functions
// named like Prelude values (map, show, return) and like the generated functions (new_*, to_*,
// extend_*); snake_case, camelCase and PascalCase names that meet in camelCase; big and small
// integers, tuples, sets, maps, policy conversions and modules, exception kinds
// (tests/test_haskell_names.hs, ctest haskell_full_names).
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace shadow {

/// Not Prelude's Maybe: the generated code always says P.Maybe.
struct Maybe {
    int value = 1;
};
/// Not Prelude's Either.
struct Either {
    int left() const { return 2; }
};
/// Not Prelude's IO.
struct IO {
    int run() const { return 3; }
};
/// Not Prelude's String.
struct String {
    std::string text = "s";
    std::string concat(const std::string& other) const { return text + other; }
};
/// Not the runtime's Handle, Value or CppError.
struct Handle {
    int id = 4;
};
struct Value {
    double amount = 5;
};
struct CppError {
    int code = 6;
};

/// Methods named like Haskell keywords and Prelude values.
class Words {
public:
    int type() const { return 1; }
    int data() const { return 2; }
    int let() const { return 3; }
    int in() const { return 4; }
    int where() const { return 5; }
    int of() const { return 6; }
    int case_() const { return 7; }
    int module() const { return 8; }
    int import() const { return 9; }
    int instance() const { return 10; }
    int deriving() const { return 11; }
    int newtype() const { return 12; }
    int foreign() const { return 13; }
    int forall() const { return 14; }
    int mdo() const { return 15; }
    int infixl() const { return 16; }
    int then() const { return 17; }
    int map() const { return 18; }
    int show() const { return 19; }
    /// camelCase and snake_case spellings of one name meet in camelCase: both survive.
    int camelCase() const { return 20; }
    int camel_case() const { return 21; }
    int HTTPServer() const { return 22; }
    static int create() { return 23; }
};

/// Fields and parameters named like keywords.
struct Record {
    int type = 1;
    int class_ = 2;
    std::string default_ = "v";
    int method(int let, int in = 3) const { return let + in; }
};

/// Haskell constructors (True, Nothing, Just) and an alias as enumerators.
enum class Token { type, where, True, Nothing, Just, lower_case };
enum Color { red, green = 5, blue, crimson = 0 };
/// An enumerator spelled like the type it would be prefixed with.
enum class Mode { mode_on, off };

inline int token_value(Token t) { return static_cast<int>(t); }
inline Token next_token(Token t) { return static_cast<Token>((static_cast<int>(t) + 1) % 6); }
inline Color next_color(Color c) { return c == red ? green : blue; }
inline int mode_value(Mode m) { return static_cast<int>(m); }

/// Prelude names: generated code must not depend on them.
inline int map(int x) { return x * 2; }
inline std::string show(int x) { return "<" + std::to_string(x) + ">"; }
inline int return_(int x) { return x + 1; }
inline int id(int x) { return -x; }
inline int length(const String& s) { return static_cast<int>(s.text.size()); }
inline int error(int x) { return x * 3; }

/// Named like the generated functions of class Words and Record.
inline int new_words() { return 30; }
inline int words_type() { return 31; }
inline int to_words() { return 32; }
inline int record_set_type() { return 33; }

inline std::set<int> unique(const std::vector<int>& values) { return {values.begin(), values.end()}; }
inline std::map<int, std::string> names() { return {{1, "one"}, {2, "two"}}; }
inline std::tuple<int, std::string, bool> triple() { return {1, "two", true}; }
inline std::pair<std::string, std::vector<int>> nested() { return {"n", {1, 2}}; }
inline std::optional<std::string> maybe(std::optional<std::string> value) { return value; }
inline std::function<int(int, int)> multiplier() { return [](int a, int b) { return a * b; }; }
inline int fold(const std::vector<int>& values, int init, const std::function<int(int, int)>& f) {
    for (int v : values) init = f(init, v);
    return init;
}
inline void each(const std::vector<int>& values, const std::function<void(int)>& f) {
    for (int v : values) f(v);
}
inline int count() {
    static int n = 0;
    return ++n;
}
inline int call_twice(const std::function<int()>& f) { return f() + f(); }
inline std::uint64_t big() { return 18446744073709551615ULL; }
inline std::uint64_t echo_u64(std::uint64_t value) { return value; }
inline std::int64_t smallest() { return INT64_MIN; }
inline std::int64_t echo_i64(std::int64_t value) { return value; }
inline std::uint8_t echo_u8(std::uint8_t value) { return value; }
/// std::variant has no Haskell mapping: reported without the policy exclude (or --skip-unsupported).
inline std::string kind_of_value(const std::variant<int, std::string>& value) { return value.index() == 0 ? "int" : "string"; }
inline std::string echo(const std::string& text) { return text; }
inline std::string with_nul() { return std::string("a\0b", 3); }
inline void swap(int& a, int& b) { std::swap(a, b); }
inline void append(std::string& text, const std::string& more) { text += more; }

/// Samples converted to and from std::vector<double> by the policy ("convert").
struct Samples {
    std::vector<double> values;
    Samples() = default;
    Samples(std::vector<double> v) : values(std::move(v)) {}
    operator std::vector<double>() const { return values; }
};
inline double total(const Samples& samples) {
    double sum = 0;
    for (double value : samples.values) sum += value;
    return sum;
}
inline Samples doubled(const Samples& samples) {
    Samples out = samples;
    for (double& value : out.values) value *= 2;
    return out;
}
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    if (x == 0) throw std::runtime_error("zero");
    if (x == 1) throw std::overflow_error("one");
    if (x == 2) throw std::domain_error("two");
    if (x == 3) throw std::length_error("three");
    if (x == 4) throw std::out_of_range("four");
    if (x == 5) throw 5;
    return x;
}

/// Nested class: OuterInner.
struct Outer {
    struct Inner {
        int depth() const { return 2; }
    };
    Inner inner() const { return {}; }
};

/// Constants named in SCREAMING_CASE and like a keyword.
constexpr int MAX_SIZE = 64;
constexpr double then = 0.5;

namespace stats {
/// In the policy module stats: statsMean.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double value : values) sum += value;
    return values.empty() ? 0 : sum / values.size();
}
/// Named like the top-level function.
inline int map(double a) { return static_cast<int>(a) + 100; }
} // namespace stats

/// Writes to C++ std::cout.
inline void say(const std::string& text) { std::cout << text << std::flush; }
/// Writes to C++ std::cout without flushing it.
inline void say_buffered(const std::string& text) { std::cout << text; }
/// Writes with C printf before and after calling f.
inline void say_around(const std::function<void()>& f) { std::printf("before\n"); f(); std::printf("after\n"); }

} // namespace shadow
