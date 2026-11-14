#pragma once
// Names Octave reserves or already uses, and Octave protocols (--octave-api full): methods and
// fields named like Octave keywords and classdef protocol functions (disp, size, numel, ...),
// names differing only in case, updated reference arguments, operator[] and operator(), integer
// widths, containers and nested policy modules (tests/test_octave_full_names.m, ctest
// octave_full_names).
#include <complex>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace names {

/// Not containers.Map.
struct Map {
    int count() const { return 1; }
};

/// Methods named like Octave keywords and classdef protocol functions.
class Words {
public:
    int end() const { return 1; }
    int function() const { return 2; }
    int classdef() const { return 3; }
    int methods() const { return 4; }
    int properties() const { return 5; }
    int global() const { return 6; }
    int disp() const { return 7; }
    int size() const { return 8; }
    int numel() const { return 9; }
    int length() const { return 10; }
    int isempty() const { return 11; }
    int isclosed() const { return 12; }
    int eq(int x) const { return x; }
    int rosetta_h() const { return 13; }
    int camelCase() const { return 14; }
    int CamelCase() const { return 15; }
    /// Shares its name with a property of the class.
    int label() const { return 16; }
    static int create() { return 17; }
};

/// Fields named like keywords and protocol functions.
struct Span {
    double end = 2;
    double begin = 1;
    int properties = 3;
    int size = 4;
    int disp = 5;
};

/// Names that differ only in case.
inline int value() { return 1; }
inline int Value() { return 2; }
struct Item {
    int id = 1;
};
inline int item() { return 3; }

/// Free functions named like Octave functions keep their names (inside the package); keywords do not.
inline int disp(int x) { return x + 1; }
inline int size(const std::vector<double>& v) { return static_cast<int>(v.size()); }
inline int end(int x) { return x + 2; }
inline int global(int x) { return x + 3; }

enum class Color { red, green = 5, end };
inline int color_value(Color c) { return static_cast<int>(c); }
inline Color next_color(Color c) { return c == Color::red ? Color::green : Color::end; }

/// Mutable reference parameters come back as extra outputs.
inline void bounds(const std::vector<double>& values, double& lo, double& hi) {
    if (values.empty()) throw std::invalid_argument("no values");
    lo = hi = values[0];
    for (double v : values) { lo = v < lo ? v : lo; hi = v > hi ? v : hi; }
}
inline int increment(int& x) { return ++x; }
inline void append(std::vector<std::string>& words, const std::string& word) { words.push_back(word); }

/// operator[] (1-based through obj(i), 0-based through obj{i}) and size().
class Row {
public:
    explicit Row(int n = 3) : values_(static_cast<std::size_t>(n)) { for (int i = 0; i < n; ++i) values_[static_cast<std::size_t>(i)] = i * 1.5; }
    std::size_t size() const { return values_.size(); }
    double operator[](std::size_t i) const {
        if (i >= values_.size()) throw std::out_of_range("Row index");
        return values_[i];
    }
    void clear() { values_.clear(); }
private:
    std::vector<double> values_;
};

/// A callable object: operator().
struct Polynomial {
    std::vector<double> coefficients;
    double operator()(double x) const {
        double result = 0;
        for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) result = result * x + *it;
        return result;
    }
};

/// Comparison operators: == (and ~= derived), < (and > derived).
struct Version {
    int major = 0, minor = 0;
    Version() = default;
    Version(int a, int b) : major(a), minor(b) {}
    bool operator==(const Version& o) const { return major == o.major && minor == o.minor; }
    bool operator<(const Version& o) const { return major < o.major || (major == o.major && minor < o.minor); }
};

inline std::complex<double> twice(std::complex<double> z) { return 2.0 * z; }
inline std::set<int> unique(const std::vector<int>& values) { return {values.begin(), values.end()}; }
inline std::tuple<int, std::string, bool> triple() { return {1, "two", true}; }
inline std::uint64_t big() { return 18446744073709551615ULL; }
inline std::int64_t smallest() { return INT64_MIN; }
inline std::uint64_t echo_u64(std::uint64_t value) { return value; }
inline std::int8_t echo_i8(std::int8_t value) { return value; }
inline std::optional<int> maybe(std::optional<int> value) { return value; }
inline std::string kind_of_value(const std::variant<int, std::string>& value) { return value.index() == 0 ? "int" : "string"; }
inline std::map<int, std::string> numbered() { return {{1, "one"}, {2, "two"}}; }
inline double total(const std::map<std::string, double>& scores) {
    double sum = 0;
    for (const auto& item : scores) sum += item.second;
    return sum;
}
inline bool negate(bool value) { return !value; }
inline std::vector<bool> flags() { return {true, false, true}; }
inline std::vector<std::string> words() { return {"a", "bb"}; }
inline std::vector<std::vector<int>> grid() { return {{1, 2, 3}, {4, 5, 6}}; }
inline std::vector<std::vector<double>> ragged() { return {{1}, {2, 3}}; }
inline double sum_matrix(const std::vector<std::vector<double>>& rows) {
    double sum = 0;
    for (const auto& row : rows) for (double v : row) sum += v;
    return sum;
}
inline void each(const std::vector<int>& values, const std::function<void(int)>& f) {
    for (int value : values) f(value);
}
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    if (x == 0) throw std::runtime_error("zero");
    if (x == 1) throw std::overflow_error("one");
    if (x == 2) throw std::out_of_range("two");
    return x;
}

namespace stats {
/// In the policy module stats: names.stats.mean.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double value : values) sum += value;
    return values.empty() ? 0 : sum / static_cast<double>(values.size());
}
} // namespace stats

namespace inner {
/// In the policy module tools: names.tools.depth.
inline int depth() { return 2; }
} // namespace inner

} // namespace names
