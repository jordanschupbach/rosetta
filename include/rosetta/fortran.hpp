#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedFortranSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedFortranSource generate_fortran(const Module& module, const std::string& name,
    bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
