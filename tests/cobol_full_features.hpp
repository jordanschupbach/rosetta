#pragma once
// The full COBOL API test fixture: examples/python_features.hpp plus COBOL-only cases.
#include "python_features.hpp"

#include <string>
#include <utility>

namespace features {

/// Swaps the two ends of a segment in place (an in/out pair).
inline void swap_ends(std::pair<int, int>& ends) { std::swap(ends.first, ends.second); }
/// Appends a suffix to a label in place (an in/out string).
inline void suffix(std::string& label, const std::string& tail = "!") { label += tail; }

} // namespace features
