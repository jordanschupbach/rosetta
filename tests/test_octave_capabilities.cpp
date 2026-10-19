#include "rosetta/frontend.hpp"
#include "rosetta/octave.hpp"
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
        const auto header = temporary.path() / "octave api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Octave result type"},
            {"enum definition", "enum class Mode : int;", "enum definition requires named values"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Octave parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Octave parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Octave parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Octave parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Octave parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Octave parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Octave parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Octave parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Octave parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Octave field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Octave field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Octave parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Octave parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy;
            policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_octave(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_octave(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.source, "operation == \"sentinel\"") && !contains(partial.source, "operation == \"unsupported\""), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_octave(module, "api", false, policy);
        require(contains(generated.source, "DEFMETHOD_DLD(api,") && contains(generated.source, "rosetta_oct::Native") &&
            contains(generated.source, "arity(args, 0, 1)"), "native module/type/defaults missing");
        require(!contains(generated.source, "\"hidden\"") && !contains(generated.source, "\"secret\""), "private members exposed");
        require(generated.source == rosetta::generate_octave(module, "api", false, policy).source, "nondeterministic output");
        rejects([&] { rosetta::generate_octave(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_octave(module, "function", false, policy); }, "Octave keyword");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_octave(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_octave(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_octave(module, "api", false, policy); }, "borrowed Octave");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_octave(module, "api", true, policy); }, "callback or enum flag policies");
        const auto enum_module = parse("enum class Mode { a = 1, alias = 1, b = 2 }; using Alias = Mode; Alias echo(Alias); struct Object { Mode mode; Mode get(); }; ");
        policy = {}; policy.classes["Object"] = {};
        const auto enum_source = rosetta::generate_octave(enum_module, "api", false, policy).source;
        require(contains(enum_source, "enum_values<::Mode>") && contains(enum_source, "values.assign(\"alias\"") &&
            contains(enum_source, "rosetta_oct::from<::Mode>") && contains(enum_source, "require()->mode"), "enum codecs, aliases or fields missing");
        const auto incomplete = parse("enum class Mode : int; Mode echo(Mode); int sentinel();");
        rejects([&] { rosetta::generate_octave(incomplete, "api"); }, "unsupported Octave");
        const auto omitted = rosetta::generate_octave(incomplete, "api", true);
        require(!contains(omitted.source, "operation == \"echo\"") && contains(omitted.source, "operation == \"sentinel\""), "incomplete enum function retained");
        const auto collision = parse("namespace ns { enum class Mode { a }; } int ns__Mode();");
        rejects([&] { rosetta::generate_octave(collision, "api"); }, "name collision");
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_octave(reserved, "api", true, policy); }, "reserved or duplicate");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "octave bindings.cpp";
        const auto dep = temporary.path() / "octave bindings.d";
        const auto ir = temporary.path() / "octave bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "octave", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "DEFMETHOD_DLD(api,") && std::filesystem::exists(ir) && std::filesystem::exists(dep), "Octave CLI artifacts missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R-only option accepted for Octave");
        invalid = {header.string(), "--language", "octave", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "Octave output overwrote input");
        std::cout << "PASS Octave defaults, access, lifetime policy, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
