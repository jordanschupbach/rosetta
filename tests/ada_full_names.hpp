#pragma once
// Names and values that are hard for Ada (--language ada; tests/ada/test_ada_names.adb, ctest
// ada_full_names): names alike but for case (Ada ignores case) or once in Ada_Case (getName, get_name);
// reserved words; classes named like the runtime's types or Standard's; members named like
// Native_Object_Record's operations; methods named like types; a renamed virtual method an Ada type
// extension overrides; multiple inheritance; C++ hiding; operators, to_string and operator(); 64-bit
// values; in out parameters.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace names {

/// Named like the runtime's base class.
struct NativeObject { int native = 2; };
/// Named like the runtime's error type.
struct NativeError { std::string what = "not an exception"; };
/// Named like a runtime type without its T.
struct RosettaCall { int id = 3; };
/// Named like the policy module's package (which yields).
struct Data { int value = 4; };
/// Named like Standard's types.
struct Integer { int value = 5; };
struct string_view_like { std::string text = "s"; };
/// Named like the runtime's Image primitive (to_string) and a Native_Error_Kind literal.
struct Image { int width = 6; std::string to_string() const { return "image"; } };
struct Callback { int calls = 7; };
inline int make_integer(const Integer& i) { return i.value; }
/// Named like the runtime's package and its prefix.
inline int rosetta(int x) { return x * 3; }
inline int rosetta_dispatch(int x) { return x * 4; }
/// Two classes alike but for case.
struct point { int x = 1; };
struct Point { int y = 2; };
inline int point_x(const point& p) { return p.x; }
inline int point_y(const Point& p) { return p.y; }

// Reserved words take _Kw; names alike but for case take a number.
inline int begin(int x) { return x; }
inline std::string repeat(const std::string& text) { return text + text; }
inline int in(int self, int result) { return self + result; }
inline int length(const std::string& text) { return static_cast<int>(text.size()) * 10; }
inline int copy(int x) { return x + 1; }
inline int total(int x) { return x; }
inline int Total(int x, int y) { return x + y; }   // alike but for case: Total2

/// Cases that are Ada reserved words, named like types, or alike but for case.
enum class Mode { on, On, self, type, end, Result, off, Cursor };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Members named like Native_Object_Record's operations, and names alike but for case.
struct Cursor {
    int position = 0;
    int count = 2;                                      // the field yields to the method
    int Count() const { return count * 10; }
    bool close() { return true; }                       // the generated Close comes first
    std::string cast() const { return "cast"; }
    std::string class_name() const { return "cursor"; }
    bool equals(int x) const { return x == 3; }
    int free() const { return 7; }
    int destroy() const { return 8; }
    std::string getName() const { return "camel"; }
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int result() const { return 9; }
    int label(int self) const { return self * 2; }
    int move(int by) { position += by; return position; }
    static int create() { return 5; }
    std::string image() const { return "image"; }
    int finalize() const { return 11; }
    int is_closed() const { return 12; }
    int data() const { return 13; }
};

/// A virtual method renamed in Ada (Close is Native_Object_Record's): an Ada type extension overrides
/// the renamed method, and C++ calls it.
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

/// Multiple inheritance: Counted is the parent class, Named's members are declared again.
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

/// C++ hiding: Leaf::scale overloads Base's in Ada; Leaf::size returns another type, so it takes
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

/// Operators, to_string (Image) and operator() (Call).
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
inline std::tuple<int, std::string, double> triple() { return {1, "two", 3.5}; }

namespace data {
inline int twice(int x) { return 2 * x; }
}

} // namespace names
