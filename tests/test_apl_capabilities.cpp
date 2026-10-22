#include "rosetta/frontend.hpp"
#include "rosetta/apl.hpp"
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
        const auto header = temporary.path() / "apl api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported APL result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported APL parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported APL parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported APL parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported APL parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported APL parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported APL parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported APL parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported APL parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported APL parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported APL field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported APL field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported APL parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported APL parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
            {"apl name", "int unsupported_café();", "cannot be an APL identifier"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_apl(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_apl(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.gnu, "∇Z←api∆sentinel;⍙M;⍙E") && contains(partial.dyalog, "∇ r←sentinel;e") &&
                !contains(partial.gnu, "unsupported") && !contains(partial.dyalog, "unsupported"), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("#include <string>\nstruct Object { Object(int = 42); int get(int = 1); static int make(); int field; const int fixed = 1;"
                                  " long long wide; unsigned long long big; double real; char letter; bool flag; std::string text;"
                                  " private: int hidden(); int secret; }; int value(int = 42); int pair(int left, int right = 2);"
                                  " void none(); std::string join(std::string a, int b); namespace ns { namespace inner { int deep(); } }");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {};
        const auto generated = rosetta::generate_apl(module, "api", false, policy);
        const auto& source = generated.source;
        const auto& gnu = generated.gnu;
        const auto& dyalog = generated.dyalog;
        // GNU APL: niladic, monadic and dyadic forms; methods take the object on the left.
        require(contains(gnu, "∇Z←api∆value B;⍙M;⍙E") && contains(gnu, "∇Z←api∆pair B;⍙M;⍙E") && contains(gnu, "∇api∆none;⍙M;⍙E;Z") &&
            contains(gnu, "∇Z←api∆deep;⍙M;⍙E") && contains(gnu, "∇Z←api∆Object∆new B;⍙M;⍙E") && contains(gnu, "∇Z←A api∆Object∆get B;⍙M;⍙E") &&
            contains(gnu, "∇Z←api∆Object∆make;⍙M;⍙E") && contains(gnu, "∇Z←api∆Object∆field B;⍙M;⍙E") &&
            contains(gnu, "∇A api∆Object∆set_field B;⍙M;⍙E;Z") && contains(gnu, "∇api∆Object∆close B;⍙M;⍙E;Z") &&
            contains(gnu, "∇Z←api∆Object∆fixed B") && !contains(gnu, "set_fixed") && contains(gnu, " ⍙M ⎕ES ⍙E\n") &&
            contains(gnu, " ⍝ pair(int left, int right)\n"), "GNU APL functions incorrect");
        require(gnu.rfind("#!/usr/bin/env apl --script\n", 0) == 0 && contains(gnu, " Z←'./libapi_gnu_apl.so' ⎕FX 'api⍙native'") &&
            contains(gnu, "∇Z←api∆last_error;⍙M;⍙E") && contains(gnu, "→(3=⎕NC 'api⍙native')/0"), "GNU APL loader incorrect");
        // Dyalog: namespace functions, ⎕NA declarations and a class with properties.
        require(contains(dyalog, ":Namespace api\n") && contains(dyalog, ":EndNamespace\n") && contains(dyalog, "⍙L←'./libapi_apl.so'") &&
            contains(dyalog, "∇ r←value args;e") && contains(dyalog, "∇ none;e;r") && contains(dyalog, "∇ r←deep;e") &&
            contains(dyalog, "    :Class Object\n") && contains(dyalog, ":Implements Destructor") && contains(dyalog, "∇ ⍙New0;e") &&
            contains(dyalog, "∇ ⍙New args;e") && contains(dyalog, ":Access Public Shared") && contains(dyalog, ":Property field") &&
            contains(dyalog, "∇ set value;e;r") && contains(dyalog, "⍙N.⍙Call value.NewValue"), "Dyalog namespace incorrect");
        require(contains(dyalog, "('value' 0 1 'i' 'i' '⍙f") && contains(dyalog, "('pair' 1 2 'ii' 'i' '⍙f") &&
            contains(dyalog, "('join' 2 2 'si' 's' '⍙f") && contains(dyalog, "('Object::big' 0 0 '' 'u' '⍙f") &&
            contains(dyalog, "('Object::letter' 0 0 '' 'c' '⍙f") && contains(dyalog, "('Object::flag' 0 0 '' 'b' '⍙f") &&
            contains(dyalog, "('Object::real' 0 0 '' 'f' '⍙f") && contains(dyalog, "('Object::Object' 0 1 'i' 'h' '⍙f"),
            "Dyalog specs incorrect");
        require(contains(dyalog, "|rosetta_api_pending_size'") && contains(dyalog, "I8 U8 >I8'") && contains(dyalog, "<U1[] U8 I8 >U8'") &&
            contains(dyalog, "'@MODULE@") == false && contains(dyalog, "'api: ',label"), "Dyalog ⎕NA declarations incorrect");
        // C++: one C ABI, plus the GNU APL bridge behind ROSETTA_GNU_APL.
        require(contains(source, "extern \"C\" std::int32_t rosetta_api_1(") && contains(source, "rosetta_apl::guard") &&
            contains(source, "rosetta_apl::live<::Object>(self, \"Object\")") && contains(source, "rosetta_apl::close<::Object>(self, \"Object\")") &&
            contains(source, "#ifdef ROSETTA_GNU_APL\n") && contains(source, "extern \"C\" void* get_function_mux") &&
            contains(source, "std::string(\"api: \") + label") && contains(source, "rosetta_gnu::arguments(B, 2, 2, true)") &&
            source.find("#include \"") < source.find("#include \"Native_interface.hh\""), "C++ stubs or bridge incorrect");
        require(!contains(source, "hidden") && !contains(source, "secret") && !contains(gnu, "hidden") && !contains(dyalog, "hidden"),
            "private members exposed");
        require(source == rosetta::generate_apl(module, "api", false, policy).source && gnu == rosetta::generate_apl(module, "api", false, policy).gnu &&
            dyalog == rosetta::generate_apl(module, "api", false, policy).dyalog, "nondeterministic output");
        const auto placed = rosetta::generate_apl(module, "api", false, policy, "/opt/it's here/");
        require(contains(placed.gnu, "'/opt/it''s here/libapi_gnu_apl.so' ⎕FX") && contains(placed.dyalog, "⍙L←'/opt/it''s here/libapi_apl.so'"),
            "library directory incorrect");
        rejects([&] { rosetta::generate_apl(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_apl(module, "_api", false, policy); }, "module name");
        rejects([&] { rosetta::generate_apl(module, "api", false, policy, ""); }, "library directory");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_apl(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_apl(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_apl(module, "api", false, policy); }, "borrowed APL");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_apl(module, "api", true, policy); }, "callback or enum flag policies");
        // Namespaces are dropped from APL names, so they can collide.
        rejects([&] { rosetta::generate_apl(parse("namespace a { int f(); } int f();"), "api"); }, "APL name collision: f");
        rejects([&] { rosetta::generate_apl(parse("int last_error();"), "api"); }, "APL name collision: last_error");
        rosetta::BindingPolicy closing; closing.classes["Object"] = {};
        rejects([&] { rosetta::generate_apl(parse("struct Object {}; namespace n { int Object(); }"), "api", false, closing); },
            "APL name collision: Object");
        rejects([&] { rosetta::generate_apl(parse("struct Object { void close(); };"), "api", false, closing); }, "APL name collision: close");
        rejects([&] { rosetta::generate_apl(parse("struct Object { int count; void set_count(int); };"), "api", false, closing); },
            "APL name collision: set_count");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "apl bindings.cpp";
        const auto dep = temporary.path() / "apl bindings.d";
        const auto ir = temporary.path() / "apl bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "apl", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_apl::guard") &&
            contains(rosetta::detail::read_file(temporary.path() / "api.apl"), "∇Z←api∆value B") &&
            contains(rosetta::detail::read_file(temporary.path() / "api.apln"), "∇ r←value args") &&
            std::filesystem::exists(ir) && contains(rosetta::detail::read_file(dep), "api.apl:") && contains(rosetta::detail::read_file(dep), "api.apln:"),
            "APL CLI artifacts missing");
        auto placed_args = args;
        placed_args.insert(placed_args.end(), {"--gnu-apl-output", (temporary.path() / "gnu" / "x.apl").string(),
            "--dyalog-output", (temporary.path() / "dyalog" / "x.apln").string(), "--apl-library-dir", "/usr/lib/rosetta"});
        require(rosetta::run_cli(placed_args, out, error) == 0 &&
            contains(rosetta::detail::read_file(temporary.path() / "gnu" / "x.apl"), "'/usr/lib/rosetta/libapi_gnu_apl.so'") &&
            contains(rosetta::detail::read_file(temporary.path() / "dyalog" / "x.apln"), "'/usr/lib/rosetta/libapi_apl.so'"),
            "APL scripts not written to their options");
        auto same = args; same.insert(same.end(), {"--gnu-apl-output", (temporary.path() / "x").string(), "--dyalog-output", (temporary.path() / "x").string()});
        require(rosetta::run_cli(same, out, error) == 1 && contains(error.str(), "APL scripts and inputs/outputs must have distinct paths"),
            "APL scripts shared a path");
        std::vector<std::string> invalid{header.string(), "--language", "go", "--module", "api", "-o", output.string(), "--apl-library-dir", "x"};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language apl"), "APL-only option accepted for Go");
        invalid = {header.string(), "--language", "apl", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "APL output overwrote input");
        std::cout << "PASS APL forms, specs, access, names, collisions, loaders, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
