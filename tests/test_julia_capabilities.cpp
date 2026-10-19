#include "rosetta/frontend.hpp"
#include "rosetta/julia.hpp"
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
        const auto header = temporary.path() / "julia api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Julia result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Julia parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Julia parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Julia parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Julia parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Julia parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Julia parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Julia parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Julia parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Julia parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Julia field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Julia field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Julia parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Julia parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_julia(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_julia(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "sentinel(args...)") && !contains(partial.interface, "unsupported(args...)"), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_julia(module, "api", false, policy);
        require(contains(generated.source, "rosetta_api_call") && contains(generated.interface, "module api") &&
            contains(generated.interface, "finalizer(close, owner)") && contains(generated.source, "arity(count, 0, 1)"), "ABI/owners/defaults missing");
        require(!contains(generated.interface, "\"hidden\"") && !contains(generated.source, "->secret"), "private members exposed");
        const auto again = rosetta::generate_julia(module, "api", false, policy);
        require(generated.source == again.source && generated.interface == again.interface, "nondeterministic output");
        rejects([&] { rosetta::generate_julia(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_julia(module, "module", false, policy); }, "reserved Julia");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_julia(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_julia(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_julia(module, "api", false, policy); }, "borrowed Julia");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_julia(module, "api", true, policy); }, "callback or enum flag policies");
        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_julia(collision, "api"); }, "name collision");
        const auto runtime_name = parse("int C_NULL();");
        rejects([&] { rosetta::generate_julia(runtime_name, "api"); }, "reserved Julia export");
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_julia(reserved, "api", true, policy); }, "reserved or duplicate");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "julia bindings.cpp";
        const auto interface = temporary.path() / "julia bindings.jl";
        const auto dep = temporary.path() / "julia bindings.d";
        const auto ir = temporary.path() / "julia bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "julia", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
            contains(rosetta::detail::read_file(interface), "module api") && std::filesystem::exists(ir), "Julia CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "julia\\ bindings.jl"), "Julia interface dependency missing");
        const auto custom = temporary.path() / "custom.jl";
        auto custom_args = args; custom_args.insert(custom_args.end(), {"--julia-output", custom.string()});
        require(rosetta::run_cli(custom_args, out, error) == 0 && std::filesystem::exists(custom), "custom interface missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R option accepted for Julia");
        for (const auto& path : {header, output, dep, ir}) {
            invalid = args; invalid.insert(invalid.end(), {"--julia-output", path.string()});
            require(rosetta::run_cli(invalid, out, error) == 1, "interface collision accepted");
        }
        invalid = {header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--julia-output", custom.string()};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "requires --language julia"), "Julia-only option accepted for Lua");
        std::cout << "PASS Julia policies, defaults, access, determinism, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
