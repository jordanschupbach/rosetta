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

// The full C# API (--csharp-api full): see src/csharp_full.cpp.
struct CSharpFullOptions {
    std::size_t parts = 1;            // > 1: further translation units in `parts`
    std::string managed_namespace;    // C# namespace (default: the module name in PascalCase)
    std::string library;              // shared library base name (default: the module name)
    std::string functions_class;      // static class of the module functions (default: Functions)
    std::string framework;            // target framework of the .csproj (default: net10.0)
};
struct GeneratedCSharpFullSource {
    std::string source;
    std::string interface;            // the C# source
    std::string project;              // the .csproj
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;   // further C++ translation units
};
GeneratedCSharpFullSource generate_csharp_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                               const BindingPolicy& policy = {}, const CSharpFullOptions& options = {});
} // namespace rosetta
