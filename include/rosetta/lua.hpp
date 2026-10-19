#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {

struct GeneratedSource {
    std::string source;
    std::vector<std::string> diagnostics;
};

GeneratedSource generate_lua(const Module& module, const std::string& name,
                             bool skip_unsupported = false, const BindingPolicy& policy = {});

} // namespace rosetta
