#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedPythonSource {
    std::string source;
    std::vector<std::string> diagnostics;
    // With parts > 1: further translation units, compiled and linked with source.
    std::vector<std::string> parts;
};
// parts > 1 spreads the wrappers over that many sources (source holds module init),
// so large libraries compile in parallel.
GeneratedPythonSource generate_python(const Module& module, const std::string& name,
                                     bool skip_unsupported = false, const BindingPolicy& policy = {},
                                     std::size_t parts = 1);
} // namespace rosetta
