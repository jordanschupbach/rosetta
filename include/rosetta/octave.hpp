#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <utility>

namespace rosetta {
struct GeneratedOctaveSource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedOctaveSource generate_octave(const Module& module, const std::string& name,
                                     bool skip_unsupported = false, const BindingPolicy& policy = {});

// --octave-api full: a native oct-file (__MODULE__.oct: source plus parts) and an Octave package
// of .m files with help text (files: paths relative to the output directory, e.g. +pkg/Class.m).
struct OctaveFullOptions {
    std::size_t parts = 1;
    std::string package; // Octave package (default: the module name)
};
struct GeneratedOctaveFullSource {
    std::string source;
    std::vector<std::string> parts;
    std::vector<std::pair<std::string, std::string>> files;
    std::string contents;      // +pkg/Contents.m, the package overview
    std::string contents_path; // its path relative to the output directory
    std::vector<std::string> diagnostics;
};
GeneratedOctaveFullSource generate_octave_full(const Module& module, const std::string& name, bool skip_unsupported,
                                               const BindingPolicy& policy, const OctaveFullOptions& options);
} // namespace rosetta
