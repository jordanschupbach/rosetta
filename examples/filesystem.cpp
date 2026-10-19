#include "demo.hpp"
#include "filesystem_library.hpp"

namespace demo {
std::string path_normalize(std::string bytes) { return feature_library::files::path(bytes).lexically_normal().generic_string(); }
std::string path_join(std::string base, std::string child) {
    return (feature_library::files::path(base) / feature_library::files::path(child)).generic_string();
}
std::vector<std::string> path_components(std::string bytes) {
    std::vector<std::string> result;
    for (const auto& component : feature_library::files::path(bytes)) result.push_back(component.generic_string());
    return result;
}
std::tuple<std::string, std::string, std::string> path_parts(std::string bytes) {
    const auto value = feature_library::files::path(bytes);
    return {value.parent_path().generic_string(), value.filename().generic_string(), value.extension().generic_string()};
}
FileWorkspace::FileWorkspace() : native_(std::make_shared<feature_library::files::Workspace>()) {}
FileWorkspace::FileWorkspace(std::shared_ptr<feature_library::files::Workspace> native) : native_(std::move(native)) {}
FileWorkspace FileWorkspace::share() const { return FileWorkspace(native_); }
std::string FileWorkspace::location() const { return native_->location(); }
bool FileWorkspace::exists(std::string relative) const { return native_->exists(relative); }
void FileWorkspace::directories(std::string relative) { native_->directories(relative); }
void FileWorkspace::write(std::string relative, std::string bytes) { native_->write(relative, bytes); }
std::string FileWorkspace::read(std::string relative) const { return native_->read(relative); }
unsigned long long FileWorkspace::size(std::string relative) const { return native_->size(relative); }
std::vector<std::string> FileWorkspace::entries(std::string relative, bool recursive) const { return native_->entries(relative, recursive); }
void FileWorkspace::rename(std::string from, std::string to) { native_->rename(from, to); }
bool FileWorkspace::erase(std::string relative, bool recursive) { return native_->erase(relative, recursive); }
int FileWorkspace::live() { return feature_library::files::Workspace::alive.load(); }
int FileWorkspace::cleanup_errors() { return feature_library::files::Workspace::cleanup_errors.load(); }
} // namespace demo
