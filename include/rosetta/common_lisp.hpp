#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedCommonLispSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedCommonLispSource generate_common_lisp(const Module& module, const std::string& name,
                                               bool skip_unsupported = false, const BindingPolicy& policy = {});

// The full Common Lisp API (--lisp-api full): CLOS classes, generic functions, overloads,
// Lisp subclasses (directors), containers, enums as keywords, callbacks, and docstrings with
// the C++ documentation. `interface` is the .lisp file, `system` its ASDF system definition.
struct LispFullOptions {
    std::size_t parts = 1;                // > 1: further translation units in `parts`
    std::vector<std::string> nicknames;   // package nicknames
    std::string library;                  // shared library base name (default: the module name)
    std::string interface_name;           // the .lisp file's name in the .asd (default: the package)
};
struct GeneratedLispSource {
    std::string source;
    std::string interface;
    std::string system;
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;
};
GeneratedLispSource generate_common_lisp_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                              const BindingPolicy& policy = {}, const LispFullOptions& options = {});
} // namespace rosetta
