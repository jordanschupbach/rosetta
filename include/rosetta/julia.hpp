#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedJuliaSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedJuliaSource generate_julia(const Module& module, const std::string& name,
                                   bool skip_unsupported = false, const BindingPolicy& policy = {});

// --julia-api full: abstract types for classes, generic functions with C++ overload resolution,
// @subclass directors, properties, Base operators, Markdown docstrings from Doxygen.
struct JuliaFullOptions {
    std::size_t parts = 1;   // > 1: further translation units in `parts`
    std::string package;     // Julia module name (default: the module name, capitalized)
    std::string library;     // shared library base name (default: the module name)
};
struct GeneratedJuliaFullSource {
    std::string source;
    std::string interface;   // the Julia module (src/<Package>.jl)
    std::string project;     // Project.toml
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;
};
GeneratedJuliaFullSource generate_julia_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                             const BindingPolicy& policy = {}, const JuliaFullOptions& options = {});
} // namespace rosetta
