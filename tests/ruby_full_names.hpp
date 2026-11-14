#pragma once
// Names Ruby reserves or already uses, and Ruby protocols (--ruby-api full): classes named like
// Ruby core classes, methods named like Ruby keywords and Kernel/Object methods, predicates and
// setters, operators, hash/eql?, each/Enumerable, Comparable, to_s, flags, big integers and policy
// modules (tests/test_ruby_full_names.rb, ctest ruby_full_names).
#include <complex>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace shadow {

/// Not ::Object.
struct Object {
    int value = 1;
};
/// Not ::Kernel.
struct Kernel {
    int puts() const { return 2; }
};
/// Not ::Comparable.
struct Comparable {};
/// Not ::Complex.
struct Complex {
    double re = 3;
    double im = 4;
    double norm() const { return re * re + im * im; }
};
/// Not ::Set.
struct Set {
    int size() const { return 0; }
};
/// Not ::Proc.
struct Proc {};
/// Not Error, the module the runtime defines.
struct Error {
    int code = 5;
};

/// Methods named like Ruby keywords and Object methods.
class Words {
public:
    int end() const { return 1; }
    int then() const { return 2; }
    int object_id() const { return 3; }
    int send(int x) const { return x; }
    int begin() const { return 4; }
    int next() const { return 5; }
    int display() const { return 6; }
    int hash_code() const { return 7; }
    int freeze() const { return 8; }
    bool is_empty() const { return name_.empty(); }
    bool has_name() const { return !name_.empty(); }
    bool is_named(const std::string& name) const { return name_ == name; }
    bool empty() const { return name_.empty(); }
    /// Sets the name.
    void set_name(const std::string& name) { name_ = name; }
    const std::string& get_name() const { return name_; }
    std::string to_string() const { return "Words(" + name_ + ")"; }
    static int create() { return 9; }
    int camelCase() const { return 10; }
    int HTTPServer() const { return 11; }
private:
    std::string name_;
};

/// to_string with only defaulted parameters is also to_s.
struct Table {
    std::string to_string(int max_rows = 3) const { return "Table(" + std::to_string(max_rows) + ")"; }
};

/// A point with ==, < and std::hash: Comparable, eql? and hash.
struct Point {
    int x = 0, y = 0;
    Point() = default;
    Point(int x_, int y_) : x(x_), y(y_) {}
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    bool operator<(const Point& other) const { return x < other.x || (x == other.x && y < other.y); }
};

/// size() and operator[]: each and Enumerable; a mutable operator[] also gives []=.
class Row {
public:
    explicit Row(int n = 3) : values_(n) { for (int i = 0; i < n; ++i) values_[i] = i * 1.5; }
    std::size_t size() const { return values_.size(); }
    double& operator[](std::size_t i) {
        if (i >= values_.size()) throw std::out_of_range("Row index");
        return values_[i];
    }
private:
    std::vector<double> values_;
};

/// A parent handing out a child that refers to it.
struct Node {
    int value = 1;
    std::vector<int> items{1, 2, 3};
    Node& self() { return *this; }
    std::shared_ptr<Node> sibling() const { auto node = std::make_shared<Node>(); node->value = value + 1; return node; }
};

enum class Permission : unsigned { read = 1, write = 2, execute = 4 };
enum Color { red, green = 5, blue };

inline unsigned permission_bits(Permission p) { return static_cast<unsigned>(p); }
inline Permission all_permissions() { return static_cast<Permission>(7); }
inline Color next_color(Color c) { return c == red ? green : blue; }

inline std::complex<double> twice(std::complex<double> z) { return 2.0 * z; }
inline std::vector<double> range(int n) {
    std::vector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(i);
    return out;
}
inline std::map<std::string, int> counts(const std::vector<std::string>& words) {
    std::map<std::string, int> out;
    for (const auto& word : words) ++out[word];
    return out;
}
inline std::set<int> unique(const std::vector<int>& values) { return {values.begin(), values.end()}; }
inline double apply(const std::function<double(double)>& f, double x) { return f(x); }
inline void each(const std::vector<int>& values, const std::function<void(int)>& f) {
    for (int value : values) f(value);
}
inline std::function<int(int, int)> multiplier() { return [](int a, int b) { return a * b; }; }
inline std::pair<int, double> pair() { return {1, 2.5}; }
inline std::tuple<int, std::string, bool> triple() { return {1, "two", true}; }
inline double norm_of(const Complex& c) { return c.norm(); }
inline std::uint64_t big() { return 18446744073709551615ULL; }
inline std::int64_t smallest() { return INT64_MIN; }
inline std::uint64_t echo_u64(std::uint64_t value) { return value; }
inline std::int8_t echo_i8(std::int8_t value) { return value; }
inline std::optional<int> maybe(std::optional<int> value) { return value; }
inline std::string kind_of_value(const std::variant<int, std::string>& value) { return value.index() == 0 ? "int" : "string"; }
inline std::variant<int, std::string> either(bool text) { if (text) return std::string("text"); return 3; }
inline std::string echo(const std::string& text) { return text; }
inline std::string keyword_args(int a, int b = 2, const std::string& c = "c", bool d = false) {
    return std::to_string(a) + std::to_string(b) + c + (d ? "!" : "");
}
inline int class_name_length(const Object& o) { return o.value; }

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
    if (x == 2) throw std::logic_error("two");
    return x;
}
inline int lookup(int x) {
    if (x < 0) throw std::out_of_range("missing");
    return x;
}

namespace stats {
/// In the policy module Stats.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double value : values) sum += value;
    return values.empty() ? 0 : sum / values.size();
}
} // namespace stats

/// Writes to C++ std::cout (ordered with Ruby's $stdout output).
inline void say(const std::string& text) { std::cout << text; }

namespace detail {
// Not bound (policy exclude): a declaration named like a ruby.h function-like macro, which the
// generated source must parse before including ruby.h.
struct StringValue {
    std::string value;
    StringValue(std::string text) : value(std::move(text)) {}
};
} // namespace detail

} // namespace shadow

template<> struct std::hash<shadow::Point> {
    std::size_t operator()(const shadow::Point& p) const noexcept { return std::hash<int>{}(p.x * 31 + p.y); }
};
