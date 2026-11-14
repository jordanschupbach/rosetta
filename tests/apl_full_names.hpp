#pragma once
// APL names and values (--language apl --apl-api full): names differing only in case, C++ names
// equal to the runtime's (Delete, fn, call, live, last_error) or to generated suffixes (new,
// subclass, set), leading and doubled underscores, operators next to methods with their APL names,
// enums nested in classes, updated reference arguments, integer widths, complex numbers, optional,
// variant, maps, booleans, matrices, void callbacks, error kinds and nested policy modules
// (tests/test_apl_full_names.apl, ctest apl_full_names).
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

/// Names that differ only in case: APL names are case-sensitive.
inline int value() { return 1; }
inline int Value() { return 2; }
inline int VALUE() { return 3; }

/// C++ names equal to the runtime's functions get a trailing underscore.
inline int Delete(int x) { return x + 10; }
inline int fn(int x) { return x + 20; }
inline int live() { return 30; }

/// Leading, doubled and trailing underscores and digits are APL name characters.
inline int _hidden() { return 4; }
inline int two__parts() { return 5; }
inline int trailing_() { return 6; }
inline int v2x() { return 7; }

/// Methods named like the generated constructor functions and operators; a virtual method makes
/// Words∆subclass exist, so the C++ method subclass becomes Words∆subclass_.
class Words {
public:
    enum class Mode { off, on = 3 };
    virtual ~Words() = default;
    /// Virtual: APL implementations (Words∆subclass) may override it.
    virtual int speak() const { return 8; }
    int subclass() const { return 1; }
    int set() const { return 2; }
    int plus(int x) const { return x + 100; }
    int operator+(int x) const { return x + 200; }
    int camelCase() const { return 3; }
    int CamelCase() const { return 4; }
    Mode mode(Mode m) const { return m; }
    static int create() { return 5; }
    /// The field and its setter (Words∆count∆set).
    int count = 6;
};

/// A string default with a quote.
inline std::string quoted(const std::string& text = "it's") { return text + "!"; }

/// Mutable reference parameters come back in the result.
inline void bounds(const std::vector<double>& values, double& lo, double& hi) {
    if (values.empty()) throw std::invalid_argument("no values");
    lo = hi = values[0];
    for (double v : values) { lo = v < lo ? v : lo; hi = v > hi ? v : hi; }
}
inline int increment(int& x) { return ++x; }

inline std::complex<double> twice(std::complex<double> z) { return 2.0 * z; }
inline std::set<int> unique(const std::vector<int>& values) { return {values.begin(), values.end()}; }
inline std::tuple<int, std::string, bool> triple() { return {1, "two", true}; }
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
// A pair of numbers is a simple numeric vector in APL, also when its items are floats.
inline std::pair<double, double> halves(double x) { return {x / 2, x / 2}; }
inline double sum_pair(const std::pair<double, double>& p) { return p.first + p.second; }
inline double sum_pairs(const std::vector<std::pair<double, double>>& ps) {
    double sum = 0;
    for (const auto& p : ps) sum += p.first + p.second;
    return sum;
}
inline double sum_triple(const std::tuple<double, int, double>& t) { return std::get<0>(t) + std::get<1>(t) + std::get<2>(t); }
inline int each(const std::vector<int>& values, const std::function<void(int)>& f) {
    for (int value : values) f(value);
    return static_cast<int>(values.size());
}
inline std::string join(const std::vector<std::string>& parts, const std::function<std::string(std::string, std::string)>& f) {
    std::string result;
    for (const auto& part : parts) result = result.empty() ? part : f(result, part);
    return result;
}
inline int speak_of(const Words& words) { return words.speak(); }
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    if (x == 0) throw std::runtime_error("zero");
    if (x == 1) throw std::overflow_error("one");
    if (x == 2) throw std::out_of_range("two");
    if (x == 3) throw std::length_error("three");
    return x;
}

namespace stats {
/// In the policy module stats: names∆stats∆mean.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double value : values) sum += value;
    return values.empty() ? 0 : sum / static_cast<double>(values.size());
}
} // namespace stats

namespace inner {
/// In the policy module tools: names∆tools∆depth.
inline int depth() { return 2; }
} // namespace inner

} // namespace names
