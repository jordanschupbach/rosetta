#pragma once
// Names that collide with Java (--java-api full, tests/TestJavaNames.java, ctest java_full_names):
// Java keywords and contextual names (as methods, parameters, fields and enumerators; C++ keywords
// such as class or new come in through the policy's renames), java.lang.Object's methods, and
// classes named like the java.lang and java.util types and the binding's own helper types
// (Complex, Pair, Ref), which share the generated package.
#include <complex>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace jnames {

/// Not java.lang.String.
struct String {
    std::string text = "mine";
    std::size_t length() const { return text.size() + 100; }
};
/// Not java.lang.Integer.
struct Integer { int value = 5; };
/// Not java.lang.Math.
struct Math { static int twice(int x) { return 2 * x; } };
/// Not java.lang.System.
struct System { int exit_code = 3; };
/// Not java.lang.Thread.
struct Thread { int id() const { return 7; } };
/// Not java.lang.Object.
struct Object { int marker = 9; };
/// Not java.lang.Override.
struct Override {};
/// Not java.util.List.
struct List { int size() const { return 3; } };
/// Not java.util.Map.
struct Map {};
/// Not java.util.Optional.
struct Optional {};
/// Not java.util.function.Function.
struct Function {};
/// Not the binding's std::complex record.
struct Complex { double re = 3, im = 4; double norm() const { return re * re + im * im; } };
/// Not the binding's std::pair record.
struct Pair {};
/// Not the binding's mutable reference holder.
struct Ref {};

/// Enumerators named like Java keywords and contextual names.
enum class Word { null, var, record, package, yield };

/// Methods named like Java keywords and java.lang.Object's methods.
class Keywords {
public:
    int klass() const { return 1; }        // renamed to class by the policy
    int fresh() const { return 2; }        // renamed to new
    int fallback() const { return 3; }     // renamed to default
    int integer() const { return 4; }      // renamed to int
    int package() const { return 5; }
    int synchronized() const { return 6; }
    int var() const { return 7; }
    int record() const { return 8; }
    int instanceof(int interface, int extends) const { return interface * 10 + extends; }
    int yield() const { return 9; }
    std::string get_class() const { return "C++ class"; }
    int wait() const { return 10; }
    int notify() const { return 11; }
    std::size_t hash_code() const { return 12; }
    bool equals(const Keywords&) const { return true; }
    std::string to_string() const { return "Keywords!"; }
    Keywords clone() const { return *this; }
    int finalize() const { return 13; }
    int close() const { return 14; }
    int klass_field = 15;                  // renamed to class: getClass is final, so getClassField
    Word word = Word::record;
};

inline std::string greet(const String& who) { return "hello " + who.text; }
inline std::vector<std::string> words() { return {"a", "b"}; }
inline std::map<std::string, int> counts(const std::vector<std::string>& items) {
    std::map<std::string, int> out;
    for (const auto& item : items) ++out[item];
    return out;
}
inline std::optional<double> maybe(bool some) { return some ? std::optional<double>(2.5) : std::nullopt; }
inline std::complex<double> twice(std::complex<double> z) { return 2.0 * z; }
inline std::pair<int, std::string> pair() { return {1, "one"}; }
inline double apply(const std::function<double(double)>& f, double x) { return f(x); }
inline double fold3(const std::function<double(double, double, double)>& f) { return f(1, 2, 3); }
inline void bump(int& x) { ++x; }
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    return x;
}
inline std::string to_string() { return "module"; }
inline int wait() { return 20; }
inline Word word_of(int i) { return static_cast<Word>(i); }
inline double norm_of(const Complex& c) { return c.norm(); }
/// Writes to C++ std::cout (ordered with System.out when output is piped).
inline void say(const std::string& text) { std::cout << text; }
/// Writes through C stdio, then calls back into Java (which prints too).
inline void say_around(const std::function<void()>& f) { std::printf("before\n"); f(); std::printf("after\n"); }

} // namespace jnames
