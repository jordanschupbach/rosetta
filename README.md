# Rosetta

Rosetta generates source code that connects C++ libraries to other languages through their native extension or FFI APIs. Lua uses a Lua 5.4 C API module loaded with `require`; R uses a native `.Call` bridge with R6, S3 or S4 interfaces; Python and Ruby use native extensions with owned classes; Octave uses native `.oct` modules with owned objects.

Rosetta itself is implemented in C++17, built with CMake, and tested with C++, Lua, R, Python, Ruby and Octave. This is a working prototype. It parses real C++ with Clang, keeps declarations in a shared model, and emits native bindings. Its long-term goal is coverage of every C++ feature through a native mapping, an explicit adapter, a concrete instantiation, or an opaque handle. An unrestricted C++ program cannot automatically have an equivalent interface in every language; ownership, specialization, and runtime policies sometimes need user input.

The Lua feature-family demo is complete through generated bindings and explicit native/Lua adapters. See the [feature coverage plan](docs/lua-feature-plan.md) for each contract and the [completion audit](docs/lua-coverage-audit.md) for implementation, test evidence and remaining product work.

## Try it

Prerequisites: a C++20 compiler for the demos (Rosetta's core remains C++17), Clang for parsing input, CMake 3.20+, Ninja, just, nlohmann/json 3.9+ with its CMake package, and Lua 5.4 with development headers. Set `-DROSETTA_BUILD_EXAMPLES=OFF` to build the core with C++17 without Lua. The compiler process adapter currently requires POSIX (Linux/macOS); Windows support is planned. The Nix flake supplies the development dependencies:

```sh
nix develop
just test
just demo
```

For an interactive Lua session, run this directly from the repository, including outside a Nix shell:

```sh
just lua-repl
```

This enters the flake's `lua-repl` shell, configures a separate `build/lua-repl` directory, builds Rosetta, the demo and the two interoperability modules, and starts Lua 5.4 with the library available as the global `demo`. You need Nix with flakes enabled and just on your host; the compiler, CMake, Ninja and Lua come from Nix. Subsequent invocations rebuild changed sources before starting the REPL.

```lua
demo.demo__add(20, 22)       -- 42
demo.demo__greet("Lua")      -- "Hello, Lua!"
require("demo") == demo     -- true
```

The session prepends the built module's directory and platform suffix to `package.cpath`, preserving Lua's existing search paths. Press Ctrl-D or call `os.exit()` to leave. To use an already configured build without Nix, run `cmake --build build --target lua-repl`.

`just test` configures and builds the generator, generates bindings, compiles them with the original C++ implementation, runs the C++ tests and CLI smoke test through CTest, and loads the shared library in Lua for behavioral checks. The toolchain was verified with Clang 19 and Lua 5.4.7 on Linux. Other toolchains need validation.

Run `just lua-test` for the Lua verification suite, including outside a Nix shell. It enters Nix, configures an isolated `build/lua-tests` directory with Clang, Lua examples and testing enabled and R runtime examples disabled, builds the native modules, and runs all CTests in that configuration. This includes the Lua feature, ownership/error, callback, concurrency, interoperability and REPL tests, plus shared compiler, CLI and dependency checks. Clang also enables the native named-module/header-unit and PCH build checks. Changed sources rebuild on subsequent invocations.

Development commands:

| Command | Action |
| --- | --- |
| `just` | List recipes |
| `just configure [CMAKE_ARGS...]` | Configure a Debug Ninja build |
| `just build` | Build the CLI, tests and Lua example |
| `just test` | Build and run all CTest tests |
| `just demo` | Build and run the Lua example |
| `just lua-test` | Enter Nix and run the Lua feature, native, CLI and shared compiler/build suite |
| `just r-demo` | Build and test the R6 example (requires R and R6) |
| `just r-test` | Enter Nix and run the R feature, capability, CLI and shared compiler/build suite |
| `just python-test` | Enter Nix and run Python runtime, capability, CLI and shared compiler/build tests |
| `just python-repl` | Enter Nix and open Python with demo/verification extensions loaded |
| `just ruby-test` | Enter Nix and run Ruby runtime, capability, CLI and shared compiler/build tests |
| `just ruby-repl` | Enter Nix and open IRB with demo/verification extensions loaded |
| `just octave-test` | Enter Nix and run Octave runtime, capability, CLI and shared compiler/build tests |
| `just octave-repl` | Enter Nix and open Octave with demo/verification modules loaded |
| `just r-repl` | Enter Nix and start R with R6, S3 and S4 demos and feature bindings loaded |
| `just lua-repl` | Enter Nix, build the demo bindings and start Lua with `demo` loaded |
| `just run [ARGS...]` | Build and invoke Rosetta; show usage when no arguments are given |
| `just clean` | Remove the build directory |

CMake also works directly, without just or Ninja:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For a generator-only build without Lua development dependencies, configure with `-DROSETTA_BUILD_EXAMPLES=OFF`. Tests still exercise the C++ frontend and backend. Use `-DBUILD_TESTING=OFF` to omit tests. Enable strict compiler checks with `-DROSETTA_WARNINGS_AS_ERRORS=ON`. `cmake --install build --prefix /your/prefix` installs the CLI and the CMake generation helper. The installed executable needs Clang available when generating bindings, and does not need Lua or JSON headers at runtime.

Set `-DCMAKE_CXX_COMPILER=clang++` to choose the compiler used to build Rosetta, and `-DROSETTA_CLANG_EXECUTABLE=/path/to/clang++` to choose the parser used by tests and the example. Set `-DROSETTA_LUA_EXECUTABLE=/path/to/lua5.4` when multiple Lua interpreters are installed. CMake validates the interpreter version. Use `-DCMAKE_PREFIX_PATH=/path/to/dependencies` if CMake cannot locate JSON or Lua. Pass these options through `just configure`; subsequent recipes preserve cached options. The CLI's `--clang` option selects its parser independently.

For your own library:

```sh
build/rosetta include/library.hpp --module library \
  -o build/library_bindings.cpp --dump-ir build/library.json \
  -- -std=c++20 -Iinclude -DMY_LIBRARY_OPTION=1

clang++ -std=c++20 -fPIC -shared $(pkg-config --cflags lua) \
  build/library_bindings.cpp src/library.cpp \
  $(pkg-config --libs lua) -o build/library.so
```

Use identical definitions, include paths, language standards, ABI options, and dependency settings when parsing and compiling. Link an existing library instead of `src/library.cpp` when appropriate. This manual linking example uses Linux flags and pkg-config for convenience; Rosetta's own build and example use CMake's Lua discovery. For another platform, use CMake module targets with that platform's linking/export settings. Choose a Lua 5.4 pkg-config package explicitly if the system's `lua` package is another version.

Compiler-discovered incremental dependencies are available with `--depfile build/api.d`. The installed `share/rosetta/cmake/Rosetta.cmake` helper exposes `rosetta_generate_lua(HEADER api.hpp MODULE api OUTPUT generated/api.cpp)` and wires dependency tracking into CMake. Both repository modules use it. See [dependency collection and CMake usage](docs/lua-feature-plan.md#compiler-dependencies-and-incremental-builds) for compilation-database options, input protection and format limits.

Modules accepting callbacks need host thread support (`Threads::Threads` in CMake, or the platform's thread flags such as `-pthread`). The demo links it automatically. Scalar-only modules omit the callback runtime.

For an existing CMake project, enable `CMAKE_EXPORT_COMPILE_COMMANDS` and import a translation unit's settings:

```sh
build/rosetta include/library.hpp --module library \
  --compile-commands project/build --translation-unit src/library.cpp \
  -o build/library_bindings.cpp --dump-ir build/library.json
```

`--compile-commands` accepts a directory or a JSON file. Translation-unit selectors are relative to your current directory; include paths and forced includes run relative to the selected command's working directory. The selected source supplies parsing context; Rosetta still exports declarations from the input header. If multiple records match, add `--command-index N` using the database's zero-based array index. With no selectors, the database must contain exactly one record. Extra flags after `--` apply last, so they can override the imported standard or definitions.

Rosetta uses the configured `--clang`, preserves common GCC/Clang parsing options, and removes source, compile/output and dependency-output arguments. It never executes the database's compiler or builds generated dependencies. `arguments` takes precedence over `command`; command strings support literal POSIX quoting without shell expansion or execution. Response files, compiler launchers and forwarded driver options are currently rejected; supply expanded arguments or manual `--` flags instead. Imported flags must be compatible with the selected Clang. The IR records the compiler path, working directory, semantic flags and selected database record for inspection.

## R and R6

For an interactive R session, run `just r-repl` directly from the repository, including outside a Nix shell. This enters the flake's `r-repl` shell, builds the R6 examples in a separate `build/r-repl` directory, and starts R with the `r_demo` and `r_verify` export lists loaded. Nix supplies R, R6 and the build dependencies. Changed sources rebuild on subsequent invocations. Try `r_demo$r_demo__add(20, 22)` or `counter <- r_demo$r_demo__Counter$new(20); counter$add(22); counter$close()`. Exit with `q()` or Ctrl-D. The session uses its own startup profile and skips saved workspaces and user/site environment files. With an already configured R-enabled build, `cmake --build build --target r-repl` starts the same session without entering Nix.

Run `just r-test` to verify R support. It enters Nix, builds an isolated `build/r-tests` configuration with R enabled and Lua runtime examples disabled, and runs all its CTests. This includes shared generator/CLI, compilation-database and dependency tests; capability/rejection fixtures across R6/S3/S4; scalar/encoding/error checks; each object system's dispatch, fields, aliasing and ownership/GC; native template, hierarchy, operator, worklist and async facades; and REPL startup/registration. `just r-demo` remains a short R6 Counter smoke test. The [R verification matrix](docs/roadmap.md#r-verification) maps all 21 roadmap families to evidence and open work. Passing rejection tests establish an explicit capability boundary; they do not establish support for the rejected feature.

The feature REPL exports include:

```r
r_verify$r_verify__maximum(20, 42)
series <- r_verify$r_verify__Series$new(20, 2)
series$text()           # "[20, 21]"; indices in this native facade start at zero
series$scaled_sum(2)    # 82
series$close()
task <- r_verify$r_verify__AsyncTask$new(21, 0, FALSE)
task$wait(2000)
task$result()           # 42
task$close()
```

Select R with `--language r` and its object system with `--object-system r6`, `s3` or `s4`. R6 is the default for R. Lua remains the CLI default. All three R object systems share an identical native bridge, conversions and ownership policy; the selection changes the generated R interface. Unimplemented systems such as `r5` or `s7` fail explicitly. R-specific options are rejected for Lua.

```sh
just run examples/r_demo.hpp --language r --object-system r6 \
  --module r_demo --policy examples/r_demo.policy.json \
  -o build/r_demo.cpp --r-output build/r_demo.R

R CMD SHLIB build/r_demo.cpp -o build/r_demo.so
```

The generated C++ requires C++17, R development headers, and the original library's implementation when it is not header-only. Set the R build toolchain's C++ standard to C++17 or newer. The extended native verification module uses C++20 and thread support to exercise the same native libraries as Lua. The Nix development shell supplies R and the R6 package. Only R6 interfaces require the R6 package; S3 uses base R, and S4 uses the standard `methods` package. Rosetta itself can generate any of these interfaces without R installed. For a compiled and tested CMake example, run `just r-demo` from the development shell. CMake can also enable it with `-DROSETTA_BUILD_R_EXAMPLES=ON`; use `ROSETTA_R_EXECUTABLE` and `ROSETTA_RSCRIPT_EXECUTABLE` to select matching R installations.

In R:

```r
dyn.load(file.path("build", paste0("r_demo", .Platform$dynlib.ext)))
source("build/r_demo.R")
r_demo$r_demo__add(20L, 22)  # 42
counter <- r_demo$r_demo__Counter$new(20)
counter$add(22)              # 42
counter$count <- 10          # active binding updates the native field
counter$value()              # 10
counter$close()
```

Generated exports live in a list named by `--module`, with C++ namespaces joined by `__`. The shared library's basename must match the module name so R calls its registration function. Load the library before sourcing the R interface, and keep it loaded while using any generated functions, classes or native objects. `--r-output` defaults to the C++ output path with its extension replaced by `.R`. Both outputs participate in input/output protection and `--depfile` tracking. Existing compilation-database, policy and IR options also apply to R.

Classes use the existing version-1 policy format, for example:

```json
{"version": 1, "classes": {"r_demo::Counter": {"ownership": "owned"}}}
```

The shared R backend supports free functions, one constructor per selected class, instance/static methods, and public scalar/string fields. R6 exposes fields through active bindings and static methods through instances. S3/S4 expose field accessors and class-level static functions as described below. Fields marked `const` are readonly. Public methods and fields can be restricted with the existing `methods` and `fields` policy lists; explicitly naming private members is an error. Supported values are booleans, ordinary C++ integer types, float/double, `std::string` and copied `const std::string&` inputs; void results become `NULL`. All parameters are required in R, including C++ parameters with defaults, and use positional names `arg1`, `arg2`, etc. R input strings are translated from their declared encoding to UTF-8; generated string results are marked UTF-8. Embedded NUL results are rejected by R rather than treated as R strings.

Conversions require length-one values and reject missing values, non-finite numbers, fractional integers, out-of-range integers and implicit string/logical-to-number coercion. Integers are returned as R numeric scalars and must lie within the exact range ±2^53. Native exceptions become R errors after C++ cleanup; R allocation errors unwind native temporaries through `R_UnwindProtect`.

Each R6 instance owns a tagged native external pointer with an R finalizer. `close()` releases it immediately and is idempotent; GC also releases unclosed objects. Methods reject closed and unserialized pointers. Calling `initialize()` on an existing owner is rejected. Cloning is disabled because copying an external pointer would share ownership. Native pointers cannot be saved and restored as live native objects across R sessions.

Automatic C++ overloads/inheritance, class-valued parameters/results, borrowed ownership, callbacks, container codecs, enums and other advanced Lua capabilities are not yet supported in R. Named native facades in `r_verify` demonstrate selected template instantiations, native hierarchy casts, series operators, worklist containers and callback-free async tasks through supported scalar/class interfaces in all three object systems; they do not add automatic mappings for those C++ types or R protocols. Unsupported declarations fail generation, or produce explicit warnings and are omitted with `--skip-unsupported`. Lifecycle names such as `close` and `initialize` are reserved.

### S3 and S4 classes

Generate an S3 interface with the same class policy:

```sh
just run examples/r_demo.hpp --language r --object-system s3 \
  --module r_demo --policy examples/r_demo.policy.json \
  -o build/r_demo.cpp --r-output build/r_demo_s3.R
```

Use `--object-system s4` and `--r-output build/r_demo_s4.R` for S4. Both interfaces can use the same compiled `r_demo` shared library. After loading it with `dyn.load()`, source the chosen interface:

```r
source("build/r_demo_s3.R")  # or build/r_demo_s4.R
Counter <- r_demo$r_demo__Counter
x <- Counter$new(20)
Counter$add(x, 22)          # 42: S3 UseMethod or S4 standardGeneric dispatch
Counter$set_count(x, 10)
Counter$get_count(x)        # 10
Counter$get_initial(x)      # readonly; set_initial() raises an error
Counter$answer()            # static C++ method, no receiver
Counter$close(x)
```

Each class descriptor contains `new`, its R `class` name, instance generics, field `get_NAME`/`set_NAME` generics, static functions and `close`. The same functions are also exported at module level, for example `r_demo$r_demo__Counter__add(x, 22)`. Generic and class identities include the module name, preventing collisions between independently loaded modules. Native method names colliding with generated accessors are diagnosed.

S3 instances are classed lists containing the native external pointer. S3 methods use `UseMethod` and standard method registration. S4 instances are formal classes defined with `methods::setClass`, with a typed `.ptr` external-pointer slot; methods use `setGeneric`/`setMethod`. Inspect them with `inherits(x, Counter$class)` for S3, or `methods::is(x, Counter$class)`/`isS4(x)` for S4. Use the descriptor's `new()` factory to construct native storage. Bare S4 `methods::new()` does not run a C++ constructor; calls on that uninitialized handle are rejected. The native pointer slot is implementation storage; native fields are accessed through the generated functions rather than copied into R slots.

These are wrappers with reference semantics. Assignment and ordinary R copies share the native pointer; mutation through one alias is visible through the others. `close()` is idempotent and invalidates every alias. The native finalizer releases storage after the last reachable pointer is collected. Serialized/restored pointers are rejected as unusable. R-level subclasses inherit dispatch; automatic C++ inheritance, downcasting and copying remain outside this initial mapping.

`just r-repl` loads S3 examples as `r_s3$demo` and `r_s3$verify`, and S4 examples as `r_s4$demo` and `r_s4$verify`, alongside the existing R6 `r_demo`/`r_verify` lists. For example:

```r
Counter <- r_s4$demo$r_demo__Counter
x <- Counter$new(20)
Counter$add(x, 22)
Counter$close(x)
```

The installed CMake helper also provides `rosetta_generate_r`; `OBJECT_SYSTEM` accepts `r6`, `s3` or `s4` and defaults to `r6`:

```cmake
rosetta_generate_r(HEADER api.hpp MODULE api OBJECT_SYSTEM r6
    OUTPUT generated/api.cpp R_OUTPUT generated/api.R POLICY api.policy.json)
```

It accepts the same compiler flags, compilation-database selectors and dependency options as `rosetta_generate_lua`. The caller owns compilation, R linkage and deployment of the two generated files.

## Python

Select Python with `--language python`:

```sh
just run examples/r_demo.hpp --language python --module python_demo \
  --policy examples/r_demo.policy.json -o build/python_demo.cpp \
  --dump-ir build/python_demo.json --depfile build/python_demo.d
```

This produces C++ source using CPython's C API. Compilation requires CPython 3.12+ development headers, and the extension must match the interpreter ABI. Generating source does not require Python installed. Compiler/database, policy, IR, dependency tracking and input/output protection options work as for the other backends; R-only options are rejected for Python.

Run `just python-test` to verify Python support through Nix in an isolated `build/python-tests` configuration, with Lua and R runtime examples disabled. The suite covers scalar boundaries, full signed/unsigned 64-bit integers, Unicode/NULs, defaults, exceptions, fields, type/ownership guards, reference counts, GC cycles, context managers, native templates/operators/worklists/async tasks, interpreter shutdown and REPL startup. Capability tests reject unsupported declarations explicitly. The [Python verification matrix](docs/roadmap.md#python-verification) tracks all 21 roadmap families and remaining work.

Run `just python-repl` from the repository, including outside a Nix shell. Nix supplies Python and build dependencies, builds the extensions in `build/python-repl`, and opens an isolated Python session with `demo`, `verify`, `Counter`, `Series`, `Worklist` and `AsyncTask` loaded. Subsequent invocations rebuild changed sources. Exit with `exit()` or Ctrl-D.

```python
demo.r_demo__add(20, 22)       # 42
with Counter(20) as counter:
    counter.add(22)           # 42
    counter.count = 10
    counter.label = "Python 😀"
    counter.value()           # 10

with Series() as series:      # native defaults: first=20, count=2
    series.get()             # default index=0: 20
    series.scaled_sum(2)     # 82

with AsyncTask(21, 0, False) as task:
    task.wait(2000)
    task.result()            # 42
```

The examples reuse `r_demo.hpp`, `r_verification.hpp` and their owned-class policies. These are ordinary C++ libraries without R API dependencies. Exports retain flattened native namespace names, such as `python_demo.r_demo__Counter`; the REPL supplies shorter aliases. Python extensions need no R, Lua, R6 or Python wrapper package.

Supported now: free functions, one selected constructor per class, instance/static methods, public scalar/string properties and readonly fields. Visible trailing C++ defaults work on non-overloaded functions, methods and constructors. Calls accept positional arguments only. Codecs support ordinary C++ integer types, bool, float/double, `std::string` and copied `const std::string&`; void becomes `None`. Integers reject bools/floats and preserve every supported C++ integer exactly as Python `int`. Booleans require `bool`; floating-point inputs accept int/float, reject bool/non-finite values and enforce range. Strings map to UTF-8 Python `str`, preserve embedded NULs and reject bytes or invalid Unicode.

Instances own one native object. Assignment aliases that owner; `close()` is idempotent and invalidates aliases. `with` closes on success and exceptions. Closed/uninitialized handles reject operations, and reinitialization is rejected. Copying, deep copying, pickling and Python subclassing are unsupported. GC handles instance/type/module cycles and releases storage exactly once. Static methods are callable on the class without an instance.

C++ `bad_alloc` maps to `MemoryError`, `overflow_error` to `OverflowError`, `out_of_range` to `IndexError`, `invalid_argument` to `ValueError`, and other standard/unknown exceptions to `RuntimeError`. Python allocation/conversion errors retain their Python exception. No C++ exception crosses the Python ABI. The extension uses multi-phase initialization and per-module heap types, requires the GIL and declines subinterpreter support. Native async workers never access Python; waits hold the GIL, and native task destruction cancels/joins workers.

Automatic enums, containers, callbacks, class-valued inputs/results, borrowed ownership, C++ inheritance, overload dispatch and Python operator/iterator protocols remain open. Native facades demonstrate selected mechanisms through supported scalar/owned-class APIs; they are not automatic codecs. Unsupported declarations fail generation, or are omitted with explicit diagnostics under `--skip-unsupported`. Lifecycle/copy names are reserved, and policies cannot expose private members.

The installed CMake helper provides `rosetta_generate_python`:

```cmake
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter Development.Module)
rosetta_generate_python(HEADER api.hpp MODULE library
    OUTPUT generated/library.cpp POLICY api.policy.json)
Python3_add_library(library MODULE WITH_SOABI generated/library.cpp src/library.cpp)
target_compile_features(library PRIVATE cxx_std_17)
```

The helper accepts the same compiler, compilation-database and dependency options as other generation functions. `WITH_SOABI` supplies the interpreter-specific suffix. `-DROSETTA_BUILD_PYTHON_EXAMPLES=ON` builds both repository extensions; the core generator has no Python build dependency. In an already configured Python-enabled build, `cmake --build build --target python-repl` opens the REPL without Nix. The verification module uses C++20 and native thread support, matching the reused native library.

## Ruby

Select Ruby with `--language ruby`:

```sh
just run examples/r_demo.hpp --language ruby --module ruby_demo \
  --policy examples/r_demo.policy.json -o build/ruby_demo.cpp \
  --dump-ir build/ruby_demo.json --depfile build/ruby_demo.d
```

This emits an MRI Ruby C API extension with `Init_ruby_demo`. Compiling requires Ruby 3.1+ development headers and a matching interpreter/library; source generation has no Ruby dependency. The shared model, policy, compiler/database, IR, dependency and input-protection options apply. R-only CLI options are rejected.

Run `just ruby-test` for the isolated `build/ruby-tests` suite, including outside a Nix shell. It verifies scalar boundaries, full signed/unsigned 64-bit integers, binary strings/NULs, defaults, exceptions, methods/properties, frozen objects, explicit/block cleanup, cycles, compaction, native template/operator/worklist/async facades, shutdown and REPL startup. A C++ probe deliberately raises Ruby errors and exits blocks to verify native automatic destructors run. Unsupported-feature tests and the [21-family Ruby verification matrix](docs/roadmap.md#ruby-verification) distinguish current mappings from remaining work.

Run `just ruby-repl` to build both extensions in `build/ruby-repl` and open IRB with `RubyDemo`, `RubyVerify`, `Counter`, `Series`, `Worklist` and `AsyncTask` loaded:

```ruby
RubyDemo.r_demo__add(20, 22) # 42
Counter.open(20) do |counter|
  counter.add(22)           # 42
  counter.count = 7
  counter.label = "Ruby 😀"
  counter.label            # binary String with the same UTF-8 bytes
end                        # closes even if the block raises or breaks
Series.open { |series| series.scaled_sum(2) } # 82; native defaults
Counter.live               # 0
```

Exit with `exit` or Ctrl-D. These commands use the selected interpreter's default gems, avoiding incompatible extensions installed in user gem paths. The REPL loads the exact built files. An already configured Ruby-enabled build can use `cmake --build build --target ruby-repl` directly.

Native free functions retain flattened namespace names (`r_demo__add`). The extension name becomes a CamelCase Ruby module (`ruby_demo` → `RubyDemo`). Class names flatten namespaces and capitalize the first letter (`r_demo::Counter` → `RubyDemo::R_demo__Counter`); the REPL supplies shorter aliases. Existing top-level module constants and colliding/reserved exports are rejected. Ruby uses the same native-only `r_demo.hpp`, `r_verification.hpp` and ownership policies as R/Python; no R/Python runtime or binding gem is required.

Supported now: free functions, one selected constructor, visible trailing defaults, instance/static methods, mutable/readonly scalar and string properties. Calls accept positional arguments only. Bool requires `true`/`false`; ordinary integer types require `Integer` with exact native range checks, including all int64/uint64 values. Float/double accept `Float` or `Integer` with finite/range checks. No `to_int`, `to_f` or `to_str` coercion hooks run. `std::string` and copied `const std::string&` accept actual Ruby strings as bytes; results have ASCII-8BIT encoding, preserve arbitrary bytes and embedded NULs, and can be tagged UTF-8 with `force_encoding(Encoding::UTF_8)` when the native API promises UTF-8. Void maps to `nil`.

Each TypedData instance owns one native object. Assignment aliases the owner; `close` is idempotent and invalidates aliases. `.open(args) { |owner| ... }` requires a block, returns its result and closes on every exit. GC releases unreachable owners, including Ruby instance-variable cycles, and handles compaction without native Ruby references. Reinitialization, `dup`, `clone`, Marshal and subclassing are rejected. Frozen owners permit const methods/getters and lifecycle cleanup; mutations reject with `FrozenError`.

Native `bad_alloc` maps to `NoMemoryError`, `overflow_error` to `RangeError`, `out_of_range` to `IndexError`, `invalid_argument` to `ArgumentError`, and other/unknown exceptions to `RuntimeError`. Error messages are bounded to 1023 bytes. Raising Ruby C API calls use protected calls so Ruby errors unwind C++ temporaries before resuming the Ruby exception, following the [Ruby extension API](https://docs.ruby-lang.org/en/3.4/extension_rdoc.html). Calls hold the GVL; native async workers never call Ruby, and close/GC/shutdown cancels and joins pending tasks. Ractor execution is unsupported.

Automatic enums/containers, overload dispatch, callbacks, class-valued/borrowed results, C++ inheritance and Ruby operator/iterator protocols remain open. Unsupported declarations fail generation, or are omitted with diagnostics under `--skip-unsupported`. Native facades verify their named mechanisms through supported APIs.

The installed CMake helper provides `rosetta_generate_ruby`:

```cmake
find_package(Ruby 3.1 REQUIRED)
rosetta_generate_ruby(HEADER api.hpp MODULE library
    OUTPUT generated/library.cpp POLICY api.policy.json)
add_library(library MODULE generated/library.cpp src/library.cpp)
target_include_directories(library SYSTEM PRIVATE ${Ruby_INCLUDE_DIRS})
target_link_libraries(library PRIVATE ${Ruby_LIBRARIES})
target_compile_features(library PRIVATE cxx_std_17)
set_target_properties(library PROPERTIES PREFIX "")
# Match the module suffix to RbConfig::CONFIG['DLEXT'] for this interpreter.
```

Enable `ROSETTA_BUILD_RUBY_EXAMPLES` for the repository examples; [RubyExample.cmake](cmake/RubyExample.cmake) shows interpreter-specific suffixes, linkage and test/REPL wiring. The verification facade uses C++20 and native thread support. The core remains independent of Ruby headers/libraries.

## Octave

Select GNU Octave with `--language octave`:

```sh
just run examples/r_demo.hpp --language octave --module octave_demo \
  --policy examples/r_demo.policy.json -o build/octave_demo.cpp \
  --dump-ir build/octave_demo.json --depfile build/octave_demo.d
mkoctfile -std=c++17 build/octave_demo.cpp -o build/octave_demo.oct
```

This generates a single C++ [Octave oct-file](https://docs.octave.org/v10.3.0/Getting-Started-with-Oct_002dFiles.html), with no generated `.m` wrapper or package dependency. The module filename must match `--module`; compilation requires Octave 9+ headers/libraries matching the interpreter. Generating source has no Octave dependency. Compiler/database, policy, IR, dependency and input-protection options work as for other backends; R-only options are rejected.

Run `just octave-test` to build and test in an isolated `build/octave-tests` directory through Nix. It covers numeric ranges and shapes, exact typed int64/uint64 boundaries, binary/Unicode bytes and NULs, defaults, native errors, property/method guards, aliasing, close/last-owner destruction, `onCleanup`, module pinning, serialization rejection, native template/operator/worklist/async facades, shutdown and REPL startup. Unsupported features are rejected explicitly. The [Octave verification matrix](docs/roadmap.md#octave-verification) tracks all 21 feature families and remaining work.

Run `just octave-repl` to build both modules in `build/octave-repl` and start a clean command-line Octave session with `demo`, `verify`, `Counter`, `Series`, `Worklist` and `AsyncTask` loaded:

```octave
demo.r_demo__add(20, 22)       # int64(42)
counter = Counter.new(20);
cleanup = onCleanup(@() counter.close());
counter.add(22)               # int64(42)
counter.count = 7;
counter.label = 'Octave 😀';
counter.label
clear cleanup counter;       # closes native storage
series = Series.new();       # visible native defaults
series.scaled_sum(2)          # int64(82)
clear series;                # last owner releases storage
Counter.live()               # int64(0)
```

Exit with `exit` or Ctrl-D. Both tests and REPL disable user/site startup files and history. An already configured Octave-enabled build can use `cmake --build build --target octave-repl` directly.

Calling the native module without arguments returns an API struct: `demo = octave_demo()`. Free functions retain flattened namespace names (`demo.r_demo__add`). Each selected class is a struct with `.new` and static methods (`demo.r_demo__Counter.answer()`). Instances are opaque native Octave values with dot properties and parenthesized methods. They share reference ownership on assignment, including when stored in cells/structs; native storage is destroyed after the last alias disappears. `close()` is idempotent and invalidates every alias. `onCleanup` provides scope cleanup. The representation is not a `classdef` class and provides no subclassing, native copies or serialization. Store native results before further indexing; chained indexing and object arrays are unsupported. The loaded module is pinned with `mlock` to keep registered types/destructors valid for the interpreter lifetime; rebuilding requires a new Octave session.

Supported now: scalar/string free functions, one selected constructor per class, visible trailing defaults, instance/static methods, mutable/readonly properties, and void calls with no output. Calls accept positional arguments and at most one output. Bool requires a scalar logical. Integer inputs accept real integer scalars or finite whole floating scalars within their consecutive-integer range (`2^53` for double, `2^24` for single), followed by native range checks. Use typed `int64`/`uint64` inputs for larger values, constructed with integer arithmetic rather than rounded floating literals. Signed native results use Octave `int64`, unsigned results use `uint64`, preserving every supported native integer. Float results use `single`, double results use `double`; inputs require real numeric scalars and finite/range checks. Complex values, numeric arrays, sparse numeric values, characters-as-numbers and implicit logical conversions are rejected. `std::string` and copied `const std::string&` use character row vectors, preserve arbitrary bytes/NULs and perform no text transcoding.

Native failures have identifiers: `rosetta:allocation` for `bad_alloc`, `rosetta:range` for overflow, `rosetta:index` for out-of-range, `rosetta:argument` for invalid arguments, and `rosetta:native` for other/unknown exceptions. Conversion and API misuse report `rosetta:conversion`; serialization reports `rosetta:ownership`. Existing Octave execution/interrupt/exit exceptions propagate. Native tasks never access Octave from workers; wait blocks the interpreter, and close/destruction/shutdown cancels and joins pending work.

Named C++ enums are exported as structs of constants (`api.namespace__Mode.member`); public nested enum names are flattened in the same way. Enum parameters, results, constructor arguments and properties use their underlying numeric transport, including exact `int64`/`uint64` boundaries and logical values for bool-backed enums. Aliases and duplicate enumerator values are supported. Inputs and native results must match a declared value; invalid values report `rosetta:conversion`. A visible enum definition with named values is required. Enum flag policies and unnamed bit combinations remain unsupported.

Automatic matrices/containers, overloads, native callbacks, class-valued/borrowed inputs/results, C++ inheritance and Octave operator/indexing/iteration protocols remain open. Named native facades verify selected mechanisms through the supported scalar/owned APIs. Unsupported declarations fail generation, or are omitted with diagnostics under `--skip-unsupported`.

The installed CMake helper provides `rosetta_generate_octave`, accepting the same compiler/database/dependency options:

```cmake
rosetta_generate_octave(HEADER api.hpp MODULE library
    OUTPUT generated/library.cpp POLICY api.policy.json)
# Compile the generated source plus native implementation using matching
# Octave include directories, liboctinterp/liboctave and a .oct suffix.
```

Enable `ROSETTA_BUILD_OCTAVE_EXAMPLES` for the repository modules, including the enum fixture in `examples/octave_values.hpp`. [OctaveExample.cmake](cmake/OctaveExample.cmake) queries include/library directories from `mkoctfile`, checks its interpreter version, and shows complete CMake targets. The core remains independent of Octave libraries. Examples reuse the same native-only headers/policies as R/Python/Ruby; the verification facade needs C++20 and thread support.

## Current Lua interface

```cpp
namespace math {
int add(int a, int b);
}
```

```lua
local library = require("library")
assert(library.math__add(20, 22) == 42)
```

Supported today:

- Free functions in the input header, including namespaced functions, inline definitions, and `extern "C"` functions. Redeclarations are deduplicated. Included dependency declarations are excluded.
- Integer types from `char` through `unsigned long long`, `float`, `double`, `bool`, `const char*`, and `void` returns.
- Scalar and enum aliases resolved in parameters and returns, including namespace scopes. Named scoped/unscoped enums export integer tables; inputs are range-checked against named values or explicit flag bases, including nested value fields and callback results. Try `demo.demo__feature_identity(demo.demo__Feature.classes | demo.demo__Feature.templates)`. See [flag policies](docs/lua-feature-plan.md#explicit-flag-policies) and [enum value codecs](docs/lua-feature-plan.md#enums-in-value-codecs-and-callbacks). The initial shared type categories and source locations appear in JSON output.
- Explicit C++ facades for concrete function/class/variadic template instantiations, demonstrated by `demo__template_max_int`, `demo__template_box_int`, and `demo__template_sum_three`. Automatic template instantiation is still planned.
- Exact argument counts, strict Lua types, integral conversion and range checks, finite floating-point inputs, and checked integer results. Numeric strings and truthy non-booleans are rejected. Lua integer representability limits unsigned results.
- C strings passed as borrowed pointers for the duration of the call; embedded NUL input is rejected. Returned C strings are copied into Lua, and null returns become `nil`. The C++ library must keep returned pointers valid through the copy and must not retain input pointers. Owning `char*` result policies require an explicit native adapter.
- C++ exceptions translated into Lua errors, including unknown exception types. Exception text is bounded to 511 bytes. Generated wrappers call Lua's error API after the C++ catch scope exits.
- Binary-safe `std::string`/`std::string_view`, copied `std::vector`/`std::array` tables and `std::optional` values, including nested sequences and const-reference inputs. Lua result allocation runs under a protected call so C++ temporaries are destroyed on failure. Inputs are dense arrays; unsupported mutation, custom allocator and ambiguous nil representations are diagnosed. See the [conversion rules](docs/lua-feature-plan.md#value-conversion-rules) for lifetime constraints.
- Opt-in class/struct objects with constructors, instance/static methods, public field accessors, readonly views, explicit close/GC/`__close`, copies, move-only results and unique/shared ownership. A versioned `--policy` file selects classes and declares owner-anchored borrowed results. Try `local c <close> = demo.demo__Counter.new(20, "example"); c:add(22); print(c:value())` in the REPL. See [class policies](docs/lua-feature-plan.md#class-policies-and-lifetime-rules) for selection, borrowing and remaining limits.
- Policy-controlled synchronous/retained `std::function` callbacks, protected Lua errors, reentrancy, originating-coroutine retention and safe off-thread release. Calls reject expired lifetimes, wrong threads and closed VMs; active native objects cannot be closed from a callback. Try `demo.demo__invoke_callback(function(v) return v + 2 end, 40)`. The demo includes an event source and a C-style callback/context facade. See [callback policies](docs/lua-feature-plan.md#callback-policies-and-lifetime-rules), including explicit unregister/close for self-capturing retained handlers.
- Reusable C++ hierarchy facades demonstrate multiple/virtual inheritance, checked down/crosscasts, native identity and a retained Lua virtual override. Shared views own the native object independently. Try `local e <close> = demo.demo__make_diamond('diamond', 42); local v <close> = e:as_value(); print(v:value())`. See [hierarchy adapters](docs/lua-feature-plan.md#hierarchy-adapters).
- A supplied Lua layer over named C++ series operations demonstrates checked arithmetic/bitwise operators, native truncating division/remainder, Lua floor division/modulo, comparisons, indexing/mutation, calls, conversions and versioned native iteration. The REPL preloads `Series`: try `local s <close> = Series({20, 21}); local t <close> = s * 2; print(t[2])`. See [operator and iterator adapters](docs/lua-feature-plan.md#operator-and-iterator-adapters).
- A reusable native task/future facade supports polling, bounded wait, repeatable results/errors, cancellation and explicit Lua-thread completion dispatch. Close/GC/shutdown cancel and join workers without running callbacks. Try `local t <close> = demo.demo__AsyncTask.new(21, 0, false); t:wait(2000); print(t:result())`. See [async tasks](docs/lua-feature-plan.md#async-task-adapter).
- Automatic deque/list/forward-list, set/multiset and multimap codecs preserve sequence order and multi-container duplicates. Nil-capable set elements use boxes such as `{{}, {"text"}}`. Try `demo.demo__ordered_set({"z", "a"})` or `demo.demo__multi_map({{"key", "first"}, {"key", "second"}})`. See [container rules](docs/lua-feature-plan.md#automatic-sets-multimaps-and-linear-sequences).
- Namespace aliases and explicit type imports retain canonical enum/class identity, including nested types and aliased standard containers. Try `demo.demo_names__imported_operation(demo.demo__Operation.multiply)`. See [name-resolution rules](docs/lua-feature-plan.md#namespace-aliases-and-imported-names).
- Independently generated `interop_a` and `interop_b` modules share native record identity through versioned tickets. The REPL preloads `Interop.new_a`, `Interop.new_b`, `Interop.to_a` and `Interop.to_b`; imported owners survive source close/GC. See [module interoperability](docs/lua-feature-plan.md#independently-generated-module-interoperability).
- `AwaitExecutor` runs native C++20 `co_await` tasks through explicit owner-thread queue turns, with cancellation, stored errors and prompt frame/callback cleanup. The REPL provides bounded `Awaitables.drain` and `Awaitables.result` helpers. See [awaitables and executors](docs/lua-feature-plan.md#native-awaitable-and-cooperative-executor).
- `ClassCallbacks.transform(record, callback)` demonstrates native class-valued callback arguments/results with explicit copies, temporary argument cleanup and nested frame restoration. See [class-value callbacks](docs/lua-feature-plan.md#class-value-callback-adapter).
- Returned `NativeCallable` handles demonstrate captured functors, bound members, free-function pointers, native copies/shared slots and retained Lua callbacks. The REPL preloads `Callables.capture`, `Callables.bound` and other callable/close protocols. See [returned callables](docs/lua-feature-plan.md#returned-native-callable-handles).
- `NativeStationary` demonstrates immovable native objects with shared identity, readonly borrows and const/mutable/static data. Native-form facades also adapt local/anonymous types and multidimensional C arrays with separate restricted-pointer buffers. See [native forms](docs/lua-feature-plan.md#native-declaration-and-storage-forms).
- Native module/partition/header-unit and PCH facades demonstrate native build integration. Try `demo.demo__native_module_shift(40)` (Clang module capability) or `demo.demo__native_pch_shift(40)`. See [modules and PCH](docs/lua-feature-plan.md#native-modules-header-units-and-precompiled-headers).
- `NativeBoundary` demonstrates a versioned C ABI to a separately compiled C++ library with exceptions and RTTI disabled, using checked values, allocator-owned handles and status-based callback errors. See [native ABI boundary](docs/lua-feature-plan.md#versioned-native-abi-boundary).
- Typed `MemberSelector` handles demonstrate data-member pointers, base adjustment, const/ref-qualified member-function pointers, virtual dispatch and native null checks without exposing pointer bytes. See [member selectors](docs/lua-feature-plan.md#typed-member-pointer-selectors) for the Lua operations.
- Native synchronization adapters demonstrate checked atomic updates, compare/exchange, joined worker groups, mutex-guarded volatile storage and thread-local isolation. Try `local s <close> = demo.demo__ConcurrentState.new(0, 42); s:parallel_add(4, 100, 1); print(s:load())`. Run `just concurrency-tsan` for the independent native race check. See [synchronization adapters](docs/lua-feature-plan.md#native-synchronization-and-qualified-storage-adapters).
- A native `Worklist` facade demonstrates stack, queue and min/max priority queues with snapshots, cloning and shared ownership. Try `local q <close> = demo.demo__Worklist.new(demo.demo__WorkOrder.highest); q:push("a"); q:push("z"); print(q:pop())`. See [worklist adapters](docs/lua-feature-plan.md#native-container-adaptor-worklists).
- Native filesystem adapters expose lexical path helpers and owned temporary workspaces with binary I/O, copied directory snapshots, rename/removal and shared-owner cleanup. Try `local f <close> = demo.demo__FileWorkspace.new(); f:write("message", "hello"); print(f:read("message"))`. See [filesystem adapters](docs/lua-feature-plan.md#filesystem-adapters).
- Automatic `std::variant` codecs use `{index=1, value=payload}` tables, preserving duplicate alternatives and nil payloads. `std::monostate` maps to nil. Try `demo.demo__variant_identity({index=3, value="text"})` in `just lua-repl`. See [automatic variants](docs/lua-feature-plan.md#automatic-variants).
- Explicit native adapters demonstrate a tagged integer/string union, checked bitfields, packed storage, a map of variants, and an `any` containing integers, strings or tuples. Binary values, wrong-type errors and replacement lifetimes have runtime tests. See [record and standard-library adapters](docs/lua-feature-plan.md#record-and-standard-library-adapters).
- C++20 native facades expose selected constexpr/consteval/constinit results, constants/macros, template specializations and constraints, deduction, an immediate literal and three-way comparison. Try `demo.demo__immediate_answer()` or `demo.demo__template_pattern(40)`. Invalid immediate/constraint uses have compiler rejection tests. See [compile-time adapters](docs/lua-feature-plan.md#compile-time-and-template-adapters).
- Binary-buffer facades demonstrate pointer/span operations, shared storage, readonly weak slices and generation invalidation. Explicit adapters cover pointer-to-pointer output arrays and scalar out/in-out parameters, with failure cleanup tests. See [buffer adapters](docs/lua-feature-plan.md#buffer-and-out-parameter-adapters).
- Named call adapters demonstrate free/member/constructor overload selection, native default expressions, typed C varargs, initializer lists and ref-qualified methods. The supplied `Calls` layer adds explicit Lua dispatch and ambiguity checks; try `Calls.choose(41)` or `Calls.scale(21)` in the REPL. See [call adapters](docs/lua-feature-plan.md#overload-default-and-variadic-adapters).
- A reusable expected-style result facade exposes typed errors, status/error codes, nested causes and captured callback failures. Inspect a result or explicitly rethrow with `value()`: try `local r <close> = demo.demo__divide_result(1, 0); print(r:code(), r:message())`. See [structured errors](docs/lua-feature-plan.md#structured-error-adapters).
- Explicit ownership slots demonstrate consuming moves, shared promotion, weak locking and stateful custom deleters over aligned placement-constructed resources. Failed promotion preserves ownership; GC/close/shutdown release resources. See [ownership adapters](docs/lua-feature-plan.md#transfer-custom-deleter-and-weak-ownership-adapters).
- A native C++20 coroutine generator supports lazy iteration, repeatable value access, retained exceptions, cancellation and frame cleanup. The REPL preloads `Generators.values(handle)` for Lua iteration. Tests cover Lua coroutine close and publication retry without skipped yields. See [native generators](docs/lua-feature-plan.md#native-coroutine-generator-adapter).
- Explicit Unicode adapters validate UTF-8 and convert through native UTF-16/32, `char8_t` and supported `wchar_t` strings. Tests check every Unicode scalar against Lua's separate encoder, malformed input and allocation failures. Raw byte-string behavior stays independent. See [Unicode adapters](docs/lua-feature-plan.md#unicode-and-wide-character-adapters).
- Exact decimal adapters expose full-range u64 and capability-checked native signed/unsigned 128-bit arithmetic. Finite `long double` operations use decimal transport with native precision, including subnormals and negative zero. Try `demo.demo__unsigned128_add('18446744073709551615', '1')`. See [extended numerics](docs/lua-feature-plan.md#extended-numeric-adapters).
- Automatic trailing defaults for supported non-overloaded functions, constructors and methods. Try `demo.demo__scale(21)` or `demo.demo__Counter.new(41)`. Defaults execute in C++ on each omitted-argument call; explicit nil keeps its normal meaning. See [default arguments](docs/lua-feature-plan.md#automatic-default-arguments).
- Checked `std::chrono` duration conversions and immutable system/steady time-point handles, with explicit rounding and clock-domain errors. Try `demo.demo__time_convert(-1500, 'ms', 's', 'nearest')`. See [time adapters](docs/lua-feature-plan.md#chrono-duration-and-time-point-adapters).
- Automatic tuples/pairs as positional tables, including empty/nested products, const elements, optional slots and copied callback values. Try `demo.demo__product_identity({'tuple', 42})`. See [product values](docs/lua-feature-plan.md#automatic-tuples-and-pairs).
- Automatic ordered/unordered maps use dictionaries or typed entry lists, preserving optional entries and rejecting duplicate native keys. Try `demo.demo__string_map({answer='42'})`. See [map codecs](docs/lua-feature-plan.md#automatic-maps).
- Public nested classes/enums, scoped aliases and out-of-class definitions use existing class and enum policies. Try `local item <close> = demo.demo__NestedOwner__Item.new(41); print(item:shifted())`; private/protected type paths remain hidden. See [nested declarations](docs/lua-feature-plan.md#public-nested-declarations).
- JSON export of the shared declaration model and actionable errors for unsupported declarations. `--skip-unsupported` explicitly permits partial output and prints warnings. A module with no supported functions still fails.

Automatic general pointer/out-parameter bindings, mutable scalar/value references, consuming rvalue/unique-pointer arguments, automatic class hierarchy casts, overloads, automatic template instantiation, direct C callback-pointer generation, general virtual override directors, globals, and other standard-library objects are not bound yet. Internal-linkage functions are rejected. Namespace names are currently flattened with `__`; collisions are errors. Floating-point results follow Lua's numeric representation, including nonfinite results; input range checks do not promise lossless floating-point conversion. See the [Lua feature coverage goal](docs/lua-feature-plan.md) for the implementation checklist and demo mechanisms.

Generated modules use Lua's standard C API, not LuaJIT's `ffi` extension. The generator emits C++ adapter source, which is compiled and linked with your C++ code. Additional backends will be able to use a shared C ABI shim when their native FFI consumes C symbols.

See [architecture](docs/architecture.md) for extension boundaries and [roadmap](docs/roadmap.md) for the C++ feature inventory, milestones, and acceptance criteria.
