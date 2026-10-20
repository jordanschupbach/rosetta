#include "rosetta/frontend.hpp"
#include "rosetta/java.hpp"
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
        const auto header = temporary.path() / "java api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Java result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Java parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Java parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Java parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Java parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Java parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Java parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Java parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Java parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Java parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Java field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Java field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Java parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Java parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_java(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_java(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "long sentinel()") && !contains(partial.interface, " unsupported("), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_java(module, "java_api", false, policy);
        require(contains(generated.source, "Java_java_1api_invoke") && contains(generated.interface, "public final class java_api") &&
            contains(generated.interface, "implements java.lang.AutoCloseable") && contains(generated.interface, "long value()") &&
            contains(generated.interface, "long value(long value0)"), "JNI/typed API/owners/defaults missing");
        require(!contains(generated.interface, " hidden(") && !contains(generated.source, "->secret"), "private members exposed");
        const auto packaged = rosetta::generate_java(module, "java_api", false, policy, "org.example_bindings");
        require(contains(packaged.interface, "package org.example_bindings;") && contains(packaged.source, "Java_org_example_1bindings_java_1api_invoke"), "package/JNI escaping missing");
        rejects([&] { rosetta::generate_java(module, "api", false, policy, "org..bad"); }, "Java package component");
        const auto again = rosetta::generate_java(module, "java_api", false, policy);
        require(generated.source == again.source && generated.interface == again.interface, "nondeterministic output");
        rejects([&] { rosetta::generate_java(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_java(module, "class", false, policy); }, "reserved Java");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_java(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_java(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_java(module, "api", false, policy); }, "borrowed Java");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_java(module, "api", true, policy); }, "callback or enum flag policies");
        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_java(collision, "api"); }, "name collision");
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_java(reserved, "api", true, policy); }, "reserved or duplicate");
        const auto accessor = parse("struct Object { int field; int get_field(); }; ");
        rejects([&] { rosetta::generate_java(accessor, "api", true, policy); }, "reserved or duplicate");
        parse("int value(int = 42);");
        const auto output = temporary.path() / "java bindings.cpp";
        const auto interface = temporary.path() / "api.java";
        const auto dep = temporary.path() / "java bindings.d";
        const auto ir = temporary.path() / "java bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "java", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "Java_api_invoke") &&
            contains(rosetta::detail::read_file(interface), "public final class api") && std::filesystem::exists(ir), "Java CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.java"), "Java interface dependency missing");
        const auto custom = temporary.path() / "other directory" / "api.java";
        auto custom_args = args; custom_args.insert(custom_args.end(), {"--java-output", custom.string()});
        require(rosetta::run_cli(custom_args, out, error) == 0 && std::filesystem::exists(custom), "custom interface missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R option accepted for Java");
        invalid = args; invalid.insert(invalid.end(), {"--java-output", (temporary.path() / "wrong.java").string()});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "filename must match"), "invalid Java filename accepted");
        invalid = args; invalid.insert(invalid.end(), {"--java-output", interface.string(), "--dump-ir", interface.string()});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "distinct paths"), "Java output collision accepted");
        invalid = {header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--java-output", custom.string()};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language java"), "Java option accepted for Lua");
        std::cout << "PASS Java policies, defaults, access, determinism, JNI names, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
