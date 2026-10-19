#include "rosetta/compilation_database.hpp"
#include "rosetta/model.hpp"
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <cctype>
#include <set>

namespace rosetta {
namespace {
using Json = nlohmann::json;
std::string string(const Json& value, const std::string& context, bool nonempty = true) {
    if (!value.is_string()) throw BindingError(context + " must be a string");
    auto result = value.get<std::string>();
    if ((nonempty && result.empty()) || result.find('\0') != std::string::npos)
        throw BindingError(context + " must be nonempty and contain no NUL bytes");
    return result;
}
std::filesystem::path resolve(const std::filesystem::path& base, const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(path.is_absolute() ? path : base / path);
}
// Literal POSIX words only. Never ask a shell to interpret a database command.
std::vector<std::string> words(const std::string& command) {
    std::vector<std::string> result;
    std::string word;
    char quote = 0;
    bool started = false;
    for (std::size_t i = 0; i < command.size(); ++i) {
        const char c = command[i];
        if (quote == '\'') {
            if (c == '\'') quote = 0;
            else word += c;
        } else if (c == '\\') {
            if (++i == command.size()) throw BindingError("compilation command has a trailing escape");
            const char next = command[i];
            if (quote == '"' && next != '"' && next != '\\' && next != '$' && next != '`' && next != '\n') word += '\\';
            if (next != '\n') { word += next; started = true; }
        } else if (c == '$' || c == '`' || (!quote && std::string(";|&<>()").find(c) != std::string::npos)) {
            throw BindingError("compilation command contains nonliteral shell syntax; use an arguments array");
        } else if (quote) {
            if (c == quote) quote = 0;
            else word += c;
        } else if (c == '\'' || c == '"') {
            quote = c;
            started = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (started) { result.push_back(std::move(word)); word.clear(); started = false; }
        } else { word += c; started = true; }
    }
    if (quote) throw BindingError("compilation command has an unterminated quote");
    if (started) result.push_back(std::move(word));
    return result;
}
std::vector<std::string> parser_flags(const std::vector<std::string>& arguments, const CompilationCommand& command) {
    if (arguments.empty() || arguments.front().empty() || arguments.front().front() == '-')
        throw BindingError("compilation arguments need a compiler executable");
    const auto executable = std::filesystem::path(arguments.front()).filename().string();
    if (executable == "ccache" || executable == "sccache" || executable == "env")
        throw BindingError("compilation command launchers are unsupported; provide direct compiler arguments");
    for (std::size_t i = 1; i < arguments.size(); ++i)
        if (!arguments[i].empty() && arguments[i].front() == '@')
            throw BindingError("compilation database response files are unsupported; expand them into arguments");
    const std::set<std::string> drop = {"-c", "-S", "-E", "-M", "-MM", "-MD", "-MMD", "-MG", "-MP",
        "-emit-llvm", "--analyze", "-fsyntax-only", "-save-temps", "--save-temps", "-ftime-trace", "-fsave-optimization-record"};
    const std::set<std::string> output = {"-o", "--output", "-MF", "-MT", "-MQ", "-MJ", "-serialize-diagnostics", "--serialize-diagnostics"};
    const std::set<std::string> paired = {"-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros", "-D", "-U",
        "-isysroot", "--sysroot", "-target", "--target", "-x", "-std", "-F", "-iframework", "-iprefix", "-iwithprefix",
        "-iwithprefixbefore", "-resource-dir", "--gcc-toolchain", "-B", "-isystem-after"};
    std::vector<std::string> result;
    bool source_found = false;
    const auto source = [&](const std::string& arg) { return !arg.empty() && resolve(command.directory, arg) == command.file; };
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        if (arg.empty()) throw BindingError("unsupported empty compiler operand in compilation database");
        if (arg.front() == '@') throw BindingError("compilation database response files are unsupported; expand them into arguments");
        if (arg == "-Xclang" || arg == "-Xpreprocessor" || arg == "-Xassembler" || arg == "-Xlinker" || arg == "-mllvm" || arg.rfind("-Wp,", 0) == 0)
            throw BindingError("forwarded compiler options are unsupported in compilation databases; pass parser options after --");
        if (drop.count(arg) || arg.rfind("-save-temps=", 0) == 0 || arg.rfind("--save-temps=", 0) == 0 ||
            arg.rfind("-ftime-trace=", 0) == 0 || arg.rfind("-foptimization-record-", 0) == 0) continue;
        if (output.count(arg) || paired.count(arg)) {
            if (++i == arguments.size()) throw BindingError("missing compilation argument after " + arg);
            if (paired.count(arg)) { result.push_back(arg); result.push_back(arguments[i]); }
            continue;
        }
        if (arg.rfind("--output=", 0) == 0 || arg.rfind("-serialize-diagnostics=", 0) == 0 || arg.rfind("--serialize-diagnostics=", 0) == 0 ||
            arg.rfind("-MF", 0) == 0 || arg.rfind("-MT", 0) == 0 || arg.rfind("-MQ", 0) == 0 || arg.rfind("-MJ", 0) == 0 ||
            (arg.size() > 2 && arg.rfind("-o", 0) == 0 && arg.rfind("-objc", 0) != 0 && arg.rfind("-openmp", 0) != 0 && arg.rfind("-opt-", 0) != 0)) continue;
        if (arg == "--") {
            for (++i; i < arguments.size(); ++i) {
                if (!source(arguments[i])) throw BindingError("extra compiler input in compilation database: " + arguments[i]);
                source_found = true;
            }
            break;
        }
        if (arg.front() != '-') {
            if (!source(arg)) throw BindingError("unsupported compiler operand in compilation database: " + arg);
            source_found = true;
        } else result.push_back(arg);
    }
    if (!source_found) throw BindingError("compilation command does not contain its translation unit source");
    return result;
}
} // namespace

