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

// --ocaml-api full: the C++ library with its OCaml stubs (source, split into `parts` sources when
// more than one), the OCaml module NAME.ml, its interface NAME.mli (with odoc documentation) and a
// findlib META file.
struct OcamlFullOptions {
    std::size_t parts = 1;
    std::string version;
};
struct GeneratedOcamlFullSource {
    std::string source;
    std::vector<std::string> parts;  // further C++ sources (parts > 1)
    std::string ml;
    std::string mli;
    std::string meta;
    std::vector<std::string> diagnostics;
};
GeneratedOcamlFullSource generate_ocaml_full(const Module& module, const std::string& name, bool skip_unsupported,
                                             const BindingPolicy& policy, const OcamlFullOptions& options);
} // namespace rosetta
