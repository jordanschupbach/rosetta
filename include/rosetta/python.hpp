#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedPythonSource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedPythonSource generate_python(const Module& module, const std::string& name,
                                     bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
