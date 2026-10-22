#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedTclSource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedTclSource generate_tcl(const Module& module, const std::string& name,
                                bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
