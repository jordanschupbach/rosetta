// Generator and CLI checks of the Visual Basic backend (--language visual-basic) that need no .NET
// runtime: output shape, keyword escaping, case-insensitive collisions, options and artifacts.
#include "rosetta/frontend.hpp"
#include "rosetta/visual_basic.hpp"
#include "rosetta/cli.hpp"
#include "process.hpp"
#include <functional>
#include <iostream>
#include <sstream>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
void rejects(const std::function<void()>& action, const std::string& expected) {
    try { action(); }
    catch (const rosetta::BindingError& error) {
        require(contains(error.what(), expected), "expected '" + expected + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + expected);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        rosetta::detail::TemporaryDirectory temporary;
        const auto header = temporary.path() / "vb api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++17"});
        };
        const auto policy_file = temporary.path() / "policy.json";
        rosetta::detail::write_file(policy_file, "{\"version\": 1, \"select\": \"all\", \"names\": \"flat\"}\n");
        const auto policy = rosetta::load_policy(policy_file);

        // Output shape, keywords and determinism.
        const auto module = parse("enum class Mode { on, On, next };\n"
                                  "struct Error { int next() { return 1; } int end = 2; int get(int step) const { return step; } };\n"
                                  "struct Point { double x = 0; bool operator==(const Point&) const { return true; } };\n"
                                  "inline int twice(int value = 21) { return 2 * value; }\n");
        const auto generated = rosetta::generate_visual_basic(module, "api", false, policy);
        const auto& vb = generated.interface;
        require(contains(vb, "Option Strict On") && contains(vb, "Namespace Global.Api") && contains(vb, "Namespace Global.Api.RosettaRuntime"), "file layout");
        require(contains(vb, "Partial Public Class [Error]") && contains(vb, "Public Function [Next]() As Integer") &&
                contains(vb, "Public Property [End] As Integer") && contains(vb, "Function [Get]([step] As Integer) As Integer"), "keyword escaping");
        require(contains(vb, "[on] = 0L") && contains(vb, "On_ = 1L") && contains(vb, "[next] = 2L"), "enum labels that differ in case");
        require(contains(vb, "Public Shared Operator =(self As Point, x2 As Point) As Boolean") &&
                contains(vb, "Public Shared Operator <>(self As Point, x2 As Point) As Boolean"), "= and its partner <>");
        require(contains(vb, "Public Shared Function Twice() As Integer") && contains(vb, "Public Shared Function Twice(value As Integer) As Integer"), "default arguments");
        require(contains(generated.source, "#define ROSETTA_FULL_VB") && contains(generated.source, "rosetta_api_call"), "C++ library");
        require(contains(generated.project, "<RootNamespace></RootNamespace>") && contains(generated.project, "<AssemblyName>Api</AssemblyName>"), "project");
        const auto again = rosetta::generate_visual_basic(module, "api", false, policy);
        require(generated.source == again.source && generated.interface == again.interface, "nondeterministic output");
        std::cout << "PASS VB output, keywords and determinism\n";

        // Options.
        rosetta::VisualBasicOptions options;
        options.managed_namespace = "Company.Bindings";
        options.functions_class = "Native";
        options.library = "api_native";
        options.parts = 3;
        const auto custom = rosetta::generate_visual_basic(module, "api", false, policy, options);
        require(contains(custom.interface, "Namespace Global.Company.Bindings") && contains(custom.interface, "Class Native") &&
                contains(custom.interface, "\"api_native.so\"") && custom.parts.size() == 2, "options");
        options = {}; options.managed_namespace = "Api.End";
        rejects([&] { rosetta::generate_visual_basic(module, "api", false, policy, options); }, "namespace");
        options = {}; options.functions_class = "Next";
        rejects([&] { rosetta::generate_visual_basic(module, "api", false, policy, options); }, "functions class");
        options = {}; options.parts = 0;
        rejects([&] { rosetta::generate_visual_basic(module, "api", false, policy, options); }, "parts");
        rejects([&] { rosetta::generate_visual_basic(module, "bad-name", false, policy); }, "module name");
        std::cout << "PASS VB options\n";

        // Type names that differ only in case collide in VB.
        const auto cased = parse("struct point { int x = 0; }; struct Point { int y = 0; }; inline int use(const Point& p) { return p.y; }");
        rejects([&] { rosetta::generate_visual_basic(cased, "api", false, policy); }, "case-insensitive");
        const auto partial = rosetta::generate_visual_basic(cased, "api", true, policy);
        require(contains(partial.interface, "Partial Public Class point") && !contains(partial.interface, "Partial Public Class Point"), "collided class dropped");
        rejects([&] { rosetta::generate_visual_basic(parse("struct Functions {};"), "api", false, policy); }, "collides with another type");
        rejects([&] { rosetta::generate_visual_basic(parse("struct NATIVEOBJECT {};"), "api", false, policy); }, "collides with another type");
        std::cout << "PASS VB case-insensitive collisions\n";

        // CLI and artifacts.
        parse("inline int value(int x = 42) { return x; }");
        const auto output = temporary.path() / "vb bindings.cpp";
        const auto dep = temporary.path() / "vb bindings.d";
        const auto interface = temporary.path() / "api.vb";
        const auto project = temporary.path() / "Api.vbproj";
        const std::vector<std::string> args{header.string(), "--language", "visual-basic", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
                contains(rosetta::detail::read_file(interface), "Public Shared Function Value("), "VB CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.vb"), "VB interface dependency missing");
        auto with_project = args;
        with_project[2] = "vb";
        with_project.insert(with_project.end(), {"--vb-project", project.string(), "--parts", "2"});
        require(rosetta::run_cli(with_project, out, error) == 0 && std::filesystem::exists(project) &&
                std::filesystem::exists(temporary.path() / "vb bindings_1.cpp"), "vb alias, project and parts");
        for (const auto& path : {header, output, dep}) {
            auto invalid = args; invalid.insert(invalid.end(), {"--vb-output", path.string()});
            require(rosetta::run_cli(invalid, out, error) == 1, "interface collision accepted");
        }
        const std::vector<std::string> lua{header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--vb-namespace", "Api"};
        require(rosetta::run_cli(lua, out, error) == 1 && contains(error.str(), "require --language visual-basic"), "VB-only option accepted for Lua");
        std::cout << "PASS VB CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
