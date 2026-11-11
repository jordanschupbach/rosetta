#pragma once

#include "rosetta/model.hpp"
#include <filesystem>

namespace rosetta {

struct ParseOptions {
    // Files or directories whose declarations are exported in addition to the header itself.
    std::vector<std::filesystem::path> scopes{};
    // Class template specializations to instantiate and export, spelled as C++ types.
    std::vector<std::string> instantiations{};
    // Parse declarations only; much faster for large libraries but leaves inline bodies unchecked.
    bool skip_function_bodies = false;
};

Module parse(const std::filesystem::path& header, const std::string& clang = "clang++",
             const std::vector<std::string>& flags = {"-std=c++17"},
             const std::filesystem::path& directory = {}, bool collect_dependencies = false,
             const ParseOptions& options = {});

} // namespace rosetta
