#include "rosetta/cli.hpp"
#include "rosetta/frontend.hpp"
#include "rosetta/lua.hpp"
#include "rosetta/r.hpp"
#include "rosetta/python.hpp"
#include "rosetta/ruby.hpp"
#include "rosetta/octave.hpp"
#include "rosetta/julia.hpp"
#include "rosetta/java.hpp"
#include "rosetta/csharp.hpp"
#include "rosetta/fortran.hpp"
#include "rosetta/common_lisp.hpp"
#include "rosetta/tcl.hpp"
#include "rosetta/guile.hpp"
#include "rosetta/ocaml.hpp"
#include "rosetta/go.hpp"
#include "rosetta/apl.hpp"
#include "rosetta/cobol.hpp"
#include "rosetta/compilation_database.hpp"
#include "process.hpp"
#include "dependencies.hpp"

#include <filesystem>
#include <ostream>
#include <set>
#include <charconv>
#include <optional>

namespace rosetta {
namespace {
constexpr const char* usage = R"(Usage: rosetta HEADER --module NAME -o OUTPUT [options] [-- CLANG_FLAGS...]

Generate native language bindings from a C++ header.
  --language lua|r|python|ruby|octave|julia|java|csharp|fortran|common-lisp|tcl|guile|ocaml|go|apl|cobol  Target language (default: lua)
  --object-system r6|s3|s4  R object system (default: r6)
  --r-output PATH       Generated R interface (default: OUTPUT with extension .R)
  --julia-output PATH   Generated Julia interface (default: OUTPUT with extension .jl)
  --lisp-output PATH    Generated Common Lisp interface (default: OUTPUT with extension .lisp)
  --guile-output PATH   Generated Guile module (default: MODULE.scm beside OUTPUT)
  --ocaml-output PATH   Generated OCaml module (default: MODULE.ml beside OUTPUT)
  --go-output PATH      Generated Go package file (default: MODULE.go beside OUTPUT)
  --gnu-apl-output PATH GNU APL script (default: MODULE.apl beside OUTPUT)
  --dyalog-output PATH  Dyalog APL namespace script (default: MODULE.apln beside OUTPUT)
  --apl-library-dir DIR Directory the APL scripts load their libraries from (default: .)
  --cobol-output PATH   Generated COBOL copybook (default: MODULE.cpy beside OUTPUT)
  --cobol-string-size N Byte capacity of COBOL string fields (default: 256)
  --java-output PATH    Generated Java interface (default: MODULE.java beside OUTPUT)
  --fortran-output PATH Generated Fortran interface (default: MODULE.f90 beside OUTPUT)
  --csharp-output PATH Generated C# interface (default: MODULE.cs beside OUTPUT)
  --csharp-namespace NAME C# namespace (default: global namespace)
  --java-package NAME   Java package (default: unnamed package)
  --clang PATH           Clang executable (default: clang++)
  --dump-ir PATH         Export the normalized declaration model as JSON
  --depfile PATH         Export compiler-discovered inputs as a Make/Ninja depfile
  --policy PATH          Explicit class selection and lifetime policy (JSON)
  --compile-commands PATH  Compilation database file or build directory
  --translation-unit PATH  Select the source command supplying parser flags
  --command-index N      Select a zero-based database entry (for duplicate configurations)
  --skip-unsupported     Explicitly allow partial generation, with warnings
  -o, --output PATH      Generated C++ source
  -h, --help             Show this help
)";
}

