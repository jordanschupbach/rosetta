#include "rosetta/frontend.hpp"
#include "rosetta/ocaml.hpp"
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
        const auto header = temporary.path() / "ocaml api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported OCaml result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported OCaml parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported OCaml parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported OCaml parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported OCaml parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported OCaml parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported OCaml parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported OCaml parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported OCaml parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported OCaml parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported OCaml field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported OCaml field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported OCaml parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported OCaml parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
            {"module name", "namespace _hidden { int unsupported(); }", "cannot be an OCaml module name"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_ocaml(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_ocaml(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.interface, "val sentinel : unit -> int") && !contains(partial.interface, "unsupported"),
                "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("struct Object { Object(int = 42); int get(int = 1); static int make(); int field; const int fixed = 1;"
                                  " long long wide; unsigned long long big; double real; char letter; bool flag; private: int hidden(); int secret; };"
                                  " int value(int = 42); int pair(int left, int right = 2);"
                                  " int six(int, int, int, int, int, int); namespace ns { namespace inner { int deep(); } }");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_ocaml(module, "api", false, policy);
        const auto& source = generated.source;
        const auto& ml = generated.interface;
        // Defaulted parameters are optional labeled arguments ahead of the positional ones.
        require(contains(ml, "val value : ?arg0:int -> unit -> int") && contains(ml, "val pair : ?right:int -> int -> int") &&
            contains(ml, "let pair ?right:o1 a0 = rosetta_external_") && contains(ml, "val create : ?arg0:int -> unit -> t") &&
            contains(ml, "val get : ?arg0:int -> t -> int") && contains(ml, "val make : unit -> int"), "functions/defaults incorrect");
        require(contains(ml, "module Object : sig\n    type t") && contains(ml, "val close : t -> unit") &&
            contains(ml, "val field : t -> int") && contains(ml, "val set_field : t -> int -> unit") && contains(ml, "val fixed : t -> int") &&
            !contains(ml, "set_fixed"), "class module, close or accessors incorrect");
        require(contains(ml, "val wide : t -> int64") && contains(ml, "val big : t -> int64") && contains(ml, "val real : t -> float") &&
            contains(ml, "val letter : t -> char") && contains(ml, "val flag : t -> bool"), "scalar representations incorrect");
        require(contains(ml, "module Ns : sig\n    module Inner : sig\n      val deep : unit -> int"), "namespace modules incorrect");
        // OCaml passes more than five arguments to a bytecode stub as an array.
        require(contains(ml, "val six : int -> int -> int -> int -> int -> int -> int") && contains(source, "_byte(value* argv, int argn)") &&
            contains(source, "CAMLxparam5(a0, a1, a2, a3, a4);\nCAMLxparam1(a5);"), "bytecode stub missing");
        require(contains(ml, "exception Error of error_kind * string") && contains(ml, "Callback.register_exception \"rosetta.api.error\"") &&
            contains(ml, "Api.Error(%s, %S)") && contains(source, "caml_named_value(\"rosetta.api.error\")") &&
            contains(source, "rosetta_api_initialize"), "exception registration incorrect");
        require(contains(source, "\"rosetta.api.Object\", &rosetta_ocaml::finalize<::Object>") &&
            source.find("#include \"") < source.find("#include <caml/"), "custom operations or include order incorrect");
        require(!contains(source, "hidden") && !contains(source, "secret") && !contains(ml, "hidden"), "private members exposed");
        require(source == rosetta::generate_ocaml(module, "api", false, policy).source &&
            ml == rosetta::generate_ocaml(module, "api", false, policy).interface, "nondeterministic output");
        rejects([&] { rosetta::generate_ocaml(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_ocaml(module, "list", false, policy); }, "reserved OCaml standard library");
        rejects([&] { rosetta::generate_ocaml(module, "Stdlib", false, policy); }, "reserved OCaml standard library");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_ocaml(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_ocaml(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_ocaml(module, "api", false, policy); }, "borrowed OCaml");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_ocaml(module, "api", true, policy); }, "callback or enum flag policies");
        // Names OCaml spells differently, and the collisions that causes.
        const auto names = rosetta::generate_ocaml(parse("int type(); int Upper(); int labels(int begin = 1, int end = 2, int = 3);"), "api");
        require(contains(names.interface, "val type_ : unit -> int") && contains(names.interface, "val upper : unit -> int") &&
            contains(names.interface, "val labels : ?begin_:int -> ?end_:int -> ?arg2:int -> unit -> int"), "name adjustments incorrect");
        rejects([&] { rosetta::generate_ocaml(parse("int Value(); int value();"), "api"); }, "OCaml name collision: value");
        rosetta::BindingPolicy both; both.classes["Ns"] = {};
        rejects([&] { rosetta::generate_ocaml(parse("namespace ns { int value(); } struct Ns {};"), "api", true, both); },
            "OCaml module name collision: Ns");
        rosetta::BindingPolicy closing; closing.classes["Object"] = {};
        rejects([&] { rosetta::generate_ocaml(parse("struct Object { void close(); };"), "api", false, closing); },
            "OCaml name collision: close");
        rejects([&] { rosetta::generate_ocaml(parse("struct Object { int count; void set_count(int); };"), "api", false, closing); },
            "OCaml name collision: set_count");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "ocaml bindings.cpp";
        const auto dep = temporary.path() / "ocaml bindings.d";
        const auto ir = temporary.path() / "ocaml bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "ocaml", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        const auto module_file = temporary.path() / "api.ml";
        require(contains(rosetta::detail::read_file(output), "rosetta_api_initialize") &&
            contains(rosetta::detail::read_file(module_file), "val value : ?arg0:int -> unit -> int") &&
            std::filesystem::exists(ir) && std::filesystem::exists(dep) && contains(rosetta::detail::read_file(dep), "api.ml"),
            "OCaml CLI artifacts missing");
        auto renamed = args; renamed.insert(renamed.end(), {"--ocaml-output", (temporary.path() / "other.ml").string()});
        require(rosetta::run_cli(renamed, out, error) == 1 && contains(error.str(), "must match the module name"), "misnamed OCaml module accepted");
        auto invalid = args; invalid.insert(invalid.end(), {"--guile-output", (temporary.path() / "api.scm").string()});
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "requires --language guile"), "Guile-only option accepted for OCaml");
        invalid = {header.string(), "--language", "ocaml", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "OCaml output overwrote input");
        std::cout << "PASS OCaml defaults, access, names, modules, bytecode stubs, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
