#include "rosetta/frontend.hpp"
#include "rosetta/tcl.hpp"
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
        const auto header = temporary.path() / "tcl api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Tcl result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Tcl parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Tcl parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Tcl parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Tcl parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Tcl parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Tcl parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Tcl parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Tcl parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Tcl parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Tcl field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Tcl field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Tcl parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Tcl parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_tcl(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_tcl(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.source, "\"::api::sentinel\"") && !contains(partial.source, "::api::unsupported"),
                "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); static int make(); int field; const int fixed = 1;"
                                  " private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_tcl(module, "api", false, policy);
        require(contains(generated.source, "int Api_Init(Tcl_Interp* interp)") && contains(generated.source, "\"::api::Object\"") &&
            contains(generated.source, "Tcl_PkgProvideEx(interp, \"api\", \"1.0\"") && contains(generated.source, "arity(interp, objc, objv, 1, 0, 1, \"?arg1?\")"),
            "init/commands/defaults missing");
        require(contains(generated.source, "{\"get\", \"cget\", \"configure\", \"close\", nullptr}") &&
            contains(generated.source, "{\"new\", \"make\", nullptr}") && contains(generated.source, "{\"-field\", \"-fixed\", nullptr}") &&
            contains(generated.source, "option -fixed is readonly"), "subcommands/options missing");
        require(!contains(generated.source, "\"hidden\"") && !contains(generated.source, "secret"), "private members exposed");
        require(generated.source == rosetta::generate_tcl(module, "api", false, policy).source, "nondeterministic output");
        require(contains(rosetta::generate_tcl(module, "Mixed_Case", false, policy).source, "int Mixed_case_Init("), "init name not Tcl-cased");
        rejects([&] { rosetta::generate_tcl(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_tcl(module, "tcl", false, policy); }, "reserved Tcl namespace");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_tcl(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_tcl(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_tcl(module, "api", false, policy); }, "borrowed Tcl");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_tcl(module, "api", true, policy); }, "callback or enum flag policies");
        policy = {}; policy.classes["Object"] = {};
        for (const auto* name : {"close", "cget", "configure"})
            rejects([&] { rosetta::generate_tcl(parse(std::string("struct Object { void ") + name + "(); };"), "api", true, policy); },
                "reserved or duplicate Tcl object subcommand");
        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_tcl(collision, "api"); }, "name collision");
        // Tcl command names stay inside the module namespace.
        const auto names = rosetta::generate_tcl(parse("int set(); int puts(int);"), "api");
        require(contains(names.source, "\"::api::set\"") && contains(names.source, "Tcl_Export(interp, space, \"set\", 0)"), "Tcl names not namespaced");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "tcl bindings.cpp";
        const auto dep = temporary.path() / "tcl bindings.d";
        const auto ir = temporary.path() / "tcl bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "tcl", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "Api_Init") && std::filesystem::exists(ir) && std::filesystem::exists(dep), "Tcl CLI artifacts missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R-only option accepted for Tcl");
        invalid = {header.string(), "--language", "tcl", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "Tcl output overwrote input");
        std::cout << "PASS Tcl defaults, access, subcommands, options, names, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
