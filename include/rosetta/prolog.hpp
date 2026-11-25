#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rosetta {

struct PrologOptions {
    std::size_t parts = 1;   // C++ library source files
    std::string module;      // Prolog module (default: the module name in snake_case)
    std::string library;     // native library base name (default: the module name)
};

struct GeneratedPrologSource {
    std::string source;                       // the C++ library with its SWI-Prolog foreign glue (first part)
    std::string prolog;                       // the Prolog module (.pl): the API, its runtime and PlDoc
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;           // further C++ parts
};

// The default Prolog module of a Rosetta module (shape_kit, ShapeKit -> shape_kit).
std::string prolog_module_name(const std::string& name);

// Prolog (--language prolog): a SWI-Prolog 9 module over a C ABI library of the module (one entry
// point, as the other full C ABI backends) that carries the foreign glue the module loads with
// load_foreign_library/2; the library builds against SWI-Prolog's headers.
GeneratedPrologSource generate_prolog(const Module& module, const std::string& name, bool skip_unsupported = false,
                                      const BindingPolicy& policy = {}, const PrologOptions& options = {});

} // namespace rosetta
