#pragma once
#include <tuple>
#include <string>
#include <variant>
#include <set>

// A small independent input verifies that advanced runtime additions do not
// make ordinary generated modules depend on callback/class policies or threads.
namespace baseline {
inline std::set<int> sorted(std::set<int> value) { return value; }
inline int add(int left, int right) { return left + right; }
inline bool invert(bool value) { return !value; }
inline const char* identity(const char* value) { return value; }
inline int defaults(int value = 42) { return value; }
inline std::tuple<int, std::string> product(std::tuple<int, std::string> value) { return value; }
inline std::variant<std::monostate, int> sum(std::variant<std::monostate, int> value) { return value; }
}
namespace selected_baseline = baseline;
namespace baseline_import {
using selected_baseline::add;
inline int invoke(int left, int right) { return add(left, right); }
}
