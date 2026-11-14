#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedFortranSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedFortranSource generate_fortran(const Module& module, const std::string& name,
    bool skip_unsupported = false, const BindingPolicy& policy = {});

// The full API (--fortran-api full): a Fortran 2018 module over the shared C ABI library.
struct FortranFullOptions {
    std::size_t parts = 1;          // C++ library source parts
    std::string fortran_module;     // the Fortran module name (default: the native module name)
};
struct GeneratedFortranFullSource {
    std::string source;             // the C++ library (part 1)
    std::string interface;          // the Fortran source (runtime module, main module, submodule modules)
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts; // C++ library parts 2..N
};
GeneratedFortranFullSource generate_fortran_full(const Module& module, const std::string& name, bool skip_unsupported,
    const BindingPolicy& policy, const FortranFullOptions& options = {});
} // namespace rosetta
