#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rosetta {

struct PhpOptions {
    std::size_t parts = 1;   // C++ library source files
    std::string namespace_;  // PHP namespace (default: the module name in PascalCase; a\b nests)
    std::string library;     // native library base name (default: the module name)
};

struct GeneratedPhpSource {
    std::string source;                       // the C++ library (first part)
    std::string php;                          // the PHP API and its runtime (one file)
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;           // further C++ parts
};

// PHP (--language php): a PHP 8.1+ file calling a C ABI library of the module through the FFI
// extension (one entry point, as the other full C ABI backends).
GeneratedPhpSource generate_php(const Module& module, const std::string& name, bool skip_unsupported = false,
                                const BindingPolicy& policy = {}, const PhpOptions& options = {});

} // namespace rosetta
