#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <utility>

namespace rosetta {
struct GeneratedJavaSource {
    std::string source;
    std::string interface;
    std::vector<std::string> diagnostics;
};
GeneratedJavaSource generate_java(const Module& module, const std::string& name,
                                 bool skip_unsupported = false, const BindingPolicy& policy = {},
                                 const std::string& package = {});

// The full Java API (--java-api full): see src/java_full.cpp.
struct JavaFullOptions {
    std::size_t parts = 1;            // > 1: further translation units in `parts`
    std::string package;              // Java package (default: the module name)
    std::string library;              // JNI library base name for System.loadLibrary (default: the module name)
    std::string functions_class;      // class of the module functions (default: the module name in PascalCase)
};
struct GeneratedJavaFullSource {
    std::string source;               // the C++ library (with its JNI glue)
    std::string package_info;         // PACKAGE/package-info.java
    std::vector<std::pair<std::string, std::string>> files; // the other .java files, relative to the source root
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;   // further C++ translation units
};
GeneratedJavaFullSource generate_java_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                           const BindingPolicy& policy = {}, const JavaFullOptions& options = {});
} // namespace rosetta
