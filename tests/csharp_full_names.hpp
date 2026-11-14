#pragma once
// Classes named like the .NET types that generated C# code and the C# runtime use (--csharp-api
// full): they share the generated namespace, so the generated code must still reach the .NET ones
// (tests/TestCSharpNames.cs, ctest csharp_full_names).
#include <complex>
#include <functional>
#include <map>
#include <set>
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
/// Samples converted to and from std::vector<double> by the policy ("convert").
struct Samples {
    std::vector<double> values;
    Samples() = default;
    Samples(std::vector<double> v) : values(std::move(v)) {}
    operator std::vector<double>() const { return values; }
};
inline double total(const Samples& samples) {
    double sum = 0;
    for (double value : samples.values) sum += value;
    return sum;
}
inline Samples doubled(const Samples& samples) {
    Samples out = samples;
    for (double& value : out.values) value *= 2;
    return out;
}
inline int fail(int x) {
    if (x < 0) throw std::invalid_argument("negative");
    return x;
}

} // namespace shadow
