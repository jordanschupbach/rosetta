#include "rosetta/frontend.hpp"
#include "rosetta/ruby.hpp"
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
        const auto header = temporary.path() / "ruby api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Ruby result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Ruby parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Ruby parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Ruby parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Ruby parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Ruby parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Ruby parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Ruby parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Ruby parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Ruby parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Ruby field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Ruby field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Ruby parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Ruby parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy;
            policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_ruby(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_ruby(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.source, "\"sentinel\", RUBY_METHOD_FUNC") && !contains(partial.source, "\"unsupported\", RUBY_METHOD_FUNC"), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_ruby(module, "api", false, policy);
        require(contains(generated.source, "Init_api") && contains(generated.source, "rb_define_class_under") &&
            contains(generated.source, "arity(argc, 0, 1)"), "native module/type/defaults missing");
        require(!contains(generated.source, "\"hidden\"") && !contains(generated.source, "\"secret\""), "private members exposed");
        require(generated.source == rosetta::generate_ruby(module, "api", false, policy).source, "nondeterministic output");
        rejects([&] { rosetta::generate_ruby(module, "bad-name", false, policy); }, "module name");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_ruby(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_ruby(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_ruby(module, "api", false, policy); }, "borrowed Ruby");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_ruby(module, "api", true, policy); }, "callback or enum flag policies");
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_ruby(reserved, "api", true, policy); }, "reserved or duplicate");
        const auto invalid_constant = parse("struct _Object {};");
        policy = {}; policy.classes["_Object"] = {};
        rejects([&] { rosetta::generate_ruby(invalid_constant, "api", false, policy); }, "Ruby constant");
        require(!rosetta::generate_ruby(invalid_constant, "api", true, policy).diagnostics.empty(), "invalid constant silently omitted");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "ruby bindings.cpp";
        const auto dep = temporary.path() / "ruby bindings.d";
        const auto ir = temporary.path() / "ruby bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "ruby", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "Init_api") && std::filesystem::exists(ir) && std::filesystem::exists(dep), "Ruby CLI artifacts missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R-only option accepted for Ruby");
        invalid = {header.string(), "--language", "ruby", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "Ruby output overwrote input");
        std::cout << "PASS Ruby defaults, access, lifetime policy, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
