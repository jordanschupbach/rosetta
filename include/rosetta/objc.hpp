#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <string>
#include <utility>
#include <vector>

namespace rosetta {

// Objective-C backend (--language objective-c): Objective-C classes (Foundation, ARC) over the C
// ABI library the full C ABI backends share. `prefix` starts every generated Objective-C name and
// names the files PREFIX.h and PREFIX.m (default: the module name in PascalCase); `library` is the
// native library to link (default: the module name).
struct ObjcOptions {
    std::size_t parts = 1;
    std::string prefix;
    std::string library;
};

struct GeneratedObjcSource {
    std::string source;                                       // the C++ library
    std::string header;                                       // PREFIX.h, the API
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;                           // further C++ source parts
    std::vector<std::pair<std::string, std::string>> files;   // the other files (PREFIX.m)
};

// The default Objective-C prefix of a module.
std::string objc_prefix(const std::string& name);

GeneratedObjcSource generate_objc(const Module& module, const std::string& name, bool skip_unsupported = false,
                                  const BindingPolicy& policy = {}, const ObjcOptions& options = {});

} // namespace rosetta
