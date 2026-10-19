#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rosetta {
struct CompilationCommand {
    std::filesystem::path database;
    std::filesystem::path directory;
    std::filesystem::path file;
    std::size_t index = 0;
    std::vector<std::string> flags;
};
CompilationCommand load_compilation_command(const std::filesystem::path& database,
    const std::filesystem::path& translation_unit = {}, std::optional<std::size_t> command_index = {});
} // namespace rosetta