CompilationCommand load_compilation_command(const std::filesystem::path& path,
    const std::filesystem::path& translation_unit, std::optional<std::size_t> command_index) {
    try {
        const auto database = std::filesystem::canonical(std::filesystem::is_directory(path) ? path / "compile_commands.json" : path);
        const auto root = Json::parse(detail::read_file(database));
        if (!root.is_array() || root.empty()) throw BindingError("compilation database must be a nonempty command array");
        const auto requested = translation_unit.empty() ? std::filesystem::path{} : resolve(std::filesystem::current_path(), translation_unit);
        std::vector<CompilationCommand> candidates;
        for (std::size_t i = 0; i < root.size(); ++i) {
            const auto& entry = root[i];
            if (!entry.is_object() || !entry.contains("directory") || !entry.contains("file"))
                throw BindingError("compilation database entries need directory and file fields");
            const auto directory = resolve(database.parent_path(), string(entry.at("directory"), "compilation directory"));
            const auto file = resolve(directory, string(entry.at("file"), "compilation file"));
            if ((!command_index || *command_index == i) && (requested.empty() || requested == file))
                candidates.push_back({database, directory, file, i, {}});
        }
        if (candidates.empty()) throw BindingError("no compilation command matches the requested translation unit/index");
        if (candidates.size() != 1) throw BindingError("ambiguous compilation database; select --translation-unit and/or --command-index");
        auto selected = std::move(candidates.front());
        if (!std::filesystem::is_directory(selected.directory)) throw BindingError("compilation working directory does not exist: " + selected.directory.string());
        const auto& entry = root[selected.index];
        std::vector<std::string> arguments;
        if (entry.contains("arguments")) {
            if (!entry["arguments"].is_array()) throw BindingError("compilation arguments must be an array");
            for (const auto& argument : entry["arguments"]) arguments.push_back(string(argument, "compilation argument", false));
        } else if (entry.contains("command")) arguments = words(string(entry["command"], "compilation command"));
        else throw BindingError("compilation command needs arguments or command");
        selected.flags = parser_flags(arguments, selected);
        return selected;
    } catch (const Json::exception& error) {
        throw BindingError("invalid compilation database: " + std::string(error.what()));
    }
}
} // namespace rosetta
