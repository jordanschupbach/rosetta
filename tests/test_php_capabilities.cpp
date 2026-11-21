// Generator and CLI checks of the PHP backend (--language php) that need no PHP: output shape,
// reserved and case-insensitive names, override compatibility, options and the CLI artifacts.
#include "rosetta/frontend.hpp"
#include "rosetta/php.hpp"
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
        const auto header = temporary.path() / "php api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++17"});
        };
        const auto policy_file = temporary.path() / "policy.json";
        rosetta::detail::write_file(policy_file, "{\"version\": 1, \"select\": \"all\", \"names\": \"flat\"}\n");
        const auto policy = rosetta::load_policy(policy_file);

        // Output shape, names and determinism.
        const auto module = parse("enum class Mode { on, On, Class };\n"
                                  "struct Cursor { int list() const { return 1; } int close() { return 0; } int getName() const { return 0; }\n"
                                  "  int get_name(int) const { return 1; } int loop = 2; };\n"
                                  "struct Point { double x = 0; bool operator==(const Point&) const { return true; } double operator[](int) const { return 0; } };\n"
                                  "struct array { int n = 0; };\n"
                                  "struct Base { int size() const { return 1; } double scale(double f) const { return f; } virtual ~Base() = default; virtual int area() const { return 0; } };\n"
                                  "struct Derived : Base { const char* size() const { return \"\"; } double scale(double f, double g) const { return f * g; } int area() const override { return 1; } };\n"
                                  "inline int twice(int value = 21) { return 2 * value; }\n"
                                  "inline int print(int x) { return x; }\n"
                                  "inline int pick(int) { return 0; } inline int pick(const char*) { return 1; }\n");
        const auto generated = rosetta::generate_php(module, "api", false, policy);
        const auto& php = generated.php;
        require(contains(php, "<?php\n") && contains(php, "declare(strict_types=1);") && contains(php, "namespace Api {") &&
                contains(php, "namespace Api\\Rosetta {") && contains(php, "abstract class NativeObject"), "file layout");
        require(contains(php, "int rosetta_api_call(") && contains(php, "'API_PHP_LIBRARY'") && contains(php, "'libapi.so'") &&
                !contains(php, "@NS@") && !contains(php, "@C@") && !contains(php, "@LIB@"), "runtime placeholders");
        require(contains(php, "enum Mode: int") && contains(php, "case On = 0;") && contains(php, "case On2 = 1;") &&
                contains(php, "case Class2 = 2;"), "enum cases");
        require(contains(php, "public function list(): int") && contains(php, "public function close2(): int") &&
                contains(php, "public function getName(): int") && contains(php, "public function getName2(int $arg1): int"), "method names");
        require(contains(php, "'loop' => ") && contains(php, "@property int $loop"), "properties");
        require(contains(php, "class Point extends NativeObject implements \\ArrayAccess") && contains(php, "public function equals(Point $arg1): bool") &&
                contains(php, "public function offsetGet(mixed $offset): mixed"), "operators");
        require(contains(php, "class Array_ extends NativeObject") && contains(php, "function print_(int $x): int"), "reserved words");
        require(contains(php, "class Derived extends Base") && contains(php, "public function area(): int") &&
                contains(php, "public function size2(): ?string") && contains(php, "public function scale2(float $f, float $g): float"),
                "overrides PHP rejects are renamed");
        require(contains(php, "function twice(int $value = 21): int") && contains(php, "\\array_slice([$value], 0, \\func_num_args())"),
                "default arguments: only the arguments passed");
        require(contains(php, "function pick(int|string|null $arg1): int"), "overloads as union types");
        require(contains(php, "static::class !== self::class"), "PHP subclasses are C++ subclasses");
        require(contains(generated.source, "#define ROSETTA_FULL_PHP") && contains(generated.source, "rosetta_api_call"), "C++ library");
        const auto again = rosetta::generate_php(module, "api", false, policy);
        require(generated.source == again.source && generated.php == again.php, "nondeterministic output");
        std::cout << "PASS PHP output, names and determinism\n";

        // Options.
        rosetta::PhpOptions options;
        options.namespace_ = "Vendor\\Api";
        options.library = "api_native";
        options.parts = 3;
        const auto custom = rosetta::generate_php(module, "api", false, policy, options);
        require(contains(custom.php, "namespace Vendor\\Api {") && contains(custom.php, "\\Vendor\\Api\\Rosetta\\Runtime::call(") &&
                contains(custom.php, "'libapi_native.so'") && custom.parts.size() == 2, "options");
        options = {}; options.namespace_ = "Vendor\\class";
        rejects([&] { rosetta::generate_php(module, "api", false, policy, options); }, "namespace");
        options = {}; options.library = "lib/api";
        rejects([&] { rosetta::generate_php(module, "api", false, policy, options); }, "library name");
        options = {}; options.parts = 0;
        rejects([&] { rosetta::generate_php(module, "api", false, policy, options); }, "parts");
        rejects([&] { rosetta::generate_php(module, "bad-name", false, policy); }, "module name");
        std::cout << "PASS PHP options\n";

        // Class names PHP compares case-insensitively stay apart.
        const auto cased = parse("struct point { int x = 0; }; struct Point { int y = 0; }; inline int use(const Point& p) { return p.y; }");
        const auto both = rosetta::generate_php(cased, "api", false, policy);
        require(contains(both.php, "class Point extends") && contains(both.php, "class Point_ extends"), "case-colliding classes");
        std::cout << "PASS PHP name collisions\n";

        // CLI.
        parse("inline int value(int x = 42) { return x; }");
        const auto output = temporary.path() / "php bindings.cpp";
        const auto dep = temporary.path() / "php bindings.d";
        const std::vector<std::string> args{header.string(), "--language", "php", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
                contains(rosetta::detail::read_file(temporary.path() / "api.php"), "function value(int $x = 42): int"), "PHP CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.php"), "PHP interface dependency missing");
        auto custom_args = args;
        const auto other = temporary.path() / "other.php";
        custom_args.insert(custom_args.end(), {"--php-output", other.string(), "--php-namespace", "My\\Api", "--php-library", "native", "--parts", "2"});
        require(rosetta::run_cli(custom_args, out, error) == 0 && contains(rosetta::detail::read_file(other), "namespace My\\Api {") &&
                contains(rosetta::detail::read_file(other), "'libnative.so'") && std::filesystem::exists(temporary.path() / "php bindings_1.cpp"),
                "PHP options and parts");
        auto invalid = args;
        invalid.insert(invalid.end(), {"--php-namespace", "My\\list"});
        require(rosetta::run_cli(invalid, out, error) == 1, "reserved namespace accepted");
        const std::vector<std::string> lua{header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--php-output", "x.php"};
        require(rosetta::run_cli(lua, out, error) == 1 && contains(error.str(), "require --language php"), "PHP-only option accepted for Lua");
        std::cout << "PASS PHP CLI\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
