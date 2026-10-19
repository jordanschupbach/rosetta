#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {

// R object systems share a native bridge but require different R interfaces.
enum class RObjectSystem { r6, s3, s4 };

struct GeneratedRSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};

GeneratedRSource generate_r(const Module& module, const std::string& name,
                            RObjectSystem object_system = RObjectSystem::r6,
                            bool skip_unsupported = false, const BindingPolicy& policy = {});

} // namespace rosetta
