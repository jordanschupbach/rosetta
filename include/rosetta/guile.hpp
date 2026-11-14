#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedGuileSource {
    std::string source;     // C++ extension (libguile)
    std::string interface;  // Scheme module NAME.scm that loads it
    std::vector<std::string> diagnostics;
};
GeneratedGuileSource generate_guile(const Module& module, const std::string& name,
                                    bool skip_unsupported = false, const BindingPolicy& policy = {});

// --guile-api full: a libguile extension (source plus parts), the Scheme module file that loads it
// and defines the documented public API, and one file per policy submodule.
struct GuileFullOptions {
    std::size_t parts = 1;
    std::string module;    // Guile module name, "(a b)" or "a b" (default: the module name)
    std::string extension; // load-extension library name (default: lib<name>_guile)
};
struct GeneratedGuileFullSource {
    std::string source;
    std::vector<std::string> parts;
    std::string scheme;    // the module file (NAME.scm)
    std::vector<std::pair<std::string, std::string>> submodules; // path relative to the module file's directory, text
    std::vector<std::string> diagnostics;
};
GeneratedGuileFullSource generate_guile_full(const Module& module, const std::string& name, bool skip_unsupported,
                                             const BindingPolicy& policy, const GuileFullOptions& options);
} // namespace rosetta
