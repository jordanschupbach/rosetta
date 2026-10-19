#include "rosetta/compilation_database.hpp"
#include "rosetta/frontend.hpp"
#include "rosetta/lua.hpp"
#include "rosetta/cli.hpp"
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using Json = nlohmann::json;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
template<class Operation> void rejects(Operation operation, const std::string& expected) {
    try { operation(); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(expected) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + expected);
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--cwd") { std::cout << std::filesystem::current_path().string(); return 0; }
    if (argc != 2) return 1;
    try {
        const std::string clang = argv[1];
        const auto parent_cwd = std::filesystem::current_path();
        rosetta::detail::TemporaryDirectory temporary;
        const auto root = temporary.path() / "project with spaces";
        const auto build = root / "build";
        std::filesystem::create_directories(build);
        const auto header = root / "include" / "api.hpp";
        const auto unit = root / "src" / "unit.cpp";
        const auto other = root / "src" / "other.cpp";
        const auto database = build / "compile_commands.json";
        rosetta::detail::write_file(root / "config.hpp", "#define FORCE_CONTEXT 1\n");
        rosetta::detail::write_file(root / "include" / "types.hpp", "using DependencyInt = int;\n");
        rosetta::detail::write_file(header, R"cpp(
            #pragma once
            #include "types.hpp"
            #ifndef FORCE_CONTEXT
            #error missing forced include from compile command
            #endif
            static_assert(__cplusplus >= 202002L);
            #ifdef MSG
            static_assert(sizeof(MSG) == 12);
            #endif
            int base();
            #if FEATURE_API
            DependencyInt feature(DependencyInt value);
            #endif
        )cpp");
        rosetta::detail::write_file(unit, "#include \"../include/api.hpp\"\n");
        rosetta::detail::write_file(other, "int other();\n");
        const auto marker = root / "compiler-must-not-run";
        const auto fake = root / "fake compiler";
        rosetta::detail::write_file(fake, "#!/bin/sh\ntouch '" + marker.string() + "'\nexit 1\n");
        std::filesystem::permissions(fake, std::filesystem::perms::owner_all);
        const Json command{{"directory", "."}, {"file", "../src/unit.cpp"}, {"output", "object.o"},
            {"arguments", {fake.string(), "-I", "../include", "-include", "../config.hpp", "-DFEATURE_API=1", "-std=c++20", "-Werror",
                "-o", "object.o", "-MMD", "-MFdeps.d", "-MT", "target", "-MJ", "fragment.json", "-ftime-trace=trace.json",
                "--serialize-diagnostics", "diagnostics.dia", "-c", "../src/unit.cpp"}}};
        const auto write = [&](Json value) { rosetta::detail::write_file(database, value.dump()); };
        write(Json::array({command}));
        const auto selected = rosetta::load_compilation_command(build);
        require(selected.file == unit && selected.directory == build && selected.index == 0, "database path resolution");
        const auto module = rosetta::parse(header, clang, selected.flags, selected.directory);
        require(module.functions.size() == 2 && module.functions[1].parameters[0].canonical == "int", "real compiler context not applied");
        require(rosetta::generate_lua(module, "api").diagnostics.empty(), "context-generated binding rejected");
        require(std::filesystem::current_path() == parent_cwd, "compiler invocation changed parent cwd");
        require(!std::filesystem::exists(marker), "database executable was run");
        for (const auto* output : {"object.o", "deps.d", "fragment.json", "trace.json", "diagnostics.dia"})
            require(!std::filesystem::exists(build / output), "parse wrote build artifact: " + std::string(output));
        const auto relative_clang = std::filesystem::relative(clang, parent_cwd);
        require(rosetta::parse(header, relative_clang.string(), selected.flags, selected.directory).functions.size() == 2,
            "relative parser executable was resolved against the child directory");

        auto quoted = command;
        quoted.erase("arguments");
        quoted["command"] = R"cmd(ignored -I"../include" -include '../config.hpp' -DFEATURE_API=1 -std=c++20 "-DMSG=\"hello world\"" -o object.o -c '../src/unit.cpp')cmd";
        write(Json::array({quoted}));
        const auto literal = rosetta::load_compilation_command(database);
        require(std::find(literal.flags.begin(), literal.flags.end(), "-DMSG=\"hello world\"") != literal.flags.end(), "literal command quoting changed");
        require(rosetta::parse(header, clang, literal.flags, literal.directory).functions.size() == 2, "quoted command parse failed");
        quoted["command"] = "ignored '-DLITERAL=$HOME' -c ../src/unit.cpp";
        write(Json::array({quoted}));
        require(rosetta::load_compilation_command(database).flags[0] == "-DLITERAL=$HOME", "single-quoted literal was expanded");
        quoted["command"] = std::string("ignored ") + char(92) + "\n -c ../src/unit.cpp";
        write(Json::array({quoted}));
        require(rosetta::load_compilation_command(database).flags.empty(), "line continuation made an empty operand");
        auto preferred = command;
        preferred["command"] = "ignored $(touch must-not-run)";
        write(Json::array({preferred}));
        require(rosetta::load_compilation_command(database).flags == selected.flags, "arguments array was not preferred");

        auto second = command;
        second["arguments"][5] = "-DFEATURE_API=0";
        write(Json::array({command, second}));
        rejects([&] { rosetta::load_compilation_command(database); }, "ambiguous");
        rejects([&] { rosetta::load_compilation_command(database, unit); }, "ambiguous");
        require(rosetta::load_compilation_command(database, unit, 1).index == 1, "duplicate configuration selector failed");
        rejects([&] { rosetta::load_compilation_command(database, other, 1); }, "no compilation command");
        rejects([&] { rosetta::load_compilation_command(database, {}, 7); }, "no compilation command");
        auto different = command;
        different["file"] = "../src/other.cpp";
        different["arguments"] = {"ccache", "ignored", "-c", "../src/other.cpp"};
        write(Json::array({command, different}));
        require(rosetta::load_compilation_command(database, unit).file == unit, "unselected launcher blocked selection");
        rejects([&] { rosetta::load_compilation_command(database, other); }, "launchers");

        write(Json::array({command}));
        const auto generated = root / "generated.cpp", ir_path = root / "api.json";
        const auto cli = [&](std::vector<std::string> extra, std::string output = {}) {
            std::vector<std::string> arguments{header.string(), "--module", "api", "--clang", clang, "--compile-commands", build.string(),
                "--translation-unit", unit.string(), "--dump-ir", ir_path.string(), "-o", output.empty() ? generated.string() : output};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            std::ostringstream out, error;
            const auto status = rosetta::run_cli(arguments, out, error);
            return std::pair<int, std::string>{status, error.str()};
        };
        const auto result = cli({"--", "-UFEATURE_API", "-DFEATURE_API=0"});
        require(result.first == 0, result.second);
        const auto ir = Json::parse(rosetta::detail::read_file(ir_path));
        require(ir["functions"].size() == 1 && ir["functions"][0]["name"] == "base", "CLI override precedence failed");
        require(ir["parse_context"]["directory"] == build.string() && ir["parse_context"]["translation_unit"] == unit.string() &&
            ir["parse_context"]["compilation_database"] == database.string() && ir["parse_context"]["command_index"] == 0,
            "IR compilation provenance missing");
        require(cli({"--", "-std=c++17"}).first != 0, "explicit standard override was ignored");
        const auto saved_database = rosetta::detail::read_file(database), saved_source = rosetta::detail::read_file(unit);
        require(cli({}, database.string()).second.find("distinct paths") != std::string::npos &&
            cli({}, unit.string()).second.find("distinct paths") != std::string::npos, "compile inputs were not protected");
        require(rosetta::detail::read_file(database) == saved_database && rosetta::detail::read_file(unit) == saved_source, "compile inputs overwritten");

        for (const auto& syntax : std::vector<std::string>{"ignored $(touch marker) -c ../src/unit.cpp", "ignored -c ../src/unit.cpp; touch marker",
            "ignored \"unterminated", std::string("ignored ") + char(92)}) {
            quoted["command"] = syntax; write(Json::array({quoted}));
            rejects([&] { rosetta::load_compilation_command(database); }, "compilation command");
        }
        for (const std::vector<std::string>& arguments : {
            std::vector<std::string>{"compiler", "@flags.rsp", "../src/unit.cpp"},
            {"compiler", "-include", "@flags.rsp", "../src/unit.cpp"},
            {"compiler", "-Xclang", "-load", "../src/unit.cpp"}, {"compiler", "-o"}, {"compiler", "-I"},
            {"compiler", "-DVALUE=1"}, {"compiler", "other.cpp", "../src/unit.cpp"},
            {"compiler", std::string("-DVALUE=x\0y", 11), "../src/unit.cpp"}}) {
            auto invalid = command; invalid["arguments"] = arguments; write(Json::array({invalid}));
            rejects([&] { rosetta::load_compilation_command(database); }, "compilation");
        }
        for (Json invalid : {Json::array(), Json::object(), Json::array({Json{{"file", "unit.cpp"}}})}) {
            write(std::move(invalid)); rejects([&] { rosetta::load_compilation_command(database); }, "compilation");
        }
        rosetta::detail::write_file(database, "invalid json");
        rejects([&] { rosetta::load_compilation_command(database); }, "invalid compilation database");
        const auto executable = std::filesystem::absolute(argv[0]).string();
        const auto child = rosetta::detail::run_process({executable, "--cwd"}, build);
        require(child.exit_code == 0 && child.output == build.string(), "child working directory was ignored");
        const auto missing = rosetta::detail::run_process({executable, "--cwd"}, build / "missing");
        require(missing.exit_code == 126 && missing.error.find("working directory") != std::string::npos, "chdir failure was not reported");
        require(std::filesystem::current_path() == parent_cwd, "parent cwd changed");
        rejects([&] { rosetta::detail::run_process({executable, std::string("x\0y", 3)}); }, "NUL");
        rejects([&] { rosetta::detail::run_process({executable, "--cwd"}, std::filesystem::path(build.string() + std::string("\0bad", 4))); }, "NUL");
        std::ostringstream out, error;
        for (const std::string selector : {"--translation-unit", "--command-index"}) {
            out.str("");
            error.str("");
            require(rosetta::run_cli({header.string(), "--module", "api", "-o", generated.string(), selector, "0"}, out, error) == 1 &&
                error.str().find("require --compile-commands") != std::string::npos, "orphan database selector accepted");
        }
        std::cout << "Compilation database selection, literal argv, context and output guards passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
