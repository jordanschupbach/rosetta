#pragma once
// Names that are hard for Rust (--language rust; tests/test_rust_names.rs, ctest rust_full_names):
// classes named like the prelude, std and runtime types generated code uses, Rust keywords as
// method, parameter and enumerator names, members named like generated ones (close, new, clone,
// as_base), names that only snake_case makes collide, and multiple inheritance.
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace names {

/// Not std::string::String.
struct String {
    std::string text = "mine";
    std::string get() const { return text; }
};
/// Not std::vec::Vec.
struct Vec {
    int size() const { return 3; }
};
/// Not std::option::Option.
struct Option {
    bool set = true;
};
/// Not the crate's Result alias.
struct Result {
    int code = 0;
};
/// Not the crate's Error.
struct Error {
    int code = 7;
};
/// Not std::collections::HashMap.
struct HashMap {
    int buckets = 16;
};
/// Not std::sync::Arc.
struct Arc {};
/// Not the runtime's Value.
struct Value {
    double amount = 2.5;
};
/// Not Box.
struct Box {
    double width = 2;
    double volume() const { return width * width * width; }
};

inline std::string text_of(const String& s) { return s.text; }
inline std::vector<std::string> words() { return {"a", "b"}; }
inline std::map<std::string, int> counts(const std::vector<std::string>& words) {
    std::map<std::string, int> out;
    for (const auto& word : words) ++out[word];
    return out;
}
inline int code_of(const Error& error) { return error.code; }
inline double volume_of(const Box& box) { return box.volume(); }
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    return x;
}

/// Keyword enumerators, and enumerators that only differ in case.
enum class Mode { on, On, type, match, Self };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// Rust keywords as member and parameter names, members named like generated ones, and names
/// that collide once in snake_case.
struct Cursor {
    int position = 0;
    int count = 2;                                          // a field and a method named alike
    int Count() const { return count * 10; }                // count -> count_field
    int type() const { return 1; }                          // type_
    int match(int fn, int ref) const { return fn * ref; }   // match_(fn_, ref_)
    int yield(int a, int b = 4) const { return a * b; }     // yield_, yield_with_b (not yield__with_b)
    int loop(int n) const { return n; }                     // loop_
    int loop(double x, int n) const { return int(x) + n; }  // loop_x_n (not loop__x_n)
    int move(int self) { position += self; return position; }  // move_(self_)
    bool close() { return true; }                           // the generated close comes first
    int clone() const { return 42; }                        // a C++ clone next to Clone
    static int create() { return 5; }
    std::string getName() const { return "camel"; }         // get_name
    std::string get_name(int times) const { std::string s; for (int i = 0; i < times; ++i) s += "snake"; return s; }
    int value(int rc) const { return rc + 1; }              // a parameter named like a generated local
};

/// new as a static method, besides the constructors.
struct Factory {
    int made = 0;
    Factory() = default;
    explicit Factory(int made) : made(made) {}
    static Factory make(int made) { return Factory(made); }
};

/// Multiple inheritance: the second base is reached through as_named.
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

/// Three levels deep: as_base reaches every ancestor.
class Base {
public:
    virtual ~Base() = default;
    virtual std::string describe() const { return "base"; }
    std::string name() const { return "base"; }
};
class Middle : public Base {
public:
    std::string describe() const override { return "middle"; }
};
class Leaf : public Middle {
public:
    std::string describe() const override { return "leaf"; }
    std::string name() const { return "leaf"; }   // hides Base::name
};
inline std::string describe_base(const Base& base) { return base.describe(); }

} // namespace names
