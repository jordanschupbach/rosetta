#pragma once
// Names and values that are hard for Perl (--language perl; tests/test_perl_names.pl, ctest
// perl_full_names): Perl builtins as function names; classes named like the runtime's packages,
// a policy module's package, or alike but for case; members named like the subs Perl or
// NativeObject reserve; a renamed virtual method a Perl subclass overrides; multiple inheritance;
// C++ hiding; operators, to_string and operator(); 64-bit unsigned values; booleans.
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <iostream>
#include <functional>
#include <string>
#include <vector>

namespace names {

/// Named like the runtime's base class.
struct NativeObject { int native = 2; };
/// Named like the runtime's error classes.
struct Error { std::string what = "not an exception"; };
/// Named like the runtime's package.
struct Rosetta { int id = 3; };
/// Named like the policy module's package (which yields).
struct Data { int value = 4; };
/// Two classes alike but for case.
struct point { int x = 1; };
struct Point { int y = 2; };
inline int point_x(const point& p) { return p.x; }
inline int point_y(const Point& p) { return p.y; }

// Perl builtins as function names: they take an underscore (imports could not override them).
inline int print(int x) { return x; }
inline int length(const std::string& text) { return static_cast<int>(text.size()) * 10; }
inline std::vector<int> sort(std::vector<int> values) { std::vector<int> out(values.rbegin(), values.rend()); return out; }
inline int count(const std::vector<int>& values) { return static_cast<int>(values.size()); }   // not a builtin

/// Cases that are Perl specials or alike but for case.
enum class Mode { on, On, BEGIN, STDIN, off };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Members named like the subs Perl and NativeObject reserve, and names alike in snake_case.
struct Cursor {
    int position = 0;
    int count = 2;                                      // the field yields to the method
    int Count() const { return count * 10; }
    int new_() const { return 1; }                      // Perl name new_
    bool close() { return true; }                       // the generated close comes first
    std::string cast() const { return "cast"; }
    bool isa() const { return false; }
    int import() const { return 7; }
    std::string getName() const { return "camel"; }
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int print(int self) const { return self * 2; }      // a builtin is a fine method name
    int move(int by) { position += by; return position; }
    static int create() { return 5; }
};

/// A virtual method renamed in Perl (close is NativeObject's): a Perl subclass overrides the
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

/// Multiple inheritance: Perl's @ISA lists both bases.
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

/// C++ hiding: Leaf::scale and Leaf::size hide Base's.
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

/// Operators (`$money * 2`, not `2 * $money`), to_string, operator().
struct Money {
    long long cents = 0;
    Money() = default;
    explicit Money(long long c) : cents(c) {}
    Money operator+(const Money& other) const { return Money(cents + other.cents); }
    Money operator*(long long n) const { return Money(cents * n); }
    bool operator<(const Money& other) const { return cents < other.cents; }
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
/// Prints, then throws: the text must still come before the caller's later output.
inline void say_then_throw(const std::string& text) { std::printf("%s\n", text.c_str()); throw std::invalid_argument("after printing"); }
/// Writes `text` to std::cout and stdout, around a callback that may print too (output order).
inline void say_around(const std::string& text, const std::function<void()>& f) {
    std::cout << text << '[';
    f();
    std::printf("]\n");
}

namespace data {
inline int twice(int x) { return 2 * x; }
}

} // namespace names
