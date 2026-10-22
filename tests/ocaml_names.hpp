#pragma once
#include <string>

// C++ names that OCaml spells differently: keywords take a trailing
// underscore, and values and labels start with a lowercase letter.
inline int type(int value = 42) { return value; }
inline int Upper(int value) { return value; }
inline int list(int value = 42) { return value; }
inline int labels(int first = 1, int second = 2) { return 10 * first + second; }
inline int keywords(int begin = 1, int end = 2) { return 10 * begin + end; }
inline int unnamed(int = 5) { return 5; }
inline int required_then_default(int base, int Step = 1) { return base + Step; }
// More than five arguments use a separate bytecode stub.
inline long long seven(int a, int b, int c, int d, int e, int f, long long g) { return a + b + c + d + e + f + g; }
inline int eight(int a, int b, int c, int d, int e, int f, int g, int h = 8) { return a + b + c + d + e + f + g + h; }
namespace outer { namespace inner { inline int depth() { return 2; } } inline int depth() { return 1; } }
class point {
public:
    int x, y;
    char tag = 'p';
    double weight = 1.5;
    explicit point(int x = 0, int y = 0) : x(x), y(y) {}
    int sum(int scale = 1) const { return scale * (x + y); }
    int method(int a, int b, int c, int d, int e, int f = 6) const { return x + a + b + c + d + e + f; }
    static int origin() { return 0; }
};
