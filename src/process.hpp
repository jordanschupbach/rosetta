#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rosetta::detail {

// Private POSIX process adapter. Uses argv directly, never a shell.
struct ProcessResult {
    int exit_code;
    std::string output;
    std::string error;
};

class TemporaryDirectory {
public:
    TemporaryDirectory();
    ~TemporaryDirectory();
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

ProcessResult run_process(const std::vector<std::string>& arguments, const std::filesystem::path& directory = {});
std::string read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, const std::string& contents);

} // namespace rosetta::detail
