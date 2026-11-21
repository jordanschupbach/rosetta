#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
// Visual Basic (.NET) bindings (--language visual-basic): see src/vb_full.cpp. Native classes become
// VB classes with inheritance, overloads, properties, operators and XML documentation, over the
// C ABI library every full backend shares.
struct VisualBasicOptions {
    std::size_t parts = 1;            // > 1: further translation units in `parts`
    std::string managed_namespace;    // VB namespace (default: the module name in PascalCase)
    std::string library;              // shared library base name (default: the module name)
    std::string functions_class;      // class of the module functions (default: Functions)
    std::string framework;            // target framework of the .vbproj (default: net10.0)
};
struct GeneratedVisualBasicSource {
    std::string source;               // the C++ library (part 1)
    std::string interface;            // the VB source
    std::string project;              // the .vbproj
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;   // further C++ translation units
};
GeneratedVisualBasicSource generate_visual_basic(const Module& module, const std::string& name, bool skip_unsupported = false,
                                                 const BindingPolicy& policy = {}, const VisualBasicOptions& options = {});
} // namespace rosetta
