#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rosetta {

struct HaskellOptions {
    std::size_t parts = 1;   // C++ library source files
    std::string module;      // Haskell module (default: the module name in PascalCase)
};

struct GeneratedHaskellSource {
    std::string source;                       // the C++ library (first part)
    std::string haskell;                      // the Haskell module (MODULE.hs): the API and its Haddock
    std::string runtime;                      // its runtime module (MODULE/Rosetta.hs: MODULE.Rosetta)
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;           // further C++ parts
};

// The default Haskell module of a Rosetta module (shape_kit, shapeKit -> ShapeKit).
std::string haskell_module_name(const std::string& name);

// Haskell (--language haskell): a GHC module over a C ABI library of the module (one entry point,
// as the other full C ABI backends) that it calls through the FFI; MODULE.Rosetta is its runtime.
GeneratedHaskellSource generate_haskell(const Module& module, const std::string& name, bool skip_unsupported = false,
                                        const BindingPolicy& policy = {}, const HaskellOptions& options = {});

} // namespace rosetta
