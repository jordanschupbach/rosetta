#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedCSharpSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedCSharpSource generate_csharp(const Module& module, const std::string& name,
    bool skip_unsupported = false, const BindingPolicy& policy = {}, const std::string& managed_namespace = {});
} // namespace rosetta
