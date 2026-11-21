#pragma once
// Names that are hard for PHP (--language php; tests/test_php_names.php, ctest php_full_names):
// reserved words as class, function, method, enum case and parameter names; names PHP compares
// case-insensitively; classes named like the runtime's; members named like NativeObject's or PHP's
// magic methods; a derived class hiding a method with an incompatible signature; multiple
// inheritance.
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace names {

/// A reserved word as a class name.
struct list {
    int size() const { return 3; }
};
/// A type name PHP reserves.
struct object {
    int id = 1;
};
/// Named like the runtime's NativeObject.
struct NativeObject {
    int native = 2;
};
/// Named like the runtime's Complex.
struct Complex {
    double re = 1, im = 2;
};
/// Named like a PHP class outside the namespace (fine: namespaced).
struct Closure {
    int run() const { return 4; }
};
/// Two classes PHP sees as one (class names are case-insensitive).
struct point { int x = 1; };
struct Point { int y = 2; };
inline int point_y(const Point& p) { return p.y; }
inline int point_x(const point& p) { return p.x; }

// Reserved words and builtins as function names.
inline int print(int x) { return x; }
inline int echo(int x) { return x + 1; }
inline int count(const std::vector<int>& values) { return static_cast<int>(values.size()); }   // \count is a builtin
inline std::string strlen(const std::string& text) { return text + "!"; }

/// Reserved words as enum cases, and cases that differ only in case.
enum class Mode { on, On, Class, list, fn };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Reserved words as methods and parameters, members named like generated or magic ones, and
/// names PHP compares case-insensitively.
struct Cursor {
    int position = 0;
    int count = 2;                                             // a property and a method named alike
    int Count() const { return count * 10; }                   // count
    int list() const { return 1; }                             // a reserved word as a method is fine
    int match(int fn, int function) const { return fn * function; }
    int move(int self) { position += self; return position; }
    bool close() { return true; }                              // the generated close comes first
    std::string cast() const { return "cast"; }                // so does cast
    int offsetGet(int i) const { return i; }                   // ArrayAccess names
    std::string getName() const { return "camel"; }            // getName
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    std::string GETNAME() const { return "upper"; }            // the same name in PHP
    int value(int rosettaResult) const { return rosettaResult + 1; }  // a parameter named like a generated local
    int self(int self) const { return self * 2; }
    static int create() { return 5; }
};

/// Multiple inheritance: the second base is reached through asNamed().
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

/// Hiding: Leaf::name keeps the base's signature (an override in PHP); Leaf::scale needs another
/// argument and Leaf::size returns another type, which PHP would reject as overrides (renamed).
class Base {
public:
    virtual ~Base() = default;
    virtual std::string describe() const { return "base"; }
    std::string name() const { return "base"; }
    double scale(double factor) const { return factor; }
    int size() const { return 1; }
};
class Middle : public Base {
public:
    std::string describe() const override { return "middle"; }
};
class Leaf : public Middle {
public:
    std::string describe() const override { return "leaf"; }
    std::string name() const { return "leaf"; }
    double scale(double factor, double extra) const { return factor * extra; }
    std::string size() const { return "big"; }
};
inline std::string describe_base(const Base& base) { return base.describe(); }

} // namespace names
