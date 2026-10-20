#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// This concrete demo uses POSIX descriptors for cleanup without C++ allocations.
// Paths and metadata use std::filesystem; other hosts can replace
// the workspace resource implementation without changing its binding facade.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace feature_library::files {
namespace fs = std::filesystem;
inline fs::path path(const std::string& bytes) {
    if (bytes.find('\0') != std::string::npos) throw std::invalid_argument("path contains NUL");
    return fs::path(bytes);
}
[[noreturn]] inline void system_failure(const char* operation) {
    const int code = errno;
    throw std::system_error(code, std::generic_category(), operation);
}

class Workspace {
    std::string directory_;
    int descriptor_ = -1;

    // Destruction must also work when C++ allocation has failed. Never build
    // paths or use allocating filesystem iterators here. Do not follow links.
    static bool clear(int descriptor) noexcept {
        const int copy = ::dup(descriptor);
        if (copy < 0) return false;
        DIR* stream = ::fdopendir(copy);
        if (!stream) { ::close(copy); return false; }
        // Unlinking while iterating may skip entries on some filesystems
        // (e.g. APFS on macOS), so rescan until a pass removes nothing.
        bool ok = true, removed = true;
        while (removed) {
            removed = false;
            ok = true;
            ::rewinddir(stream);
            for (;;) {
                errno = 0;
                const auto* entry = ::readdir(stream);
                if (!entry) { if (errno) ok = false; break; }
                if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
                struct stat status {};
                if (::fstatat(descriptor, entry->d_name, &status, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; continue; }
                if (S_ISDIR(status.st_mode)) {
                    const int child = ::openat(descriptor, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                    if (child < 0) { ok = false; continue; }
                    if (!clear(child)) ok = false;
                    ::close(child);
                    if (::unlinkat(descriptor, entry->d_name, AT_REMOVEDIR) != 0) ok = false;
                    else removed = true;
                } else if (::unlinkat(descriptor, entry->d_name, 0) != 0) ok = false;
                else removed = true;
            }
        }
        ::closedir(stream);
        return ok;
    }
    fs::path resolve(const std::string& relative, bool root_allowed = false) const {
        const auto input = path(relative);
        if (relative.empty()) {
            if (!root_allowed) throw std::invalid_argument("workspace path must not be empty");
            return fs::path(directory_);
        }
        if (input.has_root_path()) throw std::invalid_argument("workspace path must be relative");
        auto result = fs::path(directory_);
        unsigned depth = 0;
        for (const auto& part : input) {
            if (part.empty()) continue; // trailing directory separator
            if (part == "." || part == "..") throw std::invalid_argument("workspace dot components are forbidden");
            if (++depth > 32) throw std::invalid_argument("workspace path exceeds 32 components");
            result /= part;
            std::error_code error;
            const auto status = fs::symlink_status(result, error);
            if (error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("workspace status", result, error);
            if (fs::is_symlink(status)) throw std::invalid_argument("workspace symlinks are forbidden");
        }
        if (!depth) throw std::invalid_argument("workspace path must name an entry");
        return result;
    }
    struct StagedFile {
        std::string name;
        int descriptor;
        explicit StagedFile(const fs::path& destination)
            : name((destination.parent_path() / ".rosetta-write-XXXXXX").string()), descriptor(::mkstemp(name.data())) {
            if (descriptor < 0) system_failure("create staged file");
        }
        ~StagedFile() noexcept { if (descriptor >= 0) ::close(descriptor); ::unlink(name.c_str()); }
        StagedFile(const StagedFile&) = delete;
        StagedFile& operator=(const StagedFile&) = delete;
    };
    struct Directory {
        DIR* stream;
        explicit Directory(const fs::path& source) {
            const int descriptor = ::open(source.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (descriptor < 0) system_failure("open workspace directory");
            stream = ::fdopendir(descriptor);
            if (!stream) {
                const int code = errno;
                ::close(descriptor);
                throw std::system_error(code, std::generic_category(), "read workspace directory");
            }
        }
        ~Directory() noexcept { ::closedir(stream); }
        Directory(const Directory&) = delete;
        Directory& operator=(const Directory&) = delete;
    };
    static void snapshot(const fs::path& source, const fs::path& root, bool recursive,
                         unsigned depth, std::vector<std::string>& result) {
        if (depth > 32) throw std::invalid_argument("workspace traversal exceeds 32 directories");
        Directory directory(source);
        for (;;) {
            errno = 0;
            const auto* entry = ::readdir(directory.stream);
            if (!entry) { if (errno) system_failure("read workspace entry"); break; }
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
            const auto child = source / entry->d_name;
            result.push_back(child.lexically_relative(root).generic_string());
            if (recursive && fs::is_directory(fs::symlink_status(child))) snapshot(child, root, true, depth + 1, result);
        }
    }
public:
    inline static std::atomic<int> alive{0};
    inline static std::atomic<int> cleanup_errors{0};
    Workspace() : directory_((fs::temp_directory_path() / "rosetta-files-XXXXXX").string()) {
        if (!::mkdtemp(directory_.data())) system_failure("create workspace");
        descriptor_ = ::open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (descriptor_ < 0) {
            const int code = errno;
            if (::rmdir(directory_.c_str()) != 0) ++cleanup_errors;
            throw std::system_error(code, std::generic_category(), "open workspace");
        }
        ++alive;
    }
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    ~Workspace() noexcept {
        bool ok = clear(descriptor_);
        ::close(descriptor_);
        if (::rmdir(directory_.c_str()) != 0) ok = false;
        if (!ok) ++cleanup_errors;
        --alive;
    }
    std::string location() const { return directory_; }
    bool exists(const std::string& relative) const { return fs::exists(resolve(relative, true)); }
    void directories(const std::string& relative) { fs::create_directories(resolve(relative)); }
    void write(const std::string& relative, const std::string& bytes) {
        if (bytes.size() > 16 * 1024 * 1024) throw std::length_error("workspace file exceeds 16 MiB");
        const auto destination = resolve(relative);
        StagedFile file(destination);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto written = ::write(file.descriptor, bytes.data() + offset, bytes.size() - offset);
            if (written < 0) { if (errno == EINTR) continue; system_failure("write staged file"); }
            if (!written) throw std::runtime_error("staged file write made no progress");
            offset += static_cast<std::size_t>(written);
        }
        // Native rename is atomic on this workspace's filesystem. Preparation
        // failures leave an existing destination unchanged and remove staging.
        fs::rename(file.name, destination);
    }
    std::string read(const std::string& relative) const {
        const auto source = resolve(relative);
        if (!fs::is_regular_file(source)) throw std::invalid_argument("workspace regular file expected");
        if (fs::file_size(source) > 16 * 1024 * 1024) throw std::length_error("workspace file exceeds 16 MiB");
        std::ifstream input(source, std::ios::binary);
        if (!input) throw std::runtime_error("workspace file could not be opened");
        std::string result;
        char bytes[4096];
        while (input.read(bytes, sizeof(bytes)) || input.gcount()) {
            if (static_cast<std::size_t>(input.gcount()) > 16 * 1024 * 1024 - result.size())
                throw std::length_error("workspace file exceeds 16 MiB");
            result.append(bytes, static_cast<std::size_t>(input.gcount()));
        }
        if (!input.eof()) throw std::runtime_error("workspace file read failed");
        return result;
    }
    unsigned long long size(const std::string& relative) const {
        const auto source = resolve(relative);
        if (!fs::is_regular_file(source)) throw std::invalid_argument("workspace regular file expected");
        return fs::file_size(source);
    }
    std::vector<std::string> entries(const std::string& relative, bool recursive) const {
        const auto source = resolve(relative, true);
        std::vector<std::string> result;
        const auto root = fs::path(directory_);
        // Some standard-library directory iterator implementations allocate
        // inside noexcept internals. A descriptor adapter keeps allocation
        // failure catchable and releases traversal resources during unwinding.
        snapshot(source, root, recursive, 0, result);
        std::sort(result.begin(), result.end());
        return result;
    }
    void rename(const std::string& from, const std::string& to) {
        const auto source = resolve(from), destination = resolve(to);
        if (fs::exists(destination)) throw std::invalid_argument("workspace rename destination exists");
        fs::rename(source, destination);
    }
    bool erase(const std::string& relative, bool recursive) {
        const auto destination = resolve(relative);
        if (recursive && fs::is_directory(destination)) {
            Directory directory(destination);
            if (!clear(::dirfd(directory.stream))) throw std::runtime_error("workspace recursive erase failed");
        }
        return fs::remove(destination);
    }
};
} // namespace feature_library::files
