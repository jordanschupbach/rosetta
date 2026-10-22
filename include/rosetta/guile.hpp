#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedGuileSource {
    std::string source;     // C++ extension (libguile)
    std::string interface;  // Scheme module NAME.scm that loads it
    std::vector<std::string> diagnostics;
};
GeneratedGuileSource generate_guile(const Module& module, const std::string& name,
                                    bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
