#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <vector>

namespace rosetta {
// The Pascal backend (--language pascal): see src/pascal_full.cpp. A Free Pascal unit over a shared
// C++ library with one C ABI entry point (the full C ABI backends' library, cabi_full.hpp).
struct PascalOptions {
    std::size_t parts = 1;   // > 1: further C++ translation units in `parts`
    std::string unit;        // Pascal unit name (default: the module name)
    std::string library;     // shared library the unit links (external 'NAME'; default: the module name)
};
struct GeneratedPascalSource {
    std::string source;      // C++ source of the shared library
    std::string pascal;      // the Pascal unit
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;   // further C++ translation units
};
GeneratedPascalSource generate_pascal(const Module& module, const std::string& name, bool skip_unsupported = false,
                                      const BindingPolicy& policy = {}, const PascalOptions& options = {});
} // namespace rosetta
