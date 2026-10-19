#pragma once

#include "rosetta/model.hpp"
#include <filesystem>

namespace rosetta {

Module parse(const std::filesystem::path& header, const std::string& clang = "clang++",
             const std::vector<std::string>& flags = {"-std=c++17"},
             const std::filesystem::path& directory = {}, bool collect_dependencies = false);

} // namespace rosetta
