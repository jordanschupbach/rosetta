#pragma once
// Names and values that are hard for Prolog (--language prolog; tests/test_prolog_names.pl, ctest
// prolog_full_names): system predicates as function names; class prefixes that concatenate into
// another class's predicate (Pipe::line_count, PipeLine::count); classes alike but for case; a
// class Object whose data method meets the runtime's object_data/2; a field and a method of one
// name; enumerators named true, null or alike but for case; a virtual method whose predicate is
// renumbered and still overridden; multiple inheritance; C++ hiding; operators; 64-bit values,
// booleans, writebacks and callbacks.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace names {

// System predicates as function names: they take a number.
inline int length(const std::string& text) { return static_cast<int>(text.size()) * 10; }
inline int print(int x) { return x; }
inline std::vector<int> sort(std::vector<int> values) { return std::vector<int>(values.rbegin(), values.rend()); }
inline int succ(int x) { return x + 2; }
inline int call(int x) { return x * 3; }     // call/2 is a system predicate
inline int count(const std::vector<int>& values) { return static_cast<int>(values.size()); }   // not one

/// Two classes alike but for case: prefixes point and point_2.
struct point { int x = 1; };
struct Point { int y = 2; };

/// A class named like the runtime: object_data/2 is the runtime's.
struct Object {
    int data() const { return 9; }
    int id = 4;
};

/// pipe_line_count/2 is Pipe's line_count and PipeLine's count: the second takes a number.
struct Pipe {
    int line_count() const { return 1; }
};
struct PipeLine {
    virtual ~PipeLine() = default;
    virtual int count() const { return 2; }
};
inline int pipe_line_total(const PipeLine& line) { return line.count() * 100; }

/// Enumerators named like Prolog's constants and alike but for case.
enum class Mode { on, On, true_, null, off };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// A field and a method alike in snake_case, and methods alike in snake_case.
struct Cursor {
    int position = 0;
    int count = 2;
    int Count() const { return count * 10; }
    std::string getName() const { return "camel"; }
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int move(int by) { position += by; return position; }
    static int create() { return 5; }
};

/// Multiple inheritance.
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

/// Operators and operator().
struct Money {
    long long cents = 0;
    Money() = default;
    explicit Money(long long c) : cents(c) {}
    Money operator+(const Money& other) const { return Money(cents + other.cents); }
    Money operator*(long long n) const { return Money(cents * n); }
    bool operator<(const Money& other) const { return cents < other.cents; }
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
inline void nothing() {}
inline int call_twice(const std::function<int(int)>& f, int x) { return f(f(x)); }
inline void each(const std::vector<int>& values, const std::function<void(int)>& f) { for (const int v : values) f(v); }

namespace data {
inline int twice(int x) { return 2 * x; }
}

} // namespace names
