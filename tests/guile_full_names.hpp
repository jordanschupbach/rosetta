#pragma once
// Names Scheme reserves or already uses, and Scheme conventions (--guile-api full): functions named
// like syntax (define, lambda, if, quote) and core procedures (list, length, display, apply),
// GOOPS names (make, initialize, class-name, <string>), predicates (?) and mutators (!), case,
// operators, flag enumerations, big integers, complex numbers, maps from hash tables, policy
// exceptions and modules (tests/test_guile_full_names.scm, ctest guile_full_names).
#include <complex>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace shadow {

/// Shadows Scheme syntax: renamed define_.
inline int define(int x) { return x + 1; }
/// Shadows Scheme syntax: renamed lambda_.
inline int lambda(int x) { return x + 2; }
/// Shadows Scheme syntax: renamed if_.
inline int if_(int x) { return x + 3; }
/// Shadows Scheme syntax: renamed quote_.
inline std::string quote(const std::string& text) { return "'" + text + "'"; }
/// Replaces the core procedure list (other arguments call Guile's list).
inline std::string list(int count) { return "list of " + std::to_string(count); }
/// Replaces the core procedure length.
inline int length(const std::string& text) { return static_cast<int>(text.size()) * 10; }
/// Replaces the core procedure display.
inline std::string display(double value) { return "<" + std::to_string(static_cast<int>(value)) + ">"; }
/// Shadows the GOOPS procedure make: renamed make_.
inline int make(int x) { return x * 2; }
/// A parameter named like syntax and the wrapper's own names.
inline int formals(int lambda, int args, int self) { return lambda + args + self; }

/// Not GOOPS's <string>: renamed <string_>.
struct String {
    std::string text = "s";
    int size() const { return static_cast<int>(text.size()); }
};
/// Not GOOPS's <class>.
struct Class {
    int id = 4;
};

/// Predicates, mutators, case and core-procedure method names.
class Words {
public:
    bool is_empty() const { return name_.empty(); }
    bool has_name() const { return !name_.empty(); }
    bool matches(const std::string& name) const { return name_ == name; }
    /// Sets the name.
    void set_name(const std::string& name) { name_ = name; }
    const std::string& get_name() const { return name_; }
    int length() const { return static_cast<int>(name_.size()); }
    int display() const { return 6; }
    int initialize() const { return 7; }
    int class_name() const { return 8; }
    static int create() { return 9; }
    int camelCase() const { return 10; }
    int HTTPServer() const { return 11; }
    int reverse() const { return 12; }
private:
    std::string name_;
};

/// Operators beyond arithmetic.
struct Bits {
    unsigned value = 0;
    Bits() = default;
    explicit Bits(unsigned v) : value(v) {}
    Bits operator&(const Bits& o) const { return Bits(value & o.value); }
    Bits operator|(const Bits& o) const { return Bits(value | o.value); }
    Bits operator^(const Bits& o) const { return Bits(value ^ o.value); }
    Bits operator~() const { return Bits(~value); }
    Bits operator<<(int n) const { return Bits(value << n); }
    Bits operator>>(int n) const { return Bits(value >> n); }
    Bits operator/(const Bits& o) const {
        if (o.value == 0) throw std::domain_error("division by zero bits");
        return Bits(value / o.value);
    }
    Bits operator%(const Bits& o) const { return Bits(value % o.value); }
    bool operator<(const Bits& o) const { return value < o.value; }
    bool operator<=(const Bits& o) const { return value <= o.value; }
    bool operator>(const Bits& o) const { return value > o.value; }
    bool operator>=(const Bits& o) const { return value >= o.value; }
    bool operator==(const Bits& o) const { return value == o.value; }
    int operator()(int x) const { return static_cast<int>(value) + x; }
};

/// A mutable indexer: ref and ref-set!.
struct Row {
    std::vector<int> cells = std::vector<int>(3, 0);
    int& operator[](int index) {
        if (index < 0 || index >= 3) throw std::out_of_range("Row index");
        return cells[static_cast<std::size_t>(index)];
    }
};

/// Enumerators differing in case only keep distinct symbols.
enum class Case { None, none, Mixed_Case };
/// A flag enumeration (policy enum_flags): values are integers.
enum class Permission : unsigned { read = 1, write = 2, execute = 4 };
inline unsigned permission_bits(Permission p) { return static_cast<unsigned>(p); }
inline Permission all_permissions() { return static_cast<Permission>(7); }

inline std::int64_t big(std::int64_t x) { return x; }
inline std::uint64_t ubig(std::uint64_t x) { return x; }
inline std::complex<double> rotate(std::complex<double> z) { return z * std::complex<double>(0, 1); }
inline int total(const std::map<std::string, int>& counts) {
    int sum = 0;
    for (const auto& [key, value] : counts) sum += value;
    return sum;
}
inline std::unordered_map<std::string, int> counts() { return {{"a", 1}}; }
inline std::set<int> unique(const std::vector<int>& values) { return {values.begin(), values.end()}; }
inline std::tuple<int, std::string, double> triple() { return {1, "two", 3.0}; }
inline std::variant<int, std::string> either(bool text) {
    if (text) return std::string("text");
    return 5;
}
inline std::string describe_either(const std::variant<int, std::string>& value) {
    return value.index() == 0 ? "int" : "string";
}
inline int lookup(const std::map<std::string, int>& table, const std::string& key) {
    return table.at(key); // std::out_of_range, mapped to key-error by the policy
}
inline const char* c_text(bool empty) { return empty ? nullptr : "c"; }
inline int hex_default(int mask = 0xff, double ratio = 1, bool on = 1) { return on ? mask + static_cast<int>(ratio) : 0; }

} // namespace shadow

namespace shadow::stats {
/// Mapped by the policy to the (names stats) module.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double v : values) sum += v;
    return values.empty() ? 0 : sum / static_cast<double>(values.size());
}
/// Replaces the core procedure max in (names stats).
inline double max(const std::vector<double>& values) {
    double best = values.empty() ? 0 : values[0];
    for (double v : values) best = v > best ? v : best;
    return best;
}
} // namespace shadow::stats
