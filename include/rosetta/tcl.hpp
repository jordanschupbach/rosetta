#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedTclSource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedTclSource generate_tcl(const Module& module, const std::string& name,
                                bool skip_unsupported = false, const BindingPolicy& policy = {});

// The full Tcl API (--tcl-api full): a stubs-enabled Tcl extension with TclOO classes, plus the
// documented package file (NAME.tcl) and its pkgIndex.tcl.
struct TclFullOptions {
    std::size_t parts = 1;
    std::string tcl_namespace; // default: the module name
    std::string library;       // shared library basename; default: the module name
    std::string version = "1.0";
    std::string tcl_file;      // package file name referenced by pkgIndex.tcl; default NAME.tcl
};
struct GeneratedTclFullSource {
    std::string source;
    std::vector<std::string> parts;
    std::string tcl;
    std::string pkg_index;
    std::vector<std::string> diagnostics;
};
GeneratedTclFullSource generate_tcl_full(const Module& module, const std::string& name, bool skip_unsupported,
                                         const BindingPolicy& policy, const TclFullOptions& options);
} // namespace rosetta
