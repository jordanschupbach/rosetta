#include "rosetta/frontend.hpp"
#include "rosetta/lua.hpp"
#include "process.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected Clang and Lua include directory");
        rosetta::detail::TemporaryDirectory temporary;
        const auto header = temporary.path() / "flags.hpp";
        const auto output = temporary.path() / "flags.cpp";
        rosetta::detail::write_file(header,
            "#include <vector>\n#include <tuple>\n#include <functional>\n"
            "enum class Flags : unsigned char { zero=0, first=1, second=2, both=3, high=128 }; Flags echo(Flags);\n"
            "enum class Signed : signed char { first=1, negative=-128 }; Signed echo_signed(Signed);\n"
            "enum class Huge : unsigned long long { high=1ULL<<63 }; Huge echo_huge(Huge);\n"
            "enum class Boolean : bool { enabled=true }; Boolean echo_bool(Boolean);\n"
            "std::vector<Flags> sequence(std::vector<Flags>); std::tuple<Boolean, Flags> product(std::tuple<Boolean, Flags>);\n"
            "enum class Forward : unsigned; std::vector<Forward> opaque_output(); void notify(std::function<void(Forward)>);\n");
        const auto module = rosetta::parse(header, argv[1]);
        const auto compile = [&](const rosetta::BindingPolicy& policy, const std::string& expected) {
            rosetta::detail::write_file(output, rosetta::generate_lua(module, "flags", false, policy).source);
            const auto result = rosetta::detail::run_process({argv[1], "-std=c++17", "-fsyntax-only", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                "-Wno-unused-command-line-argument", "-I", argv[2], output.string()});
            if (expected.empty() ? result.exit_code != 0 : result.exit_code == 0 || result.error.find(expected) == std::string::npos)
                throw std::runtime_error("unexpected enum flag compilation result: " + result.error);
        };
        rosetta::BindingPolicy policy;
        policy.enum_flags["Flags"] = {"first", "second", "high"};
        policy.enum_flags["Boolean"] = {"enabled"};
        policy.callbacks["notify"][1] = rosetta::CallbackLifetime::call;
        compile(policy, "");
        for (const auto& name : {"zero", "both"}) {
            policy.enum_flags["Flags"] = {name};
            compile(policy, "flag basis must be a positive single-bit enumerator");
        }
        policy.enum_flags["Flags"] = {"first"};
        policy.enum_flags["Signed"] = {"negative"};
        compile(policy, "flag basis must be a positive single-bit enumerator");
        policy.enum_flags.erase("Signed");
        policy.enum_flags["Huge"] = {"high"};
        compile(policy, "flag basis exceeds Lua integer range");
        std::cout << "Native flag basis, bool transport and range compilation checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
