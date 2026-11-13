#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {

// The JavaScript (Node.js) backend: a Node-API addon with classes, inheritance, overloads,
// JavaScript subclasses of C++ classes (directors), callbacks and containers, plus TypeScript
// declarations (`types`, a .d.ts file) whose JSDoc carries the C++ documentation for editors.
struct GeneratedJavaScriptSource {
    std::string source;
    std::string types;
    std::vector<std::string> diagnostics;
    // parts > 1: further translation units, compiled and linked with source.
    std::vector<std::string> parts;
};
GeneratedJavaScriptSource generate_javascript(const Module& module, const std::string& name, bool skip_unsupported = false,
                                              const BindingPolicy& policy = {}, std::size_t parts = 1);

} // namespace rosetta
