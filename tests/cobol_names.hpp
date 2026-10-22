#pragma once
#include <string>

// COBOL names: upper case with hyphens, namespaces dropped, and parameters
// that would clash with a record's own fields (SELF, GIVEN, RESULT) renamed.
inline int type(int value = 42) { return value; }
inline int _private_name() { return 7; }
inline int labels(int first = 1, int second = 2) { return 10 * first + second; }
inline std::string join(std::string a, std::string b = "-", std::string c = "!") { return a + b + c; }
// Defaults of different types can each be omitted.
inline double mixed(int a, double b = 1.5, int c = 2) { return a + b + c; }
inline char shout(char letter) { return letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter; }
inline unsigned long long length(const std::string& text) { return text.size(); }
inline int keywords(int given, int result, int display) { return given + result + display; }
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
