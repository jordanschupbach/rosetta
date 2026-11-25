// Generator and CLI checks of the Haskell backend (--language haskell) that need no GHC: output
// shape, names, the runtime module, options and the CLI artifacts.
#include "rosetta/frontend.hpp"
#include "rosetta/haskell.hpp"
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
        const auto header = temporary.path() / "haskell api.hpp";
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
        const auto generated = rosetta::generate_haskell(module, "Api", false, policy);
        const auto& hs = generated.haskell;
        require(contains(hs, "{-# LANGUAGE NoImplicitPrelude #-}") && contains(hs, "module Api\n  (\n") &&
                contains(hs, "import qualified Prelude as P\n") && contains(hs, "import qualified Api.Rosetta as R\n") &&
                contains(hs, "    -- * Cursor\n") && contains(hs, "    R.CppError(..)\n"), "module layout");
        const auto& runtime = generated.runtime;
        require(contains(runtime, "module Api.Rosetta") && contains(runtime, "foreign import ccall safe \"rosetta_Api_call\"") &&
                contains(runtime, "classes = [(\"Cursor\", []), (\"Point\", []), (\"Base\", []), (\"Other\", []), (\"Derived\", [2, 3])]") &&
                !contains(runtime, "@M@") && !contains(runtime, "@C@") && !contains(runtime, "@CLASSES@"), "runtime module and placeholders");
        require(contains(hs, "data Mode\n  = ModeOn  -- ^ C++ @on@ (0).\n  | ModeOn'  -- ^ C++ @On@ (1).\n  | ModeTrue  -- ^ C++ @true\\_@ (2).\n") &&
                contains(hs, "deriving (P.Eq, P.Ord, P.Show, P.Read, P.Bounded, P.Enum)") && contains(hs, "    ModeOn' -> \"On\"\n"), "enumerations");
        require(contains(hs, "newtype Cursor = Cursor' R.Handle") && contains(hs, "class R.CppObject a => IsCursor a") &&
                contains(hs, "\ncursorGetName\n") && contains(hs, "\ncursorGetName'\n") && contains(hs, "\ncursorCount\n") &&
                contains(hs, "\ncursorCount' :: IsCursor a => a -> P.IO P.Int") && contains(hs, "\ncursorSetCount :: ") &&
                contains(hs, "\ncursorMake :: P.IO P.Int"), "member names (methods before fields)");
        require(contains(hs, "\nlength :: P.Int -> P.IO P.Int\n") && contains(hs, "goldenRatio :: P.Double\ngoldenRatio = R.constant ") &&
                contains(hs, "{-# NOINLINE goldenRatio #-}"), "Prelude names and pure constants");
        require(contains(hs, "\npointAdd\n") && contains(hs, "\npointEquals\n") && contains(hs, "\npointAt\n"), "operators");
        require(contains(hs, "instance IsBase Derived\n") && contains(hs, "instance IsOther Derived\n") &&
                contains(hs, "data DerivedOverrides = DerivedOverrides\n  { derivedAreaOverride :: P.Maybe (Derived -> P.IO P.Int)") &&
                contains(hs, "extendDerived _overrides =") && contains(hs, "R.overrideWith (\\_h -> _f (Derived' _h))") &&
                contains(hs, "\nderivedAreaBase\n"), "multiple inheritance and extended objects");
        require(contains(hs, "twice :: P.IO P.Int\n") && contains(hs, "twiceWithValue :: P.Int -> P.IO P.Int\n") &&
                contains(hs, "@value@ = @21@"), "default arguments: a function per argument count");
        require(contains(hs, ":: IORef.IORef P.Int  -- ^ C++ writes it back") && contains(hs, "R.writeBack _reply 0 _x1"), "written-back arguments");
        require(contains(hs, "each :: (P.Int -> P.IO P.Int) -> P.IO P.Int"), "functions");
        const auto& source = generated.source;
        require(contains(source, "#define ROSETTA_FULL_HASKELL") && contains(source, "rosetta_Api_call") && contains(source, "rosetta_Api_init"), "C++ library");
        const auto again = rosetta::generate_haskell(module, "Api", false, policy);
        require(generated.source == again.source && generated.haskell == again.haskell && generated.runtime == again.runtime, "nondeterministic output");
        require(rosetta::haskell_module_name("shape_kit") == "ShapeKit" && rosetta::haskell_module_name("shapeKit") == "ShapeKit", "default module name");
        std::cout << "PASS Haskell output, names and determinism\n";

        // Options.
        rosetta::HaskellOptions options;
        options.module = "Vendor.Api";
        options.parts = 3;
        const auto custom = rosetta::generate_haskell(module, "Api", false, policy, options);
        require(contains(custom.haskell, "module Vendor.Api\n") && contains(custom.haskell, "import qualified Vendor.Api.Rosetta as R") &&
                contains(custom.runtime, "module Vendor.Api.Rosetta") && contains(custom.runtime, "\"rosetta_Api_call\"") &&
                custom.parts.size() == 2, "options");
        for (const auto* bad : {"vendor", "Vendor..Api", "Vendor.", "Vendor-Api"}) {
            options = {}; options.module = bad;
            rejects([&] { rosetta::generate_haskell(module, "Api", false, policy, options); }, "module identifier");
        }
        for (const auto* taken : {"Prelude", "Data.Map", "GHC.Exts"}) {
            options = {}; options.module = taken;
            rejects([&] { rosetta::generate_haskell(module, "Api", false, policy, options); }, "standard module");
        }
        options = {}; options.parts = 0;
        rejects([&] { rosetta::generate_haskell(module, "Api", false, policy, options); }, "parts");
        rejects([&] { rosetta::generate_haskell(module, "bad-name", false, policy); }, "module name");
        std::cout << "PASS Haskell options\n";

        // CLI.
        parse("inline int value(int x = 42) { return x; }");
        const auto output = temporary.path() / "haskell bindings.cpp";
        const auto dep = temporary.path() / "haskell bindings.d";
        const std::vector<std::string> args{header.string(), "--language", "haskell", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_api_call") &&
                contains(rosetta::detail::read_file(temporary.path() / "Api.hs"), "valueWithX :: P.Int -> P.IO P.Int") &&
                contains(rosetta::detail::read_file(temporary.path() / "Api" / "Rosetta.hs"), "module Api.Rosetta"), "Haskell CLI artifacts missing");
        require(contains(rosetta::detail::read_file(dep), "Api.hs"), "Haskell interface dependency missing");
        auto custom_args = args;
        const auto other = temporary.path() / "Kit" / "Native.hs";
        custom_args.insert(custom_args.end(), {"--haskell-output", other.string(), "--haskell-module", "Kit.Native", "--parts", "2"});
        require(rosetta::run_cli(custom_args, out, error) == 0 && contains(rosetta::detail::read_file(other), "module Kit.Native\n") &&
                contains(rosetta::detail::read_file(temporary.path() / "Kit" / "Native" / "Rosetta.hs"), "module Kit.Native.Rosetta") &&
                std::filesystem::exists(temporary.path() / "haskell bindings_1.cpp"), "Haskell options and parts");
        const auto dotted = temporary.path() / "dotted.cpp";
        const std::vector<std::string> dotted_args{header.string(), "--language", "haskell", "--module", "api", "--clang", argv[1],
            "-o", dotted.string(), "--haskell-module", "Vendor.Kit"};
        require(rosetta::run_cli(dotted_args, out, error) == 0 && std::filesystem::exists(temporary.path() / "Kit.hs"), "default output: the last module part");
        auto invalid = args;
        invalid.insert(invalid.end(), {"--haskell-module", "my-api"});
        require(rosetta::run_cli(invalid, out, error) == 1, "invalid module accepted");
        auto same = args;
        same.insert(same.end(), {"--haskell-output", header.string()});
        require(rosetta::run_cli(same, out, error) == 1 && contains(error.str(), "distinct paths"), "module over the header accepted");
        const std::vector<std::string> lua{header.string(), "--language", "lua", "--module", "api", "-o", output.string(), "--haskell-output", "x.hs"};
        require(rosetta::run_cli(lua, out, error) == 1 && contains(error.str(), "require --language haskell"), "Haskell-only option accepted for Lua");
        std::cout << "PASS Haskell CLI\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
