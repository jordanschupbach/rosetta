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

// --apl-api full: the C++ library every full C ABI backend shares (cabi_full.hpp) plus a GNU APL
// native function, built from source (and parts) as libMODULE_gnu_apl.so against GNU APL's
// configured headers, and a GNU APL script defining PREFIX∆... functions with ⍝ documentation.
struct AplFullOptions {
    std::size_t parts = 1;
    std::string prefix;          // name prefix of the APL functions (default: the module name)
    std::string library_dir = "."; // where the script loads libMODULE_gnu_apl.so from
};
struct GeneratedAplFullSource {
    std::string source;
    std::vector<std::string> parts;
    std::string script;          // MODULE.apl
    std::vector<std::string> diagnostics;
};
GeneratedAplFullSource generate_apl_full(const Module& module, const std::string& name, bool skip_unsupported,
                                         const BindingPolicy& policy, const AplFullOptions& options);
} // namespace rosetta
