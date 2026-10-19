#include "process.hpp"
#include <iostream>
#include <stdexcept>
namespace {
namespace fs = std::filesystem;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void run(const std::vector<std::string>& command) {
    const auto result = rosetta::detail::run_process(command);
    require(result.exit_code == 0, result.error + result.output);
}
void replace(const fs::path& path, const std::string& from, const std::string& to) {
    auto text = rosetta::detail::read_file(path);
    const auto offset = text.find(from);
    require(offset != std::string::npos, "fixture replacement absent");
    text.replace(offset, from.size(), to);
    rosetta::detail::write_file(path, text);
}
std::string quoted(const fs::path& path) { return "[==[" + path.string() + "]==]"; }
}
int main(int argc, char** argv) {
    if (argc != 5) return 1;
    try {
        const std::string clang = argv[1], cmake = argv[2], ninja = argv[3];
        const fs::path source = argv[4];
        rosetta::detail::TemporaryDirectory temporary;
        const auto root = temporary.path() / "native project with spaces";
        const auto build = root / "build";
        fs::create_directories(root / "examples");
        fs::copy(source / "examples/modules", root / "examples/modules", fs::copy_options::recursive);
        for (const char* file : {"pch.cpp", "pch_config.hpp"}) fs::copy_file(source / "examples" / file, root / "examples" / file);
        rosetta::detail::write_file(root / "examples/demo.hpp", R"cpp(
#pragma once
#include <string>
namespace demo {
bool native_modules_available(); int native_module_shift(int); int native_header_unit_answer(); int native_pch_shift(int);
std::string native_module_text(std::string, bool); int native_module_live();
}
)cpp");
        rosetta::detail::write_file(root / "main.cpp", R"cpp(
#include "examples/demo.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    if (!demo::native_modules_available()) {
        try { (void)demo::native_module_shift(40); } catch (const std::runtime_error&) { return demo::native_pch_shift(40) == 42 ? 0 : 1; }
        return 1;
    }
    if (demo::native_module_text("x", false) != "xx") return 2;
    try { (void)demo::native_module_text("x", true); return 3; } catch (const std::runtime_error&) {}
    if (demo::native_module_live() != 0) return 4;
    std::cout << demo::native_module_shift(40) << ':' << demo::native_header_unit_answer() << ':' << demo::native_pch_shift(40);
}
)cpp");
        rosetta::detail::write_file(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(native_fixture LANGUAGES CXX)\n"
            "set(CMAKE_CXX_EXTENSIONS OFF)\nset(ROSETTA_WARNINGS_AS_ERRORS ON)\n"
            "function(rosetta_warnings target)\n target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)\nendfunction()\n"
            "include(" + quoted(source / "cmake/NativeModuleExample.cmake") + ")\n"
            "add_executable(probe main.cpp)\ntarget_link_libraries(probe PRIVATE rosetta_module_native rosetta_pch_native)\n");
        run({cmake, "-S", root.string(), "-B", build.string(), "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja,
            "-DCMAKE_CXX_COMPILER=" + clang, "-DCMAKE_BUILD_TYPE=Debug", "-DROSETTA_BUILD_MODULE_EXAMPLE=ON"});
        const auto compile = [&] { run({cmake, "--build", build.string(), "--parallel", "2"}); };
        const auto result = [&](const std::string& expected) {
            const auto output = rosetta::detail::run_process({(build / "probe").string()});
            require(output.exit_code == 0 && output.output == expected, "native module/PCH output: " + output.output + output.error);
        };
        compile(); result("42:42:42");
        const auto program_time = fs::last_write_time(build / "probe");
        const auto bmi_time = fs::last_write_time(build / "native-modules/Debug/library.pcm");
        compile(); require(fs::last_write_time(build / "probe") == program_time &&
            fs::last_write_time(build / "native-modules/Debug/library.pcm") == bmi_time, "no-op build rebuilt native modules");
        // A partition edit must propagate through its re-export and consumers.
        replace(root / "examples/modules/operations.cppm", "return value + Bias;", "return value + Bias - 1;");
        compile(); result("41:42:42");
        replace(root / "examples/modules/header_unit.hpp", "return 42;", "return 43;");
        compile(); result("41:43:42");
        replace(root / "examples/pch_config.hpp", "BIAS 2", "BIAS 3");
        compile(); result("41:43:43");
        rosetta::detail::write_file(root / "private.cpp", "import rosetta_demo_native; int main() { Record hidden; }\n");
        const auto hidden = rosetta::detail::run_process({clang, "-std=c++20", "-fsyntax-only", "-fPIC",
            "-fmodule-file=rosetta_demo_native=" + (build / "native-modules/Debug/library.pcm").string(),
            "-fmodule-file=rosetta_demo_native:operations=" + (build / "native-modules/Debug/operations.pcm").string(), (root / "private.cpp").string()});
        require(hidden.exit_code != 0 && (hidden.error.find("unknown type name 'Record'") != std::string::npos ||
            hidden.error.find("use of undeclared identifier 'Record'") != std::string::npos), "module implementation type escaped exports");
        // Debug and Release must never reuse the same BMI/PCH artifacts.
        const auto multi = root / "multi";
        run({cmake, "-S", root.string(), "-B", multi.string(), "-G", "Ninja Multi-Config", "-DCMAKE_MAKE_PROGRAM=" + ninja,
            "-DCMAKE_CXX_COMPILER=" + clang, "-DCMAKE_CONFIGURATION_TYPES=Debug;Release", "-DROSETTA_BUILD_MODULE_EXAMPLE=ON"});
        run({cmake, "--build", multi.string(), "--config", "Debug", "--parallel", "2"});
        const auto debug_time = fs::last_write_time(multi / "native-modules/Debug/library.pcm");
        run({cmake, "--build", multi.string(), "--config", "Release", "--parallel", "2"});
        require(fs::exists(multi / "native-modules/Release/library.pcm") &&
            fs::last_write_time(multi / "native-modules/Debug/library.pcm") == debug_time, "configurations shared a BMI");
        for (const char* config : {"Debug", "Release"}) {
            const auto output = rosetta::detail::run_process({(multi / config / "probe").string()});
            require(output.exit_code == 0 && output.output == "41:43:43", "multi-config native result mismatch");
        }
        // Disabled capability remains explicit and still builds the PCH-backed API.
        replace(root / "examples/pch_config.hpp", "BIAS 3", "BIAS 2");
        run({cmake, "-S", root.string(), "-B", build.string(), "-DROSETTA_BUILD_MODULE_EXAMPLE=OFF"});
        compile(); run({(build / "probe").string()});
        std::cout << "Native module/header-unit/PCH build graph passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
