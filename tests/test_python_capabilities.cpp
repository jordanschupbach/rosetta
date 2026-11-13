#include "rosetta/frontend.hpp"
#include "rosetta/python.hpp"
#include "rosetta/policy.hpp"
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
        const auto header = temporary.path() / "python api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Python result type"},
            {"pointer", "void unsupported(int*);", "unsupported Python parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Python parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Python parameter type"},
            {"unexported class value", "struct Object {}; void unsupported(Object);", "is not exported to Python"},
            {"wide string", "#include <string>\nvoid unsupported(std::wstring);", "only char strings"},
            {"union", "union Object { int i; double d; };", "unions", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Python field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Python field type", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Python parameter type"},
            {"mutable global", "inline int unsupported = 42;", "mutable global variables"},
            {"pointer result", "int* unsupported();", "unsupported Python result type"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy;
            policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_python(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_python(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.source, "{\"sentinel\", ") && !contains(partial.source, "{\"unsupported\", "), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); int field; private: int hidden(); int secret; }; int value(int = 42);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_python(module, "api", false, policy);
        require(contains(generated.source, "PyInit_api") && contains(generated.source, "PyType_FromModuleAndSpec") &&
            contains(generated.source, "rosetta_py::collect(args, kwargs, names, 1, 0, a, &n)"), "native module/type/defaults missing");
        require(!contains(generated.source, "\"hidden\"") && !contains(generated.source, "\"secret\""), "private members exposed");
        require(generated.source == rosetta::generate_python(module, "api", false, policy).source, "nondeterministic output");
        rejects([&] { rosetta::generate_python(module, "bad-name", false, policy); }, "module name");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_python(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_python(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_python(module, "api", false, policy); }, "borrowed Python");
        // Python callables are reference counted, so callback lifetime policies need no adapter.
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rosetta::generate_python(module, "api", true, policy);
        // A C++ close() replaces the inherited native close().
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        require(contains(rosetta::generate_python(reserved, "api", false, policy).source, "native->close()"), "C++ close() not exported");
        const auto duplicate = parse("struct Object { int count; void set(int); }; namespace a { int Object(); }");
        policy = {}; policy.classes["Object"] = {}; policy.names = "flat";
        rejects([&] { rosetta::generate_python(duplicate, "api", true, policy); }, "Python export name collision: Object");

        // Supported families generate without diagnostics.
        const struct { const char* label; const char* source; const char* expected; } supported[] = {
            {"enum", "enum class Mode { a, b }; Mode flip(Mode);", "IntEnum"},
            {"overload", "int twice(int); double twice(double);", "for (int pass = 0; pass < 2; ++pass)"},
            {"containers", "#include <vector>\n#include <map>\n#include <optional>\n#include <string>\n"
                "std::map<std::string, std::vector<double>> group(const std::vector<std::optional<int>>&);", "Param<const std::vector<std::optional<int>> &>"},
            {"mutable reference", "#include <vector>\nvoid fill(std::vector<double>&);", "p0.writeback(a[0])"},
            {"callback", "#include <functional>\nint apply(std::function<int(int)>, int);", "Param<std::function<int (int)>>"},
            {"string view", "#include <string_view>\nint length(std::string_view);", "Param<std::basic_string_view<char>>"},
            {"inheritance", "struct Base { virtual ~Base(); virtual int value() const = 0; }; struct Object : Base { int value() const override; };",
                "add_base<Object, Base>"},
            {"director", "struct Base { virtual ~Base(); virtual int value() const = 0; }; int read(const Base&);", "_director final : Base"},
            {"operators", "struct Object { Object operator+(const Object&) const; bool operator==(const Object&) const; };", "install_slot(type, \"__add__\")"},
            {"constant", "inline constexpr double tau = 6.28;", "\"tau\""},
        };
        for (const auto& test : supported) {
            rosetta::BindingPolicy all; all.select_all = true;
            const auto generated_case = rosetta::generate_python(parse(test.source), "api", false, all);
            require(contains(generated_case.source, test.expected), std::string(test.label) + ": expected " + test.expected);
            std::cout << "PASS supported " << test.label << '\n';
        }

        {
            // Policy modules: free functions of a mapped namespace go to a submodule; anything else
            // in that namespace, malformed names and two namespaces sharing a module are rejected.
            rosetta::BindingPolicy mapped; mapped.select_all = true; mapped.names = "flat";
            mapped.modules["lib::data"] = "data";
            const auto generated_modules = rosetta::generate_python(parse(
                "namespace lib { int value(); namespace data { int value(); int other(int); } }"), "api", false, mapped);
            require(contains(generated_modules.source, "PyModule_New(\"api.data\")")
                    && contains(generated_modules.source, "rosetta_submodule_functions_0[]"), "submodule not emitted");
            rejects([&] { rosetta::generate_python(parse("namespace lib::data { struct Object {}; }"), "api", false, mapped); },
                    "class lib::data::Object is in mapped namespace");
            rejects([&] { rosetta::generate_python(parse("namespace lib::data { enum class Mode { a }; }"), "api", false, mapped); },
                    "enumeration lib::data::Mode is in mapped namespace");
            auto excluded = mapped; excluded.exclude = {"lib::data::Object"};
            rosetta::generate_python(parse("namespace lib::data { struct Object {}; int value(); }"), "api", false, excluded);
            rejects([&] { rosetta::generate_python(parse("namespace lib { namespace other { int data(); } namespace data { int value(); } }"), "api", false, mapped); },
                    "Python export name collision: data");
            const auto policy_path = temporary.path() / "modules.json";
            for (const auto& [body, reason] : std::vector<std::pair<std::string, std::string>>{
                     {R"({"version": 1, "modules": {"lib::data": "da-ta"}})", "must be an identifier"},
                     {R"({"version": 1, "modules": {"lib::*": "data"}})", "exact namespaces"},
                     {R"({"version": 1, "modules": {"lib::a": "data", "lib::b": "data"}})", "maps two namespaces to data"}}) {
                rosetta::detail::write_file(policy_path, body);
                rejects([&] { rosetta::load_policy(policy_path); }, reason);
            }
            std::cout << "PASS policy modules\n";
        }

        parse("int value(int = 42);");
        const auto output = temporary.path() / "python bindings.cpp";
        const auto dep = temporary.path() / "python bindings.d";
        const auto ir = temporary.path() / "python bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "python", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "PyInit_api") && std::filesystem::exists(ir) && std::filesystem::exists(dep), "Python CLI artifacts missing");
        auto invalid = args; invalid.insert(invalid.end(), {"--object-system", "s3"});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language r"), "R-only option accepted for Python");
        invalid = {header.string(), "--language", "python", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "Python output overwrote input");
        std::cout << "PASS Python defaults, access, lifetime policy, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
