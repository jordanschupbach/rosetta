# datamunge as the binding test case

[datamunge](../../datamunge) (`~/projects/datamunge`) is a large real C++ library: data frames, models, plots, autodiff, neural networks and GIS, with Doxygen comments on its public API. Every Rosetta backend should be able to bind it completely. This plan tracks each language through three projects:

- **rosetta**: a *full* backend for the language (`--<lang>-api full` or equivalent) that binds what datamunge needs and turns its Doxygen comments into the language's own documentation format.
- **datamunge**: the generated binding replaces SWIG (or is new), built, packaged, tested and demonstrated the same way as the existing Rosetta languages.
- **mep** (`~/projects/mep`): the generated documentation appears in the editor (hover, completion and `gh`), the Run button runs datamunge examples, and a language UI mode (console, Help and panels) exists for the language.

## Definition of done (per language)

A language is checked off when every item below holds.

**rosetta**
- [ ] A full backend covers the API surface datamunge uses: classes and ownership, overloads, inheritance, enums, standard containers, callbacks and errors. Each unsupported construct is reported (`--skip-unsupported`), never silently dropped.
- [ ] Doxygen `@brief`, `@param`, `@return`, `@throws`, `@note` and code blocks are converted to the language's documentation format and attached where its tools look for them.
- [ ] Generation can be split into `--parts`, and the backend handles policy modules.
- [ ] Feature tests run in the language itself, with a `just <lang>-test` recipe. README, architecture and roadmap entries are written.

**datamunge**
- [ ] `src/<pkg>/rosetta/{bindings.hpp,policy.json}` exists, plus a `scripts/prebuild_bindings.py <lang>` entry.
- [ ] `just prebuild-/build-/test-/run-/repl-<lang>` recipes work, with a nix dev shell and package. The SWIG path for the language is removed.
- [ ] Tests live in `tests/<lang>/` (or `bindings_tests/<lang>/`). The examples in `examples/<lang>/` are ports of the shared example set, each with a `CMD:` header.
- [ ] The README bindings table is updated.

**mep**
- [ ] mep has its own language server for the language (`src/<lang>_lsp*.cpp`, `mep-<lang>-lsp`), with diagnostics, completion, hover, signature help, definitions and an outline. An external server does not count.
- [ ] mep has its own formatter for the language (`src/<lang>_format*.cpp`), run by `gf` and by the server's formatting request.
- [ ] A tree-sitter grammar for the language is vendored in `grammars/tree-sitter-<lang>`, with highlight, fold and structure queries.
- [ ] The generated documentation shows on hover, in completion and through `gh` for `datamunge` symbols. mep's own server reads the generated docs artifact (docstrings, stub or declarations).
- [ ] The Run button runs a datamunge example: a per-extension default exists and the `CMD:` header works.
- [ ] A language UI mode exists with a console, a Help tab and language panels. It is registered in `mep.language_ui_modes` and has a help page.
- [ ] Language-server and UI tests and help pages are written.

## Status

| Language | rosetta full + docs | datamunge | mep LSP | mep formatter | tree-sitter | mep docs | Run button | UI mode |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Python | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| R | ✅ (roxygen → Rd) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Lua | ✅ (LuaCATS stub) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Common Lisp | ✅ (docstrings) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| COBOL | ✅ (copybook `*>>`) | ✅ | ✅ | ✅ | ✅ (vendored) | ✅ | ✅ | ✅ |
| JavaScript | ✅ (`index.d.ts` JSDoc) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Julia | ✅ (Markdown docstrings) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`julia`) | ✅ |
| C# | ✅ (XML doc comments) | ✅ | ✅ | ✅ | ✅ (`c_sharp`) | ✅ | ✅ (`dotnet run`) | ✅ |
| Fortran | ✅ (FORD `!>`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`gfortran`) | ✅ |
| Ruby | ✅ (YARD `.rb`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`ruby`) | ✅ |
| Octave | ✅ (`+pkg` `.m` help) | ✅ | ✅ | ✅ | ✅ (`matlab`) | ✅ | ✅ (`octave-cli`) | ✅ |
| Tcl | ✅ (`.tcl` doc comments) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`tclsh`) | ✅ |
| Guile Scheme | ✅ (docstrings, GOOPS) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`guile`) | ✅ |
| Java | ✅ (Javadoc `.java`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`java`) | ✅ |
| Go | ✅ (godoc, cgo) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`go run`) | ✅ |
| OCaml | ✅ (odoc `.mli`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`ocaml`) | ✅ |
| APL (GNU, Dyalog) | ✅ (GNU native `⎕FX`, `⍝` docs; Dyalog not done) | ☐ (none) | ✅ | ✅ | ✅ (written) | ☐ | ✅ (`apl --script`) | ✅ |
| Visual Basic | ✅ (XML doc comments, `.vbproj`) | ✅ | ✅ | ✅ | ✅ (written) | ✅ | ✅ (`dotnet run`) | ✅ |
| Rust | ✅ (rustdoc, Cargo crate) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`cargo run`) | ✅ |
| PHP | ✅ (PHPDoc, one file over FFI) | ✅ | ✅ | ✅ | ✅ (vendored) | ✅ | ✅ (`php`) | ✅ |
| Perl | ✅ (POD, module over XS glue) | ✅ | ✅ | ✅ | ✅ (vendored) | ✅ | ✅ (`perl`) | ✅ |
| Swift | ✅ (DocC, Swift package) | ✅ | ✅ | ✅ | ✅ (vendored) | ✅ | ✅ (`swift`) | ✅ |
| Pascal | ✅ (XML doc comments, Free Pascal unit) | ✅ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Dart | ✅ (dartdoc, Dart package) | ✅ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Ada | ✅ (GNATdoc, Ada package) | ✅ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Objective-C | ✅ (`///` doc comments, `.h` and `.m`) | ✅ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Prolog | ✅ (PlDoc, SWI-Prolog module over foreign glue) | ✅ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Haskell | ✅ (Haddock, GHC module over the FFI) | ✅ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |

