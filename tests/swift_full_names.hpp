#pragma once
// Names and values that are hard for Swift (--language swift; tests/test_swift_names.swift, ctest
// swift_full_names): classes named like the runtime's types, the standard library's or the module;
// Swift keywords and standard library functions as names; members named like NativeObject's; a
// renamed virtual method a Swift subclass overrides; multiple inheritance; C++ hiding; operators,
// to_string and operator(); 64-bit values; booleans and inout parameters.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace names {

/// Named like the runtime's base class.
struct NativeObject { int native = 2; };
/// Named like the runtime's error and value types.
struct NativeError { std::string what = "not an exception"; };
struct Complex { double re = 1.5; };
/// Named like the runtime's namespace.
struct Rosetta { int id = 3; };
/// Named like standard library types (which the generated code uses).
struct String { std::string text = "text"; };
struct Array { int size = 5; };
/// Named like the module.
struct Names { int self_ = 6; };
/// Named like the policy module's enum (which yields).
struct Data { int value = 4; };
/// Two classes alike but for case.
struct point { int x = 1; };
struct Point { int y = 2; };
inline int point_x(const point& p) { return p.x; }
inline int point_y(const Point& p) { return p.y; }

// Standard library functions as function names take an underscore; keywords need backticks.
inline int print(int x) { return x; }
inline int min(int a, int b) { return a < b ? b : a; }
inline std::string repeat(const std::string& text) { return text + text; }
inline int in(int self, int rc) { return self + rc; }
inline int count(const std::vector<int>& values) { return static_cast<int>(values.size()); }   // a fine name

/// Cases that are Swift keywords, NativeObject members, or alike but for case.
enum class Mode { on, On, self, Default, rawValue, init, Type, off };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Members named like NativeObject's or Swift's, and names alike in camelCase.
struct Cursor {
    int position = 0;
    int count = 2;                                      // the field yields to the method
    int rosettaHandle = 9;
    int Count() const { return count * 10; }
    bool close() { return true; }                       // the generated close comes first
    std::string cast() const { return "cast"; }
    std::string description() const { return "described"; }
    bool isClosed() const { return false; }
    int init() const { return 7; }
    int subscript(int i) const { return i * 3; }
    std::string getName() const { return "camel"; }
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int print(int self) const { return self * 2; }      // a fine method name
    int move(int by) { position += by; return position; }
    static int create() { return 5; }
};

/// A virtual method renamed in Swift (close is NativeObject's): a Swift subclass overrides the
/// renamed method, and C++ calls it.
class Resource {
public:
    virtual ~Resource() = default;
    virtual std::string close() { return "C++ close"; }
};
inline std::string close_resource(Resource& resource) { return resource.close(); }
class File : public Resource {
public:
    std::string close() override { return "file close"; }
};

/// Multiple inheritance: Counted is the superclass, Named's members are declared again.
class Named {
public:
    virtual ~Named() = default;
    virtual std::string label() const { return "named"; }
    std::string tag = "t";
};
class Counted {
public:
    virtual ~Counted() = default;
    int counted() const { return 3; }
};
class Thing : public Counted, public Named {
public:
    std::string label() const override { return "thing"; }
};
inline std::string label_of(const Named& named) { return named.label(); }
inline int counted_of(const Counted& counted) { return counted.counted(); }
inline Named* as_named(Thing& thing) { return &thing; }

/// C++ hiding: Leaf::scale overloads Base's in Swift; Leaf::size returns another type, so it takes
/// another name.
class Base {
public:
    virtual ~Base() = default;
    virtual std::string describe() const { return "base"; }
    double scale(double factor) const { return factor; }
    int size() const { return 1; }
};
class Leaf : public Base {
public:
    std::string describe() const override { return "leaf"; }
    double scale(double factor, double extra) const { return factor * extra; }
    std::string size() const { return "big"; }
};
inline std::string describe_base(const Base& base) { return base.describe(); }

/// Operators, to_string (the description) and operator() (callAsFunction).
struct Money {
    long long cents = 0;
    Money() = default;
    explicit Money(long long c) : cents(c) {}
    Money operator+(const Money& other) const { return Money(cents + other.cents); }
    Money operator*(long long n) const { return Money(cents * n); }
    bool operator<(const Money& other) const { return cents < other.cents; }
    bool operator!() const { return cents == 0; }
    std::string to_string() const { return std::to_string(cents / 100) + "." + (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100); }
    long long operator()(long long extra) const { return cents + extra; }
};

// Values.
inline std::uint64_t biggest() { return UINT64_MAX; }
inline std::uint64_t echo_u64(std::uint64_t value) { return value; }
inline long long echo_i64(long long value) { return value; }
inline bool negate(bool value) { return !value; }
inline std::string kind_of(bool) { return "bool"; }
inline std::string kind_of(long long) { return "int"; }
inline std::string kind_of(const std::string&) { return "string"; }
inline void bump(int& value, std::string& text) { ++value; text += "!"; }
inline int call_twice(const std::function<int(int)>& f, int x) { return f(f(x)); }
inline std::vector<std::pair<std::string, int>> ranked(const std::vector<std::string>& names) {
    std::vector<std::pair<std::string, int>> out;
    for (std::size_t i = 0; i < names.size(); ++i) out.emplace_back(names[i], static_cast<int>(i + 1));
    return out;
}

namespace data {
inline int twice(int x) { return 2 * x; }
}

} // namespace names
