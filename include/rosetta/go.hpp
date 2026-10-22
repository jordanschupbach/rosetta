#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedGoSource {
    std::string source;     // C++ stubs for the libNAME_stubs.a archive
    std::string interface;  // Go package file NAME.go calling them through cgo
    std::vector<std::string> diagnostics;
};
GeneratedGoSource generate_go(const Module& module, const std::string& name,
                              bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