"Run button ✅" above means only that mep has a default for the file extension. Each language still has to show that a datamunge example runs from its `CMD:` header.

## Order

The languages are grouped by the native mechanism they share, so that shared work is done once.

1. **Julia** (C ABI, `ccall`). Lisp's full backend just established a full C ABI layer, and Julia has the same shape. Julia docstrings map directly onto Doxygen, and mep's Julia server already shows the docstrings of definitions it can see.
2. **C#** (C ABI, P/Invoke). This is the second consumer of the C ABI shape: factor out the shared C ABI planning from `lisp_full`/`julia_full` at this point. Documentation becomes XML doc comments. mep's C# server and UI mode already exist.
3. **Fortran** (C ABI, `ISO_C_BINDING`). The third C ABI consumer. Documentation becomes FORD `!>` comments, which mep's Fortran server reads.
4. **Catch-up for finished languages.** JavaScript gets a UI mode: a Node console, Help from `index.d.ts`, and panels. COBOL gets a mep formatter (fixed and free format) and a tree-sitter grammar, which replaces the hand-written highlighting in `cobol_syntax.cpp`.
5. **Ruby** (native extension). Modeled on the full Python backend. Documentation becomes YARD in a generated `.rb` file. mep needs a tree-sitter grammar, then its own Ruby parser, language server (which reads the YARD stub) and formatter, replacing solargraph.
6. **Octave** (native `.oct`). Documentation becomes help text for `help` and `doc`; mep's Octave server reads the generated `.m` stubs.
7. **Tcl** (Tcl 9 stubs). Documentation goes in generated `.tcl` comments. Rosetta side done: `--tcl-api full` writes a stubs extension with TclOO classes plus a package directory (`pkgIndex.tcl`, documented `NAME.tcl`); datamunge still has to replace its SWIG binding with `src/datamungetcl`.
8. **Guile Scheme** (libguile). Documentation becomes procedure docstrings, read by mep's Scheme server.
9. **Java** (JNI). Documentation becomes Javadoc, read by mep's Java server; the console is jshell.
10. **Go** (cgo). Documentation becomes godoc comments, read by mep's Go server. Go has no REPL, so its UI mode is run output plus a docs panel.
11. **OCaml** (C stubs). Documentation goes in `.mli` odoc comments. mep needs a grammar, its own OCaml server (reading `.mli` files) and a formatter in ocamlformat's default style, replacing ocamllsp. The console is utop.
12. **APL** (C ABI through `⎕NA`, GNU APL native functions). Documentation goes in generated namespace comments. mep needs a grammar, its own APL server and formatter, and a Run-button default for `.apl`/`.apls`.

Ruby, OCaml and APL carry the most mep work, because each needs a new parser, language server and formatter.

## Progress log

Entries are added as each language is checked off.

### Julia (2026-11-13)

