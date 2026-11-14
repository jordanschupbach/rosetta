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
| COBOL | ✅ (copybook `*>>`) | ✅ | ✅ | ☐ | ☐ | ✅ | ✅ | ✅ |
| JavaScript | ✅ (`index.d.ts` JSDoc) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ☐ |
| Julia | ✅ (Markdown docstrings) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (`julia`) | ✅ |
| C# | ✅ (XML doc comments) | ✅ | ✅ | ✅ | ✅ (`c_sharp`) | ✅ | ✅ (`dotnet run`) | ✅ |
| Fortran | ☐ | ☐ (none) | ✅ | ✅ | ✅ | ☐ | ✅ (`gfortran`) | ☐ |
| Ruby | ☐ | ☐ (SWIG `octruby`) | ☐ (solargraph today) | ☐ | ☐ | ☐ | ✅ (`ruby`) | ☐ |
| Octave | ☐ | ☐ (SWIG) | ✅ | ✅ | ✅ (`matlab`) | ☐ | ✅ (`octave-cli`) | ☐ |
| Tcl | ☐ | ☐ (SWIG) | ✅ | ✅ | ✅ | ☐ | ✅ (`tclsh`) | ☐ |
| Guile Scheme | ☐ | ☐ (SWIG) | ✅ | ✅ | ✅ | ☐ | ✅ (`guile`) | ☐ |
| Java | ☐ | ☐ (SWIG) | ✅ | ✅ | ✅ | ☐ | ✅ (`java`) | ☐ |
| Go | ☐ | ☐ (SWIG) | ✅ | ✅ | ✅ | ☐ | ✅ (`go run`) | ☐ |
| OCaml | ☐ | ☐ (SWIG) | ☐ (ocamllsp today) | ☐ | ☐ | ☐ | ✅ (`ocaml`) | ☐ |
| APL (GNU, Dyalog) | ☐ | ☐ (none) | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |

"Run button ✅" above means only that mep has a default for the file extension. Each language still has to show that a datamunge example runs from its `CMD:` header.

## Order

The languages are grouped by the native mechanism they share, so that shared work is done once.

1. **Julia** (C ABI, `ccall`). Lisp's full backend just established a full C ABI layer, and Julia has the same shape. Julia docstrings map directly onto Doxygen, and mep's Julia server already shows the docstrings of definitions it can see.
2. **C#** (C ABI, P/Invoke). This is the second consumer of the C ABI shape: factor out the shared C ABI planning from `lisp_full`/`julia_full` at this point. Documentation becomes XML doc comments. mep's C# server and UI mode already exist.
3. **Fortran** (C ABI, `ISO_C_BINDING`). The third C ABI consumer. Documentation becomes FORD `!>` comments, which mep's Fortran server reads.
4. **Catch-up for finished languages.** JavaScript gets a UI mode: a Node console, Help from `index.d.ts`, and panels. COBOL gets a mep formatter (fixed and free format) and a tree-sitter grammar, which replaces the hand-written highlighting in `cobol_syntax.cpp`.
5. **Ruby** (native extension). Modeled on the full Python backend. Documentation becomes YARD in a generated `.rb` file. mep needs a tree-sitter grammar, then its own Ruby parser, language server (which reads the YARD stub) and formatter, replacing solargraph.
6. **Octave** (native `.oct`). Documentation becomes help text for `help` and `doc`; mep's Octave server reads the generated `.m` stubs.
7. **Tcl** (Tcl 9 stubs). Documentation goes in generated `.tcl` comments.
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
