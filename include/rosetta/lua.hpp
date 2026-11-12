#pragma once

#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {

struct GeneratedSource {
    std::string source;
    std::vector<std::string> diagnostics;
};

GeneratedSource generate_lua(const Module& module, const std::string& name,
                             bool skip_unsupported = false, const BindingPolicy& policy = {});

// The full Lua API: overloads, inheritance, Lua subclasses (directors), operators, and a
// LuaCATS stub (`meta`) documenting the module for editors.
struct GeneratedLuaSource {
    std::string source;
    std::string meta;
    std::vector<std::string> diagnostics;
    // parts > 1: further translation units, compiled and linked with source.
    std::vector<std::string> parts;
};
GeneratedLuaSource generate_lua_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                     const BindingPolicy& policy = {}, std::size_t parts = 1);

} // namespace rosetta