- **rosetta:** `--julia-api full` (`src/julia_full.cpp`, runtime in `src/julia_full_runtime_julia.hpp`) reuses the C ABI runtime that `lisp_full` uses. Classes become abstract types with hidden storage structs; methods become generic functions with `Union` dispatch for secondary bases; fields become properties; operators and Base names extend Base when every method is typed; enums are Symbols; directors are subclassed with `@subclass` and `supercall`. Doxygen becomes Markdown docstrings, so `?name` shows them. The package is a `Project.toml` plus `src/<Package>.jl`. `tests/test_julia_full.jl` runs 311 checks on `python_features.hpp`. Contextual keywords (`outer`, `where`, `type`, ...) are no longer escaped. Only real keywords, `ans`, `eval` and `include` get a trailing underscore.
- **datamunge:** `src/datamungejulia` (16 parts; the policy's `data` submodule is `Data`) installs as `share/julia/Datamunge` on `JULIA_LOAD_PATH`. `just test-julia` runs 15 checks. All 31 Lisp examples are ported to `examples/julia/*_ex.jl`, each with a `# CMD: just TARGET=<stem> run-julia` header, and `just run-all-julia` passes 31/31. Static methods take the type (`zeros(Tensor, [2, 3])`), and native indices stay 0-based while returned vectors are 1-based. Julia skips the same 17 unsupported declarations as Lisp (initializer lists, `Figure`).
- **mep:** `mep-julia-lsp` resolves `using X` through `JULIA_LOAD_PATH`, environments and Manifest dev paths (`src/julia_lsp_packages.cpp`). It gives hover, completion, signatures and definition into the package's docstrings without loading Julia, at about 0.05 s per hover on the 42k-line `Datamunge.jl`. The Julia UI mode (`src/language_ui_julia.h`, `help/julia-mode.org`) runs the stock REPL with a `MepUI` init script. Variables, Modules, Inspect and Help panels are refreshed from a prompt function (through `invokelatest`, for Julia 1.12+ world age). `<leader>re`/`<leader>rr` evaluate under the source file's name and line numbers. `gh` shows the docstring and methods. `just test-julia-ui` covers this.

### C# (2026-11-13)

- **rosetta:** the planning and the C++ library that `julia_full` used are now shared as `CabiGenerator` (`src/cabi_full.{hpp,cpp}`); Julia's output is unchanged. `--csharp-api full` (`src/csharp_full.cpp`, runtime in `src/csharp_full_runtime_cs.hpp`) derives from it. Classes become `NativeObject` subclasses with C# inheritance, and virtual methods can be overridden by C# subclasses (dispatched back by reflection). Names are PascalCase, fields become properties, overloads cover default arguments, operators become C# operators or indexers, and enums are `enum : long`. Containers map to `IEnumerable`/`List`/`Dictionary`/tuples, callbacks to `Func`/`Action`, and errors to `NativeException`. Doxygen becomes XML doc comments, and `--csharp-project` writes a .NET 10 `.csproj`. The runtime's internals live in `<Namespace>.RosettaRuntime`, and .NET type names are qualified wherever a generated type hides them (datamunge has its own `Complex`). Policy conversions are typed from their target (`datamunge::Vector<double>` is `IEnumerable<double>`/`List<double>`), and a converted class is no longer also bound as a class (this applies to Julia too). Default values print as the shortest round-tripping number. `tests/TestCSharpFull.cs` runs 287 checks and `tests/TestCSharpNames.cs` 13 (ctest `csharp_full_features`, `csharp_full_names`).
- **datamunge:** `src/datamungecsharp` (16 parts, namespace `Datamunge`, native library `datamunge_csharp`) replaces the SWIG `datamungedotnet` and its NuGet lock. Examples and tests are .NET 10 file-based apps that `#:project ../../src/datamungecsharp/Datamunge.csproj`. The dev shells set `DATAMUNGE_CSHARP_LIBRARY`, so `dotnet run file.cs` works. `just test-csharp` runs 16 checks, and the nix check runs them in the sandbox. All 31 examples are ported, each with a `// CMD: just TARGET=<stem> run-csharp` header, and `just run-all-csharp` passes. The nix package `datamungecsharp` also builds a `Datamunge` NuGet package, which the `report-csharp` shell uses (`#:package Datamunge@0.0.1`). C# skips the same 17 declarations as Julia.
- **mep:** the C# LSP shows the whole XML doc comment as Markdown in hover, completion and signature help: summary, remarks, paragraphs, code blocks, parameters, returns and exceptions. Identical declarations from a copied file (a generated binding and its installed copy) count once. The C# console honours `#:project`: it builds the project once per session and references the assembly with its XML docs, so `<leader>rr`, `<leader>re` and `gh` work on datamunge examples. `just test-csharp-ui` (`tools/csharp_ui_check.py`) covers this.
- **Open:** pointer returns are nullable even for factories that never return null (callers write `!`). `size_t` is `ulong`, which needs casts for `List` indices. Pure-virtual C++ methods are `virtual`, not `abstract`, in C#, so a missing override fails at run time.

### Catch-up: JavaScript UI mode (2026-11-13)
- mep `src/language_ui_javascript.h`: a Node console with an init script. It runs the file or a single statement, and the Help tab renders JSDoc from `index.d.ts` (`mep-js-lsp --dts-doc`). Docs are in `help/javascript-mode.org`, and the check is `tools/javascript_ui_check.py` (6 tests, `just test-javascript-ui`).

### Catch-up: COBOL formatter and grammar (2026-11-13)
- mep `src/cobol_format.cpp` re-indents only, in fixed and free format: Area A headers, data nesting, PIC alignment, and scope indentation in the procedure division. It refuses any change that would alter code. It's wired to `gf` (`mep.format_cobol`) and to LSP formatting/rangeFormatting. Tests: 220 format checks and 115 LSP checks. The datamunge examples are all formatted, idempotent and accepted by cobc. On NIST COBOL85, cobc diagnostics are unchanged for 381/382 files (the exception is OBNC1M).
- `grammars/tree-sitter-cobol` is vendored from yutaro-sakamoto/tree-sitter-cobol (MIT) with a fixed/free-format scanner. It replaces the hand-written highlighter. 91 corpus tests pass, and the datamunge files parse with 0 errors.
- Open: folds stop at sections, and EXEC SQL is not parsed.

### Fortran (2026-11-13)
- rosetta `--fortran-api full` (`src/fortran_full.cpp`, `src/fortran_full_runtime_f90.hpp`) builds on the shared `CabiGenerator`:
  - classes become derived types with `extends`, type-bound generics, constructor generics, refcounted `assignment(=)` plus `final`, operators, callbacks through abstract interfaces, directors, `status=` errors and FORD `!>` docs.
  - Tests: `just fortran-test` 27/27 (features 290 checks, names 15). Generated code compiles with gfortran `-std=f2018 -Wall -Wextra -Werror`.
  - Two fixes came from datamunge: callback interfaces are now emitted after the types they import, and flushes are skipped inside functions to avoid recursive I/O when a call appears in `print`.
- datamunge `src/datamungefortran`: a shared C ABI library, a module library, and the committed `datamunge.f90` (~102k lines).
  - Recipes: prebuild, build, run, run-all and test-fortran. Results: 16 checks, 31/31 examples with `! CMD: just TARGET=x run-fortran`, and `checks.fortran` passes in nix.
  - `localBindingsHook` exports `DATAMUNGE_FORTRAN_FLAGS` and `MEP_FORTRAN_PATH`.
- mep:
  - `mep-fortran-lsp` resolves `use` across files through the workspace, `MEP_FORTRAN_PATH` and `FFLAGS -I`. It renders FORD docs as Markdown in hover, completion and signature help (151 checks; hovers under 1 ms on a 70k-line module). Hover on datamunge procedures shows their docs.
  - UI mode in `src/language_ui_fortran.h`: the console recompiles and reruns, and Help uses `--intrinsic` (`tools/fortran_ui_check.py`, 5 tests).
- Open:
  - gfortran crashes on elemental finalization, so arrays of objects leak until closed.
  - Lints don't know other files' modules.
  - Rename and references stay within one file.

### mep groundwork for the remaining languages (2026-11-14)
- Ruby: vendored tree-sitter-ruby, plus mep's own `mep-ruby-lsp` (141 checks), which reads YARD from `.rb` files on the load path, and a formatter (211 checks). The UI mode is irb with panels. solargraph is no longer the default.
- OCaml: vendored tree-sitter-ocaml (`.ml`/`.mli`), plus mep's own `mep-ocaml-lsp` (217 checks), which renders odoc from `.mli`, and an ocamlformat-style formatter (428 checks). The UI mode is utop or ocaml. ocamllsp is no longer the default.
- APL: a tree-sitter grammar written from scratch, plus `mep-apl-lsp` (179 checks) and a formatter (81 checks). The Run defaults are `apl --script` and Dyalog when present. The UI mode is a GNU APL console with a glyph bar.
- UI modes for Octave, Tcl, Guile, Java and Go. Each server now reads doc comments from other files (`--doc` CLI): `.m` help text, comments before `proc`, docstrings, Javadoc on the source path, and godoc through go.mod and the module cache.
- Open: none of these modes has been tried in the GUI (the dev-shell glibc issue). `mep-treesitter-test` stops at a pre-existing D failure.

### Ruby datamunge (2026-11-14)
- rosetta fixes from datamunge (`src/ruby_full.cpp`, `src/ruby_full_runtime.hpp`; `just ruby-test` 28/28):
  - The bound header is now included before `ruby.h`. Before, ruby.h's function-like macros such as `StringValue` rewrote declarations in datamunge's `vector.hpp`. Because of this, `std::snprintf` in the runtime became `snprintf` (ruby.h maps it to `ruby_snprintf`).
  - `to_string` also becomes `to_s` when all of its parameters have defaults, as in `to_string(max_rows = 10)`.
  - `guard()` flushes Ruby's `$stdout` before every native call and C++ `std::cout`/`stdout` after it, so `print_summary` output stays in order when output is piped.
  - New tests in `tests/ruby_full_names.hpp` and `tests/test_ruby_full_names.rb`.
- datamunge `src/datamungeruby` (16 parts, module `Datamunge`) replaces the SWIG `octruby`:
  - It holds the committed `lib/datamunge.rb` (~18k lines of YARD stubs) and a CMake MODULE build to `lib/datamunge/datamunge.so`.
  - Recipes: prebuild, build, run, run-all, repl and test-ruby. `just test-ruby` runs 16 checks, and `just run-all-ruby` passes 31/31 examples with `# CMD: just TARGET=x run-ruby`. `checks.ruby` passes in nix.
  - Nix: `nix/datamungeruby.nix`, `devShells.ruby` and `report-ruby`. `localBindingsHook` puts `build/ruby/prefix/lib` and `src/datamungeruby/lib` on `RUBYLIB`.
  - Ruby skips the same 17 declarations as C#.
- mep: `mep-ruby-lsp --doc` resolves `require "datamunge"` through `RUBYLIB` and renders the YARD docs of classes, methods and module functions.
- Open:
  - A missing pure-virtual override raises `Datamunge::NotImplementedError`. Following Ruby, that is a `ScriptError`, so a bare `rescue` doesn't catch it; `rescue Datamunge::Error` does.
  - Options structs have no keyword constructor.
- mep: an LSP hover over stdio on `Datamunge::DataFrame` in an example (with RUBYLIB from the hook) returns the YARD class doc as Markdown, so the mep docs column is ✅.

### Octave (2026-11-14)
- rosetta `--octave-api full` (`src/octave_full.cpp`, `src/octave_full_runtime.hpp`) runs on the shared C ABI layer and produces one `__M__.oct` plus a `+M` package of classdef/function `.m` files. Their plain-text help comes from Doxygen, so `help M.Class.method` works.
  - `just octave-test`: 27/27 (302 feature checks, 29 help checks, 54 name checks).
- datamunge `src/datamungeoctave` replaces SWIG:
  - It contains the committed `inst/+datamunge` (517 `.m` files plus `+Data`) and 16 wrap parts. `prebuild_octave` syncs the package and drops stale `.m` files.
  - Results: `just test-octave` 16/16, `just run-all-octave` 31/31 (`% CMD: just TARGET=x run-octave`), and `checks.octave` passes.
  - The hook sets `MEP_OCTAVE_PATH`, plus a site init file that silences Octave's spurious `shadowed-function` warnings.
- mep: `mep-octave-lsp` resolves package functions, classdef class and method help, bare names through `import`, and variable types from assignments and the `Output:` help blocks. Hover on `sales.select(...)` in an example shows the method help (127 checks).
- Open:
  - `doc` doesn't show package help.
  - Not supported: `obj(i) = v` through operator[], and object arrays.

### Tcl datamunge (2026-11-14)
- rosetta fix from datamunge (`src/tcl_full_runtime.hpp`, `src/tcl_full.cpp`; `just tcl-test` green):
  - Tcl and C++ buffer stdout separately, so with piped output `print_summary` text came after later `puts` lines. `guard()` now flushes Tcl's stdout channel before each native call and C++ `std::cout`/`stdout` after it (as Ruby's does).
  - New check in `tests/test_tcl_full_names.tcl` (a piped child interleaving `puts` with a native `say`).
