#include "rosetta/frontend.hpp"
#include "rosetta/common_lisp.hpp"
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
        const auto header = temporary.path() / "lisp api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Common Lisp result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Common Lisp parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Common Lisp parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Common Lisp parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Common Lisp parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Common Lisp parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Common Lisp parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Common Lisp parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Common Lisp parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Common Lisp parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Common Lisp field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Common Lisp field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Common Lisp parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Common Lisp parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_common_lisp(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_common_lisp(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "(defun |API|:|SENTINEL|") && !contains(partial.interface, "|UNSUPPORTED|"),
                "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; const int fixed = 1;"
                                  " private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_common_lisp(module, "api", false, policy);
        require(contains(generated.source, "rosetta_api_call") && contains(generated.interface, "(defpackage \"API\"") &&
            contains(generated.interface, "(defclass |API|:|OBJECT| (|API|:native-owner) ())") &&
            contains(generated.interface, "trivial-garbage:finalize") && contains(generated.source, "arity(count, 0, 1)"),
            "ABI/owners/defaults missing");
        require(contains(generated.interface, "(defun (setf |API|:|OBJECT__FIELD|)") &&
            contains(generated.interface, "native property is readonly"), "field accessors missing");
        require(!contains(generated.interface, "HIDDEN") && !contains(generated.source, "->secret"), "private members exposed");
        const auto again = rosetta::generate_common_lisp(module, "api", false, policy);
        require(generated.source == again.source && generated.interface == again.interface, "nondeterministic output");
        rejects([&] { rosetta::generate_common_lisp(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_common_lisp(module, "cffi", false, policy); }, "reserved Common Lisp package");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_common_lisp(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_common_lisp(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_common_lisp(module, "api", false, policy); }, "borrowed Common Lisp");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_common_lisp(module, "api", true, policy); }, "callback or enum flag policies");
        const auto collision = parse("namespace ns { int value(); } int ns__value();");
        rejects([&] { rosetta::generate_common_lisp(collision, "api"); }, "name collision");
        const auto case_collision = parse("int Value(); int value();");
        rejects([&] { rosetta::generate_common_lisp(case_collision, "api"); }, "ignoring case");
        const auto member_collision = parse("struct Object { int get(); }; int Object__get();");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_common_lisp(member_collision, "api", false, policy); }, "name collision");
        // COMMON-LISP names stay local to the generated package.
        const auto lisp_names = parse("int list(); int car(int); int close();");
        const auto names = rosetta::generate_common_lisp(lisp_names, "api");
        require(contains(names.interface, "(defun |API|:|LIST|") && contains(names.interface, "(:use)"), "CL names not local");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "lisp bindings.cpp";
        const auto interface = temporary.path() / "lisp bindings.lisp";
        const auto dep = temporary.path() / "lisp bindings.d";
        const auto ir = temporary.path() / "lisp bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "common-lisp", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
            contains(rosetta::detail::read_file(interface), "(defpackage \"API\"") && std::filesystem::exists(ir),
            "Common Lisp CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "lisp\\ bindings.lisp"), "Common Lisp interface dependency missing");
        const auto custom = temporary.path() / "custom.lisp";
        auto custom_args = args; custom_args.insert(custom_args.end(), {"--lisp-output", custom.string()});
        require(rosetta::run_cli(custom_args, out, error) == 0 && std::filesystem::exists(custom), "custom interface missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R option accepted for Common Lisp");
        for (const auto& path : {header, output, dep, ir}) {
            invalid = args; invalid.insert(invalid.end(), {"--lisp-output", path.string()});
            require(rosetta::run_cli(invalid, out, error) == 1, "interface collision accepted");
        }
        invalid = {header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--lisp-output", custom.string()};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "requires --language common-lisp"),
            "Common Lisp-only option accepted for Lua");
        std::cout << "PASS Common Lisp policies, defaults, access, names, determinism, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
