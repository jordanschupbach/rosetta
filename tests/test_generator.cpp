#include "rosetta/cli.hpp"
#include "rosetta/frontend.hpp"
#include "rosetta/lua.hpp"
#include "rosetta/r.hpp"
#include "process.hpp"
#include "type_spelling.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

template<class F> void rejects(F&& operation, const std::string& expected) {
    try { operation(); }
    catch (const rosetta::BindingError& e) {
        require(contains(e.what(), expected), "expected '" + expected + "', got: " + e.what());
        return;
    }
    throw std::runtime_error("expected failure containing: " + expected);
}

struct Fixture {
    rosetta::detail::TemporaryDirectory temporary;
    std::string clang;
    std::filesystem::path header;

    explicit Fixture(std::string compiler)
        : clang(std::move(compiler)), header(temporary.path() / "api with spaces.hpp") {}

    rosetta::Module parse(const std::string& source, const std::vector<std::string>& flags = {"-std=c++17"}) {
        rosetta::detail::write_file(header, source);
        return rosetta::parse(header, clang, flags);
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--echo") {
        std::cout << argv[2] << '\n';
        return 0;
    }
    if (argc != 2) { std::cerr << "expected Clang executable argument\n"; return 1; }
    const std::string clang = argv[1];
    using Test = std::pair<std::string, std::function<void(Fixture&)>>;
    const std::vector<Test> tests = {
        {"R6 generation and explicit capabilities", [](Fixture& f) {
            const auto module = f.parse("int add(int, int); struct Counter { Counter(int); int value() const; int count; }; ");
            rosetta::BindingPolicy policy;
            policy.classes["Counter"] = {};
            const auto generated = rosetta::generate_r(module, "api", rosetta::RObjectSystem::r6, false, policy);
            require(contains(generated.source, "R_init_api") && contains(generated.source, "R_forceSymbols(dll, TRUE)"), "native registration missing");
            require(contains(generated.interface, "R6::R6Class") && contains(generated.interface, "cloneable = FALSE") &&
                contains(generated.interface, "active = list(\"count\"") && contains(generated.interface, "exports[[\"add\"]]"), "R6 interface missing");
            rejects([&] { rosetta::generate_r(module, "api"); }, "class requires an owned class policy");
            const auto unsupported = f.parse("int ok(); int overloaded(int); int overloaded(double); void pointer(int*); ");
            rejects([&] { rosetta::generate_r(unsupported, "api"); }, "overloads");
            const auto partial = rosetta::generate_r(unsupported, "api", rosetta::RObjectSystem::r6, true);
            require(partial.diagnostics.size() == 3 && contains(partial.interface, "exports[[\"ok\"]]") &&
                !contains(partial.interface, "exports[[\"pointer\"]]"), "unsupported R declarations were silently exported");
            rejects([&] { rosetta::generate_r(module, "bad-name"); }, "module name");
        }},
        {"R CLI options, outputs and path protection", [](Fixture& f) {
            f.parse("int add(int, int);");
            const auto output = f.temporary.path() / "r bridge.cpp";
            const auto interface = f.temporary.path() / "r bridge.R";
            const auto depfile = f.temporary.path() / "r bridge.d";
            const auto ir = f.temporary.path() / "r bridge.json";
            const auto run = [&](std::vector<std::string> extra, int expected, const std::string& diagnostic) {
                std::vector<std::string> args{f.header.string(), "--module", "api", "-o", output.string(), "--clang", f.clang};
                args.insert(args.end(), extra.begin(), extra.end());
                std::ostringstream out, err;
                require(rosetta::run_cli(args, out, err) == expected, "unexpected R CLI status: " + err.str());
                require(contains(err.str(), diagnostic), "missing CLI diagnostic: " + err.str());
            };
            run({"--language", "r", "--dump-ir", ir.string(), "--depfile", depfile.string()}, 0, "");
            require(contains(rosetta::detail::read_file(output), "R_init_api") &&
                contains(rosetta::detail::read_file(interface), "`api` <- local"), "default R output missing");
            require(contains(rosetta::detail::read_file(depfile), "r\\ bridge.R"), "R interface dependency missing");
            const auto custom = f.temporary.path() / "interface.R";
            run({"--language", "r", "--object-system", "r6", "--r-output", custom.string()}, 0, "");
            require(std::filesystem::exists(custom), "custom R interface missing");
            for (const auto& system : {"s3", "s4"}) {
                run({"--language", "r", "--object-system", system, "--r-output", custom.string()}, 0, "");
                const auto text = rosetta::detail::read_file(custom);
                require(!contains(text, "R6::R6Class") && !contains(text, "requireNamespace(\"R6\""), "functional interface requires R6");
            }
            for (const auto& system : {"r5", "r4", "unknown"})
                run({"--language", "r", "--object-system", system}, 1, "unsupported R object system");
            run({"--object-system", "r6"}, 1, "require --language r");
            run({"--r-output", custom.string()}, 1, "require --language r");
            for (const auto& path : {f.header, output, ir, depfile})
                run({"--language", "r", "--r-output", path.string(), "--dump-ir", ir.string(), "--depfile", depfile.string()}, 1, "distinct paths");
            const auto alias = f.temporary.path() / "alias.R";
            std::filesystem::create_hard_link(f.header, alias);
            run({"--language", "r", "--r-output", alias.string()}, 1, "distinct paths");
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp", "int dependency();\n");
            f.parse("#include \"dependency.hpp\"\nint add(int, int);");
            run({"--language", "r", "--r-output", (f.temporary.path() / "dependency.hpp").string(), "--depfile", depfile.string()},
                1, "distinct paths");
        }},
        {"R object systems share a native bridge", [](Fixture& f) {
            const auto module = f.parse("struct Counter { Counter(int); int value() const; static int answer(); int count; const int initial; }; ");
            rosetta::BindingPolicy policy; policy.classes["Counter"] = {};
            const auto r6 = rosetta::generate_r(module, "api", rosetta::RObjectSystem::r6, false, policy);
            const auto s3 = rosetta::generate_r(module, "api", rosetta::RObjectSystem::s3, false, policy);
            const auto s4 = rosetta::generate_r(module, "api", rosetta::RObjectSystem::s4, false, policy);
            require(r6.source == s3.source && s3.source == s4.source, "object systems changed the native ABI");
            require(contains(s3.interface, "UseMethod") && contains(s3.interface, "registerS3method"), "S3 dispatch missing");
            require(contains(s4.interface, "methods::setClass") && contains(s4.interface, "methods::setMethod"), "S4 dispatch missing");
            for (const auto* text : {&s3.interface, &s4.interface}) {
                require(contains(*text, "Counter__get_count") && contains(*text, "Counter__set_count") &&
                    contains(*text, "Counter__close"), "functional class accessors missing");
                require(!contains(*text, "private$.ptr") && !contains(*text, "R6Class"), "R6 receiver leaked into functional interface");
            }
            const auto collision = f.parse("struct Counter { Counter(); int get_count(); int count; }; ");
            rejects([&] { rosetta::generate_r(collision, "api", rosetta::RObjectSystem::s3, false, policy); }, "export name collision");
            rejects([&] { rosetta::generate_r(collision, "api", rosetta::RObjectSystem::s4, false, policy); }, "export name collision");
        }},
        {"redeclarations and namespaces", [](Fixture& f) {
            const auto module = f.parse("namespace a { int f(int); int f(int); } namespace b { int f(int); }");
            const auto generated = rosetta::generate_lua(module, "api");
            require(module.functions.size() == 2, "deduplicate declarations");
            require(generated.diagnostics.empty(), "unexpected diagnostics");
            require(contains(generated.source, "\"a__f\"") && contains(generated.source, "\"b__f\""), "namespace exports");
        }},
        {"included declarations excluded", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp",
                "namespace dep { struct Hidden {}; int hidden(); }\n");
            const auto module = f.parse("#include \"dependency.hpp\"\nnamespace api { int visible(); int second(); }\n");
            require(module.functions.size() == 2 && module.functions[0].qualified_name() == "api::visible" &&
                    module.functions[1].qualified_name() == "api::second", "input header selection");
            require(module.diagnostics.empty(), "dependency declarations leaked");
        }},
        {"unsupported features and partial output", [](Fixture& f) {
            const auto module = f.parse("struct Object {}; int ok(); int f(int); double f(double); void bad(Object*); int def(int x=1); template<class T> T id(T);");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "unsupported declaration");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(contains(generated.source, "\"ok\"") && !contains(generated.source, "\"f\""), "partial exports");
            std::string diagnostics;
            for (const auto& d : generated.diagnostics) diagnostics += d + '\n';
            require(contains(diagnostics, "overloads") && contains(generated.source, "\"def\"") &&
                    contains(diagnostics, "unsupported type"), "missing capability diagnostics");
        }},
        {"default argument metadata and redeclarations", [](Fixture& f) {
            const auto module = f.parse("using Count = int; int value(Count, int); int value(int, int = 42); "
                "int all(int = 1, int = 2); struct Object { Object(int = 42); int read(int = 1) const; static int get(int = 2); };");
            require(module.functions.size() == 2 && module.functions[0].default_arguments == 1 &&
                module.functions[1].default_arguments == 2, "default metadata lost across canonical redeclarations");
            require(module.records[0].constructors[0].default_arguments == 1 &&
                module.records[0].methods[0].default_arguments == 1, "member defaults missing");
            rosetta::BindingPolicy policy;
            policy.classes["Object"] = {};
            const auto generated = rosetta::generate_lua(module, "api", false, policy);
            require(generated.diagnostics.empty(), "defaults rejected");
            require(contains(rosetta::serialize_ir(module), "\"default_arguments\": 2"), "IR defaults missing");
            auto invalid = module;
            invalid.functions[0].default_arguments = 3;
            rejects([&] { rosetta::generate_lua(invalid, "api", false, policy); }, "invalid default argument metadata");
            const auto hidden = f.parse("struct Object { int read(int = 1); private: int read(); };");
            rejects([&] { rosetta::generate_lua(hidden, "api", false, policy); }, "overloads");
            const auto private_constructor = f.parse("struct Object { Object(int, int = 1); private: Object(int); };");
            rejects([&] { rosetta::generate_lua(private_constructor, "api", false, policy); }, "constructor overloads");
            const auto templated = f.parse("struct Object { int read(int = 1); template<class T> int read(T); }; "
                "int value(int = 1); template<class T> int value(T);");
            const auto partial = rosetta::generate_lua(templated, "api", true, policy);
            require(std::any_of(partial.diagnostics.begin(), partial.diagnostics.end(), [](const std::string& diagnostic) {
                return contains(diagnostic, "default calls with member template overloads");
            }), "hidden member template overload was not rejected");
            require(!contains(partial.source, "{\"value\", rosetta_wrap_"), "unsupported template overload default was emitted");
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp", "int value();\n");
            const auto included = f.parse("#include \"dependency.hpp\"\nint value(int = 42); int safe();");
            const auto included_partial = rosetta::generate_lua(included, "api", true);
            require(!contains(included_partial.source, "{\"value\", rosetta_wrap_") && contains(included_partial.source, "{\"safe\", rosetta_wrap_"),
                "included overload escaped default-call ambiguity checks");
        }},
        {"invalid C++", [](Fixture& f) {
            rejects([&] { f.parse("this is not C++;"); }, "Clang rejected");
        }},
        {"forward template used as private opaque storage", [](Fixture& f) {
            const auto module = f.parse("template<class T> class Hidden; "
                "class Owner { Hidden<int>* private_; public: ~Owner() noexcept; int value() const; }; int answer();");
            rosetta::BindingPolicy policy;
            policy.classes["Owner"].constructors = false;
            require(module.diagnostics.empty(), "opaque forward template diagnosed as an export");
            require(rosetta::generate_lua(module, "api", false, policy).diagnostics.empty(), "private opaque template blocked its facade");
            const auto defined = f.parse("template<class T> class Hidden {}; int answer();");
            rejects([&] { rosetta::generate_lua(defined, "api"); }, "ClassTemplateDecl");
            const auto exposed = f.parse("template<class T> class Hidden; int unsupported(Hidden<int>* value);");
            rejects([&] { rosetta::generate_lua(exposed, "api"); }, "unsupported type");
        }},
        {"C++20 compile-time facade and rejection boundaries", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "compile_time.hpp", R"cpp(
                #include <type_traits>
                template<class T> concept Integral = std::is_integral_v<T>;
                template<Integral T> int constrained(T value) { return value; }
                template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
                int selected(T value) { return value; }
                consteval int immediate(int value) { return value * 2; }
                inline constinit int initialized = immediate(21);
            )cpp");
            const auto module = f.parse("#include \"compile_time.hpp\"\n"
                "inline int runtime_facade() { constexpr int result = immediate(21); return constrained(result); }\n",
                {"-std=c++20"});
            require(module.functions.size() == 1 && module.functions.front().name == "runtime_facade", "compile-time dependency leaked");
            require(rosetta::generate_lua(module, "api").diagnostics.empty(), "valid C++20 facade rejected");
            const auto immediate = f.parse("consteval int fixed() { return 42; }", {"-std=c++20"});
            rejects([&] { rosetta::generate_lua(immediate, "api"); }, "constant-expression facade");
            for (const std::string body : {
                "inline int invalid() { return constrained(1.5); }",
                "inline int invalid() { return selected(1.5); }",
                "inline int invalid(int value) { return immediate(value); }"}) {
                rejects([&] { f.parse("#include \"compile_time.hpp\"\n" + body, {"-std=c++20"}); }, "Clang rejected");
            }
        }},
        {"invalid module name", [](Fixture& f) {
            const auto module = f.parse("int f();");
            rejects([&] { rosetta::generate_lua(module, "bad-name"); }, "module name");
        }},
        {"export name collisions", [](Fixture& f) {
            const auto module = f.parse("int a__f(); namespace a { int f(); }");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "collision");
        }},
        {"internal functions", [](Fixture& f) {
            const auto module = f.parse("int ok(); static int hidden(); namespace { int secret(); }");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "internal linkage");
        }},
        {"parameter alias and C linkage", [](Fixture& f) {
            const auto generated = rosetta::generate_lua(f.parse("using Count = int; extern \"C\" { int f(Count); }"), "api");
            require(generated.diagnostics.empty() && contains(generated.source, "rosetta_read<int>"), "canonical alias type");
        }},
        {"scoped enum aliases and source locations", [](Fixture& f) {
            const auto module = f.parse("namespace api {\n enum class Mode : unsigned char { low = 2, high = 9, alias = high };\n using Choice = Mode;\n Choice echo(Choice value);\n}\n");
            require(module.enumerations.size() == 1 && module.enumerations[0].scoped, "scoped enum missing");
            require(module.enumerations[0].qualified_name() == "api::Mode" && module.enumerations[0].enumerators.size() == 3, "enum members");
            const auto& function = module.functions.at(0);
            require(function.result.kind == rosetta::TypeKind::enumeration && function.result.canonical == "api::Mode", "enum return alias");
            require(function.parameters.at(0).declaration == "api::Mode", "enum parameter identity");
            require(function.location.line == 4 && function.location.column == 9, "elided source line/column");
            const auto generated = rosetta::generate_lua(module, "api");
            require(contains(generated.source, "\"api__Mode\"") && contains(generated.source, "unknown enum value"), "enum exports and validation");
            const auto json = nlohmann::json::parse(rosetta::serialize_ir(module));
            require(json["functions"][0]["result"]["kind"] == "enumeration" &&
                    json["functions"][0]["location"]["line"] == 4, "structured IR metadata");
        }},
        {"enum-only module and forward declarations", [](Fixture& f) {
            const auto module = f.parse("enum class Mode : int; enum class Mode : int { one = 1, two }; enum Plain { zero, next };\n");
            require(module.enumerations.size() == 2 && module.enumerations[0].enumerators.size() == 2, "enum redeclarations");
            require(!module.enumerations[1].scoped, "unscoped enum classification");
            require(contains(rosetta::generate_lua(module, "api").source, "\"Mode\""), "enum-only module rejected");
        }},
        {"dependency enums used without exporting dependencies", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp", "namespace dep { enum class Mode { one, two }; using Count = long; }\n");
            const auto module = f.parse("#include \"dependency.hpp\"\ndep::Mode echo(dep::Mode); dep::Count size();\n");
            require(!module.enumerations.at(0).selected, "dependency enum selected");
            require(module.functions[1].result.canonical == "long", "dependency return alias");
            const auto source = rosetta::generate_lua(module, "api").source;
            require(!contains(source, "\"dep__Mode\"") && contains(source, "::dep::Mode::one"), "dependency enum handling");
        }},
        {"scalar aliases resolve in defining scopes", [](Fixture& f) {
            const auto module = f.parse("using Count = int; namespace a { using Count = double; using Alias = Count; Alias first(Alias); namespace nested { Alias second(Alias); } } Count third(Count);\n");
            require(module.functions.at(0).result.canonical == "double" && module.functions.at(1).result.canonical == "double" &&
                    module.functions.at(2).result.canonical == "int", "alias scope resolution");
            require(module.functions[0].result.spelling == "Alias", "public alias spelling lost");
            rosetta::generate_lua(module, "api");
        }},
        {"inline namespace alias visibility", [](Fixture& f) {
            const auto module = f.parse("namespace api { inline namespace v1 { using Count = int; enum class Mode { one }; } "
                "Count echo(Count); Mode mode(Mode); namespace nested { Count value(Count); } }\n");
            require(module.functions.at(0).result.canonical == "int" && module.functions.at(2).result.canonical == "int", "inline alias injection");
            require(module.functions.at(1).result.declaration == "api::v1::Mode", "inline enum identity");
            rosetta::generate_lua(module, "api");
        }},
        {"namespace aliases and explicit using declarations", [](Fixture& f) {
            const auto module = f.parse("#include <vector>\n"
                "namespace real { using Count = long; enum class Mode { one }; struct Object { int value; }; } "
                "namespace short_name = real; namespace chain = short_name; namespace st = std; "
                "namespace outer { using short_name::Count; using short_name::Mode; using short_name::Object; "
                "Count count(Count); Mode mode(Mode); int object(Object*); st::vector<Mode> values(st::vector<Mode>); "
                "namespace local = chain; local::Mode nested(local::Mode); } "
                "chain::Count absolute(::chain::Count);");
            require(module.functions.size() == 6 && module.records.size() == 1 &&
                std::count_if(module.enumerations.begin(), module.enumerations.end(), [](const rosetta::Enumeration& e) { return e.selected; }) == 1,
                "namespace/type aliases duplicated declarations");
            require(module.functions[0].result.canonical == "long" && module.functions[0].result.spelling == "Count", "imported typedef identity/spelling lost");
            require(module.functions[1].result.declaration == "real::Mode" && module.functions[2].parameters[0].arguments[0].declaration == "real::Object", "imported enum/record identity lost");
            require(module.functions[3].result.declaration == "std::vector" && module.functions[3].result.arguments[0].declaration == "real::Mode", "namespace alias template/argument lookup failed");
            require(module.functions[4].result.declaration == "real::Mode" && module.functions[5].result.canonical == "long", "nested/chained namespace alias failed");
            rosetta::BindingPolicy policy; policy.classes["real::Object"] = {};
            require(rosetta::generate_lua(module, "api", false, policy).diagnostics.empty(), "namespace import generation rejected");
        }},
        {"imported class templates retain primary identities", [](Fixture& f) {
            const auto module = f.parse("#include <vector>\n#include <tuple>\n#include <optional>\n"
                "namespace names { using std::vector; using std::optional; inline namespace v1 { using std::tuple; } "
                "using Count = long; vector<Count> values(vector<Count>); optional<Count> option(); tuple<Count,int> product(); } "
                "namespace target = names; target::vector<int> qualified();");
            require(module.functions[0].result.declaration == "std::vector" && module.functions[0].result.arguments[0].canonical == "long",
                "imported vector primary or argument scope lost");
            require(module.functions[1].result.declaration == "std::optional" && module.functions[2].result.declaration == "std::tuple" &&
                module.functions[3].result.declaration == "std::vector", "inline or namespace-qualified template import lost");
            rosetta::generate_lua(module, "api");
        }},
        {"dependency and inline imports retain nested identities", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp", "namespace dep { using Count = long; struct Owner { enum class Mode { one }; }; }\n");
            const auto module = f.parse("#include \"dependency.hpp\"\nnamespace imported { inline namespace v1 { using dep::Count; using dep::Owner; } "
                "Count count(); Owner::Mode mode(Owner::Mode); } "
                "using LocalOwner = dep::Owner; LocalOwner::Mode other();");
            require(module.records.empty() && module.functions.size() == 3, "dependency import selected a class");
            require(module.functions[0].result.canonical == "long" && module.functions[1].result.declaration == "dep::Owner::Mode" &&
                module.functions[2].result.declaration == "dep::Owner::Mode", "inline or alias-qualified nested type identity lost");
            const auto generated = rosetta::generate_lua(module, "api");
            require(!contains(generated.source, "\"dep__Owner__Mode\""), "dependency import exported its enum table");
            const auto ir = nlohmann::json::parse(rosetta::serialize_ir(module));
            require(ir["functions"][1]["result"]["declaration"] == "dep::Owner::Mode", "imported canonical identity missing in IR");
        }},
        {"qualified template lookup and lexical using order", [](Fixture& f) {
            const auto module = f.parse("#include <vector>\nusing Count = int; namespace types { using Count = long; } "
                "namespace outer { Count before(); using types::Count; Count after(); ::std::vector<Count> values(); } "
                "namespace real { using Count = int; } namespace ns = real; "
                "namespace shadow { namespace ns { using Count = double; } ns::Count value(); }");
            require(module.functions[0].result.canonical == "int" && module.functions[1].result.canonical == "long", "late using declaration changed an earlier signature");
            require(module.functions[2].result.arguments[0].canonical == "long", "absolute primary erased template argument lexical scope");
            require(module.functions[3].result.canonical == "double", "outer namespace alias defeated a nearer actual namespace");
            rosetta::generate_lua(module, "api");
        }},
        {"class contexts finish before later namespace imports", [](Fixture& f) {
            const auto module = f.parse("using Count = int; namespace other { using Count = long; } "
                "namespace api { struct Object { Count value(); struct Nested { auto value(){return Local{};} }; using Local = double; }; using other::Count; }");
            const auto outer = std::find_if(module.records.begin(), module.records.end(), [](const rosetta::Record& r) { return r.qualified_name() == "api::Object"; });
            const auto inner = std::find_if(module.records.begin(), module.records.end(), [](const rosetta::Record& r) { return r.qualified_name() == "api::Object::Nested"; });
            require(outer != module.records.end() && inner != module.records.end() &&
                outer->methods[0].result.canonical == "int" && inner->methods[0].result.canonical == "double",
                "namespace imports or incomplete outer class lookup changed a member signature");
        }},
        {"using declarations participate in default call safety", [](Fixture& f) {
            const auto module = f.parse("namespace source { int choose(); template<class T> int generic(T); } "
                "namespace target { using source::choose; int choose(int = 42); using source::generic; int generic(int = 42); int safe(int=42); }");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(!contains(generated.source, "{\"target__choose\", rosetta_wrap_") &&
                !contains(generated.source, "{\"target__generic\", rosetta_wrap_") &&
                contains(generated.source, "{\"target__safe\", rosetta_wrap_"), "imported overload escaped default-call safety");
        }},
        {"alias-equivalent function redeclarations", [](Fixture& f) {
            const auto module = f.parse("using Count = int; Count echo(Count); int echo(int);\n");
            require(module.functions.size() == 1, "canonical redeclarations appeared as overloads");
            rosetta::generate_lua(module, "api");
        }},
        {"nested value types and const-reference aliases", [](Fixture& f) {
            const auto module = f.parse("#include <string>\n#include <string_view>\n#include <vector>\n#include <array>\n#include <optional>\n"
                "namespace api { using Text = std::string; using Words = std::vector<Text>; "
                "Text echo(const Text&); std::array<Words, 2> grid(const std::array<Words, 2>&); "
                "std::optional<Text> maybe(std::optional<Text>); std::string_view view(std::string_view); "
                "void mutable_text(Text&); void movable_text(Text&&); "
                "std::vector<std::optional<int>> holes(); std::optional<std::optional<int>> ambiguous(); "
                "std::wstring wide(); }\n");
            const auto& parameter = module.functions.at(0).parameters.at(0);
            require(parameter.kind == rosetta::TypeKind::lvalue_reference && parameter.arguments.size() == 1, "reference node missing");
            require(parameter.arguments[0].is_const && parameter.arguments[0].kind == rosetta::TypeKind::template_instance &&
                    parameter.arguments[0].arguments[0].canonical == "char", "qualified alias child metadata");
            const auto& grid = module.functions.at(1).result;
            require(grid.arguments.size() == 2 && grid.arguments[0].arguments[0].arguments[0].canonical == "char", "nested template graph");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "unsupported type");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(generated.diagnostics.size() == 5, "unsupported mutable/ambiguous/wide values were accepted");
            require(contains(generated.source, "\"api__echo\"") && contains(generated.source, "\"api__grid\"") &&
                    contains(generated.source, "\"api__maybe\"") && contains(generated.source, "\"api__view\""), "value bindings omitted");
            const auto json = nlohmann::json::parse(rosetta::serialize_ir(module));
            require(json["functions"][0]["parameters"][0]["arguments"][0]["const"] == true, "qualified IR child");
        }},
        {"product types and capability boundaries", [](Fixture& f) {
            const auto module = f.parse("#include <tuple>\n#include <utility>\n#include <string>\n#include <optional>\n"
                "using Empty = std::tuple<>; Empty empty(Empty); "
                "std::tuple<const std::string, bool> valid(std::pair<int, std::optional<double>>); "
                "std::tuple<int&> borrowed(std::tuple<int&>); std::tuple<volatile int> vol(); "
                "std::tuple<std::optional<std::optional<int>>> ambiguous();");
            require(module.functions[0].result.arguments.empty() && module.functions[0].result.canonical == "std::tuple<>",
                "empty product had a phantom template argument");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(generated.diagnostics.size() == 3, "unsafe product elements accepted");
            require(contains(generated.source, "\"empty\"") && contains(generated.source, "\"valid\""), "product exports missing");
        }},
        {"variant capabilities and nil representation", [](Fixture& f) {
            const auto module = f.parse("#include <variant>\n#include <vector>\n#include <optional>\n#include <string>\n#include <functional>\n"
                "std::variant<std::monostate, int, int> valid(std::variant<std::monostate,int,int>); "
                "std::optional<std::variant<std::monostate,int>> optional_sum(); "
                "std::vector<std::variant<std::monostate,int>> sums(); "
                "std::vector<std::monostate> holes(); std::optional<std::monostate> ambiguous(); "
                "void borrowed(std::function<std::variant<std::string_view,int>()>);");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(generated.diagnostics.size() == 3, "nil or callback view ambiguity accepted");
            require(contains(generated.source, "\"valid\"") && contains(generated.source, "\"optional_sum\"") &&
                contains(generated.source, "\"sums\""), "variant bindings missing");
        }},
        {"extended container capabilities", [](Fixture& f) {
            const auto module = f.parse("#include <deque>\n#include <list>\n#include <forward_list>\n#include <set>\n#include <unordered_set>\n#include <map>\n#include <unordered_map>\n#include <optional>\n#include <string>\n#include <functional>\n"
                "std::deque<int> deque(std::deque<int>); std::list<int> list(); std::forward_list<int> forward(); "
                "std::set<std::optional<int>> boxed(); std::unordered_multiset<std::string> multi(); "
                "std::multimap<int,int> pairs(); std::unordered_multimap<int,int> hash_pairs(); "
                "std::list<std::optional<int>> holes(); std::set<int,std::greater<int>> custom(); "
                "std::unordered_set<std::tuple<int,int>> hash_policy(); std::set<std::unordered_set<int>> comparison(); "
                "std::map<std::unordered_multiset<int>,int> map_comparison(); "
                "void borrowed(std::function<std::set<std::string_view>()>);");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(generated.diagnostics.size() == 6, "container policy, comparison, nil or callback borrowing accepted");
            for (const auto* name : {"deque", "list", "forward", "boxed", "multi", "pairs", "hash_pairs"})
                require(contains(generated.source, std::string("{\"") + name + "\", rosetta_wrap_"), "container export missing");
        }},
        {"map value capabilities and default native policies", [](Fixture& f) {
            const auto module = f.parse("#include <map>\n#include <unordered_map>\n#include <tuple>\n#include <string>\n#include <optional>\n"
                "std::map<std::string, int> dict(std::map<std::string, int>); "
                "std::map<int, std::optional<std::string>> entries(std::map<int, std::optional<std::string>>); "
                "std::unordered_map<std::string, int> hashed(std::unordered_map<std::string, int>); "
                "std::map<std::string, int, std::less<>> custom(); "
                "std::unordered_map<std::tuple<int, int>, int> hash_policy(); "
                "std::map<std::unordered_map<int, int>, int> comparison_policy(); "
                "std::map<int, int&> references();");
            const auto generated = rosetta::generate_lua(module, "api", true);
            require(generated.diagnostics.size() == 4, "map policy/reference gaps were silently accepted");
            require(contains(generated.source, "\"dict\"") && contains(generated.source, "\"entries\"") &&
                contains(generated.source, "\"hashed\""), "map bindings omitted");
        }},
        {"recursive enum codecs and read-side completeness", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp", "namespace dep { enum class Mode : unsigned char { one=1, two=2 }; }\n");
            const auto module = f.parse("#include <vector>\n#include <tuple>\n#include <functional>\n#include \"dependency.hpp\"\n"
                "std::vector<dep::Mode> values(std::vector<dep::Mode>); "
                "std::tuple<dep::Mode> invoke(std::function<dep::Mode(dep::Mode)>);");
            rosetta::BindingPolicy policy;
            policy.callbacks["invoke"][1] = rosetta::CallbackLifetime::call;
            const auto source = rosetta::generate_lua(module, "api", false, policy).source;
            require(contains(source, "struct rosetta_codec<::dep::Mode>") && !contains(source, "\"dep__Mode\""),
                "dependency codec missing or dependency table leaked");
            const auto forward = f.parse("#include <vector>\n#include <functional>\nenum class Forward : unsigned; "
                "std::vector<Forward> output(); void notify(std::function<void(Forward)>); "
                "void input(std::vector<Forward>); void bad(std::function<Forward()>);");
            rosetta::BindingPolicy forward_policy;
            forward_policy.callbacks["notify"][1] = rosetta::CallbackLifetime::call;
            const auto partial = rosetta::generate_lua(forward, "api", true, forward_policy);
            require(partial.diagnostics.size() == 2 && contains(partial.source, "\"output\"") && contains(partial.source, "\"notify\""),
                "enum completeness did not distinguish read/write paths");
            require(contains(partial.source, "enum definition required"), "forward enum codec has no read guard");
        }},
        {"explicit enum flag policy and schema", [](Fixture& f) {
            const auto module = f.parse("enum class Flags : unsigned char { one=1, two=2 }; Flags echo(Flags);");
            const auto path = f.temporary.path() / "flags.policy.json";
            rosetta::detail::write_file(path, R"({"version":1,"enums":{"Flags":{"flags":["one","two"]}}})");
            const auto policy = rosetta::load_policy(path);
            require(!policy.explicit_class_selection && policy.enum_flags.at("Flags").size() == 2, "flag policy changed class selection");
            require(contains(rosetta::generate_lua(module, "api", false, policy).source, "unknown enum flag bits"), "flag validation missing");
            require(contains(rosetta::generate_lua(module, "api").source, "unknown enum value"), "closed enum default changed");
            for (const auto& invalid : {
                R"({"version":1,"enums":[]})", R"({"version":1,"enums":{"":{"flags":["one"]}}})",
                R"({"version":1,"enums":{"Flags":{}}})", R"({"version":1,"enums":{"Flags":{"flags":[]}}})",
                R"({"version":1,"enums":{"Flags":{"flags":["one","one"]}}})",
                R"({"version":1,"enums":{"Flags":{"flags":"one"}}})",
                R"({"version":1,"enums":{"Flags":{"flags":["one"],"flag":true}}})"}) {
                rosetta::detail::write_file(path, invalid);
                rejects([&] { rosetta::load_policy(path); }, "enum policy");
            }
            auto unknown = policy;
            unknown.enum_flags["Typo"] = {"one"};
            rejects([&] { rosetta::generate_lua(module, "api", true, unknown); }, "unknown enum");
            unknown = policy;
            unknown.enum_flags["Flags"] = {"typo"};
            rejects([&] { rosetta::generate_lua(module, "api", true, unknown); }, "unknown enumerator");
            unknown.enum_flags["Flags"] = {};
            rejects([&] { rosetta::generate_lua(module, "api", true, unknown); }, "must not be empty");
            const auto forward = f.parse("enum class Flags : unsigned; int ok();");
            rejects([&] { rosetta::generate_lua(forward, "api", true, policy); }, "requires an enum definition");
        }},
        {"enum exports share function collision checks", [](Fixture& f) {
            const auto module = f.parse("namespace api { enum Mode { one }; } int api__Mode();\n");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "collision");
        }},
        {"incomplete enum parameters require a definition", [](Fixture& f) {
            const auto module = f.parse("enum class Mode : int; void use(Mode); int ok();\n");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "unsupported type");
        }},
        {"empty supported module", [](Fixture& f) {
            const auto module = f.parse("struct Object {};");
            rejects([&] { rosetta::generate_lua(module, "api", true); }, "no supported functions");
        }},
        {"nested declaration scopes and out-of-class definitions", [](Fixture& f) {
            const auto module = f.parse("namespace n { class Outer { "
                "enum class Hidden { no }; struct Private; protected: enum Protected { no }; "
                "public: using Count = int; enum class Mode : unsigned char; enum Plain { first, second }; struct Child; }; "
                "enum class Outer::Mode : unsigned char { ready=1, active=2 }; "
                "struct Outer::Child { using Value = Count; enum class State { one=1 }; "
                "Value value(Value x) const; private: void secret(); }; "
                "inline Outer::Child::Value Outer::Child::value(Value x = 42) const { return x; } "
                "inline void Outer::Child::secret() {} "
                "struct Outer::Private { struct Deep; enum Exposed { still_private }; }; "
                "struct Outer::Private::Deep { enum More { also_private }; }; }");
            const auto child = std::find_if(module.records.begin(), module.records.end(), [](const rosetta::Record& r) {
                return r.qualified_name() == "n::Outer::Child";
            });
            require(module.records.size() == 2 && child != module.records.end() && child->owner == "n::Outer", "nested records/access paths");
            require(child->methods.size() == 2 && child->methods[0].default_arguments == 1 &&
                child->methods[0].parameters[0].canonical == "int", "out-of-class defaults/aliases/redeclarations");
            require(child->methods[1].access == "private", "definition changed original member access");
            require(module.enumerations.size() == 3 && std::none_of(module.enumerations.begin(), module.enumerations.end(), [](const rosetta::Enumeration& e) {
                return e.owner.find("Private") != std::string::npos || e.name == "Hidden" || e.name == "Protected";
            }), "private/protected nested enum leaked");
            rosetta::BindingPolicy policy;
            policy.classes["n::Outer::Child"] = {};
            const auto source = rosetta::generate_lua(module, "api", false, policy).source;
            require(contains(source, "\"n__Outer__Child\"") && contains(source, "\"n__Outer__Mode\"") && !contains(source, "\"secret\""), "nested exports");
            policy.classes["n::Outer::Private"] = {};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "not found");
            const auto ir = nlohmann::json::parse(rosetta::serialize_ir(module));
            require(std::any_of(ir["enumerations"].begin(), ir["enumerations"].end(), [](const auto& e) { return e["owner"] == "n::Outer::Child"; }), "enum owner IR missing");
        }},
        {"dependency nested enums without dependency class exports", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp",
                "namespace dep { class Types { enum Private { secret }; public: enum class Mode { one, two }; struct Child { enum Value { first }; }; }; }\n");
            const auto module = f.parse("#include \"dependency.hpp\"\ndep::Types::Mode echo(dep::Types::Mode); dep::Types::Child::Value child(dep::Types::Child::Value);");
            require(module.records.empty() && module.enumerations.size() == 2 && !module.enumerations[0].selected, "dependency declarations selected");
            const auto source = rosetta::generate_lua(module, "api").source;
            require(contains(source, "::dep::Types::Mode::one") && !contains(source, "\"dep__Types__Mode\""), "dependency nested enum codec");
        }},
        {"default safety checks follow declaration merging", [](Fixture& f) {
            const auto free = f.parse("int choose(const int&, int); int choose(const int&, int = 1); "
                "template<class T> int choose(T&&); int safe();");
            const auto partial = rosetta::generate_lua(free, "api", true);
            require(!contains(partial.source, "\"choose\"") && contains(partial.source, "\"safe\""), "merged default bypassed overload safety");
            const auto member = f.parse("struct Object { int choose(const int&, int) const; template<class T> int choose(T&&) const; }; "
                "inline int Object::choose(const int& value, int extra = 1) const { return value + extra; }");
            rosetta::BindingPolicy policy;
            policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_lua(member, "api", false, policy); }, "member template overloads");
        }},
        {"class metadata, selection and lifetime policies", [](Fixture& f) {
            const auto module = f.parse("namespace api { class Counter { public: using Value = int; explicit Counter(Value); "
                "Counter(const Counter&); Counter(Counter&&) noexcept; ~Counter() noexcept; Value value() const; void add(Value); "
                "Counter& alias(); const Counter& view() const; static int live(); int field; private: int hidden; void secret(); }; "
                "struct Unselected {}; int read(const Counter&); }\n");
            require(module.records.size() == 2, "record discovery");
            const auto& record = module.records[0];
            require(record.qualified_name() == "api::Counter" && record.constructors.size() == 3, "constructor discovery");
            require(record.fields.at(0).access == "public" && record.fields.at(1).access == "private", "field access levels");
            require(record.methods.at(0).is_const && record.methods.at(0).result.canonical == "int", "const method and nested alias resolution");
            require(module.functions.at(0).parameters.at(0).arguments.at(0).kind == rosetta::TypeKind::record, "record reference type");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "explicit class policy");
            rosetta::BindingPolicy policy;
            policy.classes["api::Counter"] = {};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "borrow_self");
            policy.classes["api::Counter"].borrow_self = {"alias", "view"};
            const auto generated = rosetta::generate_lua(module, "api", false, policy);
            require(generated.diagnostics.empty() && contains(generated.source, "\"api__Counter\""), "selected class export");
            require(!contains(generated.source, "\"secret\"") && !contains(generated.source, "\"get_hidden\""), "nonpublic members leaked");
            require(contains(generated.source, "rosetta_publish_borrow<::api::Counter>") &&
                    contains(generated.source, "rosetta_new_object<::api::Counter>"), "lifetime lowering");
            const auto ir = nlohmann::json::parse(rosetta::serialize_ir(module));
            require(ir["records"][0]["methods"][0]["const"] == true && ir["records"][0]["fields"][1]["access"] == "private", "record IR metadata");
        }},
        {"raw member pointers require explicit adapters", [](Fixture& f) {
            const auto module = f.parse("struct Object {}; using Field = int Object::*; using Method = int (Object::*)(int) const; "
                "Field field(); void accept(Field); Method method(); void invoke(Method); int safe();");
            rosetta::BindingPolicy policy; policy.classes["Object"] = {};
            const auto generated = rosetta::generate_lua(module, "api", true, policy);
            require(generated.diagnostics.size() == 4 && contains(generated.source, "{\"safe\", rosetta_wrap_") &&
                !contains(generated.source, "{\"field\", rosetta_wrap_") && !contains(generated.source, "{\"method\", rosetta_wrap_"),
                "raw member pointer representation was accepted as a wire value");
        }},
        {"volatile and atomic storage require explicit accessors", [](Fixture& f) {
            const auto module = f.parse("#include <atomic>\nstruct Direct { volatile int value; std::atomic<int> counter; }; "
                "struct Facade { int load() const; void store(int); private: volatile int value; std::atomic<int> counter; };");
            require(module.records[0].fields[0].type.is_volatile, "volatile storage qualifier was discarded");
            rosetta::BindingPolicy policy; policy.classes["Direct"] = {}; policy.classes["Facade"] = {};
            const auto generated = rosetta::generate_lua(module, "api", true, policy);
            require(generated.diagnostics.size() == 2 && contains(generated.diagnostics[0], "Direct::value") &&
                contains(generated.diagnostics[1], "Direct::counter") && contains(generated.source, "\"load\"") && contains(generated.source, "\"store\""),
                "qualified-storage accessors were rejected or raw fields accepted silently");
        }},
        {"class policy rejects private, unknown and mismatched rules", [](Fixture& f) {
            const auto module = f.parse("class Object { public: Object(); ~Object() noexcept; int value() const; private: void secret(); int field; };\n");
            rosetta::BindingPolicy policy;
            auto& config = policy.classes["Object"];
            config.restrict_methods = true;
            config.methods = {"secret"};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "not public");
            config.methods = {"missing"};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "unknown method");
            config.methods = {"value"}; config.borrow_self = {"value"};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "borrow_self requires");
            config.borrow_self.clear(); config.restrict_fields = true; config.fields = {"field"};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "nonpublic field");
            config.fields = {"missing"};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "unknown field");
            config.fields.clear(); config.borrow_self = {"secret"};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "unselected method");
            policy.classes.clear(); policy.classes["Typo"] = {};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "not found");
        }},
        {"class construction restrictions and default constructor", [](Fixture& f) {
            auto module = f.parse("struct Plain { int x; const int version = 1; }; class Hidden { private: ~Hidden(); };\n");
            rosetta::BindingPolicy policy;
            policy.classes["Plain"] = {};
            auto source = rosetta::generate_lua(module, "api", false, policy).source;
            require(contains(source, "new ::Plain()") && contains(source, "\"get_x\"") && contains(source, "\"set_x\"") &&
                contains(source, "\"get_version\"") && !contains(source, "\"set_version\""), "aggregate/const field behavior");
            policy.classes["Hidden"] = {};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "public nonthrowing destructor");
            module = f.parse("struct Overloaded { Overloaded(int); Overloaded(double); };\n");
            policy.classes.clear(); policy.classes["Overloaded"] = {};
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "constructor overloads");
            policy.classes["Overloaded"].constructors = false;
            rosetta::generate_lua(module, "api", false, policy);
        }},
        {"policy file schema and CLI integration", [](Fixture& f) {
            f.parse("class Object { public: explicit Object(int); int value() const; };\n");
            const auto policy_path = f.temporary.path() / "policy.json";
            const auto output = f.temporary.path() / "object.cpp";
            rosetta::detail::write_file(policy_path, R"({"version":1,"classes":{"Object":{"ownership":"owned","methods":["value"],"fields":[]}}})");
            auto policy = rosetta::load_policy(policy_path);
            require(policy.classes.at("Object").restrict_methods && policy.explicit_class_selection, "policy schema");
            std::ostringstream out, err;
            require(rosetta::run_cli({f.header.string(), "--module", "api", "--clang", f.clang, "--policy", policy_path.string(),
                "-o", output.string()}, out, err) == 0, "policy CLI failed: " + err.str());
            require(contains(rosetta::detail::read_file(output), "\"Object\""), "policy-selected output missing");
            require(rosetta::run_cli({f.header.string(), "--module", "api", "--policy", policy_path.string(), "-o", policy_path.string()}, out, err) == 1,
                "policy file overwrite permitted");
            for (const std::string invalid : {R"({"version":2})", R"({"version":1,"classses":{}})",
                R"({"version":1,"classes":{"Object":{}}})", R"({"version":1,"classes":{"Object":{"ownership":"owned","methodz":[]}}})",
                R"({"version":1,"classes":{"Object":{"ownership":"owned","methods":["value","value"]}}})"}) {
                rosetta::detail::write_file(policy_path, invalid);
                rejects([&] { rosetta::load_policy(policy_path); }, "policy");
            }
        }},
        {"deleted and immediate functions", [](Fixture& f) {
            const auto module = f.parse("int ok(); int removed(int) = delete; consteval int now(int x) { return x; }", {"-std=c++20"});
            rejects([&] { rosetta::generate_lua(module, "api"); }, "deleted functions");
            const auto partial = rosetta::generate_lua(module, "api", true);
            std::string diagnostics;
            for (const auto& diagnostic : partial.diagnostics) diagnostics += diagnostic;
            require(contains(diagnostics, "deleted functions") && contains(diagnostics, "constant-expression facade"),
                "missing separate deleted/immediate diagnostics");
        }},
        {"nested function spellings and callback type metadata", [](Fixture& f) {
            const auto signature = rosetta::detail::function_spelling("std::function<int (int)> (const std::vector<std::function<void (int)>> &) const noexcept");
            require(signature && signature->result == "std::function<int (int)>" && signature->suffix == "const noexcept", "balanced function type spelling");
            const auto module = f.parse("#include <functional>\n#include <string>\n#include <vector>\n"
                "namespace api { using Signature = std::string(const std::vector<int>&, int); using Handler = std::function<Signature>; "
                "std::string invoke(const Handler&, const std::vector<int>&, int); }\n");
            const auto& type = module.functions.at(0).parameters.at(0).arguments.at(0);
            require(type.kind == rosetta::TypeKind::template_instance && type.arguments.at(0).kind == rosetta::TypeKind::function &&
                    type.arguments.at(0).arguments.size() == 3, "callback signature tree");
            rejects([&] { rosetta::generate_lua(module, "api"); }, "explicit call or retained lifetime policy");
            rosetta::BindingPolicy policy;
            policy.callbacks["api::invoke"][1] = rosetta::CallbackLifetime::call;
            const auto generated = rosetta::generate_lua(module, "api", false, policy);
            require(contains(generated.source, "rosetta_callback<") && contains(generated.source, "callback_scope"), "callback lowering missing");
            const auto ir = nlohmann::json::parse(rosetta::serialize_ir(module));
            require(ir["functions"][0]["parameters"][0]["arguments"][0]["arguments"][0]["kind"] == "function", "callback IR function node");
            policy.callbacks["api::invoke"][2] = rosetta::CallbackLifetime::call;
            rejects([&] { rosetta::generate_lua(module, "api", false, policy); }, "not a std::function argument");
        }},
        {"callback safety capability checks", [](Fixture& f) {
            const auto module = f.parse("#include <functional>\n#include <string_view>\n"
                "void no_throw(std::function<void()> callback) noexcept; "
                "void borrowed(std::function<std::string_view()> callback); "
                "void mutable_callback(std::function<int(int)>& callback); int ok();\n");
            require(module.functions.at(0).is_noexcept, "native noexcept metadata");
            const auto generated = rosetta::generate_lua(module, "api", true);
            std::string diagnostics;
            for (const auto& message : generated.diagnostics) diagnostics += message + '\n';
            require(contains(diagnostics, "noexcept") && contains(diagnostics, "unsupported callback signature"), "unsafe callbacks accepted");
            rosetta::BindingPolicy policy;
            policy.callbacks["Typo"][1] = rosetta::CallbackLifetime::retained;
            rejects([&] { rosetta::generate_lua(module, "api", true, policy); }, "unselected or unsupported callable");
        }},
        {"callback policy schema without class selection", [](Fixture& f) {
            const auto path = f.temporary.path() / "callbacks.json";
            rosetta::detail::write_file(path, R"({"version":1,"callbacks":{"api::invoke":{"1":"call","2":"retained"}}})");
            const auto policy = rosetta::load_policy(path);
            require(!policy.explicit_class_selection && policy.callbacks.at("api::invoke").at(1) == rosetta::CallbackLifetime::call &&
                policy.callbacks.at("api::invoke").at(2) == rosetta::CallbackLifetime::retained, "callback schema/lifetimes");
            for (const std::string invalid : {R"({"version":1,"callbacks":{"fn":{"0":"call"}}})",
                R"({"version":1,"callbacks":{"fn":{"01":"call"}}})", R"({"version":1,"callbacks":{"fn":{"1":"forever"}}})",
                R"({"version":1,"callbacks":{"fn":{}}})"}) {
                rosetta::detail::write_file(path, invalid);
                rejects([&] { rosetta::load_policy(path); }, "callback policy");
            }
        }},
        {"partial module exact signature", [](Fixture& f) {
            const auto generated = rosetta::generate_lua(f.parse("int f(int); int f(int, ...);"), "api", true);
            require(!generated.diagnostics.empty() && contains(generated.source, "static_cast<int (*)(int)>(&::f)"), "exact signature dispatch");
            rejects([&] { rosetta::generate_lua(f.parse("int f(int); int f(int, int = 0);"), "api"); }, "overloads");
        }},
        {"macro source provenance", [](Fixture& f) {
            rosetta::detail::write_file(f.temporary.path() / "dependency.hpp", "#define API(name) int name(int);\n");
            const auto module = f.parse("#include \"dependency.hpp\"\nAPI(visible)\nint after();\n");
            require(module.functions.size() == 2 && module.functions[0].name == "visible" &&
                    module.functions[1].name == "after", "macro expansion filename tracking");
        }},
        {"CLI protects input and hard links", [](Fixture& f) {
            f.parse("int f();");
            std::ostringstream out, err;
            auto args = std::vector<std::string>{f.header.string(), "--module", "api", "-o", f.header.string()};
            require(rosetta::run_cli(args, out, err) == 1 && contains(err.str(), "distinct paths"), "overwrite protection");
            const auto alias = f.temporary.path() / "hardlink.hpp";
            std::filesystem::create_hard_link(f.header, alias);
            args.back() = alias.string();
            require(rosetta::run_cli(args, out, err) == 1, "hardlink protection");
            require(rosetta::detail::read_file(f.header) == "int f();", "input modified");
        }},
        {"CLI outputs and compiler arguments", [](Fixture& f) {
            rosetta::detail::write_file(f.header, "#ifndef ENABLE_API\n#error missing flag\n#endif\nint ok(); struct Unsupported {};\n");
            const auto output = f.temporary.path() / "nested" / "api.cpp";
            const auto ir = f.temporary.path() / "other" / "api.json";
            std::ostringstream out, err;
            require(rosetta::run_cli({f.header.string(), "--module", "api", "-o", output.string(),
                "--clang", f.clang, "--dump-ir", ir.string(), "--skip-unsupported", "--", "-DENABLE_API=1"}, out, err) == 0,
                "CLI failed: " + err.str());
            require(contains(err.str(), "warning: Unsupported"), "partial-output warning");
            require(contains(rosetta::detail::read_file(output), "luaopen_api"), "generated source missing");
            const auto json = nlohmann::json::parse(rosetta::detail::read_file(ir));
            require(json.at("functions").size() == 1 && json.at("functions")[0].at("name") == "ok", "IR shape");
            require(json.at("header") == std::filesystem::canonical(f.header).string(), "IR header");
        }},
        {"CLI invalid options", [](Fixture&) {
            std::ostringstream out, err;
            require(rosetta::run_cli({}, out, err) == 1, "missing arguments accepted");
            require(rosetta::run_cli({"--unknown"}, out, err) == 1, "unknown option accepted");
            require(rosetta::run_cli({"--clang"}, out, err) == 1, "missing value accepted");
            require(rosetta::run_cli({"--language", "javascript"}, out, err) == 1, "unsupported language accepted");
        }},
        {"missing compiler", [](Fixture& f) {
            f.parse("int ok();");
            rejects([&] { rosetta::parse(f.header, (f.temporary.path() / "missing-compiler").string()); }, "cannot execute compiler");
        }},
        {"compiler arguments never use a shell", [self = std::filesystem::absolute(argv[0]).string()](Fixture& f) {
            const auto marker = f.temporary.path() / "must-not-exist";
            const auto value = "literal;touch " + marker.string();
            const auto process = rosetta::detail::run_process({self, "--echo", value});
            require(process.exit_code == 0 && process.output == value + '\n', "argument changed");
            require(!std::filesystem::exists(marker), "shell interpreted argument");
        }},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            Fixture fixture(clang);
            test(fixture);
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::cout << tests.size() << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