- datamunge `src/datamungetcl` (16 parts, package `datamunge` 0.0.1, namespace `::datamunge`) replaces the SWIG `Datamunge` package:
  - It holds the committed package files `pkg/datamunge/{datamunge.tcl,pkgIndex.tcl}` (~26k lines of documented stubs) and a CMake MODULE build (`USE_TCL_STUBS`, stub library, Tcl found through tclsh) into the package directory `datamunge/`.
  - Recipes: prebuild, build, run, run-all, repl (`src/datamungetcl/repl.tcl`) and test-tcl. `just test-tcl` runs 16 checks, and `just run-all-tcl` passes 31/31 examples with `# CMD: just TARGET=x run-tcl`. `checks.tcl` passes in nix.
  - Nix: `nix/datamungetcl.nix` (`$out/lib/datamunge`), `devShells.tcl` and `report-tcl`. `localBindingsHook` puts `build/tcl/prefix/lib` on `TCLLIBPATH` and `src/datamungetcl/pkg` on `MEP_TCL_PATH`.
  - Tcl skips the same 17 declarations as Ruby.
- mep: the Tcl server's cross-file lookup now covers TclOO classes and methods. `mep-tcl-lsp --doc "DataFrame sort_by"` (or `datamunge::DataFrame sort_by`) prints a method's docs, `--doc DataFrame` the class's. Hover on `$df sort_by` shows the method when the file sets `df` from `[CLASS new ...]`; inherited methods resolve through `superclass`, and an untracked object lists every class's method of that name (97 checks).
- Open:
  - The extension is built against nixpkgs' Tcl 8.6.16, so it does not load into Tcl 9 (a Tcl 9 build needs Tcl 9 headers).
  - Signature help and completion on `$obj method` are not class-aware yet.

