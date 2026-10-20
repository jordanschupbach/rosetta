#include "rosetta/frontend.hpp"
#include "rosetta/fortran.hpp"
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
        const auto header = temporary.path() / "fortran api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Fortran result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Fortran parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Fortran parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Fortran parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Fortran parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Fortran parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Fortran parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Fortran parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Fortran parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Fortran parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Fortran field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Fortran field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Fortran parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Fortran parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_fortran(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_fortran(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "function sentinel(") && !contains(partial.interface, "function unsupported("), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_fortran(module, "api", false, policy);
        require(contains(generated.source, "rosetta_api_call") && contains(generated.interface, "module api") &&
            contains(generated.interface, "rs_id") && contains(generated.source, "arity(count, 0, 1)"), "ABI/owners/defaults missing");
        require(!contains(generated.interface, "\"hidden\"") && !contains(generated.source, "->secret"), "private members exposed");
        const auto again = rosetta::generate_fortran(module, "api", false, policy);
        require(generated.source == again.source && generated.interface == again.interface, "nondeterministic output");
        rejects([&] { rosetta::generate_fortran(module, "bad-name", false, policy); }, "module name");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_fortran(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_fortran(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_fortran(module, "api", false, policy); }, "borrowed Fortran");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_fortran(module, "api", true, policy); }, "callback or enum flag policies");
        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_fortran(collision, "api"); }, "name collision");
        const auto runtime_name = parse("int api();");
        rejects([&] { rosetta::generate_fortran(runtime_name, "api"); }, "name collision");
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_fortran(reserved, "api", true, policy); }, "reserved or duplicate");

        const auto case_collision = parse("int value(); int Value();");
        rejects([&] { rosetta::generate_fortran(case_collision, "api"); }, "case insensitive");
        rejects([&] { rosetta::generate_fortran(module, std::string(64, 'a'), true); }, "module name");
        rejects([&] { rosetta::generate_fortran(parse("int rs_internal();"), "api"); }, "reserved Fortran export");
        rejects([&] { rosetta::generate_fortran(parse("int c_loc();"), "api"); }, "reserved Fortran export");
        rejects([&] { rosetta::generate_fortran(parse("int len();"), "api"); }, "reserved Fortran export");
        const auto members = parse("struct Object { void value(); void Value(); };");
        rejects([&] { rosetta::generate_fortran(members, "api", false, policy); }, "case insensitive");
        const auto accessors = parse("struct Object { void get_field(); int field; };");
        rejects([&] { rosetta::generate_fortran(accessors, "api", false, policy); }, "duplicate Fortran member");
        const auto readonly = rosetta::generate_fortran(parse("struct Object { const int initial = 42; };"), "api", false, policy);
        require(contains(readonly.interface, "get_initial") && !contains(readonly.interface, "set_initial"), "readonly setter exposed");
        const auto long_names = rosetta::generate_fortran(parse("int " + std::string(60, 'x') + "(int, int, int, int, int);"), "api");
        std::istringstream lines(long_names.interface); std::string line;
        while (std::getline(lines, line)) require(line.size() <= 132, "Fortran line exceeded standard length");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "fortran bindings.cpp";
        const auto interface = temporary.path() / "api.f90";
        const auto dep = temporary.path() / "fortran bindings.d";
        const auto ir = temporary.path() / "fortran bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "fortran", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
            contains(rosetta::detail::read_file(interface), "module api") && std::filesystem::exists(ir), "Fortran CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.f90"), "Fortran interface dependency missing");
        const auto custom = temporary.path() / "custom.f90";
        auto custom_args = args; custom_args.insert(custom_args.end(), {"--fortran-output", custom.string()});
        require(rosetta::run_cli(custom_args, out, error) == 0 && std::filesystem::exists(custom), "custom interface missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R option accepted for Fortran");
        for (const auto& path : {header, output, dep, ir}) {
            invalid = args; invalid.insert(invalid.end(), {"--fortran-output", path.string()});
            require(rosetta::run_cli(invalid, out, error) == 1, "interface collision accepted");
        }
        invalid = {header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--fortran-output", custom.string()};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "requires --language fortran"), "Fortran-only option accepted for Lua");
        std::cout << "PASS Fortran policies, defaults, access, determinism, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
