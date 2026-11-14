#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedRubySource {
    std::string source;
    std::vector<std::string> diagnostics;
};
GeneratedRubySource generate_ruby(const Module& module, const std::string& name,
                                 bool skip_unsupported = false, const BindingPolicy& policy = {});

// --ruby-api full: an MRI native extension (source plus parts) and a YARD documentation file.
struct RubyFullOptions {
    std::size_t parts = 1;
    std::string ruby_module; // Ruby module path (default: CamelCase of the module name)
    std::string native;      // require_relative path of the extension from the .rb (default: name/name)
};
struct GeneratedRubyFullSource {
    std::string source;
    std::vector<std::string> parts;
    std::string ruby;
    std::vector<std::string> diagnostics;
};
GeneratedRubyFullSource generate_ruby_full(const Module& module, const std::string& name, bool skip_unsupported,
                                           const BindingPolicy& policy, const RubyFullOptions& options);
} // namespace rosetta
