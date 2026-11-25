#pragma once
// Names and values that are hard for Objective-C (--language objective-c; tests/test_objc_names.m,
// ctest objc_full_names): classes named like the runtime's types or the module; enumerator
// constants named like classes; C and Objective-C keywords; NSObject's selectors and the ARC method
// families as names; a method taking a property's setter; a renamed virtual method an Objective-C
// subclass overrides; multiple inheritance; C++ hiding; operators, to_string and operator(); 64-bit
// values, enums at the limits, characters, optionals, booleans, pointer parameters and blocks.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace names {

/// Named like the runtime's types (with the prefix).
struct Object { int native = 2; };
struct Error { std::string what = "not an exception"; };
struct Complex { double re = 1.5; };
struct Function { int arity = 0; };
struct RosettaCall { int id = 3; };
/// Named like the module (the module class is Names).
struct Names { int self = 6; };
/// Named like the policy module's class.
struct Data { int value = 4; };
/// Two classes alike but for case.
struct point { int x = 1; };
struct Point { int y = 2; };
inline int point_x(const point& p) { return p.x; }
inline int point_y(const Point& p) { return p.y; }
/// A class named like an enumerator constant (NamesShapeCircle).
struct ShapeCircle { int r = 8; };
enum class Shape { Circle, Square };
inline int shape_value(Shape shape) { return static_cast<int>(shape); }

// NSObject's selectors take an underscore; keywords as parameter names too; ARC method families
// keep their names.
inline int description(int x) { return x; }
inline int alloc(int x) { return x + 1; }
inline int in(int self, int id) { return self + id; }
inline int new_value() { return 11; }
inline std::string copy_text(const std::string& text) { return text + text; }
inline int count(const std::vector<int>& values) { return static_cast<int>(values.size()); }   // a fine name

/// Cases alike but for case, and named like keywords.
enum class Mode { on, On, YES, Default, self, off };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Enumerators at the limits of 64 bits.
enum class Big : long long { low = INT64_MIN, high = INT64_MAX };
enum class Wide : unsigned long long { top = UINT64_MAX };
inline long long big_value(Big big) { return static_cast<long long>(big); }
inline bool is_top(Wide wide) { return wide == Wide::top; }

/// Members named like NSObject's or the runtime's, ARC method families, and a setter's name.
struct Cursor {
    int position = 0;
    int count = 2;                                      // the field yields to the method
    int id = 9;
    int new_count = 4;                                  // a getter in the new family
    int Count() const { return count * 10; }
    bool close() { return true; }                       // the generated close comes first
    std::string cast() const { return "cast"; }
    std::string description() const { return "described"; }
    std::size_t hash() const { return 42; }
    bool isClosed() const { return false; }
    int init() const { return 7; }
    int copy() const { return 8; }
    int initialize(int self) const { return self; }
    Cursor* self() { return this; }
    int new_object() const { return 12; }               // the new family
    int init_with(int value) const { return value; }    // the init family
    std::string getName() const { return "camel"; }
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int print(int self) const { return self * 2; }      // a fine method name
    int move(int by) { position += by; return position; }
    Cursor& set_position(int value) { position = value; return *this; }   // the property keeps its name
    static int create() { return 5; }
};

/// A virtual method renamed in Objective-C (close is the runtime's): an Objective-C subclass
/// overrides the renamed method, and C++ calls it.
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
inline std::string who(const Named* named) { return named ? named->label() : "nobody"; }

/// C++ hiding: Leaf::scale adds a selector to Base's; Leaf::size returns another type, so it takes
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

/// Operators, to_string (the description) and operator() (call:).
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


/// A std::function field: a block property.
struct Hook {
    std::function<int(int)> fn = [](int x) { return x + 100; };
    int fire(int x) const { return fn ? fn(x) : -1; }
};

// Values.
inline std::uint64_t biggest() { return UINT64_MAX; }
inline std::uint64_t echo_u64(std::uint64_t value) { return value; }
inline long long echo_i64(long long value) { return value; }
inline std::int8_t echo_i8(std::int8_t value) { return value; }
inline bool negate(bool value) { return !value; }
inline char next_char(char c) { return static_cast<char>(c + 1); }
inline std::string kind_of(bool) { return "bool"; }
inline std::string kind_of(long long) { return "int"; }
inline std::string kind_of(const std::string&) { return "string"; }
inline void bump(int& value, std::string& text) { ++value; text += "!"; }
inline int maybe(std::optional<int> value) { return value ? *value * 2 : -1; }
inline int call_twice(const std::function<int(int)>& f, int x) { return f(f(x)); }
inline void each(const std::vector<std::string>& items, const std::function<void(const std::string&)>& f) { for (const auto& item : items) f(item); }
inline std::vector<std::pair<std::string, int>> ranked(const std::vector<std::string>& names) {
    std::vector<std::pair<std::string, int>> out;
    for (std::size_t i = 0; i < names.size(); ++i) out.emplace_back(names[i], static_cast<int>(i + 1));
    return out;
}

namespace data {
inline int twice(int x) { return 2 * x; }
}

} // namespace names
