#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedOctaveSource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedOctaveSource generate_octave(const Module& module, const std::string& name,
                                     bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
