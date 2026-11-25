#pragma once
// Names and values that are hard for Dart (--language dart; tests/test_dart_names.dart, ctest
// dart_full_names): keywords; classes named like dart:core's or the runtime's types; functions named
// like dart:core's; members named like Object's or NativeObject's, or like core types; names that
// meet in camelCase; overloads merged into one function (and their results); a method merged with
// the base method of the same name, and one with another result; a renamed virtual method a Dart
// subclass overrides; multiple inheritance; operators, to_string and operator(); 64-bit values;
// Ref parameters; records.
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace names {

/// Named like dart:core types.
struct String { int n = 1; };
struct List { int n = 2; };
struct Object { int n = 3; };
struct Duration { int n = 4; };
/// Named like runtime types.
struct NativeObject { int native = 5; };
struct Ref { int ref = 6; };
struct Complex { double re = 7; };
/// Named like the policy module's class (which yields).
struct Data { int value = 8; };
inline int string_n(const String& s) { return s.n; }

// Keywords take an underscore; dart:core functions too; names meeting in camelCase a number.
inline int is(int x) { return x; }
inline std::string switch_(const std::string& text) { return text + text; }
inline int print(int x) { return x * 10; }
inline int identical(int a, int b) { return a == b ? 1 : 0; }
inline int new_value(int in, int var) { return in + var; }
inline int total_count(int x) { return x; }
inline int totalCount(int x, int y) { return x + y; }   // meets total_count: totalCount2

/// Enumerators that are keywords, or the extension type's members.
enum class Mode { in, value, name, values, Default, is, off };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Members named like Object's and NativeObject's, and like core types.
struct Cursor {
    int position = 0;
    int count = 2;                                      // the field meets the method: count2
    int Count() const { return count * 10; }
    bool close() { return true; }                       // the generated close comes first
    std::string cast() const { return "cast"; }
    int hash_code() const { return 11; }
    std::string runtime_type() const { return "rt"; }
    int is_closed() const { return 12; }
    std::string getName() const { return "camel"; }
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int Int() const { return 13; }
    double Double() const { return 1.5; }
    int get() const { return 14; }
    void set(int value) { position = value; }
    int move(int by) { position += by; return position; }
    static int create() { return 5; }
};

/// Overloads merged into one Dart function: positions where they disagree are Object?, and so is a
/// result they disagree on; a common base class result stays typed.
struct Base {
    virtual ~Base() = default;
    virtual std::string describe() const { return "base"; }
    double scale(double factor) const { return factor; }
    int size() const { return 1; }
};
struct Leaf : Base {
    std::string describe() const override { return "leaf"; }
    double scale(double factor, double extra) const { return factor * extra; }   // merged with Base::scale
    std::string size() const { return "big"; }                                  // another result: size2
};
inline std::string describe_base(const Base& base) { return base.describe(); }
inline std::string kind_of(bool) { return "bool"; }
inline std::string kind_of(long long) { return "int"; }
inline std::string kind_of(const std::string&) { return "string"; }
inline std::string kind_of(long long, long long) { return "two"; }
inline int twice(int x) { return 2 * x; }
inline std::string twice(const std::string& x) { return x + x; }
inline Base* make(bool leaf) { return leaf ? new Leaf : new Base; }
inline Leaf* make(int) { return new Leaf; }   // a Base and a Leaf: the result is Base

/// A virtual method renamed in Dart (close is NativeObject's): a Dart subclass overrides it, and
/// C++ calls it.
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

/// Operators, to_string (toString) and operator() (call).
struct Money {
    long long cents = 0;
    Money() = default;
    explicit Money(long long c) : cents(c) {}
    Money operator+(const Money& other) const { return Money(cents + other.cents); }
    Money operator*(long long n) const { return Money(cents * n); }
    Money operator*(const Money& other) const { return Money(cents * other.cents); }
    bool operator<(const Money& other) const { return cents < other.cents; }
    bool operator!() const { return cents == 0; }
    bool operator==(const Money& other) const { return cents == other.cents; }
    Money operator-() const { return Money(-cents); }
    std::string to_string() const { return std::to_string(cents / 100) + "." + (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100); }
    long long operator()(long long extra) const { return cents + extra; }
};
/// A to_string whose parameters all have defaults is still the string conversion (its zero-argument call).
struct Padded {
    int value = 7;
    Padded() = default;
    explicit Padded(int v) : value(v) {}
    std::string to_string(int width = 3) const {
        auto text = std::to_string(value);
        return text.size() < static_cast<std::size_t>(width) ? std::string(width - text.size(), '0') + text : text;
    }
};


// Values.
inline std::uint64_t biggest() { return UINT64_MAX; }
inline std::uint64_t echo_u64(std::uint64_t value) { return value; }
inline long long echo_i64(long long value) { return value; }
inline std::uint8_t echo_u8(std::uint8_t value) { return value; }
inline void bump(int& value, std::string& text) { ++value; text += "!"; }
inline int call_twice(const std::function<int(int)>& f, int x) { return f(f(x)); }
/// An optional callback: Dart's nullable function type `int Function(int)?` (no parentheses).
inline int call_maybe(int x, std::function<int(int)> f = {}) { return f ? f(x) : x; }
inline std::vector<std::pair<std::string, int>> ranked(const std::vector<std::string>& names) {
    std::vector<std::pair<std::string, int>> out;
    for (std::size_t i = 0; i < names.size(); ++i) out.emplace_back(names[i], static_cast<int>(i + 1));
    return out;
}
inline std::tuple<int, std::string, double> triple() { return {1, "two", 3.5}; }

namespace data {
inline int twice(int x) { return 2 * x; }
}

} // namespace names
