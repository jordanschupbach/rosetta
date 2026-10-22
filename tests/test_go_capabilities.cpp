#include "rosetta/frontend.hpp"
#include "rosetta/go.hpp"
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
        const auto header = temporary.path() / "go api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported Go result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported Go parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported Go parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported Go parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported Go parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported Go parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported Go parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported Go parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported Go parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported Go parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported Go field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported Go field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported Go parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported Go parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
            {"go name", "int ___();", "cannot be an exported Go identifier"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_go(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_go(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "func Sentinel() (int32, error)") && !contains(partial.interface, "Unsupported"),
                "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("#include <string>\nstruct Object { Object(int = 42); int get(int = 1); static int make(); int field; const int fixed = 1;"
                                  " long long wide; unsigned long long big; double real; char letter; bool flag; std::string text;"
                                  " private: int hidden(); int secret; }; int value(int = 42); int pair(int left, int right = 2);"
                                  " int six(int, int, int, int, int, int); namespace ns { namespace inner { int deep(); } }"
                                  " double mixed(int a, double b = 1, int c = 2); int keywords(int func, int len);");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_go(module, "api", false, policy);
        const auto& source = generated.source;
        const auto& go = generated.interface;
        // Defaulted parameters sharing one Go type become a variadic parameter.
        require(contains(go, "func Value(defaults ...int32) (int32, error)") && contains(go, "func Pair(left int32, defaults ...int32) (int32, error)") &&
            contains(go, "func NewObject(defaults ...int32) (*Object, error)") && contains(go, "func (o *Object) Get(defaults ...int32) (int32, error)") &&
            contains(go, "func ObjectMake() (int32, error)") && contains(go, "func Mixed(a int32, b float64, c int32) (float64, error)") &&
            contains(go, "return 0, tooMany(\"value\")"), "functions/defaults incorrect");
        require(contains(go, "type Object struct {\n\tcell unsafe.Pointer\n}") && contains(go, "func (o *Object) Close() error") &&
            contains(go, "func (o *Object) Field() (int32, error)") && contains(go, "func (o *Object) SetField(value int32) error") &&
            contains(go, "func (o *Object) Fixed() (int32, error)") && !contains(go, "SetFixed") &&
            contains(go, "runtime.AddCleanup(o, func(cell unsafe.Pointer) { C.rosetta_api_release_Object(cell) }, cell)") &&
            contains(go, "runtime.KeepAlive(o)"), "class type, close, cleanup or accessors incorrect");
        require(contains(go, "Wide() (int64, error)") && contains(go, "Big() (uint64, error)") && contains(go, "Real() (float64, error)") &&
            contains(go, "Letter() (byte, error)") && contains(go, "Flag() (bool, error)") && contains(go, "Text() (string, error)") &&
            contains(go, "SetText(value string) error"), "scalar representations incorrect");
        // Namespaces do not appear in Go names; unusable parameter names become argN.
        require(contains(go, "func Deep() (int32, error)") && contains(go, "func Six(arg0 int32, arg1 int32, arg2 int32, arg3 int32, arg4 int32, arg5 int32)") &&
            contains(go, "func Keywords(arg0 int32, arg1 int32)"), "names incorrect");
        require(contains(go, "// Code generated by Rosetta. DO NOT EDIT.") && contains(go, "package api\n") &&
            contains(go, "#cgo LDFLAGS: -L${SRCDIR} -lapi_stubs") && contains(go, "\t\"runtime\"\n") &&
            contains(go, "return \"api: \" + e.Function") && contains(go, "typedef struct { uint64_t code; char* message; size_t size; } rosetta_api_status;"),
            "package preamble incorrect");
        require(contains(source, "extern \"C\" std::int32_t rosetta_api_0(") && contains(source, "rosetta_go::release<::Object>") &&
            contains(source, "rosetta_go::guard(status"), "C++ stubs incorrect");
        require(!contains(source, "hidden") && !contains(source, "secret") && !contains(go, "Hidden"), "private members exposed");
        require(source == rosetta::generate_go(module, "api", false, policy).source &&
            go == rosetta::generate_go(module, "api", false, policy).interface, "nondeterministic output");
        require(!contains(rosetta::generate_go(parse("int f();"), "api").interface, "\"runtime\""), "runtime imported without classes");
        rejects([&] { rosetta::generate_go(module, "bad-name", false, policy); }, "package name");
        rejects([&] { rosetta::generate_go(module, "fmt", false, policy); }, "standard library package");
        rejects([&] { rosetta::generate_go(module, "func", false, policy); }, "Go keyword");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_go(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_go(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_go(module, "api", false, policy); }, "borrowed Go");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_go(module, "api", true, policy); }, "callback or enum flag policies");
        // CamelCase names and flattened namespaces can collide.
        rejects([&] { rosetta::generate_go(parse("int Value(); int value();"), "api"); }, "Go name collision: Value");
        rejects([&] { rosetta::generate_go(parse("int too_large(); int TooLarge();"), "api"); }, "Go name collision: TooLarge");
        rejects([&] { rosetta::generate_go(parse("namespace a { int f(); } int f();"), "api"); }, "Go name collision: F");
        rejects([&] { rosetta::generate_go(parse("int error();"), "api"); }, "Go name collision: Error");
        rosetta::BindingPolicy closing; closing.classes["Object"] = {};
        rejects([&] { rosetta::generate_go(parse("struct Object { void close(); };"), "api", false, closing); }, "Go name collision: Close");
        rejects([&] { rosetta::generate_go(parse("struct Object { int count; void set_count(int); };"), "api", false, closing); },
            "Go name collision: SetCount");
        rejects([&] { rosetta::generate_go(parse("struct Object { static int make(); }; int ObjectMake();"), "api", false, closing); },
            "Go name collision: ObjectMake");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "go bindings.cpp";
        const auto dep = temporary.path() / "go bindings.d";
        const auto ir = temporary.path() / "go bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "go", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        const auto package_file = temporary.path() / "api.go";
        require(contains(rosetta::detail::read_file(output), "rosetta_go::guard") &&
            contains(rosetta::detail::read_file(package_file), "func Value(defaults ...int32) (int32, error)") &&
            std::filesystem::exists(ir) && std::filesystem::exists(dep) && contains(rosetta::detail::read_file(dep), "api.go"),
            "Go CLI artifacts missing");
        auto placed = args; placed.insert(placed.end(), {"--go-output", (temporary.path() / "api" / "bindings.go").string()});
        require(rosetta::run_cli(placed, out, error) == 0 && std::filesystem::exists(temporary.path() / "api" / "bindings.go"),
            "Go package file not written to --go-output");
        auto invalid = args; invalid.insert(invalid.end(), {"--ocaml-output", (temporary.path() / "api.ml").string()});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "requires --language ocaml"), "OCaml-only option accepted for Go");
        invalid = {header.string(), "--language", "go", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "Go output overwrote input");
        std::cout << "PASS Go defaults, access, names, collisions, package preamble, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
