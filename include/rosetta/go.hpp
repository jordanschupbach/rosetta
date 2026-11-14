#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rosetta {
struct GeneratedGoSource {
    std::string source;     // C++ stubs for the libNAME_stubs.a archive
    std::string interface;  // Go package file NAME.go calling them through cgo
    std::vector<std::string> diagnostics;
};
GeneratedGoSource generate_go(const Module& module, const std::string& name,
                              bool skip_unsupported = false, const BindingPolicy& policy = {});

// The full Go API (--go-api full): see src/go_full.cpp. A cgo package over a shared C++ library
// with one C ABI entry point (the full C ABI backends' library, cabi_full.hpp).
struct GoFullOptions {
    std::size_t parts = 1;       // > 1: further C++ translation units in `parts`
    std::string package;         // Go package name (default: the module name)
    std::string module_path;     // go.mod module path (default: the package name)
    std::string library;         // shared library base name cgo links (-lNAME; default: the module name)
};
struct GeneratedGoFullSource {
    std::string source;          // C++ source of the shared library
    std::string package;         // the main Go file (package PACKAGE)
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;                          // further C++ translation units
    std::vector<std::pair<std::string, std::string>> files;  // go.mod, internal/rosetta/*.go, subpackages (relative paths)
};
GeneratedGoFullSource generate_go_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                       const BindingPolicy& policy = {}, const GoFullOptions& options = {});
} // namespace rosetta
