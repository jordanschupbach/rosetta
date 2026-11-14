#pragma once
// Names OCaml reserves or already uses (--ocaml-api full): functions, methods, fields, parameters
// and enumerators named like OCaml keywords; classes named like Stdlib modules (List, Array,
// String) and functions like Stdlib values (compare, print_string); camelCase and capitalized
// names that differ only in case style; big and small integers, tuples, sets, policy
// conversions and modules, exception kinds (tests/test_ocaml_full_names.ml, ctest ocaml_full_names).
#include <complex>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace shadow {

/// Not Stdlib.List: the generated code always says Stdlib.List.
struct List {
    std::vector<int> items{1, 2, 3};
    int length() const { return static_cast<int>(items.size()); }
};
/// Not Stdlib.Array.
struct Array {
    int get(int i) const { return i * 10; }
};
/// Not Stdlib.String.
struct String {
    std::string text = "s";
    std::string concat(const std::string& other) const { return text + other; }
};
/// Not Stdlib.Complex.
struct Complex {
    double re = 3, im = 4;
    double norm() const { return re * re + im * im; }
};
/// Not Option or Error, which the runtime uses.
struct Option {
    int value = 2;
};
struct Error {
    int code = 5;
};

/// Methods named like OCaml keywords.
class Words {
public:
    int type() const { return 1; }
    int module() const { return 2; }
    int val() const { return 3; }
    int end() const { return 4; }
    int function() const { return 5; }
    int fun() const { return 6; }
    int match() const { return 7; }
    int with() const { return 8; }
    int open() const { return 9; }
    int include() const { return 10; }
    int method() const { return 11; }
    int object() const { return 12; }
    int done() const { return 14; }
    int to() const { return 15; }
    int mod(int a, int b) const { return a % b; }
    int land(int a, int b) const { return a & b; }
    int sig() const { return 16; }
    int effect() const { return 17; }
    /// camelCase and snake_case spellings of one name stay distinct (camelCase is kept).
    int camelCase() const { return 18; }
    int camel_case() const { return 19; }
    /// Named like the reserved class functions.
    int close() const { return 20; }
    int cast() const { return 21; }
    static int create() { return 22; }
    int HTTPServer() const { return 23; }
};

/// Fields and parameters named like keywords.
struct Record {
    int type = 1;
    int end = 2;
    std::string val = "v";
    int method(int begin, int done = 3) const { return begin + done; }
};

/// Keywords, OCaml constructors (True, None) and an alias as enumerators.
enum class Token { type, end, open, True, None, Some, lower_case };
enum Color { red, green = 5, blue, crimson = 0 };

inline int token_value(Token t) { return static_cast<int>(t); }
inline Token next_token(Token t) { return static_cast<Token>((static_cast<int>(t) + 1) % 7); }
inline Color next_color(Color c) { return c == red ? green : blue; }

/// Stdlib names: generated code must not depend on them.
inline int compare(int a, int b) { return a < b ? -1 : a > b ? 1 : 0; }
inline std::string print_string(const std::string& text) { return "<" + text + ">"; }
inline int length(const List& list) { return list.length(); }
inline int ignore(int x) { return x; }
inline int raise(int x) { return -x; }
inline int not_(int x) { return !x; }
inline bool to_bool(int x) { return x != 0; }

inline std::complex<double> twice(std::complex<double> z) { return 2.0 * z; }
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
inline std::int64_t smallest() { return INT64_MIN; }
inline std::int64_t echo_i64(std::int64_t value) { return value; }
inline std::uint8_t echo_u8(std::uint8_t value) { return value; }
/// std::variant has no OCaml mapping: reported without the policy exclude (or --skip-unsupported).
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

/// Nested class: Outer_Inner.
struct Outer {
    struct Inner {
        int depth() const { return 2; }
    };
    Inner inner() const { return {}; }
};

namespace stats {
/// In the policy module Stats.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double value : values) sum += value;
    return values.empty() ? 0 : sum / values.size();
}
/// Named like the top-level function.
inline int compare(double a, double b) { return a < b ? -1 : a > b ? 1 : 0; }
} // namespace stats

/// Writes to C++ std::cout.
inline void say(const std::string& text) { std::cout << text << std::flush; }
/// Writes to C++ std::cout without flushing it.
inline void say_buffered(const std::string& text) { std::cout << text; }
/// Writes with C printf before and after calling f.
inline void say_around(const std::function<void()>& f) { std::printf("before\n"); f(); std::printf("after\n"); }

} // namespace shadow
