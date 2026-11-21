#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rosetta {

struct PerlOptions {
    std::size_t parts = 1;   // C++ library source files
    std::string package;     // Perl package (default: the module name in PascalCase; a::b nests)
    std::string library;     // native library base name (default: the module name)
};

struct GeneratedPerlSource {
    std::string source;                       // the C++ library with its XS glue (first part)
    std::string perl;                         // the Perl module (.pm): its runtime, the API and POD
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;           // further C++ parts
};

// Perl (--language perl): a Perl 5.14+ module over a C ABI library of the module (one entry point,
// as the other full C ABI backends) that carries a small XS glue, loaded with DynaLoader: no
// CPAN modules, and the library builds against Perl's own headers.
GeneratedPerlSource generate_perl(const Module& module, const std::string& name, bool skip_unsupported = false,
                                  const BindingPolicy& policy = {}, const PerlOptions& options = {});

} // namespace rosetta
