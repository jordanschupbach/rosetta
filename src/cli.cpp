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
#include "rosetta/visual_basic.hpp"
#include "rosetta/rust.hpp"
#include "rosetta/php.hpp"
#include "rosetta/perl.hpp"
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
#include <sstream>

namespace rosetta {
namespace {
constexpr const char* usage = R"(Usage: rosetta HEADER --module NAME -o OUTPUT [options] [-- CLANG_FLAGS...]

Generate native language bindings from a C++ header.
  --language lua|r|python|javascript|ruby|octave|julia|java|csharp|visual-basic|rust|php|perl|fortran|common-lisp|tcl|guile|ocaml|go|apl|cobol  Target language (default: lua)
  --object-system r6|s3|s4  R object system (default: r6)
  --r-output PATH       Generated R interface (default: OUTPUT with extension .R)
  --julia-output PATH   Generated Julia interface (default: OUTPUT with extension .jl)
  --julia-api minimal|full Julia binding style (default: minimal); full adds abstract types for
                        classes, generic functions, @subclass directors, properties, Base operators,
                        containers, callbacks and Markdown docstrings from the C++ documentation
  --julia-package NAME  Julia module name (--julia-api full; default: MODULE capitalized)
  --julia-library NAME  Shared library base name the module loads (--julia-api full)
  --julia-project PATH  Also write a Project.toml for the package (--julia-api full)
  --lisp-output PATH    Generated Common Lisp interface (default: OUTPUT with extension .lisp)
  --guile-output PATH   Generated Guile module (default: MODULE.scm beside OUTPUT)
  --guile-api minimal|full Guile binding style (default: minimal); full builds a libguile extension
                        with GOOPS classes, generic methods, Scheme subclass directors, containers,
                        callbacks, exception keys and docstrings from the C++ documentation
  --guile-module NAME   Guile module (--guile-api full; default: (MODULE); may be "a b")
  --guile-extension NAME Library load-extension opens (--guile-api full; default: libMODULE_guile)
  --ocaml-output PATH   Generated OCaml module (default: MODULE.ml beside OUTPUT)
  --ocaml-api minimal|full OCaml binding style (default: minimal); full writes MODULE.ml, MODULE.mli
                        (odoc documentation) and a findlib META beside it: a submodule per class,
                        overloads, optional arguments, callbacks, OCaml subclasses (directors) and
                        exceptions over the shared C ABI library (OUTPUT, with its C stubs)
  --go-output PATH      Generated Go package file (default: MODULE.go beside OUTPUT)
  --go-api minimal|full Go binding style (default: minimal); full writes a cgo module (go.mod, the
                        package file, internal/rosetta, a subpackage per policy module) over a
                        shared C ABI library: classes with embedding and XLike interfaces, errors,
                        overloads, containers, callbacks, Go implementations of virtual methods, godoc
  --go-package NAME     Go package name (--go-api full; default: MODULE)
  --go-module-path PATH go.mod module path (--go-api full; default: the package name)
  --go-library NAME     Shared library cgo links as -lNAME (--go-api full; default: MODULE)
  --gnu-apl-output PATH GNU APL script (default: MODULE.apl beside OUTPUT)
  --dyalog-output PATH  Dyalog APL namespace script (default: MODULE.apln beside OUTPUT)
  --apl-library-dir DIR Directory the APL scripts load their libraries from (default: .)
  --apl-api minimal|full APL binding style (default: minimal); full builds one GNU APL native
                        function library (libMODULE_gnu_apl.so) and a GNU APL script of PREFIX∆ functions
  --apl-prefix NAME     Name prefix of the APL functions (--apl-api full; default: MODULE)
  --cobol-output PATH   Generated COBOL copybook (default: MODULE.cpy beside OUTPUT)
  --cobol-string-size N Byte capacity of COBOL string fields (default: 256)
  --cobol-api API       minimal (default) or full: handles, containers, overloads, callbacks, overrides, docs
  --cobol-prefix WORD   Full API: prefix of every COBOL name (default: the module name)
  --cobol-callbacks PATH Full API: the callbacks copybook (default: MODULE-callbacks.cpy beside the copybook)
  --cobol-table-size N  Full API: OCCURS capacity of -TABLE records (default: 1000)
  --cobol-result-size N Full API: bytes of string results (default: the string size)
  --java-output PATH    Generated Java interface (default: MODULE.java beside OUTPUT)
  --fortran-output PATH Generated Fortran interface (default: MODULE.f90 beside OUTPUT)
  --fortran-api minimal|full Fortran binding style (default: minimal); full binds classes as derived
                        types with type-bound procedures and inheritance, overloads as generic
                        interfaces, containers, callbacks, extensible classes and FORD doc comments
  --fortran-module NAME Fortran module name (--fortran-api full; default: MODULE)
  --ruby-api minimal|full Ruby binding style (default: minimal); full builds an MRI native extension
                        with classes, inheritance, Ruby subclasses of virtual classes, keyword
                        arguments, blocks, operators, containers and a YARD documentation file
  --ruby-module NAME    Ruby module (--ruby-api full; default: MODULE in CamelCase; may be A::B)
  --ruby-output PATH    Generated YARD-documented Ruby file (--ruby-api full; default: MODULE.rb beside OUTPUT)
  --ruby-native PATH    require_relative path of the extension from the Ruby file, whose basename
                        names Init_<basename> (--ruby-api full; default: MODULE/MODULE)
  --tcl-api minimal|full Tcl binding style (default: minimal); full builds a stubs-enabled Tcl 8.6/9
                        extension with TclOO classes, inheritance, Tcl subclasses of virtual
                        classes, overloads, containers, callbacks and a documented package file
  --tcl-namespace NS    Tcl namespace of the commands (--tcl-api full; default: MODULE; may be a::b)
  --tcl-output PATH     Generated package file (--tcl-api full; default: MODULE.tcl beside OUTPUT)
  --tcl-pkgindex PATH   Generated pkgIndex.tcl (--tcl-api full; default: beside --tcl-output)
  --tcl-library NAME    Basename of the shared library the package loads (--tcl-api full; default: MODULE)
  --tcl-version V       Package version (--tcl-api full; default: 1.0)
  --octave-api minimal|full Octave binding style (default: minimal); full builds one native oct-file
                        (__MODULE__.oct) and a package of .m files: classdef handle classes with
                        inheritance, Octave subclasses of virtual classes, properties, operators,
                        containers, callbacks and help text for help/doc
  --octave-output DIR   Directory of the +PACKAGE folder (--octave-api full; default: OUTPUT's directory)
  --octave-package NAME Octave package (--octave-api full; default: MODULE)
  --csharp-output PATH Generated C# interface (default: MODULE.cs beside OUTPUT)
  --csharp-namespace NAME C# namespace (default: global namespace; --csharp-api full: MODULE in PascalCase)
  --csharp-api minimal|full C# binding style (default: minimal); full binds classes with inheritance,
                        virtual methods C# subclasses override, PascalCase methods and properties,
                        operators, containers, delegates and XML doc comments from the C++ documentation
  --csharp-library NAME Shared library base name the C# side loads (--csharp-api full)
  --csharp-project PATH Also write a .csproj for the interface (--csharp-api full)
  --csharp-functions NAME Static class of the module functions (--csharp-api full; default: Functions)
  --vb-output PATH      Generated Visual Basic interface (--language visual-basic; default: MODULE.vb beside
                        OUTPUT); classes with inheritance, Overridable methods VB subclasses override,
                        PascalCase methods and properties, operators, containers, delegates and XML docs
  --vb-namespace NAME   Visual Basic namespace (default: MODULE in PascalCase)
  --vb-library NAME     Shared library base name the VB side loads (default: MODULE)
  --vb-project PATH     Also write a .vbproj for the interface
  --vb-functions NAME   Class of the module functions (default: Functions)
  --rust-output DIR     Generated Cargo crate (--language rust; default: MODULE beside OUTPUT): Cargo.toml,
                        build.rs, src/lib.rs (the declared output) and src/rosetta.rs; structs with Deref
                        to their bases, traits Rust types implement for virtual methods, Result errors,
                        containers, closures and rustdoc from the C++ documentation
  --rust-crate NAME     Cargo package and crate name (default: MODULE)
  --rust-library NAME   Shared library the crate links as -lNAME (default: MODULE)
  --php-output PATH     Generated PHP file (--language php; default: MODULE.php beside OUTPUT): the runtime,
                        classes extending NativeObject (PHP subclasses override virtual methods), backed
                        enums, functions, SPL exceptions and PHPDoc; it loads the library through FFI
  --php-namespace NAME  PHP namespace (default: MODULE in PascalCase; a\b nests)
  --php-library NAME    Shared library base name the PHP file loads (default: MODULE)
  --perl-output PATH    Generated Perl module (--language perl, also pl; default: MODULE.pm beside OUTPUT):
                        the runtime, packages of the classes (Perl subclasses override virtual methods),
                        constants, functions, Error classes and POD; OUTPUT carries the XS glue, so build
                        the library with Perl's headers (perl -MExtUtils::Embed -e ccopts)
  --perl-package NAME   Perl package (default: MODULE in PascalCase; a::b nests)
  --perl-library NAME   Shared library base name the Perl module loads (default: MODULE)
  --java-package NAME   Java package (default: unnamed package; --java-api full: MODULE)
  --java-api minimal|full Java binding style (default: minimal); full binds classes with inheritance,
                         overrides, containers, callbacks and Javadoc; --java-output is then the
                         source root (default: OUTPUT's directory), PACKAGE/package-info.java the declared output
  --java-library NAME   JNI library base name for System.loadLibrary (--java-api full; default: MODULE)
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
        std::string language = "lua", object_system, r_output, julia_output, java_output, java_package, java_api, java_library, csharp_output, csharp_namespace, fortran_output, lisp_output, guile_output, ocaml_output, ocaml_api, go_output, go_api, go_package, go_module_path, go_library, gnu_apl_output, dyalog_output, apl_library_dir, cobol_output, cobol_string_size, lua_api, lua_meta, lisp_api, lisp_system, lisp_nicknames, lisp_library, julia_api, julia_package, julia_library, julia_project, csharp_api, csharp_library, csharp_project, csharp_functions, fortran_api, fortran_module, ruby_api, ruby_module, ruby_output, ruby_native, guile_api, guile_module, guile_extension, tcl_api, tcl_namespace, tcl_output, tcl_pkgindex, tcl_library, tcl_version, cobol_api, cobol_prefix, cobol_callbacks, cobol_table_size, cobol_result_size, js_types;
        std::optional<std::size_t> command_index;
        std::vector<std::string> extra_flags;
        std::vector<std::filesystem::path> scopes;
        bool skip = false, skip_bodies = false, r_package = false;
        std::string octave_api, octave_output, octave_package;
        std::string apl_api, apl_prefix;
        std::string vb_output, vb_namespace, vb_library, vb_project, vb_functions;
        std::string rust_output, rust_crate, rust_library;
        std::string php_output, php_namespace, php_library;
        std::string perl_output, perl_package, perl_library;
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
            if (arg == "--vb-output" || arg == "--vb-namespace" || arg == "--vb-library" || arg == "--vb-project" || arg == "--vb-functions") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                (arg == "--vb-output" ? vb_output : arg == "--vb-namespace" ? vb_namespace : arg == "--vb-library" ? vb_library
                    : arg == "--vb-project" ? vb_project : vb_functions) = arguments[i];
                continue;
            }
            if (arg == "--rust-output" || arg == "--rust-crate" || arg == "--rust-library") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                (arg == "--rust-output" ? rust_output : arg == "--rust-crate" ? rust_crate : rust_library) = arguments[i];
                continue;
            }
            if (arg == "--perl-output" || arg == "--perl-package" || arg == "--perl-library") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                (arg == "--perl-output" ? perl_output : arg == "--perl-package" ? perl_package : perl_library) = arguments[i];
                continue;
            }
            if (arg == "--php-output" || arg == "--php-namespace" || arg == "--php-library") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                (arg == "--php-output" ? php_output : arg == "--php-namespace" ? php_namespace : php_library) = arguments[i];
                continue;
            }
            if (arg == "--apl-api" || arg == "--apl-prefix") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                (arg == "--apl-api" ? apl_api : apl_prefix) = arguments[i];
                continue;
            }
            if (arg == "--octave-api" || arg == "--octave-output" || arg == "--octave-package") {
                if (++i == arguments.size() || arguments[i].empty() || arguments[i].front() == '-')
                    throw BindingError("missing value for " + arg);
                (arg == "--octave-api" ? octave_api : arg == "--octave-output" ? octave_output : octave_package) = arguments[i];
                continue;
            }
            if (arg == "--module" || arg == "--output" || arg == "-o" || arg == "--clang" ||
                arg == "--dump-ir" || arg == "--depfile" || arg == "--policy" || arg == "--language" || arg == "--compile-commands" ||
                arg == "--translation-unit" || arg == "--command-index" || arg == "--object-system" || arg == "--r-output" || arg == "--julia-output" || arg == "--java-output" || arg == "--java-package" || arg == "--java-api" || arg == "--java-library" || arg == "--csharp-output" || arg == "--csharp-namespace" || arg == "--fortran-output" || arg == "--lisp-output" || arg == "--guile-output" || arg == "--guile-api" || arg == "--guile-module" || arg == "--guile-extension" || arg == "--ocaml-output" || arg == "--ocaml-api" || arg == "--go-output" || arg == "--go-api" || arg == "--go-package" || arg == "--go-module-path" || arg == "--go-library" || arg == "--gnu-apl-output" || arg == "--dyalog-output" || arg == "--apl-library-dir" || arg == "--cobol-output" || arg == "--cobol-string-size" || arg == "--cobol-api" || arg == "--cobol-prefix" || arg == "--cobol-callbacks" || arg == "--cobol-table-size" || arg == "--cobol-result-size" || arg == "--include-as" || arg == "--python-parts" || arg == "--parts" || arg == "--lua-api" || arg == "--lua-meta" || arg == "--js-types" || arg == "--lisp-api" || arg == "--lisp-system" || arg == "--lisp-nicknames" || arg == "--lisp-library" || arg == "--julia-api" || arg == "--julia-package" || arg == "--julia-library" || arg == "--julia-project" || arg == "--csharp-api" || arg == "--csharp-library" || arg == "--csharp-project" || arg == "--csharp-functions" || arg == "--fortran-api" || arg == "--fortran-module" || arg == "--ruby-api" || arg == "--ruby-module" || arg == "--ruby-output" || arg == "--ruby-native" || arg == "--tcl-api" || arg == "--tcl-namespace" || arg == "--tcl-output" || arg == "--tcl-pkgindex" || arg == "--tcl-library" || arg == "--tcl-version") {
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
                else if (arg == "--julia-api") julia_api = value;
                else if (arg == "--julia-package") julia_package = value;
                else if (arg == "--julia-library") julia_library = value;
                else if (arg == "--julia-project") julia_project = value;
                else if (arg == "--csharp-api") csharp_api = value;
                else if (arg == "--csharp-library") csharp_library = value;
                else if (arg == "--csharp-project") csharp_project = value;
                else if (arg == "--csharp-functions") csharp_functions = value;
                else if (arg == "--java-output") java_output = value;
                else if (arg == "--java-package") java_package = value;
                else if (arg == "--java-api") java_api = value;
                else if (arg == "--java-library") java_library = value;
                else if (arg == "--fortran-output") fortran_output = value;
                else if (arg == "--fortran-api") fortran_api = value;
                else if (arg == "--fortran-module") fortran_module = value;
                else if (arg == "--ruby-api") ruby_api = value;
                else if (arg == "--ruby-module") ruby_module = value;
                else if (arg == "--ruby-output") ruby_output = value;
                else if (arg == "--ruby-native") ruby_native = value;
                else if (arg == "--tcl-api") tcl_api = value;
                else if (arg == "--tcl-namespace") tcl_namespace = value;
                else if (arg == "--tcl-output") tcl_output = value;
                else if (arg == "--tcl-pkgindex") tcl_pkgindex = value;
                else if (arg == "--tcl-library") tcl_library = value;
                else if (arg == "--tcl-version") tcl_version = value;
                else if (arg == "--lisp-output") lisp_output = value;
                else if (arg == "--guile-output") guile_output = value;
                else if (arg == "--guile-api") guile_api = value;
                else if (arg == "--guile-module") guile_module = value;
                else if (arg == "--guile-extension") guile_extension = value;
                else if (arg == "--ocaml-output") ocaml_output = value;
                else if (arg == "--ocaml-api") ocaml_api = value;
                else if (arg == "--go-output") go_output = value;
                else if (arg == "--go-api") go_api = value;
                else if (arg == "--go-package") go_package = value;
                else if (arg == "--go-module-path") go_module_path = value;
                else if (arg == "--go-library") go_library = value;
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
        if (language == "vb" || language == "vbnet" || language == "visualbasic") language = "visual-basic";
        if (language == "rs") language = "rust";
        if (language == "pl") language = "perl";
        if (language != "lua" && language != "r" && language != "python" && language != "javascript" && language != "ruby" && language != "octave" && language != "julia" && language != "java" && language != "csharp" && language != "visual-basic" && language != "rust" && language != "php" && language != "perl" && language != "fortran" && language != "common-lisp" && language != "tcl" && language != "guile" && language != "ocaml" && language != "go" && language != "apl" && language != "cobol") throw BindingError("unsupported language: " + language);
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
        if (language != "julia" && (!julia_api.empty() || !julia_package.empty() || !julia_library.empty() || !julia_project.empty()))
            throw BindingError("--julia-api, --julia-package, --julia-library and --julia-project require --language julia");
        if (!julia_api.empty() && julia_api != "minimal" && julia_api != "full") throw BindingError("--julia-api must be minimal or full");
        const bool julia_full = language == "julia" && julia_api == "full";
        if (!julia_full && (!julia_package.empty() || !julia_library.empty() || !julia_project.empty()))
            throw BindingError("--julia-package, --julia-library and --julia-project require --julia-api full");
        if (language != "csharp" && (!csharp_api.empty() || !csharp_library.empty() || !csharp_project.empty() || !csharp_functions.empty()))
            throw BindingError("--csharp-api, --csharp-library, --csharp-project and --csharp-functions require --language csharp");
        if (!csharp_api.empty() && csharp_api != "minimal" && csharp_api != "full") throw BindingError("--csharp-api must be minimal or full");
        const bool csharp_full = language == "csharp" && csharp_api == "full";
        if (language != "visual-basic" && (!vb_output.empty() || !vb_namespace.empty() || !vb_library.empty() || !vb_project.empty() || !vb_functions.empty()))
            throw BindingError("--vb-output, --vb-namespace, --vb-library, --vb-project and --vb-functions require --language visual-basic");
        if (language == "visual-basic" && vb_output.empty()) vb_output = (std::filesystem::path(output).parent_path() / (name + ".vb")).string();
        if (language != "rust" && (!rust_output.empty() || !rust_crate.empty() || !rust_library.empty()))
            throw BindingError("--rust-output, --rust-crate and --rust-library require --language rust");
        if (language == "rust" && rust_output.empty()) rust_output = (std::filesystem::path(output).parent_path() / name).string();
        const std::string rust_lib = language == "rust" ? (std::filesystem::path(rust_output) / "src" / "lib.rs").string() : std::string();
        if (language != "php" && (!php_output.empty() || !php_namespace.empty() || !php_library.empty()))
            throw BindingError("--php-output, --php-namespace and --php-library require --language php");
        if (language == "php" && php_output.empty()) php_output = (std::filesystem::path(output).parent_path() / (name + ".php")).string();
        if (language != "perl" && (!perl_output.empty() || !perl_package.empty() || !perl_library.empty()))
            throw BindingError("--perl-output, --perl-package and --perl-library require --language perl");
        if (language == "perl" && perl_output.empty()) perl_output = (std::filesystem::path(output).parent_path() / (name + ".pm")).string();
        if (language != "java" && (!java_api.empty() || !java_library.empty())) throw BindingError("--java-api and --java-library require --language java");
        if (!java_api.empty() && java_api != "minimal" && java_api != "full") throw BindingError("--java-api must be minimal or full");
        const bool java_full = language == "java" && java_api == "full";
        if (!java_full && !java_library.empty()) throw BindingError("--java-library requires --java-api full");
        if (!csharp_full && (!csharp_library.empty() || !csharp_project.empty() || !csharp_functions.empty()))
            throw BindingError("--csharp-library, --csharp-project and --csharp-functions require --csharp-api full");
        if (language != "cobol" && (!cobol_api.empty() || !cobol_prefix.empty() || !cobol_callbacks.empty() || !cobol_table_size.empty() || !cobol_result_size.empty()))
            throw BindingError("--cobol-api, --cobol-prefix, --cobol-callbacks, --cobol-table-size and --cobol-result-size require --language cobol");
        if (!cobol_api.empty() && cobol_api != "minimal" && cobol_api != "full") throw BindingError("--cobol-api must be minimal or full");
        const bool cobol_full = language == "cobol" && cobol_api == "full";
        if (language != "fortran" && (!fortran_api.empty() || !fortran_module.empty()))
            throw BindingError("--fortran-api and --fortran-module require --language fortran");
        if (!fortran_api.empty() && fortran_api != "minimal" && fortran_api != "full") throw BindingError("--fortran-api must be minimal or full");
        const bool fortran_full = language == "fortran" && fortran_api == "full";
        if (!fortran_full && !fortran_module.empty()) throw BindingError("--fortran-module requires --fortran-api full");
        if (language != "ocaml" && !ocaml_api.empty()) throw BindingError("--ocaml-api requires --language ocaml");
        if (!ocaml_api.empty() && ocaml_api != "minimal" && ocaml_api != "full") throw BindingError("--ocaml-api must be minimal or full");
        const bool ocaml_full = language == "ocaml" && ocaml_api == "full";
        if (language != "ruby" && (!ruby_api.empty() || !ruby_module.empty() || !ruby_output.empty() || !ruby_native.empty()))
            throw BindingError("--ruby-api, --ruby-module, --ruby-output and --ruby-native require --language ruby");
        if (!ruby_api.empty() && ruby_api != "minimal" && ruby_api != "full") throw BindingError("--ruby-api must be minimal or full");
        const bool ruby_full = language == "ruby" && ruby_api == "full";
        if (language != "guile" && (!guile_api.empty() || !guile_module.empty() || !guile_extension.empty()))
            throw BindingError("--guile-api, --guile-module and --guile-extension require --language guile");
        if (!guile_api.empty() && guile_api != "minimal" && guile_api != "full") throw BindingError("--guile-api must be minimal or full");
        const bool guile_full = language == "guile" && guile_api == "full";
        if (!guile_full && (!guile_module.empty() || !guile_extension.empty()))
            throw BindingError("--guile-module and --guile-extension require --guile-api full");
        if (!ruby_full && (!ruby_module.empty() || !ruby_output.empty() || !ruby_native.empty()))
            throw BindingError("--ruby-module, --ruby-output and --ruby-native require --ruby-api full");
        if (ruby_full && ruby_output.empty()) ruby_output = (std::filesystem::path(output).parent_path() / (name + ".rb")).string();
        if (language != "tcl" && (!tcl_api.empty() || !tcl_namespace.empty() || !tcl_output.empty() || !tcl_pkgindex.empty() || !tcl_library.empty() || !tcl_version.empty()))
            throw BindingError("--tcl-api, --tcl-namespace, --tcl-output, --tcl-pkgindex, --tcl-library and --tcl-version require --language tcl");
        if (!tcl_api.empty() && tcl_api != "minimal" && tcl_api != "full") throw BindingError("--tcl-api must be minimal or full");
        const bool tcl_full = language == "tcl" && tcl_api == "full";
        if (!tcl_full && (!tcl_namespace.empty() || !tcl_output.empty() || !tcl_pkgindex.empty() || !tcl_library.empty() || !tcl_version.empty()))
            throw BindingError("--tcl-namespace, --tcl-output, --tcl-pkgindex, --tcl-library and --tcl-version require --tcl-api full");
        if (tcl_full && tcl_output.empty()) tcl_output = (std::filesystem::path(output).parent_path() / (name + ".tcl")).string();
        if (tcl_full && tcl_pkgindex.empty()) tcl_pkgindex = (std::filesystem::path(tcl_output).parent_path() / "pkgIndex.tcl").string();
        if (tcl_full && tcl_version.empty()) tcl_version = "1.0";
        if (language != "octave" && (!octave_api.empty() || !octave_output.empty() || !octave_package.empty()))
            throw BindingError("--octave-api, --octave-output and --octave-package require --language octave");
        if (!octave_api.empty() && octave_api != "minimal" && octave_api != "full") throw BindingError("--octave-api must be minimal or full");
        const bool octave_full = language == "octave" && octave_api == "full";
        const bool apl_full = language == "apl" && apl_api == "full";
        if (!octave_full && (!octave_output.empty() || !octave_package.empty()))
            throw BindingError("--octave-output and --octave-package require --octave-api full");
        if (octave_full && octave_output.empty()) octave_output = std::filesystem::path(output).parent_path().string();
        if (octave_full && octave_output.empty()) octave_output = ".";
        // The package overview (+PACKAGE/Contents.m) is the declared output; the other .m files are written beside it.
        const auto octave_contents = octave_full ? (std::filesystem::path(octave_output) / ("+" + (octave_package.empty() ? name : octave_package)) / "Contents.m").string() : std::string();
        if (!cobol_full && (!cobol_prefix.empty() || !cobol_callbacks.empty() || !cobol_table_size.empty() || !cobol_result_size.empty()))
            throw BindingError("--cobol-prefix, --cobol-callbacks, --cobol-table-size and --cobol-result-size require --cobol-api full");
        if (language != "javascript" && !js_types.empty()) throw BindingError("--js-types requires --language javascript");
        if (language == "javascript" && js_types.empty()) js_types = (std::filesystem::path(output).parent_path() / (name + ".d.ts")).string();
        if (language != "go" && !go_api.empty()) throw BindingError("--go-api requires --language go");
        if (!go_api.empty() && go_api != "minimal" && go_api != "full") throw BindingError("--go-api must be minimal or full");
        const bool go_full = language == "go" && go_api == "full";
        if (!go_full && (!go_package.empty() || !go_module_path.empty() || !go_library.empty()))
            throw BindingError("--go-package, --go-module-path and --go-library require --go-api full");
        if (language != "python" && language != "javascript" && language != "visual-basic" && language != "rust" && language != "php" && language != "perl" && !lua_full && !lisp_full && !julia_full && !csharp_full && !cobol_full && !fortran_full && !ruby_full && !guile_full && !octave_full && !tcl_full && !java_full && !go_full && !ocaml_full && !apl_full && !(language == "r" && object_system != "s3" && object_system != "s4") && python_parts != 1)
            throw BindingError("--parts requires --language python, javascript, r (R6), lua (--lua-api full), common-lisp (--lisp-api full), julia (--julia-api full), csharp (--csharp-api full), visual-basic, rust, php, perl, fortran (--fortran-api full), ruby (--ruby-api full), guile (--guile-api full), octave (--octave-api full), tcl (--tcl-api full), java (--java-api full), go (--go-api full), ocaml (--ocaml-api full), apl (--apl-api full) or cobol (--cobol-api full)");
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
            // (use-modules (a b)) loads a/b.scm: the file is named after the last part of the module.
            std::string guile_file = name;
            if (guile_full && !guile_module.empty()) {
                std::string text = guile_module;
                for (auto& c : text) if (c == '(' || c == ')' || c == '.' || c == '/') c = ' ';
                std::istringstream parts(text);
                for (std::string part; parts >> part;) guile_file = part;
            }
            if (guile_output.empty()) guile_output = (std::filesystem::path(output).parent_path() / (guile_file + ".scm")).string();
            if (std::filesystem::path(guile_output).filename() != guile_file + ".scm")
                throw BindingError("Guile module filename must match the module name: " + guile_file + ".scm");
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
        if (language != "apl" && (!apl_api.empty() || !apl_prefix.empty())) throw BindingError("--apl-api and --apl-prefix require --language apl");
        if (!apl_api.empty() && apl_api != "minimal" && apl_api != "full") throw BindingError("--apl-api must be minimal or full");
        if (!apl_full && !apl_prefix.empty()) throw BindingError("--apl-prefix requires --apl-api full");
        // The full APL API writes one GNU APL script; there is no Dyalog script.
        if (apl_full && !dyalog_output.empty()) throw BindingError("--dyalog-output is not supported with --apl-api full (GNU APL only)");
        if (language != "apl" && (!gnu_apl_output.empty() || !dyalog_output.empty() || !apl_library_dir.empty()))
            throw BindingError("--gnu-apl-output, --dyalog-output and --apl-library-dir require --language apl");
        if (language == "apl") {
            if (gnu_apl_output.empty()) gnu_apl_output = (std::filesystem::path(output).parent_path() / (name + ".apl")).string();
            if (dyalog_output.empty() && !apl_full) dyalog_output = (std::filesystem::path(output).parent_path() / (name + ".apln")).string();
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
        // The full Java API writes a source tree: --java-output is its root, and the package's
        // package-info.java the declared output (the other .java files are written beside it).
        std::string java_package_info;
        if (java_full) {
            if (java_output.empty()) java_output = std::filesystem::path(output).parent_path().string();
            if (java_output.empty()) java_output = ".";
            std::string package_path = java_package.empty() ? name : java_package;
            for (auto& c : package_path) if (c == '.') c = '/';
            java_package_info = (std::filesystem::path(java_output) / package_path / "package-info.java").string();
        } else if (language == "java") {
            if (java_output.empty()) java_output = (std::filesystem::path(output).parent_path() / (name + ".java")).string();
            if (std::filesystem::path(java_output).filename() != name + ".java")
                throw BindingError("Java interface filename must match the module name: " + name + ".java");
        }
        if (language != "csharp" && (!csharp_output.empty() || !csharp_namespace.empty())) throw BindingError("--csharp-output and --csharp-namespace require --language csharp");
        if (language == "csharp" && csharp_output.empty()) csharp_output = (std::filesystem::path(output).parent_path() / (name + ".cs")).string();
        if (language != "fortran" && !fortran_output.empty()) throw BindingError("--fortran-output requires --language fortran");
        if (language == "fortran" && fortran_output.empty()) fortran_output = (std::filesystem::path(output).parent_path() / (name + ".f90")).string();
        const auto interface_output = tcl_full ? tcl_output : octave_full ? octave_contents : language == "ruby" ? ruby_output : language == "javascript" ? js_types : language == "cobol" ? cobol_output : language == "apl" ? gnu_apl_output : language == "go" ? go_output : language == "ocaml" ? ocaml_output : language == "guile" ? guile_output : language == "common-lisp" ? lisp_output : language == "fortran" ? fortran_output : language == "csharp" ? csharp_output : language == "visual-basic" ? vb_output : language == "rust" ? rust_lib : language == "php" ? php_output : language == "perl" ? perl_output : java_full ? java_package_info : language == "java" ? java_output : language == "julia" ? julia_output : language == "lua" ? lua_meta : r_output;
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
        const auto second_output = !tcl_pkgindex.empty() ? tcl_pkgindex : !dyalog_output.empty() ? dyalog_output : !cobol_callbacks.empty() ? cobol_callbacks : !julia_project.empty() ? julia_project : !csharp_project.empty() ? csharp_project : !vb_project.empty() ? vb_project : lisp_system;
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
                if (same_file(interface_path, path)) throw BindingError(language == "cobol" ? "COBOL copybook and inputs/outputs must have distinct paths" : language == "apl" ? "APL scripts and inputs/outputs must have distinct paths" : language == "go" ? "Go package file and inputs/outputs must have distinct paths" : language == "ocaml" ? "OCaml module and inputs/outputs must have distinct paths" : language == "guile" ? "Guile module and inputs/outputs must have distinct paths" : language == "common-lisp" ? "Common Lisp interface and inputs/outputs must have distinct paths" : language == "fortran" ? "Fortran interface and inputs/outputs must have distinct paths" : language == "csharp" ? "C# interface and inputs/outputs must have distinct paths" : language == "visual-basic" ? "Visual Basic interface and inputs/outputs must have distinct paths" : language == "rust" ? "Rust crate and inputs/outputs must have distinct paths" : language == "php" ? "PHP file and inputs/outputs must have distinct paths" : language == "perl" ? "Perl module and inputs/outputs must have distinct paths" : language == "java" ? "Java interface and inputs/outputs must have distinct paths" :
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
        std::vector<std::pair<std::filesystem::path, std::string>> package_files; // written, not dependency-tracked
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
        } else if (ruby_full) {
            RubyFullOptions options;
            options.parts = python_parts;
            options.ruby_module = ruby_module;
            options.native = ruby_native;
            const auto ruby = generate_ruby_full(module, name, skip, policy, options);
            generated = {ruby.source, ruby.diagnostics};
            for (std::size_t i = 0; i < ruby.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, ruby.parts[i]);
            }
            language_interface = ruby.ruby;
        } else if (language == "ruby") {
            const auto ruby = generate_ruby(module, name, skip, policy);
            generated = {ruby.source, ruby.diagnostics};
        } else if (tcl_full) {
            TclFullOptions options;
            options.parts = python_parts;
            options.tcl_namespace = tcl_namespace;
            options.library = tcl_library;
            options.version = tcl_version;
            options.tcl_file = std::filesystem::path(tcl_output).filename().string();
            const auto tcl = generate_tcl_full(module, name, skip, policy, options);
            generated = {tcl.source, tcl.diagnostics};
            for (std::size_t i = 0; i < tcl.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, tcl.parts[i]);
            }
            language_interface = tcl.tcl;
            second_interface = tcl.pkg_index;
        } else if (language == "tcl") {
            const auto tcl = generate_tcl(module, name, skip, policy);
            generated = {tcl.source, tcl.diagnostics};
        } else if (guile_full) {
            GuileFullOptions options;
            options.parts = python_parts;
            options.module = guile_module;
            options.extension = guile_extension;
            const auto guile = generate_guile_full(module, name, skip, policy, options);
            generated = {guile.source, guile.diagnostics};
            for (std::size_t i = 0; i < guile.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, guile.parts[i]);
            }
            language_interface = guile.scheme;
            const auto directory = std::filesystem::path(guile_output).parent_path();
            for (const auto& [relative, text] : guile.submodules) package_files.emplace_back(directory / relative, text);
        } else if (language == "guile") {
            const auto guile = generate_guile(module, name, skip, policy);
            generated = {guile.source, guile.diagnostics};
            language_interface = guile.interface;
        } else if (ocaml_full) {
            OcamlFullOptions options;
            options.parts = python_parts;
            const auto ocaml = generate_ocaml_full(module, name, skip, policy, options);
            generated = {ocaml.source, ocaml.diagnostics};
            for (std::size_t i = 0; i < ocaml.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, ocaml.parts[i]);
            }
            language_interface = ocaml.ml;
            const auto directory = std::filesystem::path(ocaml_output).parent_path();
            package_files.emplace_back(directory / (name + ".mli"), ocaml.mli);
            package_files.emplace_back(directory / "META", ocaml.meta);
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
        } else if (apl_full) {
            AplFullOptions options;
            options.parts = python_parts;
            options.prefix = apl_prefix;
            options.library_dir = apl_library_dir;
            const auto apl = generate_apl_full(module, name, skip, policy, options);
            generated = {apl.source, apl.diagnostics};
            for (std::size_t i = 0; i < apl.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, apl.parts[i]);
            }
            language_interface = apl.script;
        } else if (language == "apl") {
            const auto apl = generate_apl(module, name, skip, policy, apl_library_dir);
            generated = {apl.source, apl.diagnostics};
            language_interface = apl.gnu;
            second_interface = apl.dyalog;
        } else if (go_full) {
            GoFullOptions options;
            options.parts = python_parts;
            options.package = go_package;
            options.module_path = go_module_path;
            options.library = go_library;
            const auto go = generate_go_full(module, name, skip, policy, options);
            generated = {go.source, go.diagnostics};
            for (std::size_t i = 0; i < go.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, go.parts[i]);
            }
            language_interface = go.package;
            for (const auto& [relative, text] : go.files) package_files.emplace_back(std::filesystem::path(go_output).parent_path() / relative, text);
        } else if (language == "go") {
            const auto generated_go = generate_go(module, name, skip, policy);
            generated = {generated_go.source, generated_go.diagnostics};
            language_interface = generated_go.interface;
        } else if (octave_full) {
            OctaveFullOptions options;
            options.parts = python_parts;
            options.package = octave_package;
            const auto octave = generate_octave_full(module, name, skip, policy, options);
            generated = {octave.source, octave.diagnostics};
            for (std::size_t i = 0; i < octave.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, octave.parts[i]);
            }
            language_interface = octave.contents;
            for (const auto& [relative, text] : octave.files) package_files.emplace_back(std::filesystem::path(octave_output) / relative, text);
        } else if (language == "octave") {
            const auto octave = generate_octave(module, name, skip, policy);
            generated = {octave.source, octave.diagnostics};
        } else if (julia_full) {
            JuliaFullOptions options;
            options.parts = python_parts;
            options.package = julia_package;
            options.library = julia_library;
            const auto julia = generate_julia_full(module, name, skip, policy, options);
            generated = {julia.source, julia.diagnostics};
            for (std::size_t i = 0; i < julia.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, julia.parts[i]);
            }
            language_interface = julia.interface;
            second_interface = julia.project;
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
        } else if (fortran_full) {
            FortranFullOptions options;
            options.parts = python_parts;
            options.fortran_module = fortran_module;
            const auto fortran = generate_fortran_full(module, name, skip, policy, options);
            generated = {fortran.source, fortran.diagnostics};
            for (std::size_t i = 0; i < fortran.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, fortran.parts[i]);
            }
            language_interface = fortran.interface;
        } else if (language == "fortran") {
            const auto fortran = generate_fortran(module, name, skip, policy);
            generated = {fortran.source, fortran.diagnostics};
            language_interface = fortran.interface;
        } else if (csharp_full) {
            CSharpFullOptions options;
            options.parts = python_parts;
            options.managed_namespace = csharp_namespace;
            options.library = csharp_library;
            options.functions_class = csharp_functions;
            const auto csharp = generate_csharp_full(module, name, skip, policy, options);
            generated = {csharp.source, csharp.diagnostics};
            for (std::size_t i = 0; i < csharp.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, csharp.parts[i]);
            }
            language_interface = csharp.interface;
            second_interface = csharp.project;
        } else if (language == "visual-basic") {
            VisualBasicOptions options;
            options.parts = python_parts;
            options.managed_namespace = vb_namespace;
            options.library = vb_library;
            options.functions_class = vb_functions;
            const auto vb = generate_visual_basic(module, name, skip, policy, options);
            generated = {vb.source, vb.diagnostics};
            for (std::size_t i = 0; i < vb.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, vb.parts[i]);
            }
            language_interface = vb.interface;
            second_interface = vb.project;
        } else if (language == "rust") {
            RustOptions options;
            options.parts = python_parts;
            options.crate = rust_crate;
            options.library = rust_library;
            const auto rust = generate_rust(module, name, skip, policy, options);
            generated = {rust.source, rust.diagnostics};
            for (std::size_t i = 0; i < rust.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, rust.parts[i]);
            }
            language_interface = rust.lib;
            for (const auto& [relative, text] : rust.files) package_files.emplace_back(std::filesystem::path(rust_output) / relative, text);
        } else if (language == "php") {
            PhpOptions options;
            options.parts = python_parts;
            options.namespace_ = php_namespace;
            options.library = php_library;
            const auto php = generate_php(module, name, skip, policy, options);
            generated = {php.source, php.diagnostics};
            for (std::size_t i = 0; i < php.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, php.parts[i]);
            }
            language_interface = php.php;
        } else if (language == "perl") {
            PerlOptions options;
            options.parts = python_parts;
            options.package = perl_package;
            options.library = perl_library;
            const auto perl = generate_perl(module, name, skip, policy, options);
            generated = {perl.source, perl.diagnostics};
            for (std::size_t i = 0; i < perl.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, perl.parts[i]);
            }
            language_interface = perl.perl;
        } else if (language == "csharp") {
            const auto csharp = generate_csharp(module, name, skip, policy, csharp_namespace);
            generated = {csharp.source, csharp.diagnostics};
            language_interface = csharp.interface;
        } else if (java_full) {
            JavaFullOptions options;
            options.parts = python_parts;
            options.package = java_package;
            options.library = java_library;
            const auto java = generate_java_full(module, name, skip, policy, options);
            generated = {java.source, java.diagnostics};
            for (std::size_t i = 0; i < java.parts.size(); ++i) {
                auto path = output_path;
                path.replace_filename(output_path.stem().string() + "_" + std::to_string(i + 1) + output_path.extension().string());
                extra_sources.emplace_back(path, java.parts[i]);
            }
            language_interface = java.package_info;
            for (const auto& [relative, text] : java.files) package_files.emplace_back(std::filesystem::path(java_output) / relative, text);
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
        for (const auto& [path, text] : package_files) detail::write_file(path, text);
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
