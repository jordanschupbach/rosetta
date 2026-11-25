// Generator and CLI checks of the Prolog backend (--language prolog) that need no Prolog: output
// shape, names, the foreign glue, options and the CLI artifacts.
#include "rosetta/frontend.hpp"
#include "rosetta/prolog.hpp"
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
        const auto header = temporary.path() / "prolog api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++17"});
        };
        const auto policy_file = temporary.path() / "policy.json";
        rosetta::detail::write_file(policy_file, "{\"version\": 1, \"select\": \"all\", \"names\": \"flat\"}\n");
        const auto policy = rosetta::load_policy(policy_file);

        // Output shape, names and determinism.
        const auto module = parse("#include <functional>\n#include <string>\nenum class Mode { on, On, true_ };\n"
                                  "struct Cursor { int getName() const { return 0; } int get_name(int) const { return 1; }\n"
                                  "  int Count() const { return 0; } int count = 2; static int make() { return 1; } };\n"
                                  "struct Point { double x = 0; Point operator+(const Point&) const { return {}; } bool operator==(const Point&) const { return true; }\n"
                                  "  double operator[](int) const { return 0; } };\n"
                                  "struct Base { virtual ~Base() = default; virtual int area() const { return 0; } };\n"
                                  "struct Other { virtual ~Other() = default; };\n"
                                  "struct Derived : Base, Other { int area() const override { return 1; } };\n"
                                  "inline int twice(int value = 21) { return 2 * value; }\n"
                                  "inline int length(int x) { return x; }\n"
                                  "inline void bump(int& value) { ++value; }\n"
                                  "inline int each(const std::function<int(int)>& f) { return f(1); }\n"
                                  "constexpr double golden_ratio = 1.618;\n");
        const auto generated = rosetta::generate_prolog(module, "Api", false, policy);
        const auto& prolog = generated.prolog;
        require(contains(prolog, ":- module(api, [\n") && contains(prolog, "/** <module> Prolog interface to the C++ library Api") &&
                contains(prolog, ":- '$rosetta_init'([cursor, point, base, other, derived]).\n"), "module layout");
        require(contains(prolog, "load_foreign_library(Path, 'rosetta_Api_prolog_install')") && contains(prolog, "getenv('API_PROLOG_LIBRARY', Path)") &&
                contains(prolog, "'libApi'") && !contains(prolog, "@M@") && !contains(prolog, "@C@") && !contains(prolog, "@LIB@") &&
                !contains(prolog, "@ENV@") && !contains(prolog, "use_module(library(lists))"), "runtime placeholders");
        require(contains(prolog, "mode(on, 0).\nmode(on_2, 1).\nmode(true_2, 2).\n"), "enum facts");
        require(contains(prolog, "cursor_get_name/2") && contains(prolog, "cursor_get_name/3") && contains(prolog, "cursor_count/2") &&
                contains(prolog, "cursor_count_2/2") && contains(prolog, "cursor_set_count_2/2") && contains(prolog, "cursor_make(Result) :-"), "member names");
        require(contains(prolog, "length_2/2") && contains(prolog, "golden_ratio/1") && !contains(prolog, "    length/2"), "system predicates");
        require(contains(prolog, "point_add/3") && contains(prolog, "point_equals/3") && contains(prolog, "point_at/3"), "operators");
        require(contains(prolog, "'$rosetta_class'(4, derived, [2, 3]).") && contains(prolog, "derived_extend/3") &&
                contains(prolog, "'$rosetta_virtual'(0, area, true).") && contains(prolog, "'$rosetta_native'(4, 0, derived_area)."),
                "multiple inheritance and extended objects");
        require(contains(prolog, "    '$rosetta_dispatch'(Base, area, [Base, Result],"), "virtual dispatch");
        require(contains(prolog, "%!  twice(-Result:integer) is det.\n%!  twice(+Value:integer, -Result:integer) is det.") &&
                contains(prolog, "Value = 21"), "default arguments: a predicate per argument count");
        require(contains(prolog, "bump(Value0, Value) :-\n    '$rosetta_call'(") && contains(prolog, "['$rosetta_slot'(0, Value0)], _, Writebacks)") &&
                contains(prolog, "'$rosetta_out'(Writebacks, 0, Value0, Value)"), "written-back arguments");
        require(contains(prolog, ":- meta_predicate each(2, -).") && contains(prolog, "'$rosetta_fn'(F, true)"), "closures");
        const auto& source = generated.source;
        require(contains(source, "#define ROSETTA_FULL_PROLOG") && contains(source, "rosetta_Api_call") && contains(source, "#include <SWI-Prolog.h>") &&
                contains(source, "install_t rosetta_Api_prolog_install(void)") &&
                source.find("#include <SWI-Prolog.h>") > source.find("rosetta_Api_init("), "C++ library and its foreign glue (SWI-Prolog's headers last)");
        const auto again = rosetta::generate_prolog(module, "Api", false, policy);
        require(generated.source == again.source && generated.prolog == again.prolog, "nondeterministic output");
        require(rosetta::prolog_module_name("ShapeKit") == "shape_kit", "default module name");
        std::cout << "PASS Prolog output, names and determinism\n";

        // Options.
        rosetta::PrologOptions options;
        options.module = "vendor_api";
        options.library = "api_native";
        options.parts = 3;
        const auto custom = rosetta::generate_prolog(module, "Api", false, policy, options);
        require(contains(custom.prolog, ":- module(vendor_api, [") && contains(custom.prolog, "'libapi_native'") &&
                contains(custom.source, "vendor_api:'$rosetta_callback'") == contains(generated.source, "api:'$rosetta_callback'") &&
                custom.parts.size() == 2 && !contains(custom.parts[0], "SWI-Prolog.h"), "options (the glue only in the first part)");
        options = {}; options.module = "Vendor";
        rejects([&] { rosetta::generate_prolog(module, "Api", false, policy, options); }, "module name");
        options = {}; options.module = "lists";
        rejects([&] { rosetta::generate_prolog(module, "Api", false, policy, options); }, "library module");
        options = {}; options.library = "lib/api";
        rejects([&] { rosetta::generate_prolog(module, "Api", false, policy, options); }, "library name");
        options = {}; options.parts = 0;
        rejects([&] { rosetta::generate_prolog(module, "Api", false, policy, options); }, "parts");
        rejects([&] { rosetta::generate_prolog(module, "bad-name", false, policy); }, "module name");
        std::cout << "PASS Prolog options\n";

        // CLI.
        parse("inline int value(int x = 42) { return x; }");
        const auto output = temporary.path() / "prolog bindings.cpp";
        const auto dep = temporary.path() / "prolog bindings.d";
        const std::vector<std::string> args{header.string(), "--language", "prolog", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_prolog_install") &&
                contains(rosetta::detail::read_file(temporary.path() / "api.pl"), "value(X, Result) :-"), "Prolog CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.pl"), "Prolog interface dependency missing");
        auto custom_args = args;
        const auto other = temporary.path() / "other.pl";
        custom_args.insert(custom_args.end(), {"--prolog-output", other.string(), "--prolog-module", "my_api", "--prolog-library", "native", "--parts", "2"});
        require(rosetta::run_cli(custom_args, out, error) == 0 && contains(rosetta::detail::read_file(other), ":- module(my_api, [") &&
                contains(rosetta::detail::read_file(other), "'libnative'") && std::filesystem::exists(temporary.path() / "prolog bindings_1.cpp"),
                "Prolog options and parts");
        auto invalid = args;
        invalid.insert(invalid.end(), {"--prolog-module", "My-Api"});
        require(rosetta::run_cli(invalid, out, error) == 1, "invalid module accepted");
        auto same = args;
        same.insert(same.end(), {"--prolog-output", header.string()});
        require(rosetta::run_cli(same, out, error) == 1 && contains(error.str(), "distinct paths"), "module over the header accepted");
        const std::vector<std::string> lua{header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--prolog-output", "x.pl"};
        require(rosetta::run_cli(lua, out, error) == 1 && contains(error.str(), "require --language prolog"), "Prolog-only option accepted for Lua");
        std::cout << "PASS Prolog CLI\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
