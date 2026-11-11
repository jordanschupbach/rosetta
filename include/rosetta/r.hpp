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
    // R6 with parts > 1: further translation units, compiled and linked with source.
    std::vector<std::string> parts;
};

// R6 bindings may target an R package (package = true): the interface is then package code
// whose .onLoad calls .rosetta_onload(), and native routines come from useDynLib(registration).
// Otherwise the interface is a script that runs after dyn.load() of the library.
GeneratedRSource generate_r(const Module& module, const std::string& name,
                            RObjectSystem object_system = RObjectSystem::r6,
                            bool skip_unsupported = false, const BindingPolicy& policy = {},
                            bool package = false, std::size_t parts = 1);

namespace detail {
GeneratedRSource generate_r6(const Module& module, const std::string& name, bool skip_unsupported,
                             const BindingPolicy& policy, bool package, std::size_t parts);
}

} // namespace rosetta
