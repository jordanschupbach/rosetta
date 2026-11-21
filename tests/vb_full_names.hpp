#pragma once
// Names that are hard for Visual Basic (--language visual-basic; tests/TestVBNames.vb, ctest
// vb_full_names): classes named like the .NET types generated code and the runtime use, VB keywords,
// names that differ only in case (VB is case-insensitive), and members a base class also declares.
#include <complex>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace shadow {

/// A complex number that is not System.Numerics.Complex.
struct Complex {
    double re = 3;
    double im = 4;
    double norm() const { return re * re + im * im; }
};
/// Not System.Collections.Generic.List.
struct List {
    int size() const { return 3; }
};
/// Not System.Func.
struct Func {
    int calls = 0;
};
/// Not System.Action.
struct Action {};
/// Not System.Collections.Generic.Dictionary.
struct Dictionary {};
/// Not System.Collections.Generic.HashSet.
struct HashSet {};
/// Not System.Exception.
struct Exception {};
/// Not System.Array.
struct Array {};
/// Not System.Type.
struct Type {};
/// Not System.GC.
struct GC {};
/// Not System.IO.Path (the runtime looks for the library with it).
struct Path {};
/// A class named like a VB keyword.
struct Error {
    int code = 7;
};

inline std::complex<double> twice(std::complex<double> z) { return 2.0 * z; }
inline std::vector<double> range(int n) {
    std::vector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(i);
    return out;
}
inline std::map<std::string, int> counts(const std::vector<std::string>& words) {
    std::map<std::string, int> out;
    for (const auto& word : words) ++out[word];
    return out;
}
inline std::set<int> unique(const std::vector<int>& values) { return {values.begin(), values.end()}; }
inline double apply(const std::function<double(double)>& f, double x) { return f(x); }
inline void each(const std::vector<int>& values, const std::function<void(int)>& f) {
    for (int value : values) f(value);
}
inline std::pair<int, double> pair() { return {1, 2.5}; }
inline double norm_of(const Complex& c) { return c.norm(); }
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    return x;
}
inline int code_of(const Error& error) { return error.code; }

/// Enumerators that differ only in case, and a keyword enumerator.
enum class Mode { on, On, Next };
inline int mode_value(Mode mode) { return static_cast<int>(mode); }

/// VB keywords as member and parameter names, a field named like a method, and an indexer next to
/// a member named item.
struct Cursor {
    int position = 0;
    int count = 2;                                   // a field and a method named alike
    int Count() const { return count * 10; }
    int next() { return ++position; }                // [Next]
    int step(int to, int end) { position += to * end; return position; }  // [Step]([to], [end])
    bool stop = false;                               // a property [Stop]
    int item = 5;                                    // Item: the indexer is called Element
    int operator[](int index) const { return position + index; }
    int size(int size) const { return size * 2; }    // a parameter named like its function
    int lookup(int rosetta) const { return rosetta + 1; }  // a parameter named like the runtime class
    int value = 1;                                   // a property Value (its setter has a parameter)
    std::string to_string() const { return "cursor@" + std::to_string(position); }
    bool equals(const Cursor& other) const { return position == other.position; }
};

/// Members a derived class also declares.
class Base {
public:
    virtual ~Base() = default;
    virtual std::string describe() const { return "base"; }
    std::string name() const { return "base"; }
    int scale(int factor) const { return factor; }
    int weight() const { return 1; }
};
class Derived : public Base {
public:
    std::string describe() const override { return "derived"; }  // Overrides
    std::string name() const { return "derived"; }               // Overloads (hides by signature)
    double scale(double factor) const { return factor * 2; }      // Overloads (a new signature)
    int weight = 2;                                              // Shadows (a property hides a method)
};

} // namespace shadow
