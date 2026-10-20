#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedJavaSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedJavaSource generate_java(const Module& module, const std::string& name,
                                 bool skip_unsupported = false, const BindingPolicy& policy = {},
                                 const std::string& package = {});
} // namespace rosetta
