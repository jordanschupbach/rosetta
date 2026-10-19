#pragma once
#include <stdexcept>
#include <string>

namespace r_demo {
inline int add(int a, int b) { return a + b; }
inline bool negate(bool value) { return !value; }
inline double half(double value) { return value / 2; }
inline std::string greet(const std::string& name) { return "Hello, " + name + "!"; }
inline unsigned long long too_large() { return 9007199254740993ULL; }
inline void fail() { throw std::runtime_error("native failure"); }
class Counter {
    static int& live_storage() { static int count = 0; return count; }
public:
    int count;
    const int initial;
    std::string label = "counter";
    explicit Counter(int value) : count(value), initial(value) {
        if (value < 0) throw std::runtime_error("negative initial value");
        ++live_storage();
    }
    ~Counter() noexcept { --live_storage(); }
    int add(int amount) { count += amount; return count; }
    int value() const { return count; }
    static int answer() { return 42; }
    static int live() { return live_storage(); }
};
inline int live() { return Counter::live(); }
} // namespace r_demo
