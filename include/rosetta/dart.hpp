#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rosetta {

// Dart backend (--language dart): a Dart package over the C ABI library the full C ABI backends
// share. `package` names the Dart package and its library file lib/PACKAGE.dart (default: the
// module name in snake_case); `library` the native library it loads (default: the module name).
struct DartOptions {
    std::size_t parts = 1;
    std::string package;
    std::string library;
};

struct GeneratedDartSource {
    std::string source;                                       // the C++ library
    std::string dart;                                         // lib/PACKAGE.dart, the API
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;                           // further C++ source parts
    std::vector<std::pair<std::string, std::string>> files;   // the package's other files (relative path, text)
};

// The default Dart package name of a module.
std::string dart_package_name(const std::string& name);

GeneratedDartSource generate_dart(const Module& module, const std::string& name, bool skip_unsupported = false,
                                  const BindingPolicy& policy = {}, const DartOptions& options = {});

} // namespace rosetta
