// Generator and CLI checks of the Perl backend (--language perl) that need no Perl: output shape,
// names, the XS glue, options and the CLI artifacts.
#include "rosetta/frontend.hpp"
#include "rosetta/perl.hpp"
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
        const auto header = temporary.path() / "perl api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++17"});
        };
        const auto policy_file = temporary.path() / "policy.json";
        rosetta::detail::write_file(policy_file, "{\"version\": 1, \"select\": \"all\", \"names\": \"flat\"}\n");
        const auto policy = rosetta::load_policy(policy_file);

        // Output shape, names and determinism.
        const auto module = parse("#include <string>\nenum class Mode { on, On, BEGIN };\n"
                                  "struct Cursor { int close() { return 0; } int getName() const { return 0; } int get_name(int) const { return 1; }\n"
                                  "  int Count() const { return 0; } int count = 2; static int make() { return 1; } };\n"
                                  "struct Point { double x = 0; Point operator+(const Point&) const { return {}; } bool operator==(const Point&) const { return true; }\n"
                                  "  double operator[](int) const { return 0; } std::string to_string() const { return \"\"; } };\n"
                                  "struct Error { int n = 0; };\n"
                                  "struct Base { virtual ~Base() = default; virtual int area() const { return 0; } };\n"
                                  "struct Other { virtual ~Other() = default; };\n"
                                  "struct Derived : Base, Other { int area() const override { return 1; } };\n"
                                  "inline int twice(int value = 21) { return 2 * value; }\n"
                                  "inline int print(int x) { return x; }\n"
                                  "inline void bump(int& value) { ++value; }\n"
                                  "constexpr double golden_ratio = 1.618;\n");
        const auto generated = rosetta::generate_perl(module, "api", false, policy);
        const auto& perl = generated.perl;
        require(contains(perl, "package Api;\n") && contains(perl, "use strict;\nuse warnings;") && contains(perl, "package Api::NativeObject;") &&
                contains(perl, "package Api::Rosetta;") && contains(perl, "\n1;\n"), "module layout");
        require(contains(perl, "=encoding utf8\n\n=head1 NAME\n\nApi - ") && contains(perl, "=head1 Api::Error\n") && contains(perl, "=head1 FUNCTIONS"), "POD");
        require(contains(perl, "\"rosetta_api_perl_$name\"") && contains(perl, "'API_PERL_LIBRARY'") && contains(perl, "'libapi.'") &&
                !contains(perl, "@NS@") && !contains(perl, "@C@") && !contains(perl, "@LIB@") && !contains(perl, "@PATH@"), "runtime placeholders");
        require(contains(perl, "sub ON () { 0 }") && contains(perl, "sub ON_2 () { 1 }") && contains(perl, "sub BEGIN_2 () { 2 }"), "enum constants");
        require(contains(perl, "sub close_2 {") && contains(perl, "sub get_name {") && contains(perl, "sub get_name_2 {") &&
                contains(perl, "sub count {") && contains(perl, "sub count_2 {") && contains(perl, "sub make {\n    shift;"), "method names");
        require(contains(perl, "package Api::Error_;") && contains(perl, "sub print_ {") && contains(perl, "sub GOLDEN_RATIO {"), "reserved names");
        require(contains(perl, "'+' => sub { Api::Rosetta::binary(@_, 'add', undef, '+') }") && contains(perl, "'\"\"' => sub { $_[0]->to_string }") &&
                contains(perl, "'bool' => sub { 1 },\n    fallback => 1;") && contains(perl, "sub at {"), "operators");
        require(contains(perl, "package Api::Derived;") && contains(perl, "our @ISA = ('Api::Base', 'Api::Other');") &&
                contains(perl, "ref($self) ne __PACKAGE__"), "multiple inheritance and directors");
        require(contains(perl, "%METHODS = (0 => 'area', );"), "virtual method table");
        require(contains(perl, "sub twice {\n    return Api::Rosetta::call(") && contains(perl, "C<$value = 21>"), "default arguments: only the arguments passed");
        require(contains(perl, "Api::Rosetta::slot(0, $args[0])") && contains(perl, "Api::Rosetta::write_back(\\@_, 0, $writebacks{0})"), "written-back arguments");
        require(contains(perl, "our @EXPORT_OK = qw(twice print_ bump GOLDEN_RATIO);") && contains(perl, "use Exporter 'import';"), "exports");
        const auto& source = generated.source;
        require(contains(source, "#define ROSETTA_FULL_PERL") && contains(source, "rosetta_api_call") && contains(source, "#include \"perl.h\"") &&
                contains(source, "void rosetta_api_perl_call(pTHX_ CV* cv)") && contains(source, "void rosetta_api_perl_init(pTHX_ CV* cv)") &&
                source.find("#include \"perl.h\"") > source.find("rosetta_api_init("), "C++ library and its XS glue (Perl's headers last)");
        const auto again = rosetta::generate_perl(module, "api", false, policy);
        require(generated.source == again.source && generated.perl == again.perl, "nondeterministic output");
        std::cout << "PASS Perl output, names and determinism\n";

        // Options.
        rosetta::PerlOptions options;
        options.package = "Vendor::Api";
        options.library = "api_native";
        options.parts = 3;
        const auto custom = rosetta::generate_perl(module, "api", false, policy, options);
        require(contains(custom.perl, "package Vendor::Api;\n") && contains(custom.perl, "Vendor::Api::Rosetta::call(") &&
                contains(custom.perl, "'libapi_native.'") && contains(custom.perl, "auto/Vendor/Api/") && custom.parts.size() == 2 &&
                !contains(custom.parts[0], "perl.h"), "options (the glue only in the first part)");
        options = {}; options.package = "Vendor::print";
        rejects([&] { rosetta::generate_perl(module, "api", false, policy, options); }, "package");
        options = {}; options.package = "Vendor:Api";
        rejects([&] { rosetta::generate_perl(module, "api", false, policy, options); }, "package");
        options = {}; options.library = "lib/api";
        rejects([&] { rosetta::generate_perl(module, "api", false, policy, options); }, "library name");
        options = {}; options.parts = 0;
        rejects([&] { rosetta::generate_perl(module, "api", false, policy, options); }, "parts");
        rejects([&] { rosetta::generate_perl(module, "bad-name", false, policy); }, "module name");
        std::cout << "PASS Perl options\n";

        // CLI.
        parse("inline int value(int x = 42) { return x; }");
        const auto output = temporary.path() / "perl bindings.cpp";
        const auto dep = temporary.path() / "perl bindings.d";
        const std::vector<std::string> args{header.string(), "--language", "perl", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_perl_call") &&
                contains(rosetta::detail::read_file(temporary.path() / "api.pm"), "sub value {"), "Perl CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "api.pm"), "Perl interface dependency missing");
        auto custom_args = args;
        const auto other = temporary.path() / "Other.pm";
        custom_args[2] = "pl";
        custom_args.insert(custom_args.end(), {"--perl-output", other.string(), "--perl-package", "My::Api", "--perl-library", "native", "--parts", "2"});
        require(rosetta::run_cli(custom_args, out, error) == 0 && contains(rosetta::detail::read_file(other), "package My::Api;") &&
                contains(rosetta::detail::read_file(other), "'libnative.'") && std::filesystem::exists(temporary.path() / "perl bindings_1.cpp"),
                "Perl options, the pl alias and parts");
        auto invalid = args;
        invalid.insert(invalid.end(), {"--perl-package", "My::sub"});
        require(rosetta::run_cli(invalid, out, error) == 1, "keyword package accepted");
        const std::vector<std::string> lua{header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--perl-output", "x.pm"};
        require(rosetta::run_cli(lua, out, error) == 1 && contains(error.str(), "require --language perl"), "Perl-only option accepted for Lua");
        std::cout << "PASS Perl CLI\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
