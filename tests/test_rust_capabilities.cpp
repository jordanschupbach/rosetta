// Generator and CLI checks of the Rust backend (--language rust) that need no Rust toolchain:
// output shape, keyword escaping, name collisions, options and the crate's files.
#include "rosetta/frontend.hpp"
#include "rosetta/rust.hpp"
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
std::string file(const rosetta::GeneratedRustSource& generated, const std::string& path) {
    for (const auto& [relative, text] : generated.files) if (relative == path) return text;
    throw std::runtime_error("missing crate file " + path);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        rosetta::detail::TemporaryDirectory temporary;
        const auto header = temporary.path() / "rust api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++17"});
        };
        const auto policy_file = temporary.path() / "policy.json";
        rosetta::detail::write_file(policy_file, "{\"version\": 1, \"select\": \"all\", \"names\": \"flat\"}\n");
        const auto policy = rosetta::load_policy(policy_file);

        // Output shape, keywords and determinism.
        const auto module = parse("enum class Mode { on, On, type };\n"
                                  "struct Cursor { int type() const { return 1; } int match(int fn) const { return fn; } int close() { return 0; } int loop = 2; };\n"
                                  "struct Point { double x = 0; bool operator==(const Point&) const { return true; } };\n"
                                  "struct String { int n = 0; };\n"
                                  "inline int twice(int value = 21) { return 2 * value; }\n");
        const auto generated = rosetta::generate_rust(module, "api", false, policy);
        const auto& rs = generated.lib;
        require(contains(rs, "pub mod rosetta;") && contains(rs, "pub type Result<T> = std::result::Result<T, Error>;") &&
                contains(rs, "pub static ROSETTA_CLASSES"), "lib.rs layout");
        require(contains(rs, "pub fn type_(&self) -> Result<i32>") && contains(rs, "pub fn match_(&self, fn_: i32) -> Result<i32>") &&
                contains(rs, "pub fn loop_(&self) -> i32") && contains(rs, "pub fn set_loop(&self, value: i32)"), "keyword escaping");
        require(contains(rs, "pub fn close(&self) {") && contains(rs, "pub fn close_2(&self) -> Result<i32>"), "generated names first");
        require(contains(rs, "pub const ON: Mode = Mode(0);") && contains(rs, "pub const ON_2: Mode = Mode(1);") &&
                contains(rs, "pub const TYPE: Mode = Mode(2);"), "enumerators");
        require(contains(rs, "pub fn equal(&self, arg1: &Point) -> Result<bool>"), "operators");
        require(contains(rs, "pub struct String_ {") && !contains(rs, "pub struct String {"), "prelude names stay std's");
        require(contains(rs, "pub fn twice() -> Result<i32>") && contains(rs, "pub fn twice_with_value(value: i32) -> Result<i32>"), "default arguments");
        require(contains(generated.source, "#define ROSETTA_FULL_RUST") && contains(generated.source, "rosetta_api_call"), "C++ library");
        require(contains(file(generated, "Cargo.toml"), "name = \"api\"") && contains(file(generated, "build.rs"), "API_RUST_LIB_DIR") &&
                contains(file(generated, "src/rosetta.rs"), "#[link(name = \"api\")]") &&
                contains(file(generated, "src/rosetta.rs"), "fn rosetta_api_call(") && !contains(file(generated, "src/rosetta.rs"), "@C@"), "crate files");
        const auto again = rosetta::generate_rust(module, "api", false, policy);
        require(generated.source == again.source && generated.lib == again.lib, "nondeterministic output");
        std::cout << "PASS Rust output, keywords and determinism\n";

        // Options.
        rosetta::RustOptions options;
        options.crate = "api_bindings";
        options.library = "api_native";
        options.parts = 3;
        const auto custom = rosetta::generate_rust(module, "api", false, policy, options);
        require(contains(file(custom, "Cargo.toml"), "name = \"api_bindings\"") && contains(file(custom, "src/rosetta.rs"), "#[link(name = \"api_native\")]") &&
                contains(file(custom, "src/rosetta.rs"), "fn rosetta_api_call(") && custom.parts.size() == 2, "options");
        options = {}; options.crate = "fn";
        rejects([&] { rosetta::generate_rust(module, "api", false, policy, options); }, "crate name");
        options = {}; options.library = "lib/api";
        rejects([&] { rosetta::generate_rust(module, "api", false, policy, options); }, "library name");
        options = {}; options.parts = 0;
        rejects([&] { rosetta::generate_rust(module, "api", false, policy, options); }, "parts");
        rejects([&] { rosetta::generate_rust(module, "bad-name", false, policy); }, "module name");
        std::cout << "PASS Rust options\n";

        // Type names that only PascalCase makes equal stay apart.
        const auto cased = parse("struct point { int x = 0; }; struct Point { int y = 0; }; inline int use(const Point& p) { return p.y; }");
        const auto both = rosetta::generate_rust(cased, "api", false, policy);
        require(contains(both.lib, "pub struct Point {") && contains(both.lib, "pub struct Point_ {"), "case-colliding classes");
        std::cout << "PASS Rust name collisions\n";

        // CLI and the crate.
        parse("inline int value(int x = 42) { return x; }");
        const auto output = temporary.path() / "rust bindings.cpp";
        const auto dep = temporary.path() / "rust bindings.d";
        const std::vector<std::string> args{header.string(), "--language", "rust", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        const auto crate = temporary.path() / "api";
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
                contains(rosetta::detail::read_file(crate / "src" / "lib.rs"), "pub fn value() -> Result<i32>") &&
                std::filesystem::exists(crate / "Cargo.toml") && std::filesystem::exists(crate / "build.rs") &&
                std::filesystem::exists(crate / "src" / "rosetta.rs"), "Rust CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "lib.rs"), "Rust interface dependency missing");
        auto custom_args = args;
        custom_args[2] = "rs";
        const auto other = temporary.path() / "other crate";
        custom_args.insert(custom_args.end(), {"--rust-output", other.string(), "--rust-crate", "api_rs", "--parts", "2"});
        require(rosetta::run_cli(custom_args, out, error) == 0 && contains(rosetta::detail::read_file(other / "Cargo.toml"), "name = \"api_rs\"") &&
                std::filesystem::exists(temporary.path() / "rust bindings_1.cpp"), "rs alias, crate options and parts");
        auto invalid = args;
        invalid.insert(invalid.end(), {"--rust-crate", "self"});
        require(rosetta::run_cli(invalid, out, error) == 1, "keyword crate name accepted");
        const std::vector<std::string> lua{header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--rust-crate", "api"};
        require(rosetta::run_cli(lua, out, error) == 1 && contains(error.str(), "require --language rust"), "Rust-only option accepted for Lua");
        std::cout << "PASS Rust CLI and crate\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
