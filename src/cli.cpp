#include "rosetta/cli.hpp"
#include "rosetta/frontend.hpp"
#include "rosetta/lua.hpp"
#include "rosetta/javascript.hpp"
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
  --language lua|r|python|javascript|ruby|octave|julia|java|csharp|fortran|common-lisp|tcl|guile|ocaml|go|apl|cobol  Target language (default: lua)
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
  --cobol-api API       minimal (default) or full: handles, containers, overloads, callbacks, overrides, docs
  --cobol-prefix WORD   Full API: prefix of every COBOL name (default: the module name)
  --cobol-callbacks PATH Full API: the callbacks copybook (default: MODULE-callbacks.cpy beside the copybook)
  --cobol-table-size N  Full API: OCCURS capacity of -TABLE records (default: 1000)
  --cobol-result-size N Full API: bytes of string results (default: the string size)
  --java-output PATH    Generated Java interface (default: MODULE.java beside OUTPUT)
  --fortran-output PATH Generated Fortran interface (default: MODULE.f90 beside OUTPUT)
  --csharp-output PATH Generated C# interface (default: MODULE.cs beside OUTPUT)
  --csharp-namespace NAME C# namespace (default: global namespace)
  --java-package NAME   Java package (default: unnamed package)
  --clang PATH           Clang executable (default: clang++)
  --dump-ir PATH         Export the normalized declaration model as JSON
  --depfile PATH         Export compiler-discovered inputs as a Make/Ninja depfile
  --policy PATH          Explicit class selection and lifetime policy (JSON)
  --scope PATH           Also export declarations from this header or directory (repeatable)
  --skip-function-bodies Parse declarations only (faster for large libraries)
  --python-parts N, --parts N  Split Python or R6 output over N sources: OUTPUT holds module
                         init, the rest go to OUTPUT's stem + _1.._N-1 (compile them in parallel)
  --lua-api minimal|full Lua binding style (default: minimal); full adds overloads, inheritance,
                         Lua subclasses and operators
  --lua-meta PATH        LuaCATS stub documenting the module (--lua-api full; default: MODULE.lua
                         in a library/ directory beside OUTPUT)
  --js-types PATH        TypeScript declarations (.d.ts, with the C++ documentation as JSDoc) for the
                         --language javascript addon (default: MODULE.d.ts beside OUTPUT)
  --lisp-api minimal|full Common Lisp binding style (default: minimal); full adds CLOS classes,
                         generic functions, overloads, containers, enums, callbacks, Lisp
                         subclasses and docstrings
  --lisp-system PATH     ASDF system definition (--lisp-api full; default: PACKAGE.asd beside the
                         --lisp-output file)
  --lisp-nicknames A,B   Package nicknames (--lisp-api full)
  --lisp-library NAME    Shared library base name the interface loads (default: MODULE)
  --r-package            R6 interface as R package code (useDynLib registration, .onLoad hook)
  --include-as SPELLING  #include spelling for HEADER in generated code (default: its path)
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
        std::string database_path, translation_unit, include_as;
        std::size_t python_parts = 1;
        std::string language = "lua", object_system, r_output, julia_output, java_output, java_package, csharp_output, csharp_namespace, fortran_output, lisp_output, guile_output, ocaml_output, go_output, gnu_apl_output, dyalog_output, apl_library_dir, cobol_output, cobol_string_size, lua_api, lua_meta, lisp_api, lisp_system, lisp_nicknames, lisp_library, cobol_api, cobol_prefix, cobol_callbacks, cobol_table_size, cobol_result_size, js_types;
        std::optional<std::size_t> command_index;
        std::vector<std::string> extra_flags;
        std::vector<std::filesystem::path> scopes;
        bool skip = false, skip_bodies = false, r_package = false;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& arg = arguments[i];
            if (arg == "--") {
                extra_flags.insert(extra_flags.end(), arguments.begin() + static_cast<std::ptrdiff_t>(i + 1), arguments.end());
                break;
            }
            if (arg == "--help" || arg == "-h") { out << usage; return 0; }
            if (arg == "--skip-unsupported") { skip = true; continue; }
            if (arg == "--skip-function-bodies") { skip_bodies = true; continue; }
            if (arg == "--r-package") { r_package = true; continue; }
            if (arg == "--scope") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                scopes.emplace_back(arguments[i]);
                continue;
            }
            if (arg == "--module" || arg == "--output" || arg == "-o" || arg == "--clang" ||
                arg == "--dump-ir" || arg == "--depfile" || arg == "--policy" || arg == "--language" || arg == "--compile-commands" ||
                arg == "--translation-unit" || arg == "--command-index" || arg == "--object-system" || arg == "--r-output" || arg == "--julia-output" || arg == "--java-output" || arg == "--java-package" || arg == "--csharp-output" || arg == "--csharp-namespace" || arg == "--fortran-output" || arg == "--lisp-output" || arg == "--guile-output" || arg == "--ocaml-output" || arg == "--go-output" || arg == "--gnu-apl-output" || arg == "--dyalog-output" || arg == "--apl-library-dir" || arg == "--cobol-output" || arg == "--cobol-string-size" || arg == "--cobol-api" || arg == "--cobol-prefix" || arg == "--cobol-callbacks" || arg == "--cobol-table-size" || arg == "--cobol-result-size" || arg == "--include-as" || arg == "--python-parts" || arg == "--parts" || arg == "--lua-api" || arg == "--lua-meta" || arg == "--js-types" || arg == "--lisp-api" || arg == "--lisp-system" || arg == "--lisp-nicknames" || arg == "--lisp-library") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                const auto& value = arguments[i];
                if (arg == "--module") name = value;
                else if (arg == "--include-as") include_as = value;
                else if (arg == "--python-parts" || arg == "--parts") {
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), python_parts);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || python_parts == 0 || python_parts > 256)
                        throw BindingError("--python-parts must be an integer from 1 to 256");
                }
                else if (arg == "--output" || arg == "-o") output = value;
                else if (arg == "--clang") clang = value;
                else if (arg == "--dump-ir") dump_ir = value;
                else if (arg == "--depfile") depfile = value;
                else if (arg == "--policy") policy_path = value;
                else if (arg == "--compile-commands") database_path = value;
                else if (arg == "--translation-unit") translation_unit = value;
                else if (arg == "--language") language = value;
                else if (arg == "--lua-api") lua_api = value;
                else if (arg == "--lua-meta") lua_meta = value;
                else if (arg == "--js-types") js_types = value;
                else if (arg == "--lisp-api") lisp_api = value;
                else if (arg == "--lisp-system") lisp_system = value;
                else if (arg == "--lisp-nicknames") lisp_nicknames = value;
                else if (arg == "--lisp-library") lisp_library = value;
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
                else if (arg == "--cobol-api") cobol_api = value;
                else if (arg == "--cobol-prefix") cobol_prefix = value;
                else if (arg == "--cobol-callbacks") cobol_callbacks = value;
                else if (arg == "--cobol-table-size") cobol_table_size = value;
                else if (arg == "--cobol-result-size") cobol_result_size = value;
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
        if (language == "js" || language == "node") language = "javascript";
        if (language != "lua" && language != "r" && language != "python" && language != "javascript" && language != "ruby" && language != "octave" && language != "julia" && language != "java" && language != "csharp" && language != "fortran" && language != "common-lisp" && language != "tcl" && language != "guile" && language != "ocaml" && language != "go" && language != "apl" && language != "cobol") throw BindingError("unsupported language: " + language);
        if (language != "lua" && (!lua_api.empty() || !lua_meta.empty())) throw BindingError("--lua-api and --lua-meta require --language lua");
        if (!lua_api.empty() && lua_api != "minimal" && lua_api != "full") throw BindingError("--lua-api must be minimal or full");
        const bool lua_full = language == "lua" && lua_api == "full";
        if (!lua_full && !lua_meta.empty()) throw BindingError("--lua-meta requires --lua-api full");
        if (lua_full && lua_meta.empty()) lua_meta = (std::filesystem::path(output).parent_path() / "library" / (name + ".lua")).string();
        if (language != "common-lisp" && (!lisp_api.empty() || !lisp_system.empty() || !lisp_nicknames.empty() || !lisp_library.empty()))
            throw BindingError("--lisp-api, --lisp-system, --lisp-nicknames and --lisp-library require --language common-lisp");
        if (!lisp_api.empty() && lisp_api != "minimal" && lisp_api != "full") throw BindingError("--lisp-api must be minimal or full");
        const bool lisp_full = language == "common-lisp" && lisp_api == "full";
        if (!lisp_full && (!lisp_system.empty() || !lisp_nicknames.empty() || !lisp_library.empty()))
            throw BindingError("--lisp-system, --lisp-nicknames and --lisp-library require --lisp-api full");
        if (language != "cobol" && (!cobol_api.empty() || !cobol_prefix.empty() || !cobol_callbacks.empty() || !cobol_table_size.empty() || !cobol_result_size.empty()))
            throw BindingError("--cobol-api, --cobol-prefix, --cobol-callbacks, --cobol-table-size and --cobol-result-size require --language cobol");
        if (!cobol_api.empty() && cobol_api != "minimal" && cobol_api != "full") throw BindingError("--cobol-api must be minimal or full");
        const bool cobol_full = language == "cobol" && cobol_api == "full";
        if (!cobol_full && (!cobol_prefix.empty() || !cobol_callbacks.empty() || !cobol_table_size.empty() || !cobol_result_size.empty()))
            throw BindingError("--cobol-prefix, --cobol-callbacks, --cobol-table-size and --cobol-result-size require --cobol-api full");
        if (language != "javascript" && !js_types.empty()) throw BindingError("--js-types requires --language javascript");
        if (language == "javascript" && js_types.empty()) js_types = (std::filesystem::path(output).parent_path() / (name + ".d.ts")).string();
        if (language != "python" && language != "javascript" && !lua_full && !lisp_full && !cobol_full && !(language == "r" && object_system != "s3" && object_system != "s4") && python_parts != 1)
            throw BindingError("--parts requires --language python, javascript, r (R6), lua (--lua-api full), common-lisp (--lisp-api full) or cobol (--cobol-api full)");
        if (r_package && (language != "r" || object_system == "s3" || object_system == "s4"))
            throw BindingError("--r-package requires --language r with the R6 object system");
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
        std::string lisp_package;
        for (const char c : name) lisp_package += c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lisp_full && lisp_system.empty()) lisp_system = (std::filesystem::path(lisp_output).parent_path() / (lisp_package + ".asd")).string();
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
        std::size_t table_size = 1000;
        if (!cobol_table_size.empty()) {
            const auto parsed = std::from_chars(cobol_table_size.data(), cobol_table_size.data() + cobol_table_size.size(), table_size);
            if (parsed.ec != std::errc{} || parsed.ptr != cobol_table_size.data() + cobol_table_size.size())
                throw BindingError("COBOL table size must be a decimal integer");
        }
        std::size_t result_size = 0;
        if (!cobol_result_size.empty()) {
            const auto parsed = std::from_chars(cobol_result_size.data(), cobol_result_size.data() + cobol_result_size.size(), result_size);
            if (parsed.ec != std::errc{} || parsed.ptr != cobol_result_size.data() + cobol_result_size.size() || result_size == 0)
                throw BindingError("COBOL result size must be a positive decimal integer");
        }
        if (cobol_full && cobol_callbacks.empty())
            cobol_callbacks = (std::filesystem::path(cobol_output).parent_path() / (std::filesystem::path(cobol_output).stem().string() + "-callbacks.cpy")).string();
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
        const auto interface_output = language == "javascript" ? js_types : language == "cobol" ? cobol_output : language == "apl" ? gnu_apl_output : language == "go" ? go_output : language == "ocaml" ? ocaml_output : language == "guile" ? guile_output : language == "common-lisp" ? lisp_output : language == "fortran" ? fortran_output : language == "csharp" ? csharp_output : language == "java" ? java_output : language == "julia" ? julia_output : language == "lua" ? lua_meta : r_output;
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
        const auto second_output = !dyalog_output.empty() ? dyalog_output : !cobol_callbacks.empty() ? cobol_callbacks : lisp_system;
        const auto second_path = second_output.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(second_output);
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
                    language == "julia" ? "Julia interface and inputs/outputs must have distinct paths" : language == "javascript" ? "TypeScript declarations and inputs/outputs must have distinct paths" : language == "lua" ? "Lua stub and inputs/outputs must have distinct paths" : "R interface and inputs/outputs must have distinct paths");
        }
        const auto policy = policy_path.empty() ? BindingPolicy{} : load_policy(policy_path);
        std::vector<std::string> flags{"-std=c++17"};
        if (compilation) flags.insert(flags.end(), compilation->flags.begin(), compilation->flags.end());
        flags.insert(flags.end(), extra_flags.begin(), extra_flags.end());
        ParseOptions parse_options;
        parse_options.scopes = scopes;
        parse_options.scopes.insert(parse_options.scopes.end(), policy.scopes.begin(), policy.scopes.end());
        parse_options.skip_function_bodies = skip_bodies;
        for (const auto& requested : policy.templates) parse_options.instantiations.push_back(requested.second);
        auto module = parse(input_path, clang, flags, compilation ? compilation->directory : std::filesystem::path{}, !depfile.empty(),
                            parse_options);
        if (!include_as.empty()) module.header = include_as;
        if (compilation) {
            module.parse_context.compilation_database = compilation->database.string();
            module.parse_context.translation_unit = compilation->file.string();
            module.parse_context.command_index = compilation->index;
        }
        GeneratedSource generated;
        std::vector<std::pair<std::filesystem::path, std::string>> extra_sources;
        std::string language_interface, second_interface;
        if (language == "r") {
            const auto system = object_system == "s3" ? RObjectSystem::s3 : object_system == "s4" ? RObjectSystem::s4 : RObjectSystem::r6;
            const auto r = generate_r(module, name, system, skip, policy, r_package, python_parts);
            generated = {r.source, r.diagnostics};
            for (std::size_t i = 0; i < r.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, r.parts[i]);
            }
            language_interface = r.interface;
        } else if (language == "python") {
            const auto python = generate_python(module, name, skip, policy, python_parts);
            generated = {python.source, python.diagnostics};
            for (std::size_t i = 0; i < python.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, python.parts[i]);
            }
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
        } else if (cobol_full) {
            CobolFullOptions options;
            options.parts = python_parts;
            options.prefix = cobol_prefix;
            options.string_size = string_size;
            options.table_size = table_size;
            options.result_size = result_size;
            options.callbacks_name = std::filesystem::path(cobol_callbacks).filename().string();
            const auto cobol = generate_cobol_full(module, name, skip, policy, options);
            generated = {cobol.source, cobol.diagnostics};
            for (std::size_t i = 0; i < cobol.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, cobol.parts[i]);
            }
            language_interface = cobol.copybook;
            second_interface = cobol.callbacks;
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
        } else if (lisp_full) {
            LispFullOptions options;
            options.parts = python_parts;
            options.library = lisp_library;
            options.interface_name = std::filesystem::path(lisp_output).stem().string();
            for (std::size_t start = 0; start < lisp_nicknames.size();) {
                const auto comma = lisp_nicknames.find(',', start);
                const auto item = lisp_nicknames.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!item.empty()) options.nicknames.push_back(item);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            const auto lisp = generate_common_lisp_full(module, name, skip, policy, options);
            generated = {lisp.source, lisp.diagnostics};
            for (std::size_t i = 0; i < lisp.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, lisp.parts[i]);
            }
            language_interface = lisp.interface;
            second_interface = lisp.system;
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
        } else if (language == "javascript") {
            const auto js = generate_javascript(module, name, skip, policy, python_parts);
            generated = {js.source, js.diagnostics};
            for (std::size_t i = 0; i < js.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, js.parts[i]);
            }
            language_interface = js.types;
        } else if (lua_full) {
            const auto lua = generate_lua_full(module, name, skip, policy, python_parts);
            generated = {lua.source, lua.diagnostics};
            for (std::size_t i = 0; i < lua.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, lua.parts[i]);
            }
            language_interface = lua.meta;
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
            for (const auto& [path, source] : extra_sources) dependency_contents += detail::dependency_rule(path, prerequisites);
        }
        detail::write_file(output_path, generated.source);
        for (const auto& [path, source] : extra_sources) detail::write_file(path, source);
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
