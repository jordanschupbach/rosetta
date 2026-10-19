#include "process.hpp"
#include "rosetta/model.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>

namespace rosetta::detail {

TemporaryDirectory::TemporaryDirectory() {
    auto pattern = (std::filesystem::temp_directory_path() / "rosetta-XXXXXX").string();
    if (!::mkdtemp(pattern.data()))
        throw BindingError("cannot create temporary directory: " + std::string(std::strerror(errno)));
    path_ = pattern;
}

TemporaryDirectory::~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw BindingError("cannot read " + path.string());
    std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) throw BindingError("error reading " + path.string());
    return contents;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw BindingError("cannot write " + path.string());
    output << contents;
    output.close();
    if (!output) throw BindingError("error writing " + path.string());
}

ProcessResult run_process(const std::vector<std::string>& arguments, const std::filesystem::path& directory) {
    if (arguments.empty()) throw BindingError("empty process command");
    TemporaryDirectory temporary;
    const auto stdout_path = temporary.path() / "stdout";
    const auto stderr_path = temporary.path() / "stderr";
    // Files avoid pipe-buffer deadlocks for large ASTs and simultaneous diagnostics.
    using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
    File output(std::fopen(stdout_path.c_str(), "w+"), &std::fclose);
    File error(std::fopen(stderr_path.c_str(), "w+"), &std::fclose);
    if (!output || !error) throw BindingError("cannot capture compiler output");
    std::vector<char*> argv;
    for (const auto& arg : arguments) {
        if (arg.find('\0') != std::string::npos) throw BindingError("process arguments cannot contain NUL bytes");
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    const auto working_directory = directory.string();
    if (working_directory.find('\0') != std::string::npos) throw BindingError("process working directory cannot contain NUL bytes");
    const char* cwd = working_directory.empty() ? nullptr : working_directory.c_str();
    auto* executable = argv.front();
    auto** argv_pointer = argv.data();
    const auto pid = ::fork();
    if (pid < 0) throw BindingError("cannot start " + arguments.front());
    if (pid == 0) {
        if (::dup2(::fileno(output.get()), STDOUT_FILENO) < 0 ||
            ::dup2(::fileno(error.get()), STDERR_FILENO) < 0) ::_exit(126);
        ::close(::fileno(output.get()));
        ::close(::fileno(error.get()));
        if (cwd && ::chdir(cwd) < 0) {
            constexpr char message[] = "cannot enter compiler working directory\n";
            (void)::write(STDERR_FILENO, message, sizeof(message) - 1);
            ::_exit(126);
        }
        ::execvp(executable, argv_pointer);
        // Only async-signal-safe operations between fork and exec/exit.
        constexpr char message[] = "cannot execute compiler\n";
        (void)::write(STDERR_FILENO, message, sizeof(message) - 1);
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) throw BindingError("cannot wait for " + arguments.front());
    }
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return {code, read_file(stdout_path), read_file(stderr_path)};
}

} // namespace rosetta::detail
