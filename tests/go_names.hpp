#pragma once
#include <string>

// C++ names that Go spells differently: exported names are CamelCase, and
// parameters that would shadow Go keywords or builtins are renamed.
inline int type(int value = 42) { return value; }
inline int snake_case_name() { return 1; }
inline int labels(int first = 1, int second = 2) { return 10 * first + second; }
inline std::string join(std::string a, std::string b = "-", std::string c = "!") { return a + b + c; }
// Defaults of different Go types cannot share a variadic parameter, so all are required.
inline double mixed(int a, double b = 1.5, int c = 2) { return a + b + c; }
inline int keywords(int func, int range) { return func + range; }
inline int shadow(int string, int len, int p0) { return string + len + p0; }
inline long long seven(int a, int b, int c, int d, int e, int f, long long g) { return a + b + c + d + e + f + g; }
namespace outer { inline int depth() { return 1; } }
class point {
public:
    int x, y;
    char tag = 'p';
    double weight = 1.5;
    explicit point(int x = 0, int y = 0) : x(x), y(y) {}
    int sum(int scale = 1) const { return scale * (x + y); }
    static int origin() { return 0; }
};
