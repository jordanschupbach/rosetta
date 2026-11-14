#pragma once
// C++ names Go spells differently (keywords, predeclared identifiers, export capitalization,
// underscores), and the C++ types the full Go backend maps (sets, tuples, complex numbers, 64-bit
// integers, enums with big values).
#include <complex>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace gonames {

// Go keywords as function and parameter names.
inline int func(int type) { return type + 1; }
inline int range(int chan, int select) { return chan * select; }
inline int go(int map) { return map + 10; }
inline int select(int interface) { return interface * 3; }
inline int chan(int go, int var) { return go - var; }
/// Exported to Go as Default (the policy renames it: default is a C++ keyword).
inline std::string fallback(const std::string& string) { return "default:" + string; }

// Predeclared Go identifiers.
inline int len(int cap) { return cap * 2; }
inline int cap(int len) { return len * 3; }
inline std::string error(const std::string& error) { return "error:" + error; }
inline int make(int copy, int append) { return copy + append; }
inline int close(int new_value) { return -new_value; }
inline int copy(int nil) { return nil; }
inline int append(int iota) { return iota + 100; }
/// Renamed to new by the policy.
inline int create(int len) { return len * 4; }
inline int rosetta(int impl) { return impl + 7; }

// Names that differ only in capitalization or underscores once exported.
inline int foo() { return 1; }
inline int Foo() { return 2; }
inline int snake_case_name() { return 3; }
inline int snakeCaseName() { return 4; }
inline int _leading() { return 5; }
inline int trailing_() { return 6; }

// A class named like a Go keyword, with members named like keywords and predeclared names.
class type {
public:
    int func = 1;
    int len = 2;
    explicit type(int range = 0) : func(range + 1) {}
    int range(int map) const { return map * 2; }
    int Range() const { return 99; }
    int close() const { return 11; }
    std::string string() const { return "type"; }
    static int make() { return 42; }
};
// A class named like the runtime's Error type.
struct Error {
    int code = 3;
    std::string to_string() const { return "Error(" + std::to_string(code) + ")"; }
};
struct point { int x = 1; int y = 2; };

// Enumerators named like keywords; values beyond int64.
enum class Keyword { type, func, range };
enum class Big : std::uint64_t { small = 1, huge = 18446744073709551615ull };
inline Keyword next_keyword(Keyword keyword) { return keyword == Keyword::range ? Keyword::type : static_cast<Keyword>(static_cast<int>(keyword) + 1); }
inline Big biggest() { return Big::huge; }

// Types.
inline std::complex<double> conjugate(std::complex<double> z) { return std::conj(z); }
inline std::set<std::string> unique_words(const std::vector<std::string>& words) { return {words.begin(), words.end()}; }
inline std::size_t count_set(const std::set<int>& values) { return values.size(); }
inline std::tuple<int, std::string, double> triple(int n) { return {n, std::to_string(n), n / 2.0}; }
inline std::map<int, std::vector<double>> buckets(const std::vector<double>& values) {
    std::map<int, std::vector<double>> result;
    for (double value : values) result[static_cast<int>(value)].push_back(value);
    return result;
}
inline std::uint64_t big() { return 18446744073709551615ull; }
inline std::int64_t negate(std::int64_t value) { return -value; }
inline unsigned char low_byte(unsigned value) { return static_cast<unsigned char>(value & 0xff); }
inline int checked(int value) {
    if (value > 100) throw std::length_error("too long");
    if (value < 0) throw std::domain_error("negative");
    return value;
}
inline int quiet(int value) noexcept { return value + 1; }

// A director class whose constructors have defaults and overloads: every NewGaugeFrom variant
// takes impl (NewGaugeFromWithScale, NewGaugeFromLabel), not only the shortest.
class Gauge {
public:
    explicit Gauge(double offset = 1.0, double scale = 2.0) : offset_(offset), scale_(scale) {}
    explicit Gauge(const std::string& label) : offset_(static_cast<double>(label.size())), scale_(1.0) {}
    virtual ~Gauge() = default;
    virtual double read(double x) const { return offset_ + scale_ * x; }

private:
    double offset_, scale_;
};
inline double read_gauge(const Gauge& gauge, double x) { return gauge.read(x); }

} // namespace gonames
