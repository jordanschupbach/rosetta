#pragma once
// Names Tcl and TclOO reserve or already use (--tcl-api full): free functions named like Tcl
// commands, classes named like TclOO and Tcl built-ins, methods named like TclOO's own methods,
// fields colliding with methods, strings full of Tcl specials, flags, big integers, maps, sets,
// variants, complex numbers and policy modules (tests/test_tcl_full_names.tcl, ctest
// tcl_full_names).
#include <complex>
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

namespace quirks {

// --- Free functions named like Tcl commands ---------------------------------------------------
/// Shares its name with Tcl's set.
inline int set(int value) { return value + 1; }
/// Shares its name with Tcl's list.
inline std::vector<int> list(int count) { return std::vector<int>(static_cast<std::size_t>(count), 7); }
/// Shares its name with Tcl's string.
inline std::string string(const std::string& text) { return "<" + text + ">"; }
/// Shares its name with Tcl's info.
inline int info() { return 42; }
/// Shares its name with Tcl's rename.
inline std::string rename(const std::string& from, const std::string& to) { return from + "->" + to; }
/// Shares its name with Tcl's unknown.
inline int unknown() { return 13; }
/// Shares its name with Tcl's namespace.
inline std::string namespace_(const std::string& name) { return "ns:" + name; }
/// Names TclOO uses for methods are fine for commands.
inline int destroy() { return 1; }
inline int create() { return 2; }
inline int next() { return 3; }

/// Returns its argument unchanged: Tcl specials must survive both ways.
inline std::string echo(const std::string& text) { return text; }
/// Concatenates with a separator default that is a Tcl special.
inline std::string join(const std::vector<std::string>& parts, const std::string& separator = "$[{") {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? separator : "") + parts[i];
    return out;
}
/// A default string with spaces, quotes and braces: "a {b} \"c\"".
inline std::string quoted(const std::string& text = "a {b} \"c\"") { return text; }

// --- Classes named like TclOO and Tcl built-ins -----------------------------------------------
/// Not oo::object.
struct object {
    int value = 1;
};
/// Not oo::class.
struct class_ {
    int value = 2;
};
/// Not ::dict.
struct dict {
    int size() const { return 0; }
};

/// Methods named like TclOO's own methods and the runtime's.
class Words {
public:
    int destroy() const { return 1; }
    int eval(int x) const { return x; }
    int variable() const { return 3; }
    int varname() const { return 4; }
    int unknown() const { return 5; }
    int cget() const { return 6; }
    int configure() const { return 7; }
    int alive() const { return 8; }
    int owned() const { return 9; }
    int next() const { return 10; }
    int self() const { return 11; }
    int my() const { return 12; }
    int set(int x) const { return x * 2; }
    int create() const { return 14; }
    int new_() const { return 15; }
    static int new_count() { return 16; }
    static int create_count() { return 17; }
};

/// Static methods named like TclOO class methods.
struct Factory {
    static int create(int x) { return x + 100; }
    static int destroy() { return -1; }
    static int eval() { return -2; }
    int made = 0;
};

/// A field and a method of the same name: the method wins, the field stays in cget/configure.
struct Clash {
    int size = 3;
    int count = 4;
    int destroy = 5;
    int get_size() const { return size; }
    int count_() const { return count * 10; }
};

// --- Values ---------------------------------------------------------------------------------
/// Permission flags.
enum class Permission : unsigned { read = 1, write = 2, execute = 4 };
inline unsigned permission_bits(Permission p) { return static_cast<unsigned>(p); }
inline Permission all_permissions() { return static_cast<Permission>(7); }

/// Integer limits.
inline std::int64_t wide(std::int64_t x) { return x; }
inline std::uint64_t uwide(std::uint64_t x) { return x; }
inline std::uint64_t biggest() { return UINT64_MAX; }
inline std::int8_t tiny(std::int8_t x) { return x; }
inline bool truth(bool b) { return !b; }

inline std::map<std::string, std::vector<int>> groups() { return {{"a b", {1, 2}}, {"{c}", {}}}; }
inline std::size_t group_count(const std::map<std::string, std::vector<int>>& g) { return g.size(); }
inline std::set<std::string> unique(const std::vector<std::string>& words) { return {words.begin(), words.end()}; }
inline std::tuple<int, std::string, double> triple() { return {1, "two words", 3.5}; }
inline std::variant<int, std::string> either(bool number) {
    if (number) return 5;
    return std::string("five");
}
inline std::string kind_of(const std::variant<int, std::string>& value) { return value.index() == 0 ? "int" : "string"; }
inline std::complex<double> rotate(std::complex<double> z) { return z * std::complex<double>(0, 1); }
inline std::optional<std::string> maybe(bool give) {
    if (give) return std::string("given");
    return std::nullopt;
}
inline double hypot2(std::pair<double, double> p) { return p.first * p.first + p.second * p.second; }

/// Comparison and call operators.
struct Point {
    int x = 1, y = 2;
    bool operator<(const Point& other) const { return x < other.x; }
    int operator()(int scale) const { return (x + y) * scale; }
};

/// Errors through an exception policy.
inline int lookup(const std::string& key) {
    if (key != "one") throw std::out_of_range("no key " + key);
    return 1;
}
inline void fail_logic() { throw std::logic_error("bad logic"); }
struct Custom : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline void fail_custom() { throw Custom("custom failure"); }
inline void fail_unknown() { throw 7; }

/// Callbacks with Tcl specials in their results, and callbacks taking objects.
inline std::string call_text(const std::function<std::string(const std::string&)>& f) { return f("$x [y] {z}"); }
inline int call_point(const std::function<int(const Point&)>& f) { return f(Point{}); }
inline void call_void(const std::function<void()>& f, int times) {
    for (int i = 0; i < times; ++i) f();
}

/// Writes to C++ std::cout (ordered with Tcl's stdout channel when output is piped).
inline void say(const std::string& text) { std::cout << text; }

namespace stats {
/// In the stats module.
inline double mean(const std::vector<double>& values) {
    double sum = 0;
    for (double v : values) sum += v;
    return values.empty() ? 0 : sum / static_cast<double>(values.size());
}
/// Shares a name with a top-level function; modules keep their own names.
inline int set(int value) { return value * 100; }
} // namespace stats

} // namespace quirks
