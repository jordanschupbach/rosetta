#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rosetta {
// The Swift backend (--language swift): see src/swift_full.cpp. A Swift package over a shared C++
// library with one C ABI entry point (the full C ABI backends' library, cabi_full.hpp).
struct SwiftOptions {
    std::size_t parts = 1;   // > 1: further C++ translation units in `parts`
    std::string module;      // Swift module, package and product name (default: the module name in PascalCase)
    std::string library;     // shared library base name the package links (-lNAME; default: the module name)
};
struct GeneratedSwiftSource {
    std::string source;      // C++ source of the shared library
    std::string swift;       // Sources/MODULE/MODULE.swift: the API
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;                          // further C++ translation units
    std::vector<std::pair<std::string, std::string>> files;  // Package.swift, the runtime and the C module (relative to the package)
};
// The Swift module name for a Rosetta module name (features -> Features), unless options name one.
std::string swift_module_name(const std::string& name);
GeneratedSwiftSource generate_swift(const Module& module, const std::string& name, bool skip_unsupported = false,
                                    const BindingPolicy& policy = {}, const SwiftOptions& options = {});
} // namespace rosetta
