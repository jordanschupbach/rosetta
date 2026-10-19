#include "dependencies.hpp"
#include "rosetta/model.hpp"
#include <set>

namespace rosetta::detail {
namespace {
void validate_path(const std::string& path) {
    if (path.empty()) throw BindingError("empty dependency path");
    for (const unsigned char character : path)
        if (character < 32 || character == 127 || character == '\\')
            throw BindingError("dependency paths cannot contain control characters or literal backslashes: " + path);
}
std::string escaped(const std::string& path) {
    validate_path(path);
    std::string result;
    for (char character : path) {
        if (character == '$') result += "$$";
        else {
            if (character == ' ' || character == '#') result += '\\';
            result += character;
        }
    }
    return result;
}
} // namespace

void validate_dependency_flags(const std::vector<std::string>& flags) {
    const std::set<std::string> managed = {"-M", "-MM", "-MD", "-MMD", "-MG", "-MP", "-MF", "-MT", "-MQ"};
    for (const auto& flag : flags)
        if (managed.count(flag) || flag.rfind("-MF", 0) == 0 || flag.rfind("-MT", 0) == 0 || flag.rfind("-MQ", 0) == 0)
            throw BindingError("compiler dependency flags are managed by Rosetta during dependency collection: " + flag);
}

std::filesystem::path dependency_working_directory(const std::vector<std::string>& flags, const std::filesystem::path& directory) {
    std::string selected;
    const std::set<std::string> operands = {"-I", "-F", "-D", "-U", "-include", "-imacros", "-isystem", "-iquote",
        "-idirafter", "-iframework", "-isysroot", "--sysroot", "-resource-dir", "-target", "--target", "-x", "-Xclang", "-Xpreprocessor", "-Xlinker", "-Xassembler"};
    for (std::size_t i = 0; i < flags.size(); ++i) {
        const auto& flag = flags[i];
        if ((flag.rfind("-Xclang=", 0) == 0 && flag.find("-working-directory") != std::string::npos) ||
            (flag.rfind("-Wp,", 0) == 0 && flag.find(",-working-directory") != std::string::npos))
            throw BindingError("forwarded working-directory flags are unsupported during dependency collection");
        if (flag == "-working-directory" || flag == "--working-directory") {
            if (++i == flags.size()) throw BindingError("missing compiler working-directory value");
            selected = flags[i];
        } else if (flag.rfind("-working-directory=", 0) == 0) selected = flag.substr(19);
        else if (flag.rfind("--working-directory=", 0) == 0) selected = flag.substr(20);
        else if (operands.count(flag) && i + 1 < flags.size()) {
            if ((flag == "-Xclang" || flag == "-Xpreprocessor") &&
                (flags[i + 1] == "-working-directory" || flags[i + 1].rfind("-working-directory=", 0) == 0))
                throw BindingError("forwarded working-directory flags are unsupported during dependency collection");
            ++i;
        }
    }
    return selected.empty() ? directory : std::filesystem::canonical(directory / selected);
}

std::vector<std::string> compiler_dependencies(const std::string& rule, const std::filesystem::path& directory) {
    const std::string prefix = "rosetta-dependencies:";
    if (rule.rfind(prefix, 0) != 0) throw BindingError("unexpected compiler dependency target");
    std::set<std::string> paths;
    std::string token;
    const auto flush = [&] {
        if (token.empty()) return;
        validate_path(token);
        const auto relative = std::filesystem::path(token);
        const auto path = relative.is_absolute() ? relative : directory / relative;
        // Clang normalizes literal backslashes to separators in depfiles.
        // Reject unresolvable names rather than publishing a false dependency.
        if (!std::filesystem::is_regular_file(path)) throw BindingError("compiler dependency is not a regular file: " + path.string());
        paths.insert(std::filesystem::canonical(path).string());
        token.clear();
    };
    bool ended = false;
    for (std::size_t i = prefix.size(); i < rule.size(); ++i) {
        const char character = rule[i];
        if (ended) {
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
                throw BindingError("unexpected additional compiler dependency rule");
            continue;
        }
        if (character == '\\' && i + 1 < rule.size()) {
            const char next = rule[i + 1];
            if (next == '\n' || (next == '\r' && i + 2 < rule.size() && rule[i + 2] == '\n')) {
                flush(); i += next == '\n' ? 1 : 2; continue;
            }
            if (next == ' ' || next == '\t' || next == '#' || next == '\\') { token += next; ++i; continue; }
        }
        if (character == '$') {
            if (i + 1 == rule.size() || rule[i + 1] != '$') throw BindingError("compiler dependency variables are unsupported");
            token += '$'; ++i;
        } else if (character == ' ' || character == '\t') flush();
        else if (character == '\r' || character == '\n') { flush(); ended = true; }
        else if (character == '#') throw BindingError("unescaped hash in compiler dependency path");
        else token += character;
    }
    flush();
    if (paths.empty()) throw BindingError("compiler did not report any dependencies");
    return {paths.begin(), paths.end()};
}

std::string dependency_rule(const std::filesystem::path& target, const std::vector<std::string>& prerequisites) {
    // A colon in a target conflicts with Make's rule delimiter. Prerequisites
    // can contain colons, as compiler-produced rules do on this POSIX adapter.
    if (target.string().find(':') != std::string::npos) throw BindingError("dependency target cannot contain a colon");
    std::string result = escaped(target.string()) + ":";
    const std::set<std::string> unique(prerequisites.begin(), prerequisites.end());
    for (const auto& path : unique) result += " \\\n  " + escaped(path);
    return result + "\n";
}
} // namespace rosetta::detail
