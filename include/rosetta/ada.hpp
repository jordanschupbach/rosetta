#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rosetta {

// Ada backend (--language ada): an Ada 2022 package (spec and body) over the C ABI library the full
// C ABI backends share. `package` names the Ada package (default: the module name in Ada_Case) and its
// files (lowercase: features.ads, features.adb); `library` the native library it links (default: the
// module name).
struct AdaOptions {
    std::size_t parts = 1;
    std::string package;
    std::string library;
};

struct GeneratedAdaSource {
    std::string source;                                       // the C++ library
    std::string spec;                                         // PACKAGE.ads
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;                           // further C++ source parts
    std::vector<std::pair<std::string, std::string>> files;   // the body and project file (file name, text)
};

// The default Ada package name of a module (features -> Features, shape_kit -> Shape_Kit).
std::string ada_package_name(const std::string& name);
// The file name GNAT expects for a library unit's spec (Shape_Kit -> shape_kit.ads).
std::string ada_spec_file(const std::string& package);

GeneratedAdaSource generate_ada(const Module& module, const std::string& name, bool skip_unsupported = false,
                                const BindingPolicy& policy = {}, const AdaOptions& options = {});

} // namespace rosetta