int run_cli(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err) {
    try {
        std::string header, name, output, dump_ir, depfile, policy_path, clang = "clang++";
        std::string database_path, translation_unit;
        std::string language = "lua", object_system, r_output, julia_output, java_output, java_package, csharp_output, csharp_namespace, fortran_output, lisp_output, guile_output, ocaml_output, go_output, gnu_apl_output, dyalog_output, apl_library_dir, cobol_output, cobol_string_size;
        std::optional<std::size_t> command_index;
        std::vector<std::string> extra_flags;
        bool skip = false;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& arg = arguments[i];
            if (arg == "--") {
                extra_flags.insert(extra_flags.end(), arguments.begin() + static_cast<std::ptrdiff_t>(i + 1), arguments.end());
                break;
            }
            if (arg == "--help" || arg == "-h") { out << usage; return 0; }
            if (arg == "--skip-unsupported") { skip = true; continue; }
            if (arg == "--module" || arg == "--output" || arg == "-o" || arg == "--clang" ||
                arg == "--dump-ir" || arg == "--depfile" || arg == "--policy" || arg == "--language" || arg == "--compile-commands" ||
                arg == "--translation-unit" || arg == "--command-index" || arg == "--object-system" || arg == "--r-output" || arg == "--julia-output" || arg == "--java-output" || arg == "--java-package" || arg == "--csharp-output" || arg == "--csharp-namespace" || arg == "--fortran-output" || arg == "--lisp-output" || arg == "--guile-output" || arg == "--ocaml-output" || arg == "--go-output" || arg == "--gnu-apl-output" || arg == "--dyalog-output" || arg == "--apl-library-dir" || arg == "--cobol-output" || arg == "--cobol-string-size") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                const auto& value = arguments[i];
                if (arg == "--module") name = value;
                else if (arg == "--output" || arg == "-o") output = value;
                else if (arg == "--clang") clang = value;
                else if (arg == "--dump-ir") dump_ir = value;
                else if (arg == "--depfile") depfile = value;
                else if (arg == "--policy") policy_path = value;
                else if (arg == "--compile-commands") database_path = value;
                else if (arg == "--translation-unit") translation_unit = value;
                else if (arg == "--language") language = value;
                else if (arg == "--object-system") object_system = value;
                else if (arg == "--r-output") r_output = value;
                else if (arg == "--julia-output") julia_output = value;
                else if (arg == "--java-output") java_output = value;
                else if (arg == "--java-package") java_package = value;
                else if (arg == "--fortran-output") fortran_output = value;
                else if (arg == "--lisp-output") lisp_output = value;
                else if (arg == "--guile-output") guile_output = value;
                else if (arg == "--ocaml-output") ocaml_output = value;
                else if (arg == "--go-output") go_output = value;
                else if (arg == "--gnu-apl-output") gnu_apl_output = value;
                else if (arg == "--dyalog-output") dyalog_output = value;
                else if (arg == "--apl-library-dir") apl_library_dir = value;
                else if (arg == "--cobol-output") cobol_output = value;
                else if (arg == "--cobol-string-size") cobol_string_size = value;
                else if (arg == "--csharp-output") csharp_output = value;
                else if (arg == "--csharp-namespace") csharp_namespace = value;
                else if (arg == "--command-index") {
                    std::size_t index = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || std::to_string(index) != value)
                        throw BindingError("command index must be a nonnegative decimal integer");
                    command_index = index;
                }
            } else if (!arg.empty() && arg.front() == '-') throw BindingError("unknown option: " + arg);
            else if (header.empty()) header = arg;
            else throw BindingError("unexpected argument: " + arg);
        }
        if (header.empty() || name.empty() || output.empty())
            throw BindingError("HEADER, --module and --output are required; use --help for usage");
        if (language != "lua" && language != "r" && language != "python" && language != "ruby" && language != "octave" && language != "julia" && language != "java" && language != "csharp" && language != "fortran" && language != "common-lisp" && language != "tcl" && language != "guile" && language != "ocaml" && language != "go" && language != "apl" && language != "cobol") throw BindingError("unsupported language: " + language);
        if (language != "r" && (!object_system.empty() || !r_output.empty()))
            throw BindingError("--object-system and --r-output require --language r");
        if (language == "r") {
            if (object_system.empty()) object_system = "r6";
            if (object_system != "r6" && object_system != "s3" && object_system != "s4")
                throw BindingError("unsupported R object system: " + object_system + "; currently supported: r6, s3, s4");
            if (r_output.empty()) { auto path = std::filesystem::path(output); path.replace_extension(".R"); r_output = path.string(); }
        }
        if (language != "julia" && !julia_output.empty())
            throw BindingError("--julia-output requires --language julia");
        if (language == "julia" && julia_output.empty()) {
            auto path = std::filesystem::path(output); path.replace_extension(".jl"); julia_output = path.string();
        }
        if (language != "common-lisp" && !lisp_output.empty())
            throw BindingError("--lisp-output requires --language common-lisp");
        if (language == "common-lisp" && lisp_output.empty()) {
            auto path = std::filesystem::path(output); path.replace_extension(".lisp"); lisp_output = path.string();
        }
        if (language != "guile" && !guile_output.empty())
            throw BindingError("--guile-output requires --language guile");
        if (language == "guile") {
            if (guile_output.empty()) guile_output = (std::filesystem::path(output).parent_path() / (name + ".scm")).string();
            // (use-modules (NAME)) loads NAME.scm.
            if (std::filesystem::path(guile_output).filename() != name + ".scm")
                throw BindingError("Guile module filename must match the module name: " + name + ".scm");
        }
        if (language != "go" && !go_output.empty()) throw BindingError("--go-output requires --language go");
        if (language == "go" && go_output.empty()) go_output = (std::filesystem::path(output).parent_path() / (name + ".go")).string();
        if (language != "cobol" && (!cobol_output.empty() || !cobol_string_size.empty()))
            throw BindingError("--cobol-output and --cobol-string-size require --language cobol");
        std::size_t string_size = 256;
        if (language == "cobol") {
            if (cobol_output.empty()) cobol_output = (std::filesystem::path(output).parent_path() / (name + ".cpy")).string();
            if (!cobol_string_size.empty()) {
                const auto parsed = std::from_chars(cobol_string_size.data(), cobol_string_size.data() + cobol_string_size.size(), string_size);
                if (parsed.ec != std::errc{} || parsed.ptr != cobol_string_size.data() + cobol_string_size.size())
                    throw BindingError("COBOL string size must be a decimal integer");
            }
        }
        if (language != "apl" && (!gnu_apl_output.empty() || !dyalog_output.empty() || !apl_library_dir.empty()))
            throw BindingError("--gnu-apl-output, --dyalog-output and --apl-library-dir require --language apl");
        if (language == "apl") {
            if (gnu_apl_output.empty()) gnu_apl_output = (std::filesystem::path(output).parent_path() / (name + ".apl")).string();
            if (dyalog_output.empty()) dyalog_output = (std::filesystem::path(output).parent_path() / (name + ".apln")).string();
            if (apl_library_dir.empty()) apl_library_dir = ".";
        }
        if (language != "ocaml" && !ocaml_output.empty())
            throw BindingError("--ocaml-output requires --language ocaml");
        if (language == "ocaml") {
            if (ocaml_output.empty()) ocaml_output = (std::filesystem::path(output).parent_path() / (name + ".ml")).string();
            // OCaml names a compilation unit after its file.
            if (std::filesystem::path(ocaml_output).filename() != name + ".ml")
                throw BindingError("OCaml module filename must match the module name: " + name + ".ml");
        }
        if (language != "java" && (!java_output.empty() || !java_package.empty())) throw BindingError("--java-output and --java-package require --language java");
        if (language == "java") {
            if (java_output.empty()) java_output = (std::filesystem::path(output).parent_path() / (name + ".java")).string();
            if (std::filesystem::path(java_output).filename() != name + ".java")
                throw BindingError("Java interface filename must match the module name: " + name + ".java");
        }
        if (language != "csharp" && (!csharp_output.empty() || !csharp_namespace.empty())) throw BindingError("--csharp-output and --csharp-namespace require --language csharp");
        if (language == "csharp" && csharp_output.empty()) csharp_output = (std::filesystem::path(output).parent_path() / (name + ".cs")).string();
        if (language != "fortran" && !fortran_output.empty()) throw BindingError("--fortran-output requires --language fortran");
        if (language == "fortran" && fortran_output.empty()) fortran_output = (std::filesystem::path(output).parent_path() / (name + ".f90")).string();
        const auto interface_output = language == "cobol" ? cobol_output : language == "apl" ? gnu_apl_output : language == "go" ? go_output : language == "ocaml" ? ocaml_output : language == "guile" ? guile_output : language == "common-lisp" ? lisp_output : language == "fortran" ? fortran_output : language == "csharp" ? csharp_output : language == "java" ? java_output : language == "julia" ? julia_output : r_output;
        if (database_path.empty() && (!translation_unit.empty() || command_index))
            throw BindingError("--translation-unit and --command-index require --compile-commands");
        const auto compilation = database_path.empty() ? std::optional<CompilationCommand>{} :
            std::optional<CompilationCommand>{load_compilation_command(database_path, translation_unit, command_index)};
        const auto input_path = std::filesystem::canonical(header);
        const auto output_path = std::filesystem::weakly_canonical(output);
        const auto ir_path = dump_ir.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(dump_ir);
        const auto dep_path = depfile.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(depfile);
        const auto interface_path = interface_output.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(interface_output);
        // APL's second script: the Dyalog namespace.
        const auto second_path = dyalog_output.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(dyalog_output);
        const auto same_file = [](const auto& a, const auto& b) {
            if (a == b) return true;
            std::error_code ec;
            return std::filesystem::equivalent(a, b, ec);
        };
        if (same_file(input_path, output_path) || (!dump_ir.empty() &&
            (same_file(input_path, ir_path) || same_file(output_path, ir_path))))
            throw BindingError("input header, generated source and IR output must have distinct paths");
        if (!policy_path.empty() && (same_file(std::filesystem::canonical(policy_path), output_path) ||
            (!dump_ir.empty() && same_file(std::filesystem::canonical(policy_path), ir_path))))
            throw BindingError("binding policy and generated outputs must have distinct paths");
        if (compilation) {
            for (const auto& protected_path : {compilation->database, compilation->file})
                if (same_file(protected_path, output_path) || (!dump_ir.empty() && same_file(protected_path, ir_path)))
                    throw BindingError("compilation database/source and generated outputs must have distinct paths");
        }
        if (!depfile.empty()) {
            std::vector<std::filesystem::path> protected_paths{input_path, output_path};
            if (!dump_ir.empty()) protected_paths.push_back(ir_path);
            if (!policy_path.empty()) protected_paths.push_back(std::filesystem::canonical(policy_path));
            if (compilation) { protected_paths.push_back(compilation->database); protected_paths.push_back(compilation->file); }
            for (const auto& path : protected_paths)
                if (same_file(dep_path, path)) throw BindingError("dependency file and inputs/outputs must have distinct paths");
        }
        if (!second_path.empty()) {
            std::vector<std::filesystem::path> protected_paths{input_path, output_path, interface_path};
            if (!dump_ir.empty()) protected_paths.push_back(ir_path);
            if (!depfile.empty()) protected_paths.push_back(dep_path);
            if (!policy_path.empty()) protected_paths.push_back(std::filesystem::canonical(policy_path));
            if (compilation) { protected_paths.push_back(compilation->database); protected_paths.push_back(compilation->file); }
            for (const auto& path : protected_paths)
                if (same_file(second_path, path)) throw BindingError("APL scripts and inputs/outputs must have distinct paths");
        }
        if (!interface_path.empty()) {
            std::vector<std::filesystem::path> protected_paths{input_path, output_path};
            if (!dump_ir.empty()) protected_paths.push_back(ir_path);
            if (!depfile.empty()) protected_paths.push_back(dep_path);
            if (!policy_path.empty()) protected_paths.push_back(std::filesystem::canonical(policy_path));
            if (compilation) { protected_paths.push_back(compilation->database); protected_paths.push_back(compilation->file); }
            for (const auto& path : protected_paths)
                if (same_file(interface_path, path)) throw BindingError(language == "cobol" ? "COBOL copybook and inputs/outputs must have distinct paths" : language == "apl" ? "APL scripts and inputs/outputs must have distinct paths" : language == "go" ? "Go package file and inputs/outputs must have distinct paths" : language == "ocaml" ? "OCaml module and inputs/outputs must have distinct paths" : language == "guile" ? "Guile module and inputs/outputs must have distinct paths" : language == "common-lisp" ? "Common Lisp interface and inputs/outputs must have distinct paths" : language == "fortran" ? "Fortran interface and inputs/outputs must have distinct paths" : language == "csharp" ? "C# interface and inputs/outputs must have distinct paths" : language == "java" ? "Java interface and inputs/outputs must have distinct paths" :
                    language == "julia" ? "Julia interface and inputs/outputs must have distinct paths" : "R interface and inputs/outputs must have distinct paths");
        }
        const auto policy = policy_path.empty() ? BindingPolicy{} : load_policy(policy_path);
        std::vector<std::string> flags{"-std=c++17"};
        if (compilation) flags.insert(flags.end(), compilation->flags.begin(), compilation->flags.end());
        flags.insert(flags.end(), extra_flags.begin(), extra_flags.end());
        auto module = parse(input_path, clang, flags, compilation ? compilation->directory : std::filesystem::path{}, !depfile.empty());
        if (compilation) {
            module.parse_context.compilation_database = compilation->database.string();
            module.parse_context.translation_unit = compilation->file.string();
            module.parse_context.command_index = compilation->index;
        }
        GeneratedSource generated;
        std::string language_interface, second_interface;
        if (language == "r") {
            const auto system = object_system == "s3" ? RObjectSystem::s3 : object_system == "s4" ? RObjectSystem::s4 : RObjectSystem::r6;
            const auto r = generate_r(module, name, system, skip, policy);
            generated = {r.source, r.diagnostics};
            language_interface = r.interface;
        } else if (language == "python") {
            const auto python = generate_python(module, name, skip, policy);
            generated = {python.source, python.diagnostics};
        } else if (language == "ruby") {
            const auto ruby = generate_ruby(module, name, skip, policy);
            generated = {ruby.source, ruby.diagnostics};
        } else if (language == "tcl") {
            const auto tcl = generate_tcl(module, name, skip, policy);
            generated = {tcl.source, tcl.diagnostics};
        } else if (language == "guile") {
            const auto guile = generate_guile(module, name, skip, policy);
            generated = {guile.source, guile.diagnostics};
            language_interface = guile.interface;
        } else if (language == "ocaml") {
            const auto ocaml = generate_ocaml(module, name, skip, policy);
            generated = {ocaml.source, ocaml.diagnostics};
            language_interface = ocaml.interface;
        } else if (language == "cobol") {
            const auto cobol = generate_cobol(module, name, skip, policy, string_size);
            generated = {cobol.source, cobol.diagnostics};
            language_interface = cobol.copybook;
        } else if (language == "apl") {
            const auto apl = generate_apl(module, name, skip, policy, apl_library_dir);
            generated = {apl.source, apl.diagnostics};
            language_interface = apl.gnu;
            second_interface = apl.dyalog;
        } else if (language == "go") {
            const auto generated_go = generate_go(module, name, skip, policy);
            generated = {generated_go.source, generated_go.diagnostics};
            language_interface = generated_go.interface;
        } else if (language == "octave") {
            const auto octave = generate_octave(module, name, skip, policy);
            generated = {octave.source, octave.diagnostics};
        } else if (language == "julia") {
            const auto julia = generate_julia(module, name, skip, policy);
            generated = {julia.source, julia.diagnostics};
            language_interface = julia.interface;
        } else if (language == "common-lisp") {
            const auto lisp = generate_common_lisp(module, name, skip, policy);
            generated = {lisp.source, lisp.diagnostics};
            language_interface = lisp.interface;
        } else if (language == "fortran") {
            const auto fortran = generate_fortran(module, name, skip, policy);
            generated = {fortran.source, fortran.diagnostics};
            language_interface = fortran.interface;
        } else if (language == "csharp") {
            const auto csharp = generate_csharp(module, name, skip, policy, csharp_namespace);
            generated = {csharp.source, csharp.diagnostics};
            language_interface = csharp.interface;
        } else if (language == "java") {
            const auto java = generate_java(module, name, skip, policy, java_package);
            generated = {java.source, java.diagnostics};
            language_interface = java.interface;
        } else generated = generate_lua(module, name, skip, policy);
        std::string dependency_contents;
        if (!depfile.empty()) {
            for (const auto& dependency : module.dependencies)
                for (const auto& path : {output_path, ir_path, dep_path, interface_path, second_path})
                    if (!path.empty() && same_file(path, std::filesystem::path(dependency)))
                        throw BindingError("compiler inputs and generated outputs must have distinct paths");
            auto prerequisites = module.dependencies;
            if (!policy_path.empty()) prerequisites.push_back(std::filesystem::canonical(policy_path).string());
            if (compilation) { prerequisites.push_back(compilation->database.string()); prerequisites.push_back(compilation->file.string()); }
            dependency_contents = detail::dependency_rule(output_path, prerequisites);
            if (!interface_path.empty()) dependency_contents += detail::dependency_rule(interface_path, prerequisites);
            if (!second_path.empty()) dependency_contents += detail::dependency_rule(second_path, prerequisites);
        }
        detail::write_file(output_path, generated.source);
        if (!interface_path.empty()) detail::write_file(interface_path, language_interface);
        if (!second_path.empty()) detail::write_file(second_path, second_interface);
        if (!dump_ir.empty()) detail::write_file(ir_path, serialize_ir(module));
        if (!depfile.empty()) detail::write_file(dep_path, dependency_contents);
        for (const auto& diagnostic : generated.diagnostics) err << "warning: " << diagnostic << '\n';
        return 0;
    } catch (const std::exception& error) {
        err << "rosetta: " << error.what() << '\n';
        return 1;
    }
}

} // namespace rosetta
