#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rosetta {
// The Rust backend (--language rust): see src/rust_full.cpp. A Cargo crate over a shared C++
// library with one C ABI entry point (the full C ABI backends' library, cabi_full.hpp).
struct RustOptions {
    std::size_t parts = 1;   // > 1: further C++ translation units in `parts`
    std::string crate;       // Cargo package and library crate name (default: the module name)
    std::string library;     // shared library base name the crate links (-lNAME; default: the module name)
};
struct GeneratedRustSource {
    std::string source;      // C++ source of the shared library
    std::string lib;         // src/lib.rs of the crate
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;                          // further C++ translation units
    std::vector<std::pair<std::string, std::string>> files;  // Cargo.toml, build.rs, src/rosetta.rs (relative to the crate)
};
GeneratedRustSource generate_rust(const Module& module, const std::string& name, bool skip_unsupported = false,
                                  const BindingPolicy& policy = {}, const RustOptions& options = {});
} // namespace rosetta
