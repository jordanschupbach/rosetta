#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedAplSource {
    // C++ stubs with a C ABI, for Dyalog's ⎕NA. Compiled with ROSETTA_GNU_APL
    // (and GNU APL's configured source tree), it also holds the GNU APL native function.
    std::string source;
    std::string gnu;     // GNU APL script NAME.apl, loaded with )COPY
    std::string dyalog;  // Dyalog namespace script NAME.apln, loaded with ⎕FIX
    std::vector<std::string> diagnostics;
};
// LIBRARY_DIR is where the scripts load libNAME_gnu_apl.so and libNAME_apl.so from.
GeneratedAplSource generate_apl(const Module& module, const std::string& name, bool skip_unsupported = false,
                                const BindingPolicy& policy = {}, const std::string& library_dir = ".");
} // namespace rosetta
