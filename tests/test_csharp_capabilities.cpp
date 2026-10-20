#include "rosetta/frontend.hpp"
#include "rosetta/csharp.hpp"
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
        const auto header = temporary.path() / "csharp api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported C# result type"},
            {"opaque enum", "enum class Mode : int;", "require named values"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported C# parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported C# parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported C# parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported C# parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported C# parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported C# parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported C# parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported C# parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported C# parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported C# field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported C# field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported C# parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported C# parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_csharp(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_csharp(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "@sentinel()") && !contains(partial.interface, "@unsupported("), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_csharp(module, "api", false, policy);
        require(contains(generated.source, "rosetta_api_call") && contains(generated.interface, "class @api") &&
            contains(generated.interface, "_RosettaHandle") && contains(generated.source, "arity(count, 0, 1)"), "ABI/owners/defaults missing");
        require(!contains(generated.interface, "\"hidden\"") && !contains(generated.source, "->secret"), "private members exposed");
        const auto again = rosetta::generate_csharp(module, "api", false, policy);
        require(generated.source == again.source && generated.interface == again.interface, "nondeterministic output");
        rejects([&] { rosetta::generate_csharp(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_csharp(module, "api", false, policy, "Bad..Namespace"); }, "namespace");
        require(contains(rosetta::generate_csharp(module, "class", false, policy, "Rosetta.class").interface, "namespace @Rosetta.@class"), "keyword escaping missing");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_csharp(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_csharp(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_csharp(module, "api", false, policy); }, "borrowed C#");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_csharp(module, "api", true, policy); }, "callback or enum flag policies");
        const auto enum_module = parse("namespace ns { enum class Mode { low = -2, next, zero, high = 42, alias = high }; Mode echo(Mode); } enum class Big : unsigned long long { max = 18446744073709551615ULL }; Big echo_big(Big);");
        const auto enum_api = rosetta::generate_csharp(enum_module, "api");
        require(contains(enum_api.interface, "enum @ns__Mode : long") && contains(enum_api.interface, "@next = -1L") &&
            contains(enum_api.interface, "@alias = 42L") && contains(enum_api.interface, "@max = 18446744073709551615UL") &&
            contains(enum_api.interface, "@ns__Mode @ns__echo(@ns__Mode arg0)"), "typed enum values missing");
        require(enum_module.enumerations[0].enumerator_values == std::vector<std::string>({"-2", "-1", "0", "42", "42"}), "compiler enum values missing");
        rejects([&] { rosetta::generate_csharp(parse("enum class Mode { value__ };"), "api"); }, "reserved C# enum member");
        rejects([&] { rosetta::generate_csharp(parse("enum class Mode { a }; int Mode();"), "api"); }, "name collision");
        rejects([&] { rosetta::generate_csharp(parse("enum class Mode : __int128 { a = 1 };"), "api"); }, "64-bit integer");
        rosetta::detail::write_file(temporary.path() / "enums.hpp", "enum class External { first = 7, second }; enum class Unused { value };\n");
        const auto dependency_enum = rosetta::generate_csharp(parse("#include \"enums.hpp\"\nExternal echo(External);"), "api");
        require(contains(dependency_enum.interface, "enum @External") && !contains(dependency_enum.interface, "enum @Unused"), "dependency enum selection incorrect");

        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_csharp(collision, "api"); }, "name collision");
        const auto runtime_name = parse("int api();");
        rejects([&] { rosetta::generate_csharp(runtime_name, "api"); }, "name collision");
        const auto reserved = parse("struct Object { void Dispose(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_csharp(reserved, "api", true, policy); }, "reserved or duplicate");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "csharp bindings.cpp";
        const auto interface = temporary.path() / "api.cs";
        const auto dep = temporary.path() / "csharp bindings.d";
        const auto ir = temporary.path() / "csharp bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "csharp", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
            contains(rosetta::detail::read_file(interface), "class @api") && std::filesystem::exists(ir), "C# CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.cs"), "C# interface dependency missing");
        const auto custom = temporary.path() / "custom.cs";
        auto custom_args = args; custom_args.insert(custom_args.end(), {"--csharp-output", custom.string()});
        require(rosetta::run_cli(custom_args, out, error) == 0 && std::filesystem::exists(custom), "custom interface missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R option accepted for C#");
        for (const auto& path : {header, output, dep, ir}) {
            invalid = args; invalid.insert(invalid.end(), {"--csharp-output", path.string()});
            require(rosetta::run_cli(invalid, out, error) == 1, "interface collision accepted");
        }
        invalid = {header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--csharp-output", custom.string()};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language csharp"), "C#-only option accepted for Lua");
        std::cout << "PASS C# policies, defaults, access, determinism, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
