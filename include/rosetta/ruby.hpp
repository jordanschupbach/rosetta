#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedRubySource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedRubySource generate_ruby(const Module& module, const std::string& name,
                                 bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
