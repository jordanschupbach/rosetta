#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace rosetta::detail {
std::vector<std::string> compiler_dependencies(const std::string& rule, const std::filesystem::path& directory);
std::string dependency_rule(const std::filesystem::path& target, const std::vector<std::string>& prerequisites);
void validate_dependency_flags(const std::vector<std::string>& flags);
std::filesystem::path dependency_working_directory(const std::vector<std::string>& flags, const std::filesystem::path& directory);
} // namespace rosetta::detail
