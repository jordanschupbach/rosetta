#include "rosetta/frontend.hpp"
#include "rosetta/cli.hpp"
#include "dependencies.hpp"
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
namespace fs = std::filesystem;
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
template<class Operation> void rejects(Operation operation, const std::string& expected) {
    try { operation(); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(expected) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + expected);
}
bool contains(const std::vector<std::string>& paths, const fs::path& file) {
    return std::find(paths.begin(), paths.end(), file.string()) != paths.end();
}
std::string cmake_argument(const fs::path& path) { return "[==[" + path.string() + "]==]"; }
void changed_file(const fs::path& input, const std::string& text, const fs::path& output) {
    rosetta::detail::write_file(input, text);
    // Coarse timestamp filesystems need a new tick to prove an input edit.
    if (fs::last_write_time(input) <= fs::last_write_time(output)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        rosetta::detail::write_file(input, text);
    }
    require(fs::last_write_time(input) > fs::last_write_time(output), "test filesystem did not advance an input timestamp");
}
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 6) return 1;
    try {
        const std::string clang = argv[1];
        rosetta::detail::TemporaryDirectory temporary;
        const auto root = temporary.path() / "project with spaces";
        const auto build = root / "work";
        fs::create_directories(build);
        const auto header = root / "include" / "api.hpp";
        const auto types = root / "include" / "types #$name:.hpp";
        const auto inactive = root / "include" / "inactive.hpp";
        const auto config = root / "config file.hpp";
        const auto policy = root / "policy #$.json";
        rosetta::detail::write_file(types, "using Number = int;\n");
        rosetta::detail::write_file(inactive, "#error inactive include was selected\n");
        rosetta::detail::write_file(config, "#define FORCED_CONTEXT 1\n");
        rosetta::detail::write_file(policy, "{\"version\":1}\n");
        rosetta::detail::write_file(header, "#include \"types #$name:.hpp\"\n#if 0\n#include \"inactive.hpp\"\n#endif\n"
            "#ifndef FORCED_CONTEXT\n#error missing forced context\n#endif\nNumber value(Number);\n");
        const std::vector<std::string> flags{"-std=c++17", "-include", "../config file.hpp"};
        const auto module = rosetta::parse(header, clang, flags, build, true);
        require(module.dependencies.size() == 3 && contains(module.dependencies, header) && contains(module.dependencies, types) &&
            contains(module.dependencies, config) && !contains(module.dependencies, inactive), "compiler dependency discovery omitted or invented inputs");
        require(module.parse_context.flags == flags && module.functions[0].result.canonical == "int", "dependency collection changed parsing context");
        const auto alternate = root / "alternate";
        const auto local_config = alternate / "local.hpp";
        rosetta::detail::write_file(local_config, "#define FORCED_CONTEXT 1\n");
        rosetta::detail::write_file(build / "local.hpp", "#define FORCED_CONTEXT 2\n");
        for (const auto& working_flags : {std::vector<std::string>{"-working-directory", "../alternate", "-include", "local.hpp"},
                                         std::vector<std::string>{"-working-directory=../alternate", "-include", "local.hpp"}}) {
            const auto changed = rosetta::parse(header, clang, working_flags, build, true);
            require(contains(changed.dependencies, local_config) && !contains(changed.dependencies, build / "local.hpp"),
                "compiler working-directory override selected a false dependency");
        }
        const auto ir = nlohmann::json::parse(rosetta::serialize_ir(module));
        require(ir["dependencies"] == module.dependencies, "IR omitted compiler inputs");
        const auto rule = rosetta::detail::dependency_rule("rosetta-dependencies", module.dependencies);
        require(rosetta::detail::compiler_dependencies(rule, build) == module.dependencies, "escaped dependency names did not round trip");
        require(rule.find("\\#$$name:") != std::string::npos, "hash/dollar dependency escaping missing");
        require(rosetta::detail::compiler_dependencies(rule, build) == rosetta::parse(header, clang, flags, build, true).dependencies,
            "dependency output depends on temporary compiler paths");
        rejects([&] { rosetta::detail::compiler_dependencies("other: file\n", build); }, "unexpected compiler dependency target");
        rejects([&] { rosetta::detail::compiler_dependencies("rosetta-dependencies: $(variable)\n", build); }, "variables");
        rejects([&] { rosetta::detail::dependency_rule("bad:target", module.dependencies); }, "colon");
        rejects([&] { rosetta::detail::dependency_rule("target", {"bad\\name"}); }, "literal backslashes");
        rejects([&] { rosetta::parse(header, clang, {"-MD"}, build, true); }, "managed by Rosetta");

        const auto output = root / "generated bindings.cpp";
        const auto dump = root / "generated.json";
        const auto depfile = root / "generated.d";
        std::vector<std::string> cli{header.string(), "--module", "fixture", "--clang", clang, "-o", output.string(),
            "--dump-ir", dump.string(), "--depfile", depfile.string(), "--policy", policy.string(), "--", "-include", config.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(cli, out, error) == 0, error.str());
        const auto deptext = rosetta::detail::read_file(depfile);
        const auto source_text = rosetta::detail::read_file(output);
        const auto ir_text = rosetta::detail::read_file(dump);
        require(deptext.find("generated\\ bindings.cpp:") != std::string::npos && deptext.find("policy\\ \\#$$.json") != std::string::npos,
            "CLI depfile omitted escaped target or policy");
        const auto original_header = rosetta::detail::read_file(header);
        for (const auto& broken : {"invalid C++ syntax;\n", "template<class T> T unsupported(T);\n"}) {
            rosetta::detail::write_file(header, broken);
            require(rosetta::run_cli(cli, out, error) != 0 && rosetta::detail::read_file(output) == source_text &&
                rosetta::detail::read_file(dump) == ir_text && rosetta::detail::read_file(depfile) == deptext,
                "failed parsing/generation changed a previously valid artifact set");
        }
        rosetta::detail::write_file(header, original_header);
        const auto protected_text = rosetta::detail::read_file(types);
        for (const auto& destination : {header, types, policy, dump, output}) {
            auto invalid = cli;
            const auto index = std::find(invalid.begin(), invalid.end(), "--depfile") - invalid.begin() + 1;
            invalid[static_cast<std::size_t>(index)] = destination.string();
            std::ostringstream diagnostics;
            require(rosetta::run_cli(invalid, out, diagnostics) != 0 && diagnostics.str().find("distinct paths") != std::string::npos,
                "depfile input/output collision was not rejected");
        }
        const auto link = root / "hardlink.d";
        fs::create_hard_link(types, link);
        auto invalid = cli;
        invalid[static_cast<std::size_t>(std::find(invalid.begin(), invalid.end(), "--depfile") - invalid.begin() + 1)] = link.string();
        require(rosetta::run_cli(invalid, out, error) != 0 && rosetta::detail::read_file(types) == protected_text,
            "depfile overwrote an included header through a hard link");
        invalid = cli;
        invalid[static_cast<std::size_t>(std::find(invalid.begin(), invalid.end(), "-o") - invalid.begin() + 1)] = types.string();
        require(rosetta::run_cli(invalid, out, error) != 0 && rosetta::detail::read_file(types) == protected_text,
            "generated source overwrote an included compiler input");

        // Selected compilation commands use their own cwd; depfiles add the
        // database and selected source as generator inputs as well.
        const auto unit = root / "unit.cpp";
        const auto database = root / "compile_commands.json";
        rosetta::detail::write_file(unit, "#include \"include/api.hpp\"\n");
        rosetta::detail::write_file(database, nlohmann::json::array({{{"directory", build.string()}, {"file", unit.string()},
            {"arguments", {clang, "-include", "../config file.hpp", "-c", unit.string()}}}}).dump());
        require(rosetta::run_cli({header.string(), "--module", "fixture", "--clang", clang, "-o", output.string(),
            "--depfile", depfile.string(), "--compile-commands", database.string()}, out, error) == 0, error.str());
        require(rosetta::detail::read_file(depfile).find("compile_commands.json") != std::string::npos &&
            rosetta::detail::read_file(depfile).find("unit.cpp") != std::string::npos, "compilation context inputs missing from depfile");

        if (argc == 6) {
            const auto generated = root / "ninja" / "generated bindings.cpp";
            const auto generated_ir = root / "ninja" / "generated.json";
            const auto generated_dep = root / "ninja" / "generated.d";
            const std::string project = "cmake_minimum_required(VERSION 3.20)\nproject(dependency_fixture LANGUAGES NONE)\ninclude(" + cmake_argument(argv[5]) + ")\n"
                "rosetta_generate_lua(EXECUTABLE " + cmake_argument(argv[2]) + " HEADER " + cmake_argument(header) + " MODULE fixture CLANG " + cmake_argument(clang) +
                " OUTPUT " + cmake_argument(generated) + " IR " + cmake_argument(generated_ir) + " DEPFILE " + cmake_argument(generated_dep) +
                " POLICY " + cmake_argument(policy) + " COMPILE_COMMANDS " + cmake_argument(database) +
                " TRANSLATION_UNIT " + cmake_argument(unit) + " COMMAND_INDEX 0)\n"
                "add_custom_target(generate ALL DEPENDS " + cmake_argument(generated) + ")\n";
            rosetta::detail::write_file(root / "CMakeLists.txt", project);
            const auto configured = rosetta::detail::run_process({argv[3], "-S", root.string(), "-B", (root / "ninja").string(), "-G", "Ninja",
                "-DCMAKE_MAKE_PROGRAM=" + std::string(argv[4])});
            require(configured.exit_code == 0, configured.output + configured.error);
            const auto build_fixture = [&] {
                const auto result = rosetta::detail::run_process({argv[3], "--build", (root / "ninja").string(), "--", "-d", "explain", "-d", "keepdepfile"});
                require(result.exit_code == 0, result.output + result.error);
                return result.output + result.error;
            };
            const std::string regeneration = "Generating Lua bindings: fixture";
            require(build_fixture().find(regeneration) != std::string::npos, "fixture did not generate");
            const auto unchanged = build_fixture();
            std::string transformed;
            if (unchanged.find(regeneration) != std::string::npos)
                for (const auto& entry : fs::recursive_directory_iterator(root / "ninja" / "CMakeFiles"))
                    if (entry.path().extension() == ".d") transformed += "\ntransformed:\n" + rosetta::detail::read_file(entry.path());
            require(unchanged.find(regeneration) == std::string::npos, "unchanged depfile caused regeneration:\n" + unchanged +
                "\nraw depfile:\n" + rosetta::detail::read_file(generated_dep) + transformed);
            const auto leaf = root / "include" / "nested" / "leaf.hpp";
            rosetta::detail::write_file(leaf, "using Leaf = long;\n");
            changed_file(types, "#include \"nested/leaf.hpp\"\nusing Number = Leaf;\n", generated);
            require(build_fixture().find(regeneration) != std::string::npos, "included header edit did not regenerate bindings");
            const auto changed_ir = nlohmann::json::parse(rosetta::detail::read_file(generated_ir));
            require(changed_ir["functions"][0]["result"]["canonical"] == "long", "regeneration retained stale native types");
            require(build_fixture().find(regeneration) == std::string::npos, "regenerated depfile did not converge");
            changed_file(leaf, "using Leaf = double;\n", generated);
            require(build_fixture().find(regeneration) != std::string::npos, "new transitive dependency was not tracked");
            require(nlohmann::json::parse(rosetta::detail::read_file(generated_ir))["functions"][0]["result"]["canonical"] == "double",
                "transitive edit retained stale native types");
            changed_file(config, "#define FORCED_CONTEXT 2\n", generated);
            require(build_fixture().find(regeneration) != std::string::npos, "forced include edit did not regenerate bindings");
            changed_file(policy, "{ \"version\": 1 }\n", generated);
            require(build_fixture().find(regeneration) != std::string::npos, "policy edit did not regenerate bindings");
            changed_file(database, rosetta::detail::read_file(database) + "\n", generated);
            require(build_fixture().find(regeneration) != std::string::npos, "compilation database edit did not regenerate bindings");
            require(build_fixture().find(regeneration) == std::string::npos, "context regeneration did not converge");

            // The R backend has two generated sources; both must participate in
            // the same compiler-discovered dependency and incremental build.
            auto r_project = project;
            r_project.replace(r_project.find("rosetta_generate_lua"), std::string("rosetta_generate_lua").size(), "rosetta_generate_r");
            rosetta::detail::write_file(root / "CMakeLists.txt", r_project);
            const auto r_configured = rosetta::detail::run_process({argv[3], "-S", root.string(), "-B", (root / "ninja").string()});
            require(r_configured.exit_code == 0, r_configured.output + r_configured.error);
            const std::string r_regeneration = "Generating R bindings: fixture";
            require(build_fixture().find(r_regeneration) != std::string::npos, "R fixture did not generate");
            auto r_interface = generated;
            r_interface.replace_extension(".R");
            require(fs::exists(r_interface), "R interface was not generated");
            require(build_fixture().find(r_regeneration) == std::string::npos, "R depfile did not converge");
            fs::remove(r_interface);
            require(build_fixture().find(r_regeneration) != std::string::npos && fs::exists(r_interface), "missing R interface did not regenerate");
            changed_file(leaf, "using Leaf = float;\n", generated);
            require(build_fixture().find(r_regeneration) != std::string::npos, "R transitive input edit did not regenerate");
            require(nlohmann::json::parse(rosetta::detail::read_file(generated_ir))["functions"][0]["result"]["canonical"] == "float",
                "R regeneration retained stale types");
            require(build_fixture().find(r_regeneration) == std::string::npos, "R regeneration did not converge");
            auto python_project = project;
            python_project.replace(python_project.find("rosetta_generate_lua"), std::string("rosetta_generate_lua").size(), "rosetta_generate_python");
            rosetta::detail::write_file(root / "CMakeLists.txt", python_project);
            const auto python_configured = rosetta::detail::run_process({argv[3], "-S", root.string(), "-B", (root / "ninja").string()});
            require(python_configured.exit_code == 0, python_configured.output + python_configured.error);
            const std::string python_regeneration = "Generating Python bindings: fixture";
            require(build_fixture().find(python_regeneration) != std::string::npos, "Python fixture did not generate");
            require(rosetta::detail::read_file(generated).find("PyInit_fixture") != std::string::npos, "Python native source missing");
            require(build_fixture().find(python_regeneration) == std::string::npos, "Python depfile did not converge");
            changed_file(leaf, "using Leaf = int;\n", generated);
            require(build_fixture().find(python_regeneration) != std::string::npos, "Python transitive input edit did not regenerate");
            require(nlohmann::json::parse(rosetta::detail::read_file(generated_ir))["functions"][0]["result"]["canonical"] == "int", "Python regeneration retained stale types");
            require(build_fixture().find(python_regeneration) == std::string::npos, "Python regeneration did not converge");
            auto ruby_project = project;
            ruby_project.replace(ruby_project.find("rosetta_generate_lua"), std::string("rosetta_generate_lua").size(), "rosetta_generate_ruby");
            rosetta::detail::write_file(root / "CMakeLists.txt", ruby_project);
            const auto ruby_configured = rosetta::detail::run_process({argv[3], "-S", root.string(), "-B", (root / "ninja").string()});
            require(ruby_configured.exit_code == 0, ruby_configured.output + ruby_configured.error);
            const std::string ruby_regeneration = "Generating Ruby bindings: fixture";
            require(build_fixture().find(ruby_regeneration) != std::string::npos, "Ruby fixture did not generate");
            require(rosetta::detail::read_file(generated).find("Init_fixture") != std::string::npos, "Ruby native source missing");
            require(build_fixture().find(ruby_regeneration) == std::string::npos, "Ruby depfile did not converge");
            changed_file(leaf, "using Leaf = long;\n", generated);
            require(build_fixture().find(ruby_regeneration) != std::string::npos, "Ruby transitive input edit did not regenerate");
            require(nlohmann::json::parse(rosetta::detail::read_file(generated_ir))["functions"][0]["result"]["canonical"] == "long", "Ruby regeneration retained stale types");
            require(build_fixture().find(ruby_regeneration) == std::string::npos, "Ruby regeneration did not converge");
            auto octave_project = project;
            octave_project.replace(octave_project.find("rosetta_generate_lua"), std::string("rosetta_generate_lua").size(), "rosetta_generate_octave");
            rosetta::detail::write_file(root / "CMakeLists.txt", octave_project);
            const auto octave_configured = rosetta::detail::run_process({argv[3], "-S", root.string(), "-B", (root / "ninja").string()});
            require(octave_configured.exit_code == 0, octave_configured.output + octave_configured.error);
            const std::string octave_regeneration = "Generating Octave bindings: fixture";
            require(build_fixture().find(octave_regeneration) != std::string::npos, "Octave fixture did not generate");
            require(rosetta::detail::read_file(generated).find("DEFMETHOD_DLD(fixture,") != std::string::npos, "Octave native source missing");
            require(build_fixture().find(octave_regeneration) == std::string::npos, "Octave depfile did not converge");
            changed_file(leaf, "using Leaf = unsigned int;\n", generated);
            require(build_fixture().find(octave_regeneration) != std::string::npos, "Octave transitive input edit did not regenerate");
            require(nlohmann::json::parse(rosetta::detail::read_file(generated_ir))["functions"][0]["result"]["canonical"] == "unsigned int", "Octave regeneration retained stale types");
            require(build_fixture().find(octave_regeneration) == std::string::npos, "Octave regeneration did not converge");
        }
        std::cout << "Compiler dependencies, escaped paths, input protection and incremental regeneration passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