### Java rosetta (2026-11-14)
- rosetta `--java-api full` (`src/java_full.cpp`, `src/java_full_runtime_java.hpp`) is a `CabiGenerator` target with JNI glue, writing one `.java` file per class under the package directory:
  - Classes extend `NativeObject` (`AutoCloseable` plus a `Cleaner`), with directors, overloads, enums, containers, records, callbacks, operators as methods, and `NativeError` exceptions. The library loads from `-DMODULE.library`, `MODULE_JAVA_LIBRARY` or `System.loadLibrary`.
  - Javadoc passes `javadoc -Xdoclint:all -Werror`; mep's Java server reads it with `MEP_JAVA_SOURCEPATH` set to the source root.
  - Tests: `just java-test` 27/27 (features 299 checks, names 31, plus two doclint tests).
- Next: datamunge `src/datamungejava` to replace the SWIG bindings.

### Java datamunge (2026-11-14)
- rosetta: no generator change was needed. Piped `System.out` and C++ `std::cout`/`printf` output already come out in order (the runtime flushes `System.out` before each native call and the JVM's C stdout is unbuffered); `TestJavaNames` now checks it in a piped child (`say`, `say_around` in `tests/java_full_names.hpp`).
- datamunge `src/datamungejava` (16 parts, package `datamunge`, JNI library `libdatamunge_java`, policy module `data` as `datamunge.data.Data`) replaces the SWIG `jdatamunge` and its Gradle build:
  - It holds the committed sources `java/datamunge/**.java` (298 files with Javadoc; `prebuild_java` syncs them and drops stale files) and a CMake build of the MODULE library plus `datamunge.jar` (`add_jar`, `--release 17`).
  - Recipes: prebuild, build, run, run-all, repl (jshell with `src/datamungejava/repl.jsh`) and test-java. `just test-java` runs 16 checks, and `just run-all-java` passes 31/31 single-file examples `examples/java/<Pascal>.java` with `// CMD: just TARGET=x run-java`. `checks.java` passes in nix.
  - Nix: `nix/datamungejava.nix` (`passthru.jar`, `passthru.library`), `devShells.java` and `report-java`. `localBindingsHook` sets `DATAMUNGE_JAVA_LIBRARY`, `MEP_JAVA_CLASSPATH` (the built jar) and `MEP_JAVA_SOURCEPATH` (the committed sources).
  - Java skips the same 17 declarations as Ruby and Tcl.
- mep: `mep-java-lsp` hover now types receivers from the text: `var x = new T()` / `var x = Data.iris()`, chained calls through the return types in the Javadoc'd declarations (`df.select(...).sortBy`, chains continued over lines), inherited members through `extends` (`RPlot.saveSvg` is `Plot.saveSvg`), and bare type names through imports (80 checks). Hover on `sales.addStringColumn` and `cleaned.select(...).sortBy` in `examples/java/DataframeEx.java` shows the method's Javadoc.
- Open: options structs have getters/setters but no builder; examples that simulate data use `java.util.Random`, so those sections differ from other languages.

### Guile rosetta (2026-11-14)
- rosetta `--guile-api full` (`src/guile_full.cpp`, `src/guile_full_runtime.hpp`, name lists in `src/guile_full_names.hpp`) emits a libguile extension plus the module file `M.scm` (and `M/sub.scm` per policy module):
  - Classes are GOOPS classes under `<native-object>`; methods and field accessors are generics (`(area c)`, `(set! (x p) v)`), Scheme subclasses override virtuals with `define-method` (`next-method` runs C++), enums are symbols, containers lists/alists/bytevectors, C++ exceptions throw `M-invalid-argument` and the other keys, `close!` plus finalizers.
  - Names are kebab-case with `?`/`!`; syntax and GOOPS names get a trailing `_`, other core names (`list`, `length`, `apply`) are `#:replace`d and fall back to Guile's procedure.
  - Every public procedure is a `define`/`define*` with a docstring (summary, Arguments, Returns, Raises, C++), which `procedure-documentation` returns too (generics are documented applicable structs).
  - Tests: `just guile-test` 27/27 (features 457 checks, also auto-compiled; names 95; docs 647).
- Consumer recipe: `rosetta datamunge.hpp --language guile --guile-api full --module datamunge --guile-extension libdatamunge_guile --parts 16 -o gen/datamunge.cpp --guile-output scheme/datamunge.scm`, build `gen/datamunge*.cpp` as a MODULE library `scheme/libdatamunge_guile.so`, and put `scheme/` on `GUILE_LOAD_PATH` (or copy it into `%site-dir`).
- Next: datamunge `src/datamungeguile` to replace the SWIG binding.

### Guile datamunge (2026-11-14)
- datamunge `src/datamungeguile` replaces SWIG:
  - It contains the committed `scheme/datamunge.scm` (~40k lines) and `scheme/datamunge/data.scm`, plus 16 wrap parts. The policy renames the enum `optim::Acquisition` to `acquisition-kind` to avoid an accessor clash.
  - Results: `just test-guile` 17/17 (including `procedure-documentation`), `just run-all-guile` 31/31 (`;; CMD: just TARGET=x run-guile`), and `checks.guile` passes.
  - The hook sets `GUILE_LOAD_PATH` and `MEP_GUILE_PATH`.
- mep: `mep-scheme-lsp` handles GOOPS heads with a `goops:` prefix. It takes `define-class` and `define-generic` docs from the `;` comments above them (126 checks). Hover on `<lm>` and `print-summary` in examples shows the docs.
- Open:
  - ~~The first auto-compile of the 40k-line module takes about 38 s and prints shadowed-definition warnings.~~ Fixed (2026-11-14):
    - Every generic was defined twice: the documented `define` and then the `<documented-generic>`. The second binding is now a `set!`, so `guild compile -W shadowed-toplevel` is silent.
    - `rosetta_compile_guile` precompiles the module at build time; in rosetta, `guile_full_compiled` runs against those `.go` files, with warnings as errors.
    - In datamunge, `src/datamungeguile/CMakeLists.txt` compiles `datamunge.go` and `datamunge/data.go` (about 45 s) and installs them into `lib/guile/3.0/site-ccache`. The hook, the justfile's `GUILE` and the nix checks put that directory on `GUILE_LOAD_COMPILED_PATH`.
    - `(use-modules (datamunge))` now takes about 0.25 s, against about 1 s interpreted and about 58 s auto-compiling into an empty cache.
  - Generics shared by several classes get run-together summaries.
  - Importing all of SRFI-1 clashes on `count`.

### Go rosetta (2026-11-14)
- rosetta `--go-api full` (`src/go_full.cpp`, `src/go_full_runtime.hpp`) emits the C++ of a shared library plus a Go module: `go.mod`, package `datamunge` (`--go-package`, module path `--go-module-path`, default the package), one subpackage per policy module (`datamunge/data`) and `internal/rosetta`:
  - Classes are structs with a handle, derived classes embed their bases, `XLike` interfaces take polymorphic arguments, `NewX` constructors, `NewXFrom(impl)` directors, `Close` plus `runtime.AddCleanup`.
  - Calls return `(T, error)` with `*Error{Kind, Message, Err}` (`errors.As`) and `Must`; overloads and defaults are deterministic named variants (`Add`, `AddWithB`; `NewVec2XY`, `PickFloat64`).
  - Doxygen becomes gofmt-canonical godoc (name-first sentences, `# Headings`, code blocks, lists, `[Name]` links); `go vet` is clean and `go doc` reads it.
  - Tests: `just go-test` (features 316 checks, names 52, each also under `-race`; `go doc`, `go vet`, `gofmt`), offline with `GOPROXY=off` and caches in the build directory.
- Consumer recipe: `rosetta datamunge.hpp --language go --go-api full --module datamunge --policy … --parts 16 -o gen/datamunge.cpp --go-output go/datamunge/datamunge.go`, build `gen/datamunge*.cpp` as the SHARED library `libdatamunge.so`, and in the consumer's go.mod `require datamunge v0.0.0` plus `replace datamunge => ../go/datamunge`; build and run with `CGO_LDFLAGS="-L$LIB -Wl,-rpath,$LIB"` (or `LD_LIBRARY_PATH`) and `GOFLAGS=-mod=mod`.
- Next: datamunge `src/datamungego` to replace the SWIG `godatamunge`.

### Go datamunge (2026-11-14)
- rosetta fix from datamunge (`src/go_full.cpp`; `just _go-test`, `_csharp-test` and `_java-test` green):
  - Only the shortest director constructor took `impl`: `NewXFromWithB(b)` and `NewXFromOptions(o)` (defaults, overloads) passed a nil implementation. Every `NewXFrom*` variant now takes `impl any` first.
  - New check in `tests/test_go_full_names.go` (a `Gauge` with defaulted and overloaded constructors in `tests/go_full_names.hpp`; names 58 checks).
  - Output order needed no fix: the shared C ABI layer flushes `std::cout`/`stdout` after each call and Go's `os.Stdout` is unbuffered, so piped output stays in order.
- datamunge `src/datamungego` (16 parts, module and package `datamunge`, shared library `libdatamunge_go`, policy module `data` as `datamunge/data`) replaces the SWIG `godatamunge`/`godatamunge_legacy`:
  - It holds the committed module `go/datamunge` (go.mod, the 54k-line documented `datamunge.go`, `data/`, `internal/rosetta`; `prebuild_go` syncs it and drops stale files) and a CMake build of the SHARED library, installed with the module into `build/go/prefix`.
  - `examples/go` and `tests/go` are modules whose go.mod `replace`s `datamunge` with the committed module; the examples are `//go:build ignore` main programs.
  - Recipes: prebuild, build, run, run-all and test-go (no repl-go: Go has no REPL). `just test-go` runs 17 checks (`go test`), and `just run-all-go` passes 31/31 examples with `// CMD: just TARGET=x run-go`. `checks.go` passes in nix.
  - Nix: `nix/datamungego.nix` (with a warm Go build cache for `report-go`), `devShells.go` and `report-go`. `localBindingsHook` sets `DATAMUNGE_GO_LIB`, `CGO_LDFLAGS` (with an rpath) and `GOWORK` (`build/go/go.work`: the module, `examples/go` and `tests/go`), so `go run examples/go/x.go` works from the root and a Go file anywhere can import `datamunge`.
  - Go skips the same 17 declarations as Java.
- mep: `mep-go-lsp` hover now types receivers from the text: `x, err := pkg.F()`, `x := pkg.Must(...)`, `var x *pkg.T`, parameters, call chains (`datamunge.Must(df.Select(cols)).SortBy`, calls continued over lines) through the documented first results, and methods promoted from embedded types (`RPlot` → `Plot.SaveSvg`). Parsed packages are cached until their files change (106 checks). Hover on `cleaned.Select` in `examples/go/dataframe_ex.go` and `scatter.SaveSvg` in `examples/go/rplot_ex.go` shows the godoc.
- Open:
  - Overloads that differ by more than defaults spell every parameter (`NewMDSDataEncodedFeatureColumnsNComponentsMetric`, `NewRGBRedGreenBlueOpacity` for the 3-int form).
  - Simulated data in `timeseries_ex`, `glmm_ex` and `lmm_ex` comes from Go's `math/rand` or datamunge's RNG, so those sections differ from other languages.

### OCaml rosetta (2026-11-14)
- rosetta `--ocaml-api full` (`src/ocaml_full.cpp`, `src/ocaml_full_runtime.hpp`) emits the shared C ABI library with a small OCaml C stub appended, plus `MODULE.ml` (runtime module `Rosetta__` and wrappers), `MODULE.mli` (odoc) and a findlib `META` beside `--ocaml-output`.
  - Classes are submodules over phantom-typed objects (`type circle = [ `Circle | `Shape ] obj`; functions take `[> `Shape ] obj`, `Circle.cast` checks), overloads are type-suffixed functions (`pick_float`), defaults optional labelled arguments, directors `Shape.extend ~area:(fun self -> ...)` with `Shape.Base.area`, enums variant modules, containers arrays/lists/association lists/tuples/options, `ref` cells for mutable references, closures both ways.
  - C++ exceptions raise `Invalid_argument`, `Failure`, `Out_of_memory` or `Datamunge.Error (kind, msg)`; OCaml exceptions in callbacks come back unchanged.
  - Tests: `just ocaml-test` (features 366 checks native under GC stress and bytecode, names 57, odoc `--warn-error`, a findlib consumer build).
- Consumer recipe: `rosetta datamunge.hpp --language ocaml --ocaml-api full --module datamunge --policy … --parts 16 -o gen/datamunge.cpp --ocaml-output ocaml/datamunge/datamunge.ml`, build `gen/datamunge*.cpp` (with `-I $(ocamlc -where)`) into `ocaml/datamunge/dlldatamunge_stubs.so` and `libdatamunge_stubs.a`, then compile `datamunge.mli` (`-bin-annot`), `datamunge.cma` (`-dllib -ldatamunge_stubs`) and `datamunge.cmxa` (`-cclib -ldatamunge_stubs -cclib -lstdc++`) there; programs also link `-ldatamunge`. With `ocaml/` on `OCAMLPATH`, `ocamlfind ocamlopt -package datamunge` and dune `(libraries datamunge)` work, and mep reads `datamunge.mli` through findlib. On the Java policy (`src/datamungejava/rosetta`) it skips the same 17 declarations as the other C ABI backends; the 16 parts compile, odoc accepts the 35k-line `.mli`, and a `Polynomial` smoke test runs against `libdatamunge.so`.
- Next: datamunge `src/datamungeocaml` (rosetta) to replace the SWIG `datamungeocaml`.

### OCaml datamunge (2026-11-14)
- rosetta fixes from datamunge (`src/ocaml_full_runtime.hpp`, `src/ocaml_full.cpp`; `just _ocaml-test` green):
  - OCaml and C++ buffer stdout separately, so piped `print_string` text came after later C++ output. The runtime now flushes OCaml's stdout/stderr before each native call and when a callback returns to C++, and C++ `std::cout`/`stdout` after each call and before each callback (as Ruby's and Tcl's do). New check in `tests/test_ocaml_full_names.ml` (a child with output to a file interleaving `print_string`, `say_buffered` and `say_around`).
  - The `.mli` header claimed `[x |> M.f a]` works; the object is the first positional argument, so it now says `[M.f x a]`, and `[x |> M.f]` for functions of the object alone.
- datamunge `src/datamungeocaml` (16 parts, findlib package `datamunge`, policy module `data` as `Datamunge.Data`) replaces the SWIG `datamungeocaml` (dune, opam, swig.ml):
  - It holds the committed package `ocaml/datamunge/{datamunge.ml,datamunge.mli,META}` (the `.mli` ~35k lines of odoc) and a CMake build of `dlldatamunge_stubs.so`, `libdatamunge_stubs.a`, `datamunge.cma` and `datamunge.cmxa` (which records `-ldatamunge` with an rpath) into the package directory.
  - Recipes: prebuild, build, run, run-all, repl (utop with `src/datamungeocaml/repl.ml`; the plain toplevel when stdin is not a terminal) and test-ocaml. `just test-ocaml` runs 16 checks, and `just run-all-ocaml` passes 31/31 examples with `(* CMD: just TARGET=x run-ocaml *)`, compiled natively through `ocamlfind -package datamunge`. `checks.ocaml` passes in nix.
  - Nix: `nix/datamungeocaml.nix` (`$out/lib/datamunge`), `devShells.ocaml` (ocaml, findlib, odoc, dune, utop) and `report-ocaml`. `localBindingsHook` puts `build/ocaml/prefix/lib` on `OCAMLPATH`, the package directory on `CAML_LD_LIBRARY_PATH` and `src/datamungeocaml/ocaml` on `MEP_OCAML_PATH`.
  - OCaml skips the same 17 declarations as Java, Tcl and Ruby.
- mep: no change needed. `mep-ocaml-lsp --doc` shows `Datamunge.seql`, `Datamunge.DataFrame` and `Datamunge.DataFrame.select`; hover works on `DataFrame.select df ...`, `DataFrame.sort_by` after `|> fun df ->`, `Data.iris () |> DataFrame.nrows` and module names in `examples/ocaml` (0.2-0.6 s per request including server start; first lookups in the datamunge checkout can take seconds on a cold disk cache while the workspace is indexed).
- Open: floats print with `%g` (`6` where Tcl and Java print `6.0`); glmm_ex, lmm_ex and timeseries_ex simulate with their own generators, as the Tcl and Java ports do, so those sections differ between languages.

### datamunge: Visual Basic through Haskell (2026-11-22)
- datamunge `src/datamunge<lang>` (rosetta, 16 parts each; the C# `bindings.hpp`/`policy.json` with the language's bindings namespace) for Visual Basic, Rust, PHP, Perl, Swift, Pascal, Dart, Ada, Objective-C, Prolog and Haskell. The SWIG `rustdatamunge`, `datamungePHP` and `perldatamunge` are removed. Each has prebuild/build/test/run/run-all recipes (repl for Rust, PHP, Perl, Prolog and Haskell), a dev shell, a nix package and a check, a smoke test in `tests/<lang>` (13-16 checks: containers, pairs, a director overriding `Callback::call`, a callback's own exception coming back, C++ exception kinds, `DataFrame`, the `data` namespace), and the 31 examples with `CMD:` headers. All 31 run in every language; 29 print exactly the C# output (with csharp→lang). The two that differ do so in every language: `hypothesis_tests_ex` (the C# reference links an older `libdatamunge.so`, last digits of tiny p-values) and `optim_advanced_ex` (C#'s default `ToString()` text of classes without `to_string`).
- Packaging: Swift uses a second nixpkgs pin (`nixpkgs-swift`, no cached Swift at datamunge's pin) and swiftc without SwiftPM; Pascal installs the compiled unit, Ada the compiled package (read-only `.ali`), Objective-C `libDM` (clang, ARC, GNUstep 2.0), Prolog `prolog/datamunge.pl` beside `../lib`, Haskell a GHC package built with Cabal's `Setup` and registered in `lib/haskell/package.db`. PHP needs FFI: the shells now use nixpkgs PHP 8.4 with `ffi` instead of php-from-source.
- rosetta fixes found by datamunge (each with a regression test; suites green):
  - Rust: overload names use the required parameters' names (`new_x_y_knots`), and a keyword escape is dropped before a suffix (`box_with_color`, not `box__with_color`).
  - PHP: merged overloads with the same result declared it twice (`void` methods returned a value); nullable callables are parenthesized in PHPDoc (`(callable(int): (int|null))|null`).
  - Perl and Pascal: the language's buffered stdout is flushed before each native call and after each callback (Perl `$|`, Pascal `Output`), so piped output keeps its order with C++ (`say_around` checks).
  - Dart: nullable function types were `(F Function(...))?`, which Dart reads as a record; now `F Function(...)?`.
  - Ada: a class named like a runtime operation (`Image`) or a `Native_Error_Kind` literal (`Callback`) now gets `_Access`.
  - Prolog: `'$rosetta_member'/2` was defined after the directive that loads the library, so loading without `MODULE_PROLOG_LIBRARY` failed; new `prolog_*_beside` ctests load the library from beside the module.
  - Dart, Objective-C, Pascal, Ada: a `to_string` whose parameters all have defaults (`DataFrame::to_string(max_rows = 10)`) is now the native conversion (`toString([int? maxRows])`, `-description`, `ToString` override plus overload, `Image` override plus overload), as Swift and Rust already did.
  - All C ABI backends: C++ output is flushed when a call throws too (`lisp_full_runtime.hpp`).
- Open:
  - Ownership: factory methods returning raw pointers (`[[nodiscard]] DataFrame* select(...) const`, `Tensor* reshape(...)`, `data::iris()`, ...) are wrapped as borrowed from the receiver in every language: the result dies with its receiver and its C++ object is never deleted (Ada measured 200 x `reshape` of 1M elements: 54 MB → 1.6 GB). The policies list only `task_batch_create` as `owned`; these need `owned` entries (or a rule for `[[nodiscard]] T*` factories).
  - Classes without `to_string` print only their name; Rosetta could give them the C++ class name.
