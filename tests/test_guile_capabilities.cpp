#include "rosetta/frontend.hpp"
#include "rosetta/guile.hpp"
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
        const auto header = temporary.path() / "guile api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Guile result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Guile parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Guile parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Guile parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Guile parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Guile parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Guile parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Guile parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Guile parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Guile parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Guile field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Guile field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Guile parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Guile parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_guile(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_guile(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.source, "scm_c_define_gsubr(\"sentinel\"") && !contains(partial.interface, "unsupported") &&
                contains(partial.interface, "sentinel"),
                "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); static int make(); int field; const int fixed = 1;"
                                  " private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_guile(module, "api", false, policy);
        const auto& source = generated.source;
        const auto& scheme = generated.interface;
        require(contains(source, "void rosetta_init_api(void)") && contains(source, "scm_c_define_gsubr(\"value\", 0, 1, 0") &&
            contains(source, "scm_c_define_gsubr(\"make-Object\", 0, 1, 0") && contains(source, "scm_c_define_gsubr(\"Object-get\", 1, 1, 0") &&
            contains(source, "scm_c_define_gsubr(\"Object-make\", 0, 0, 0"), "init/procedures/defaults missing");
        require(contains(source, "\"Object?\"") && contains(source, "\"Object-close!\"") && contains(source, "\"Object-field\"") &&
            contains(source, "\"set-Object-field!\"") && contains(source, "\"Object-fixed\"") && !contains(source, "set-Object-fixed!"),
            "predicate/close/accessors missing");
        require(contains(scheme, "(define-module (api)") && contains(scheme, "set-Object-field!") && contains(scheme, "Object-close!") &&
            contains(scheme, "(load-extension found \"rosetta_init_api\")") && contains(scheme, "\"api.scm\""), "Scheme module incorrect");
        require(!contains(source, "hidden") && !contains(source, "secret"), "private members exposed");
        require(source == rosetta::generate_guile(module, "api", false, policy).source &&
            scheme == rosetta::generate_guile(module, "api", false, policy).interface, "nondeterministic output");
        rejects([&] { rosetta::generate_guile(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_guile(module, "guile", false, policy); }, "reserved Guile module");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_guile(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_guile(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_guile(module, "api", false, policy); }, "borrowed Guile");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_guile(module, "api", true, policy); }, "callback or enum flag policies");
        policy = {}; policy.classes["Object"] = {};
        rosetta::BindingPolicy both; both.classes["ns::Object"] = {}; both.classes["ns__Object"] = {};
        rejects([&] { rosetta::generate_guile(parse("namespace ns { struct Object {}; } struct ns__Object {};"), "api", true, both); },
            "Guile export name collision: make-ns__Object");
        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_guile(collision, "api"); }, "name collision");
        // Primitives take at most 10 arguments, including the object.
        const auto wide = rosetta::generate_guile(parse("int ten(int, int, int, int, int, int, int, int, int, int);"
            "int eleven(int, int, int, int, int, int, int, int, int, int, int);"), "api", true);
        require(contains(wide.interface, "ten") && !contains(wide.interface, "eleven") &&
            contains(wide.diagnostics.back(), "at most 10 arguments"), "argument limit not enforced");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "guile bindings.cpp";
        const auto dep = temporary.path() / "guile bindings.d";
        const auto ir = temporary.path() / "guile bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "guile", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        const auto module_file = temporary.path() / "api.scm";
        require(contains(rosetta::detail::read_file(output), "rosetta_init_api") && contains(rosetta::detail::read_file(module_file), "(define-module (api)") &&
            std::filesystem::exists(ir) && std::filesystem::exists(dep) && contains(rosetta::detail::read_file(dep), "api.scm"), "Guile CLI artifacts missing");
        auto renamed = args; renamed.insert(renamed.end(), {"--guile-output", (temporary.path() / "other.scm").string()});
        require(rosetta::run_cli(renamed, out, error) == 1 && contains(error.str(), "must match the module name"), "misnamed Guile module accepted");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R-only option accepted for Guile");
        invalid = {header.string(), "--language", "guile", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "Guile output overwrote input");
        std::cout << "PASS Guile defaults, access, names, argument limit, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
