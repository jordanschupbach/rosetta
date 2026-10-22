#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedOcamlSource {
    std::string source;     // C++ stubs for the NAME_stubs library
    std::string interface;  // OCaml module NAME.ml declaring the externals
    std::vector<std::string> diagnostics;
};
GeneratedOcamlSource generate_ocaml(const Module& module, const std::string& name,
                                    bool skip_unsupported = false, const BindingPolicy& policy = {});
} // namespace rosetta
