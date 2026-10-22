# Goals and feature roadmap

## Coverage policy

Support all C++ features in some way and make each additional language reuse the semantic and policy machinery. Coverage means one of four explicit outcomes: direct native mapping, generated adaptation, user-selected concrete instantiation, or opaque access through a handle or custom adapter. A compile-time construct need not have a runtime counterpart to be usable. Unsupported and inherently nonportable cases must have a reason and an extension route.

Supporting a language means implementing and testing a backend for its actual runtime/ABI. It does not imply every language can directly express inheritance, templates, exceptions, or arbitrary object layouts. Track feature coverage per backend and distinguish automatic support from policy-required adaptation.

The completed Lua feature-family demonstration is tracked in [Lua feature coverage](lua-feature-plan.md) and its [completion audit](lua-coverage-audit.md). Every family below has a tested direct or explicit adapter mechanism. The milestones and individual subfeatures below continue to track broader automatic-generation, backend and platform work.

R is now the second backend, with [R6](../README.mepml#r-and-r6), [S3 and S4](../README.mepml#s3-and-s4-classes) interfaces sharing the same native `.Call` source, scalar/string codecs and owned-class policies. S3 uses registered functional dispatch and S4 uses formal classes/methods. Reference classes and S7 remain future interfaces. The verification matrix distinguishes the initial native facades from outstanding R mappings.

## R verification

This R verification slice uses the same 21-family inventory as Lua. Run `just r-test` to build and execute it through Nix, or enable `ROSETTA_BUILD_R_EXAMPLES` and `BUILD_TESTING` in an existing build and run CTest. `just r-repl` loads R6 `r_demo`/`r_verify`, S3 `r_s3` and S4 `r_s4` for manual checks. R support is **not at Lua parity**. A native facade establishes only its named contract, and an unsupported-feature rejection establishes only a guard. Neither counts as a complete family implementation.

Validation on 2026-10-19: the Nix-backed `just r-test` command passed all 13 CTests on Linux/GCC 16/R 4.6.1, and the combined Linux/Clang 19 Lua/R build passed all 58 CTests with core warnings treated as errors. The R capability executable checks 26 unsupported declarations against each of R6, S3 and S4, plus policy/access/lifecycle guards. The actual `just r-repl` session also passed interactive S3/S4 construction, dispatch, field mutation and close checks. These results cover the tested host; foreign platforms and R allocator fault injection remain open.

| Inventory family | Executable evidence now | Remaining R work |
| --- | --- | --- |
| Translation units/preprocessing | Shared compilation-database tests; R CLI and compiler-discovered dependency regeneration in `test_generator.cpp` and `test_dependencies.cpp` | R modules/PCH/generated-header integration and replay |
| Names/identity/linkage | R runtime namespace flattening and C linkage in `r_verification.cpp`; independent `r_demo`/`r_verify` registration in `test_r_repl.R` | Nested type identities and exchanging owned objects across modules |
| Fundamental types | `test_r_scalars.R`: char/sign variants, short/int/long/long long, unsigned variants, bool, float/double, void, integer boundaries, NA/NaN/Inf and coercion rejection | Exact values beyond ±2^53, long double and extended/wide types |
| Aliases/qualifiers | Scalar alias runtime round-trip; const methods/readonly fields; volatile/mutable-reference rejection in `test_r_capabilities.cpp` | General cv/ref qualifiers and volatile adapters |
| Enums/constants | `test_r_capabilities.cpp` rejects direct enums and variable exports; C++20 constant-expression facade returns a native result | R enum tables, range/flags policies and constant exports |
| Free functions | Arity, native defaults requiring explicit R arguments, C linkage, scalar/string/void functions and native exceptions | R defaults, overload selection and typed varargs |
| Pointers/references | R6/S3/S4 ownership and wrong-handle rejection; copied const-string-reference input; direct pointers/references and borrow policies rejected | Borrowed/class-valued inputs/results, buffers/spans and member pointers |
| Arrays/strings | Unicode, Latin-1 to UTF-8, empty/long strings, NA and embedded-NUL errors; fixed-array and string-view rejection | R vectors/arrays, binary buffers and view lifetimes |
| Records/storage | Counter public fields and readonly/UTF-8 updates; pimpl facades; bitfield/union rejection | Aggregate, tagged union, bitfield and packed/aligned mappings |
| Construction/destruction | Failed construction, repeated close/GC, live native counters, serialization rejection, clone/reinitialization guards, async destruction | Copy/move/factory/smart-pointer ownership and allocation-fault coverage |
| Members/access | Static/const methods, active fields, private API exclusion, explicit private-policy rejection and reserved lifecycle names | Ref-qualified members, static data and public nested APIs |
| Inheritance/polymorphism | Reused native virtual diamond/crosscast facades; inherited R-level S3/S4 dispatch; direct C++ base/abstract classes rejected | Native hierarchy identity, receiver casts, shared ownership and reverse overrides |
| Operators/conversions | Reused native Series indexing, mutation, multiplication and string conversion via named methods in all three R systems | R operator dispatch, iteration protocols and broad operator coverage |
| Templates/concepts | Reused native maximum/Box/variadic templates and C++20 constrained sum; direct template rejection | Automatic concrete-instantiation selection and wider template families |
| Standard library | Reused stack/queue/min/max priority worklists with order, empty-state, mutation and GC checks | Recursive R container/value codecs, optional/variant/products, time/filesystem |
| Callbacks/reverse bindings | Function pointers, `std::function` and callback policy requests rejected explicitly | R closure retention, protected invocation, thread affinity, reverse overrides and shutdown |
| Errors | Standard/unknown/bad_alloc exceptions, native overflow, non-finite results, R embedded-NUL errors and repeated calls after both error paths | Typed/status/expected/nested errors, allocator injection and callback failures |
| Compile-time C++ | Reused native consteval result/concept-constrained sum; direct immediate-function rejection | More constant/template facades and R-specific compiler-negative fixtures |
| Concurrency/async | Reused native future task: bounded wait, repeatable result, stored failure, cancellation, close and GC join; direct coroutine values rejected | Atomics/TLS adapters, R callback dispatch, coroutine/executor protocols |
| ABI/platform extensions | Registered `.Call` addresses, string-lookup rejection, native C linkage and two separately loaded R modules | Cross-platform/data-model matrix, disabled-exception/RTTI and versioned ABI facades |
| Documentation/tooling | `r-test`, `r-demo`, `r-repl`, CMake generation and two-output incremental rebuilds; automated startup/registration | R package metadata/install/distribution, capability manifests and source maps |

The native verification facade is [r_verification.hpp](../examples/r_verification.hpp) / [r_verification.cpp](../examples/r_verification.cpp), with an explicit [owned-class policy](../examples/r_verification.policy.json). It reuses `feature_library.hpp`, `compile_time_library.hpp`, `worklist_library.hpp` and `async_task.hpp`, all without R/Lua API dependencies. The runtime scripts are [test_r.R](../tests/test_r.R), [test_r_scalars.R](../tests/test_r_scalars.R), [test_r_features.R](../tests/test_r_features.R) and [test_r_repl.R](../tests/test_r_repl.R). [test_r_capabilities.cpp](../tests/test_r_capabilities.cpp) checks strict rejection, explicit partial diagnostics, preserved safe exports, class selection/access and lifecycle guards, even when R is not installed.

[test_r_object_systems.R](../tests/test_r_object_systems.R) adds S3/S4 construction, true generic dispatch, R-level inherited dispatch, static methods, field getters/setters, readonly fields, shared aliases, repeated close, failed construction, forged/wrong-type handles, serialization rejection, interface reload, GC and forced-GC checks. It repeats Series/worklist/async success, error, cancellation and cleanup behavior through both functional interfaces. The scalar suite runs against all three interfaces, and all 26 unsupported-declaration cases are checked for each system. Generator tests assert byte-identical native source and diagnose functional-accessor name collisions. These verify the initial object-system mapping, not automatic C++ inheritance or broad R/Lua parity.

Next gates, in order:

1. Finish basic value/call mappings: enums/flags, R defaults and copied recursive containers with missing-value and encoding contracts.
2. Add class values/factories, borrowed owners, copy/move/readonly and native publication-failure tests.
3. Define R callback/reverse-call retention, reentrancy, thread-entry and VM shutdown contracts before exposing callbacks.
4. Port the remaining native adapters and R protocols, including hierarchy/operator/iterator, buffers, Unicode/exact numbers, time/filesystem and coroutines/executors.
5. Add R allocator fault injection, package installation/loading tests and the platform matrix before making a Lua-parity claim.

## Python verification

Python is the third backend, sharing the same model, policies and native verification library as R. `just python-test` runs the isolated CPython suite and shared compiler/CLI/build checks; `just python-repl` loads the compiled examples. This initial slice is **not Lua parity**. Direct mappings, named native facades and unsupported-feature guards are different evidence.

| Inventory family | Executable evidence now | Remaining Python work |
| --- | --- | --- |
| Translation units/preprocessing | Shared compiler/database tests; Python CLI/IR and transitive CMake dependency regeneration | Python module/PCH replay, generated-header and cross-compilation integration |
| Names/identity/linkage | Flattened namespaces, native C linkage, independent demo/verify modules and per-module heap types | Nested/shared type identity and object exchange across modules |
| Fundamental types | All ordinary integer spellings, full int64/uint64 boundaries, strict bool/float, None and coercion rejection | Extended integers/floats, wide/Unicode scalar types |
| Aliases/qualifiers | Canonical scalar alias, const-string references, const methods/readonly properties; mutable references/volatile rejected | General reference, volatile and dependent qualifier adaptation |
| Enums/constants | Direct enums/variable exports rejected; native constant-expression facade tested | Python enum/flag tables and constant exports |
| Free functions | Native defaults and positional arities, scalar/string/void results and native errors | Keywords, overload ranking, typed varargs and calling conventions |
| Pointers/references | Owned handles, uninitialized/closed/wrong-receiver guards, copied const-string inputs | Borrowed/class-valued results and inputs, buffers/spans/member pointers |
| Arrays/strings | Unicode, long/empty text, embedded NUL round-trips, surrogate/bytes rejection | Bytes/buffers, fixed arrays and view lifetime rules |
| Records/storage | Counter mutable/readonly properties and pimpl classes; bitfield/union rejection | Aggregates, tagged unions, bitfields and packed/aligned adapters |
| Construction/destruction | Failed constructors, reinitialization guards, close/with, refcounts, GC type/module cycles and shutdown | Copy/move/factories, smart ownership and allocation-fault injection |
| Members/access | Static/instance methods, get/set/delete guards, wrong-type descriptors and private-policy rejection | Ref-qualified methods, static data and public nested types |
| Inheritance/polymorphism | Native diamond/crosscast facades; C++ inheritance and Python subclassing rejected | Native hierarchy identity, casts and reverse overrides |
| Operators/conversions | Reused native Series indexing/mutation/multiplication/string facade | Python special-method/iterator protocols and broader operators |
| Templates/concepts | Native maximum/Box/variadic and constrained-sum instantiations; direct templates rejected | Configurable automatic instantiation and additional template families |
| Standard library | Four native worklist orders, empty/error/mutation/GC behavior | Recursive container/value codecs, optional/variant/products/time/filesystem |
| Callbacks/reverse bindings | Function pointers, std::function and callback policies rejected | Python retention, protected invocation, reentrancy, GIL/thread entry and shutdown |
| Errors | Typed standard/unknown/bad_alloc translation, Python conversion failures and repeated recovery | Status/expected/nested errors, allocator injection and callback failures |
| Compile-time C++ | Native consteval/concept results; direct immediate functions rejected | Broader native facades and Python-specific negative compiler fixtures |
| Concurrency/async | Native task wait/result/failure/cancel/close/GC; interpreter shutdown joins workers | GIL-releasing waits, Python dispatch, coroutine/executor protocols |
| ABI/platform extensions | CPython ABI suffix/import loader, multi-phase modules, C linkage and explicit GIL/subinterpreter limits | Stable ABI, interpreter-version/OS matrix and disabled-exception/RTTI facades |
| Documentation/tooling | CLI, CMake helper, Nix test/REPL commands, deterministic output and incremental dependencies | Packaging/wheels, installed consumer builds and capability manifests |

Evidence: [python.cpp](../src/python.cpp), [python_runtime.hpp](../src/python_runtime.hpp), [test_python.py](../tests/test_python.py), [test_python_capabilities.cpp](../tests/test_python_capabilities.cpp) and [PythonExample.cmake](../cmake/PythonExample.cmake). The Python capability test has 26 strict/partial rejection cases plus defaults, access, lifetime policy, CLI and artifact checks. Python runtimes bind the same `r_demo.hpp` and `r_verification.hpp` native examples and policies as R. R retains its explicit-argument contract; Python additionally applies the native defaults declared by the shared Series facade.

Next gates: copied container/enum mappings; class values/factories/borrows; GIL-safe retained callbacks; Python operator/iteration/async protocols; allocation faults, wheels and a CPython/platform matrix.

Validation: the isolated `just python-test` configuration passed 10/10 CTests with Clang 21.1.8 and CPython 3.14.7. The combined Lua/R/Python configuration passed 63/63 with Clang 19 and the same CPython version. Interactive `just python-repl` checks also passed for free functions, Unicode properties, context-managed ownership and native defaults. Other CPython versions and platforms remain unverified.

## Ruby verification

Ruby is the fourth backend and reuses the same native-only fixtures, policies and semantic model as R/Python. `just ruby-test` runs the isolated MRI suite and shared compiler/CLI/build checks; `just ruby-repl` starts IRB with the compiled examples. This initial implementation covers scalar and owned-class APIs; the named facades below demonstrate selected native contracts rather than automatic mappings for entire C++ families.

| Inventory family | Executable evidence now | Remaining Ruby work |
| --- | --- | --- |
| Translation units/preprocessing | Shared frontend/database tests; Ruby CLI/IR and transitive CMake regeneration | Ruby-specific module/PCH replay, generated headers, cross-compilation |
| Names/identity/linkage | Flattened free names, module/class constants, independent demo/verify modules, existing-constant rejection | Nested namespaces, configurable names, cross-module native identity |
| Fundamental types | Ordinary integer spellings, full int64/uint64 boundaries, strict bool/float, nil/coercion rejection | Extended numeric/Unicode scalar types |
| Aliases/qualifiers | Scalar alias, copied const-string reference, const methods/readonly properties; volatile/ref guards | Broader references and dependent qualifier adaptation |
| Enums/constants | Direct enum/variable exports rejected; native constant-expression facade | Ruby enum/flag constants and variable exports |
| Free functions | Native defaults, positional arities, scalar/string/void, errors | Keywords, overload ranking, varargs/calling conventions |
| Pointers/references | TypedData owned handles, allocation/closed/type/reinitialization guards | Class-valued inputs/results, factories, borrows and buffers |
| Arrays/strings | Arbitrary binary bytes, Unicode bytes, long/empty strings, embedded NULs; no coercion | Text transcoding policies, fixed arrays, spans/views |
| Records/storage | Counter mutable/readonly properties and native pimpl wrappers; bitfield/union guards | Aggregates, unions, bitfields, storage/alignment adapters |
| Construction/destruction | Constructor failures, close/open, break/exception cleanup, cycles/compaction, live counters/shutdown | Copy/move policies, factories/smart ownership, Ruby allocator-fault matrix |
| Members/access | Static/instance methods, frozen mutations, read/write guards, private-policy rejection | Ref-qualified members, static data, nested types |
| Inheritance/polymorphism | Native diamond/crosscast facades; native inheritance/Ruby subclass guards | Native casts/type identity and Ruby reverse overrides |
| Operators/conversions | Native Series get/set/scale/text facade | Ruby operators, Enumerable/iteration and conversion protocols |
| Templates/concepts | Native maximum/Box/variadic/constrained-sum instantiations; direct template rejection | Automatic configured instantiation and additional families |
| Standard library | Four worklist orders, empty/error/mutation/GC behavior | Recursive container codecs, optional/variant/products/time/filesystem |
| Callbacks/reverse bindings | Callback types/policies rejected; Ruby runtime block cleanup probe | Retained native callbacks, reentrancy, GVL/thread entry and shutdown |
| Errors | Typed standard/unknown/bad_alloc translation, repeated recovery; C++ RAII probe across Ruby errors/breaks | Status/expected/nested errors, real allocator injection, callback failures |
| Compile-time C++ | Native consteval/concept results; immediate functions rejected | Broader native facades and Ruby-specific negative compiler fixtures |
| Concurrency/async | Task wait/result/failure/cancel/close/GC and shutdown joining workers | GVL-releasing waits, Ruby dispatch, futures/scheduler protocols, Ractors |
| ABI/platform extensions | MRI Init entry point, RbConfig suffix/linkage, C linkage facade; default-gem isolation | Other Ruby versions/implementations/OSes, disabled-exception/RTTI facades |
| Documentation/tooling | CLI, CMake helper, Nix test/IRB commands, deterministic generation/incremental dependencies | Gem packaging, installed consumers and capability manifests |

Evidence: [ruby.cpp](../src/ruby.cpp), [ruby_runtime.hpp](../src/ruby_runtime.hpp), [test_ruby.rb](../tests/test_ruby.rb), [runtime probe](../tests/ruby_runtime_probe.cpp.in), [capability tests](../tests/test_ruby_capabilities.cpp) and [RubyExample.cmake](../cmake/RubyExample.cmake). Capability tests cover 26 strict/partial unsupported cases plus defaults, access, policy, constant naming, CLI artifacts and input protection. The runtime probe compiles the exact emitted runtime and checks native destructor counters across successful yields, Ruby exceptions, nonlocal break, a protected Ruby allocation-error probe and native exceptions. This verifies the boundary contract; it does not establish complete allocator-failure coverage.

Next gates: enum/container codecs; native class values/factories/borrows; retained callbacks; Ruby operator/Enumerable protocols; gem packaging and a Ruby/platform matrix.

Validation: `just ruby-test` passed 12/12 CTests with Clang 21.1.8 and MRI Ruby 3.4.9 on Linux. The combined Lua/R/Python/Ruby build passed 69/69 with Clang 19.1.7 and warnings treated as errors, including generated Ruby extensions and the runtime probe. Interactive `just ruby-repl` checks passed for free functions, block ownership, Unicode bytes and native defaults. Other Ruby versions, implementations and platforms remain unverified.

## Octave verification

Octave is the fifth backend, reusing the same native-only fixture libraries, policies, model and compiler/dependency machinery. `just octave-test` runs its isolated native/runtime and shared compiler/CLI/build suite. `just octave-repl` opens command-line Octave with the examples loaded. The initial backend exposes scalar/owned-object APIs; native facades below establish their named contracts rather than automatic mappings for entire feature families.

| Inventory family | Executable evidence now | Remaining Octave work |
| --- | --- | --- |
| Translation units/preprocessing | Shared compiler/database tests; Octave CLI/IR and transitive CMake regeneration | Octave-specific module/PCH replay, generated headers, cross compilation |
| Names/identity/linkage | Flattened API structs, constructor/static handles, private native types in independent modules | Nested namespaces, configurable export names, cross-module native identity |
| Fundamental types | Ordinary signed/unsigned types, typed int64/uint64 extremes, bounded whole floats, bool/shape guards | Extended types, numeric arrays/matrices and explicit vectorization |
| Aliases/qualifiers | Scalar alias, copied const-string reference, const methods/readonly properties; ref/volatile rejection | Broader reference/dependent qualifier adaptation |
| Enums/constants | Named enum structs, aliases, public nested enums, checked argument/result/property codecs, exact signed/unsigned/bool transports; native constant-expression facade | Enum flags, empty/opaque enums and variable exports |
| Free functions | Positional/default arities, scalar/string/void results and output-count guards | Keywords, overload ranking, varargs/calling conventions |
| Pointers/references | Shared native owner state, closed/foreign scalar guards, no exposed addresses | Class-valued inputs/results, factories, borrows and buffers |
| Arrays/strings | Character row shape checks, Unicode/arbitrary bytes/NULs, long/empty values | Matrices/arrays, text transcoding policies, spans/views |
| Records/storage | Mutable/readonly Counter properties and pimpl objects; union/bitfield guards | Aggregates, unions, bitfields, storage/alignment adapters |
| Construction/destruction | Failed constructors, aliases/cells/structs, last-owner release, close/onCleanup, serialization rejection | Copy/move/factory/smart ownership policies and allocator injection |
| Members/access | Constructor/static API structs; instance methods/properties, arity, private-policy guards | Ref-qualified members, static data, nested classes and richer introspection |
| Inheritance/polymorphism | Native diamond/crosscast facades; direct native inheritance rejected | Native hierarchy identity/casts and classdef/reverse overrides |
| Operators/conversions | Native Series get/set/scale/text facade | Octave indexing/operator/iteration protocols and chained indexing |
| Templates/concepts | Native maximum/Box/variadic/constrained-sum facades; direct template rejection | Automatic configured instantiation and additional template families |
| Standard library | Four worklist orders, empty/error/mutation/last-owner behavior | Recursive containers, optional/variant/products/time/filesystem |
| Callbacks/reverse bindings | Callback types/policies rejected; API struct dispatcher function handles | Native callback retention, protected calls, reentrancy and thread entry |
| Errors | Native typed/unknown/bad_alloc identifiers, conversion/shape failures, repeated recovery | Status/expected/nested errors, allocator injection and callback failures |
| Compile-time C++ | Native consteval/concept results; immediate functions rejected | Broader native facades and Octave-specific negative compiler fixtures |
| Concurrency/async | Task wait/result/failure/cancel/close/last-owner release and interpreter shutdown | Nonblocking interpreter dispatch, futures/executor protocols and callbacks |
| ABI/platform extensions | Oct-file entry, matching mkoctfile/interpreter discovery, private types and module pinning | Other Octave versions/platforms, MATLAB/MEX, exception/RTTI-disabled native facades |
| Documentation/tooling | CLI, CMake helper, Nix test/REPL commands, deterministic output, incremental dependencies | Package distribution, installed consumers and capability manifests |

Evidence: [octave.cpp](../src/octave.cpp), [octave_runtime.hpp](../src/octave_runtime.hpp), [test_octave.m](../tests/test_octave.m), [capability tests](../tests/test_octave_capabilities.cpp) and [OctaveExample.cmake](../cmake/OctaveExample.cmake). Capability tests cover 26 strict/partial unsupported cases plus defaults, enum codecs/aliases/fields, incomplete enum omission, export collisions, policies, access, keyword/module guards, CLI artifacts and input protection. A dedicated enum runtime fixture covers signed/unsigned 64-bit extremes, bool-backed and unscoped enums, aliases/defaults, public nested enums, constructors/methods/properties, invalid inputs/results and recovery. Runtime tests verify aliases across property assignments and containers, text/binary/HDF5 serialization rejection, module clear with a live owner, explicit scope cleanup, exact integer transports and native facade behavior. A pending 60-second worker remains live at shutdown under a 20-second CTest timeout, establishing cancellation/join behavior.

Next gates: copied matrices/containers and enum flags; native values/factories/borrows; retained callbacks; native Octave indexing/operator protocols and classdef integration; packaging and an Octave/platform matrix.

## Julia verification

Julia is the sixth backend. It emits a C ABI bridge and a Julia `ccall` module, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures. `just julia-test` runs an isolated build and runtime/shared tests; `just julia-repl` opens the examples. This initial slice covers scalar and owned-class APIs; named native facades below demonstrate selected contracts rather than general automatic mappings.

| Inventory family | Executable evidence now | Remaining Julia work |
| --- | --- | --- |
| Translation units/preprocessing | Shared parser/database tests, CLI/IR, two-output CMake regeneration and transitive inputs | Module/PCH replay, generated headers and cross compilation |
| Names/identity/linkage | Flattened modules/types/methods, reserved/export collision guards and C linkage | Renaming, nested namespace modules and cross-library owner identity |
| Fundamental types | Checked machine integers, exact Int64/UInt64 extremes, finite float/double, Bool | Extended types, BigInt, non-finite policies and vectorization |
| Aliases/qualifiers | Scalar alias, copied const-string reference, const methods/readonly fields | Broader references, volatile/dependent qualifier adaptation |
| Enums/constants | Explicit enum/variable rejection; native constant-expression facade | Julia enums/flags and constant exports |
| Free functions | Positional/default arities, scalar/string/void calls and repeated recovery | Keywords, overload ranking and varargs |
| Pointers/references | Registry IDs, record-type checks, closed/foreign owner guards | Values/factories/borrows, buffers and consuming transfer |
| Arrays/strings | Copied AbstractString inputs, Unicode/arbitrary bytes/NULs, GC stress | Arrays/matrices, views and encoding policies |
| Records/storage | Mutable/readonly fields, pimpl objects, union/bitfield/volatile guards | Aggregates, unions and storage/layout adapters |
| Construction/destruction | Constructor failure, aliases, idempotent close, finalizers/GC, copy/serialization guards | Copy/move/factory/smart ownership policies and allocator faults |
| Members/access | Selected constructors/methods/fields, static functions, defaults and access-policy guards | Ref-qualified methods, static data and richer introspection |
| Inheritance/polymorphism | Native hierarchy/crosscast facades; direct inheritance rejection | Hierarchy identity/casts and Julia dispatch integration |
| Operators/conversions | Native Series get/set/scale/text facade | Julia indexing, operators, iteration and conversion protocols |
| Templates/concepts | Native maximum/Box/variadic/constrained-sum facades | Automatic configured instantiation and template families |
| Standard library | Four worklist orders, mutation, empty/error/cleanup behavior | Recursive containers, optional/variant/products/time/filesystem |
| Callbacks/reverse bindings | Callback types/policies rejected | Protected invocation, retention, reentrancy and thread entry |
| Errors | Native typed/unknown/bad_alloc statuses, conversion errors, recovery and output cleanup | Status/expected/nested errors and fault injection |
| Compile-time C++ | Native consteval/concept results; immediate functions rejected | Broader native facades and compiler negatives |
| Concurrency/async | Threaded calls/close, task wait/result/failure/cancel/finalize/shutdown | Julia Task/event-loop dispatch and nonblocking async protocols |
| ABI/platform extensions | Versioned private C ABI, per-library typed registry, Libdl suffix | Public reusable ABI, platform/version matrix and package artifacts |
| Documentation/tooling | CLI, CMake helper, Nix test/REPL recipes and deterministic output | Packages, installed consumers and capability manifests |

Evidence: [julia.cpp](../src/julia.cpp), [julia_runtime.hpp](../src/julia_runtime.hpp), [runtime tests](../tests/test_julia.jl), [capability tests](../tests/test_julia_capabilities.cpp) and [JuliaExample.cmake](../cmake/JuliaExample.cmake). Capability tests cover 26 strict/partial unsupported cases, policies/access/defaults, deterministic generation, CLI artifacts and interface output protection. Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check the native loading path, numeric boundaries, binary strings, GC roots/finalizers, ownership errors, threaded close and native task shutdown. A 60-second worker is left rooted until interpreter exit under a bounded CTest timeout.

Next gates: enum/container/array codecs; native values/factories/borrows; callbacks; Julia protocols and asynchronous dispatch; packaging and a Julia/platform matrix.

Validation: all five Julia test groups passed with Julia 1.12.6 and Clang 19.1.7 on Linux, with generated native libraries compiled under warnings-as-errors. The selected 18-test regression suite also passed, covering Octave runtime/enum tests, all backend capability suites, and shared generator/compiler/dependency/CLI tests. Julia Base function names remain local to the generated module, and deleting a generated `.jl` interface triggers CMake regeneration. Other Julia versions and platforms remain unverified.

## Java verification

Java is the seventh backend, generating compiled Java APIs and JNI libraries from the shared frontend, model, policies and native-only fixtures. `just java-test` builds an isolated JDK 17-bytecode/JNI configuration and shared tests. `just java-demo` compiles and runs the example main class; the README also has a Nix-backed compiled Java chunk.

| Inventory family | Executable evidence now | Remaining Java work |
| --- | --- | --- |
| Translation units/preprocessing | Shared parser/database/IR and two-output dependency regeneration | Module/PCH replay, generated headers and cross compilation |
| Names/identity/linkage | Flattened Java APIs, named packages, JNI underscore escaping, access/reserved/collision guards | Renaming policies, nested namespace packages, cross-library identity |
| Fundamental types | Typed primitives, checked integer widths, full unsigned BigInteger transport, finite float/double | Extended types, numeric arrays and vectorization |
| Aliases/qualifiers | Scalar alias, copied const-string reference, const methods/readonly getters | Broader references, volatile/dependent adaptation |
| Enums/constants | Explicit enum/variable rejection and native constant-expression facade | Java enum/flag/constant exports |
| Free functions | Static typed methods, defaults as Java overloads, void calls | C++ overload resolution, varargs and calling conventions |
| Pointers/references | Opaque typed registry IDs, close and foreign-record guards | Class values/factories/borrows, buffers and ownership transfer |
| Arrays/strings | Copied byte arrays, arbitrary bytes/NULs, Unicode and explicit UTF-8 helpers | Arrays/matrices, views and text policies |
| Records/storage | Nested final owner classes, fields/accessors and pimpl fixtures | Aggregates, unions, bitfields and layout adapters |
| Construction/destruction | Constructor failure, aliases, try-with-resources, Cleaner/GC, shutdown and serialization rejection | Copy/move/factory/smart ownership and allocator faults |
| Members/access | Typed instance/static methods, private-policy guards, readonly fields, waitFor mapping | General member renaming, static data and introspection |
| Inheritance/polymorphism | Native hierarchy/crosscast facades; C++ bases and Java subclassing excluded | Hierarchy identity/casts and directors |
| Operators/conversions | Native Series get/set/scale/text facade | Java iterable/operator-style APIs and conversions |
| Templates/concepts | Native maximum/Box/variadic/constrained-sum facades | Configured instantiation and broader template families |
| Standard library | Four worklist orders, mutation/empty errors and cleanup | Recursive containers, optional/variant/products/time/filesystem |
| Callbacks/reverse bindings | Callback types/policies rejected | Global reference retention, VM thread attachment, protected callbacks |
| Errors | Standard Java exceptions, JNI checks, typed/unknown/bad_alloc native errors and recovery | Status/expected/nested errors, callback/allocator faults |
| Compile-time C++ | Native consteval/concept facade and immediate-function rejection | Broader native facades and negative compiler fixtures |
| Concurrency/async | Java calls racing close; native wait/result/failure/cancel/close/shutdown | CompletableFuture/executor integration and nonblocking dispatch |
| ABI/platform extensions | Matching JDK JNI headers, typed Java 17 compilation, JAR/native loading, Xcheck:jni | JDK/platform matrix, native artifact distribution and class-loader isolation |
| Documentation/tooling | CLI, CMake helper, Nix test/demo commands, README chunk, deterministic output | Maven/Gradle packages, installed consumers and manifests |

Evidence: [java.cpp](../src/java.cpp), [java_runtime.hpp](../src/java_runtime.hpp), [runtime tests](../tests/TestJava.java), [capability tests](../tests/test_java_capabilities.cpp), [compiled example](../examples/JavaExample.java) and [JavaExample.cmake](../cmake/JavaExample.cmake). Capability tests cover 26 strict/partial unsupported cases, typed defaults, access/policies, accessor collisions, deterministic output, package/JNI spelling and CLI interface protection. Runtime tests compile and load generated classes/libraries, preserve signed/unsigned extremes and raw string bytes, reject invalid/native failures, verify Cleaner/close/aliases/threading, and leave a 60-second worker rooted at exit under a 30-second CTest timeout.

Next gates: enum/container/array codecs; native values/factories/borrows; retained JNI callbacks; Java collections and async integration; packaged native distribution and a JDK/platform matrix.

Validation: the isolated Java Nix recipe passed 14/14 CTests with OpenJDK 21.0.12 and Clang 21.1.8 on Linux, including all backend capability suites and shared generator/compiler/dependency/CLI tests. Generated JNI libraries and Java APIs compile with warnings treated as errors, and runtime tests use `-Xcheck:jni`. The README compiled Java chunk and MEP runner selection test also passed. Java 17 bytecode is targeted; other runtime JDKs and platforms remain unverified.

## Common Lisp verification

Common Lisp emits the C ABI bridge shared with Julia and C#, plus a CFFI interface package, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures. `just lisp-test` runs an isolated build with runtime and shared tests; `just lisp-repl` opens SBCL with the examples. This initial slice covers scalar and owned-class APIs.

Evidence: [common_lisp.cpp](../src/common_lisp.cpp), [common_lisp_runtime.hpp](../src/common_lisp_runtime.hpp), [runtime tests](../tests/test_common_lisp.lisp), [capability tests](../tests/test_common_lisp_capabilities.cpp) and [CommonLispExample.cmake](../cmake/CommonLispExample.cmake). Capability tests cover the same 26 strict/partial unsupported cases as Julia, plus policies/access/defaults, case-insensitive and qualified-name collisions, `COMMON-LISP` names staying local, deterministic generation and CLI artifacts. Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check numeric boundaries, UTF-8 and byte strings, error kinds, GC during calls, finalizers, ownership errors, threaded close, native tasks and exit-time shutdown of a pending worker.

Next gates: enum/container codecs; native values/factories/borrows; callbacks; CLOS protocols (sequences, printing, generic dispatch); ASDF system packaging and an implementation matrix (CCL, ECL).

Validation: all five Common Lisp tests passed with SBCL 2.6.8, CFFI and trivial-garbage from nixpkgs and Clang on macOS (arm64), with generated native libraries compiled under warnings-as-errors. Other Lisp implementations and platforms remain unverified.

## Tcl verification

Tcl emits stub-enabled Tcl 9 extensions, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures. `just tcl-test` runs an isolated build with runtime and shared tests; `just tcl-repl` opens tclsh with the examples. This initial slice covers scalar and owned-class APIs.

Evidence: [tcl.cpp](../src/tcl.cpp), [tcl_runtime.hpp](../src/tcl_runtime.hpp), [runtime tests](../tests/test_tcl.tcl), [capability tests](../tests/test_tcl_capabilities.cpp) and [TclExample.cmake](../cmake/TclExample.cmake). Capability tests cover the shared 26 strict/partial unsupported cases, policies/access/defaults, subcommand and option tables, reserved object subcommands, Tcl-cased init names, namespaced command names, deterministic generation and CLI artifacts. Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check Tcl integer forms and 64-bit boundaries, booleans, finite/range checks, UTF-8 with NULs and non-BMP characters, errorCodes, `wrong # args`, cget/configure (including all-or-nothing assignment), destruction through close/rename/interpreter deletion, native tasks and exit with a pending worker.

Next gates: Tcl 8.6 conversions; enum/container codecs; native values/factories/borrows; callbacks; TclOO classes; multi-interpreter threading tests.

Validation: all five Tcl tests passed with Tcl 9.0.4 from nixpkgs and Clang on macOS (arm64), with generated extensions compiled under warnings-as-errors. Other platforms remain unverified.

## Guile verification

Guile emits libguile extensions plus a generated Scheme module, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures. `just guile-test` runs an isolated build with runtime and shared tests; `just guile-repl` opens a Guile REPL with the examples. This initial slice covers scalar and owned-class APIs.

Evidence: [guile.cpp](../src/guile.cpp), [guile_runtime.hpp](../src/guile_runtime.hpp), [runtime tests](../tests/test_guile.scm), [capability tests](../tests/test_guile_capabilities.cpp) and [GuileExample.cmake](../cmake/GuileExample.cmake). Capability tests cover the shared 26 strict/partial unsupported cases, policies/access/defaults, SRFI-9 style names, const fields without setters, export collisions, the 10-argument primitive limit, reserved module names, deterministic generation and CLI artifacts (including the module filename check). Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check exact/inexact numbers and 64-bit boundaries, booleans, finite/range checks, UTF-8 with NULs and non-BMP characters, `rosetta-error` through `catch`, `guard` and the exception printer, `wrong-number-of-args`, wrong-type and closed objects, close/finalizer destruction, calls from several Guile threads, native tasks and exit with a pending worker.

Next gates: enum/container codecs; native values/factories/borrows; callbacks; GOOPS classes; exceeding the 10-argument limit through rest arguments.

Validation: all five Guile tests passed with Guile 3.0.11 from nixpkgs and Clang on macOS (arm64), with generated extensions compiled under warnings-as-errors, interpreted and auto-compiled. Other platforms remain unverified.

## OCaml verification

OCaml emits C stubs plus a generated module of externals, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures; the frontend now records parameter names for the optional labeled arguments. `just ocaml-test` runs an isolated build with runtime and shared tests, in the bytecode toplevel and as a native program; `just ocaml-repl` opens the toplevel with the examples. This initial slice covers scalar and owned-class APIs.

Evidence: [ocaml.cpp](../src/ocaml.cpp), [ocaml_runtime.hpp](../src/ocaml_runtime.hpp), [runtime tests](../tests/test_ocaml.ml), [capability tests](../tests/test_ocaml_capabilities.cpp) and [OcamlExample.cmake](../cmake/OcamlExample.cmake). Capability tests cover the shared strict/partial unsupported cases, policies/access, optional labeled arguments, scalar representations, namespace and class modules, keyword and capitalization adjustments with their collisions, bytecode stubs for more than five arguments, reserved module names, deterministic generation and CLI artifacts (including the module filename check). Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check C++ integer ranges, int64 boundaries and unsigned bit patterns, chars, finite/range checks, byte strings with NULs, labels and label gaps, `Error` matching and its printer, closed objects, close/finalizer destruction, structural comparison and marshalling refusals, calls and finalizers across domains, native tasks and exit with a pending worker.

Next gates: enum/container codecs; native values/factories/borrows; callbacks; an `.mli` and findlib/dune packaging.

Validation: all eight OCaml tests (bytecode and native runtime families, the toplevel REPL script and capabilities) passed with OCaml 5.4.1 and Clang from nixpkgs on macOS (arm64), with generated stubs compiled under warnings-as-errors. Other platforms remain unverified.

## Go verification

Go emits typed C ABI stubs plus a generated cgo package, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures. `just go-test` runs an isolated build with runtime and shared tests, plain and under the race detector, and checks the generated packages with `gofmt` and `go vet`. This initial slice covers scalar and owned-class APIs.

Evidence: [go.cpp](../src/go.cpp), [go_runtime.hpp](../src/go_runtime.hpp), [runtime tests](../tests/test_go.go), [capability tests](../tests/test_go_capabilities.cpp) and [GoExample.cmake](../cmake/GoExample.cmake). Capability tests cover the shared strict/partial unsupported cases, policies/access, variadic defaults and mixed-type defaults, scalar representations, CamelCase names, flattened namespaces and their collisions, renamed parameters, reserved package names, the package preamble, deterministic generation and CLI artifacts. Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check every integer boundary, floats including infinities and NaN, byte strings with NULs and invalid UTF-8, variadic defaults and their limit, `*Error` through `errors.As`, closed, nil and zero-value objects, close/cleanup destruction, calls and cleanups across goroutines, native tasks and exit with a pending worker.

Next gates: enum/container codecs; native values/factories/borrows; callbacks and Go interfaces; Windows link flags.

Validation: all eight Go tests (four runtime families, the race-detector run, gofmt, go vet and capabilities) passed with Go 1.26.3 and Clang from nixpkgs on macOS (arm64), with generated stubs compiled under warnings-as-errors. Other platforms remain unverified.

## APL verification

APL emits one C ABI library with two front ends: a Dyalog namespace script calling it through `⎕NA`, and a GNU APL script calling a generated native function that calls the same stubs. It reuses the shared model, policies, compiler/dependency tooling and native-only fixtures. `just apl-test` runs an isolated GNU APL build with runtime and shared tests. This initial slice covers scalar and owned-class APIs.

Evidence: [apl.cpp](../src/apl.cpp), [apl_runtime.hpp](../src/apl_runtime.hpp), [GNU APL tests](../tests/test_gnu_apl.apl), [workspace-clear test](../tests/test_gnu_apl_clear.apl), [Dyalog tests](../tests/test_dyalog.apls), [capability tests](../tests/test_apl_capabilities.cpp) and [AplExample.cmake](../cmake/AplExample.cmake). Capability tests cover the shared strict/partial unsupported cases, policies/access, niladic, monadic and dyadic wrapper forms, Dyalog `⎕NA` declarations, call specs and classes, flattened namespaces and their collisions, library paths, deterministic generation and CLI artifacts. GNU APL runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check integer boundaries and integral floats, range, type and count errors, UTF-8 strings with NULs, optional arguments, nested and plain string arguments, events and `last_error`, closed, forged, wrongly typed and other-library handles, `)CLEAR` and exit destruction, and native tasks.

Next gates: run the Dyalog front end under Dyalog; enum/array/container codecs; native values/factories/borrows; callbacks; Windows.

Validation: all seven GNU APL tests (five runtime families, workspace clear and capabilities) passed with GNU APL 2.0 (Homebrew) and Clang from nixpkgs on macOS (arm64), with generated code compiled under warnings-as-errors; the GNU APL bridge also compiles against GNU APL 1.9's headers (nixpkgs). The Dyalog front end is generated and checked by the capability tests, and its C ABI was exercised directly, but it has not run under Dyalog. Linux remains unverified.

## COBOL verification

COBOL emits a copybook of parameter records plus C++ stubs that GnuCOBOL programs `CALL` with `-fstatic-call`, reusing the shared model, policies, compiler/dependency tooling and native-only fixtures. `just cobol-test` runs an isolated build with runtime and shared tests. This initial slice covers scalar and owned-class APIs.

Evidence: [cobol.cpp](../src/cobol.cpp), [cobol_runtime.hpp](../src/cobol_runtime.hpp), [runtime tests](../tests/test_cobol.cob), [fixed-format test](../tests/test_cobol_fixed.cob), [capability tests](../tests/test_cobol_capabilities.cpp) and [CobolExample.cmake](../cmake/CobolExample.cmake). Capability tests cover the shared strict/partial unsupported cases, policies/access, record layouts and field usages, string fields and their capacity, `-GIVEN` defaults, renamed parameters, camelCase words, the status record, fixed-format columns, case-insensitive and CALL-name collisions, word lengths, deterministic generation and CLI artifacts. Runtime tests reuse `r_demo.hpp` and `r_verification.hpp` and check every integer boundary, floats, booleans, trimmed and exact-length strings with NULs and UTF-8, overlong results, defaults and their limit, status kinds, messages, `RETURN-CODE` and `OMITTED`, closed, forged and wrongly typed handles, native tasks and exit with a pending worker.

Next gates: COBOL tables for arrays/containers; enum codecs; native values/factories/borrows; callbacks; other COBOL compilers; Windows.

Validation: all six COBOL runtime programs (five free-format families and the fixed-format build) passed with GnuCOBOL 3.2 (Homebrew) and Clang from nixpkgs on macOS (arm64), with generated stubs compiled under warnings-as-errors. Other platforms remain unverified.

## Milestones

| Phase | Work | Completion criteria |
| --- | --- | --- |
| 0: executable prototype (implemented) | C++17 generator and tests, CMake/CTest build, just development commands, Clang adapter, shared model, Lua 5.4 emitter, scalar and borrowed C-string free functions, strict conversions, exceptions, diagnostics, CLI, example | Generated shared library loads in Lua; success and error paths run in tests |
| 1: reliable declaration foundation | Structured type graph, stable IDs and source locations, robust Clang adapter, compiler matrix, compilation databases, explicit selection/rename/ignore config, versioned manifests | Real dependency-heavy headers parse without leaking dependency declarations; every selected declaration has an emitted/adapted/rejected outcome; deterministic output |
| 2: useful procedural APIs | Alias resolution in every position, enums, overload dispatch, defaults, constants, namespace tables, strings, buffer/length adapters and references with direction policies | Bind a real procedural library; boundary and ambiguity tests; no silent lossy integer conversion or lifetime assumptions |
| 3: object model | Owned/borrowed userdata, constructors/destructors, methods, fields/properties, constness, copy/move, smart pointers, class identity | Bind a small class library; GC/explicit-close/destructor counters and sanitizers show no leaks or dangling borrowers |
| 4: reusable ABI and additional languages (R, Python, Ruby, Octave, Julia, Java, Common Lisp, Tcl, Guile, OCaml, Go, APL and COBOL started) | R6/S3/S4, initial CPython, MRI Ruby, Octave, Julia, Java, Common Lisp, Tcl, Guile, OCaml, Go, APL and COBOL backends reuse extraction and policies; shared C shim, error protocol, ABI/version manifest and broader coverage remain planned | Same fixture libraries bind across runtimes; shared semantic extraction and ownership policies; no Lua-specific model nodes |
| 5: advanced C++ | Inheritance, operators, templates and standard containers, callbacks, virtual dispatch, policies for concurrency | Feature fixtures execute in both languages; ambiguous overloads and invalid policies produce precise diagnostics |
| 6: broad language/platform support | C#/PInvoke, Java/JNI, JS native or Wasm, Swift/C, Go/cgo and others; packaging and platform matrix | Each backend has published capability coverage, native loading tests, documented runtime constraints, and distributable artifacts |

Phases represent dependency order, not time estimates. Develop each feature end to end through parsing, model representation, policy validation, generated code, runtime tests, and documentation. Do not postpone lifetime correctness until the advanced-feature phase.

## C++ feature inventory

Everything below is planned unless explicitly marked implemented. Named enum bindings, qualified aliases, initial recursive/function types and record metadata, explicit template facades, binary strings/views, copied values, policy-selected class objects and smart ownership, and lifetime-controlled `std::function` callbacks are implemented within the limits listed in the Lua feature plan. Protected publication/invocation, allocation-failure tests, native destructor counters, owner pins and shutdown tests establish cleanup behavior. The remaining strategies describe intended mappings rather than current capabilities.

| Feature group | Scope and intended mapping |
| --- | --- |
| Translation units and preprocessing | Headers and sources; include paths, defines/undefines, forced includes, compiler target and standard, `compile_commands.json`, generated headers, conditional APIs, macro expansion provenance. Macros are not AST declarations: expose evaluable object-like constants or explicit wrapper functions; function-like macros require opt-in adapters. Modules/header units and PCH need compiler-aware discovery. |
| Names, identity, linkage | Namespaces (basic free-function discovery implemented), inline/anonymous namespaces, nested declarations, namespace aliases, using declarations, overload identities, C linkage (basic calls implemented), visibility/export attributes, internal linkage, ODR deduplication, multi-header and multi-module shared type registration. Internal functions may later use explicitly requested same-translation-unit adapters. |
| Fundamental types | Scalar integers/bools/float/double implemented. Add `void`, all signed/unsigned spellings, wide/UTF characters, `char8_t`, `wchar_t`, `long double`, extended integers and platform types. Preserve width, sign, representation, qualifiers, nullability and encoding; use checked or boxed representations where the target cannot represent values. |
| Aliases and qualifiers | `typedef`, `using`, alias templates, cv/restrict qualifiers, volatile access rules, `auto`/deduced returns and `decltype`. Resolve canonical identity without discarding public spelling. Parameter scalar aliases partially implemented. |
| Enums and constants | Scoped/unscoped enums, explicit underlying types, aliases, flags, enum validation policies, compile-time constants, `constexpr`/`constinit` variables. Expose immutable values or typed wrappers appropriate to the target. |
| Free functions | Basic functions and trailing defaults on supported non-overloaded declarations implemented. Extend overload resolution, deleted/unavailable functions, `noexcept`, attributes, function pointers, calling conventions, variadic functions with explicit typed adapters, initializer-list parameters and out parameters. Reject ambiguous runtime calls. |
| Pointers and references | Const C-string borrowing implemented. Add nullable pointers, lvalue/rvalue references, pointer-to-pointer, void/opaque pointers, member pointers, incomplete types, pointer arithmetic only through explicit buffer/handle APIs, array decay, spans and ownership transfer. Require length and lifetime policies. |
| Arrays and strings | Fixed/multidimensional arrays, byte buffers with embedded NUL, encoding-aware C strings, `std::string`, `string_view`, wide strings, `std::array`, `span`, and pointer/length pairs. Distinguish copy, view, mutation, and retained arguments. |
| Records and storage | Structs/classes, public fields, nested types, aggregate initialization, standard-layout/POD records, bitfields through accessors, packed/aligned types, unions through tagged/custom adapters, forward declarations, flexible implementation extensions. Avoid assuming target layouts match C++. |
| Construction and destruction | Constructor overloads, factories, explicit/implicit conversion rules, destructors, RAII, copy/move construction and assignment, noncopyable/nonmovable objects, value category, placement allocation through explicit adapters. Exactly-once release across GC and explicit close. |
| Members and access | Instance/static methods and data, const/ref-qualified methods, properties, access restrictions, friend-supplied wrappers. Bind public APIs; private/protected access requires an authorized C++ wrapper rather than bypassing language access controls. |
| Inheritance and polymorphism | Single/multiple/virtual inheritance, pointer adjustment, base/derived conversion, virtual/abstract methods, RTTI and downcasts, diamond identity, pure virtuals and final classes. Preserve object identity and reject invalid casts. |
| Operators and conversions | Arithmetic/comparison/bitwise operators, indexing, call operator, iterators, explicit conversion operators, user-defined literals and spaceship comparisons. Map to target protocols or named methods where a direct operator does not exist. |
| Templates and concepts | Class/function/variable/alias templates, full/partial specialization, non-type and template-template parameters, parameter packs, deduction, constrained templates, concepts, SFINAE, dependent names. Expose selected concrete instantiations; provide config and adapter hooks for unbounded families. Never imply a finite generator can preinstantiate every possible type. |
| Standard library types | `unique_ptr`/`shared_ptr`/`weak_ptr`, vectors/maps/sets/sequences, pair/tuple, optional/variant/any, `std::function`, chrono, filesystem and error codes. Use reusable typemaps with explicit copying, iteration, mutation, identity and ownership semantics. Allocator and custom-deleter behavior stays inside C++. |
| Callbacks and reverse bindings | Function pointers, capturing lambdas through callable adapters, `std::function`, callback contexts, function object lifetimes, reverse virtual dispatch/directors, callback exception translation and cancellation. Define retention, reentrancy, thread entry and runtime shutdown behavior. |
| Errors | Basic native exception translation implemented. Add typed exceptions, error-code/expected/status mappings, nested exceptions, callback error propagation, allocation failure and partially constructed object cleanup. No exceptions or runtime jumps may cross an unsupported ABI boundary. |
| Compile-time C++ | `constexpr`, `consteval`, `constinit`, compile-time reflection when available, type traits and metaprogramming. Evaluate or instantiate in C++ and export results/adapters. Immediate-only functions require generated constant-expression wrappers rather than runtime calls. |
| Concurrency and async | Thread-local data, atomics, synchronization wrappers, native threads invoking callbacks, Lua state affinity, Python interpreter locks, futures/promises, coroutines/generators, cancellation, executor integration. Use explicit scheduler and synchronization policies; target runtimes differ. |
| ABI and platform extensions | C++ standard versions, compiler extensions/attributes, calling conventions, symbol visibility, shared/static linking, Windows exports, macOS module linking, cross compilation, endianness, integer/data models, exception/RTTI-disabled builds. Isolate nonportable features behind explicit capabilities. |
| Documentation and tooling | Source comments, target type declarations and API docs, generated source maps, diagnostic locations, reproducible output, incremental dependency tracking, CMake/Meson integration, packaging, CLI schema stability, plugin API versioning and compatibility tests. |

## Next implementation slice

1. Grow the initial type categories, canonical alias resolution and source locations into a complete structured type graph. Macro/header provenance and canonical redeclarations have fixtures; complex attributed declarations need more coverage.
   Compilation-database selection now imports working directory, include/forced-include paths, definitions and standards from a selected translation unit, with explicit ambiguity diagnostics and IR context provenance. Real compiler fixtures exercise relative paths, quoted arguments, override precedence and output protection. Response files, launchers, forwarded options, module/PCH discovery and versioned replay manifests remain pending; compiler-discovered depfiles and incremental CMake generation are now implemented.
2. Named enums and exact signature calls now work. Trailing-default metadata and calls now work for supported non-overloaded functions, methods and constructors. Add explicit overload groups. Specify target dispatch and ambiguity rules before emission.
   Explicit enum flag bases now allow validated combinations while unconfigured enums retain closed named-value checks. Native compile-time basis/range checks, signed/unsigned/bool transport tests and strict policy diagnostics are implemented. Recursive enum codecs now cover vector/array/optional/tuple/pair values and copied callbacks, including flag validation and read-side completeness checks. Public nested enums/classes and out-of-class definitions now retain ownership/scope/access metadata. General enum selection, anonymous/template-specific declarations and boxed transport remain pending.
   Named call adapters and a supplied Lua layer now demonstrate selected overloads, native defaults, ambiguity checks, typed bounded C varargs, initializer lists and ref-qualified members. General overload metadata, dispatch and selection policies remain pending.
   A native expected-style result facade also demonstrates typed/status/error-code failures, nested exceptions and captured callbacks with inspection/rethrow and allocation-failure tests. General generated exception/result mappings remain pending.
3. Versioned class/borrow selection and call-only/retained callback policies now exist. Extend them with general selection/renaming, argument direction, consuming transfer and richer owner relationships. Keep normal scalar bindings automatic and make unresolved lifetime questions diagnostic errors.
   Explicit slots now demonstrate unique transfer, shared promotion, stateful custom deleters, aligned placement construction and weak locking, including failure-preserving ownership tests. General consuming/allocator/deleter/weak-pointer policies remain pending.
   Native C++20 generators now demonstrate coroutine frames, lazy yields, exceptions/cancellation, Lua iteration and coroutine-close cleanup. General asynchronous awaitable/executor integration remains pending.
   Explicit Unicode adapters now exercise UTF-8/16/32, char8_t and supported wchar_t strings, including all scalar values and malformed/allocation-failure cases. General encoding policies and broader platform validation remain pending.
   Decimal adapters now exercise full-range u64, capability-checked native signed/unsigned 128-bit arithmetic and finite long double precision/limits. General numeric typemaps, nonfinite/payload policies and broader ABI validation remain pending.
   Chrono facades now exercise checked integral duration casts/rounding and system/steady time points, with clock-domain validation, native metadata/sampling and failure/lifetime tests. General chrono typemaps, arbitrary clocks/periods, civil calendars/time zones and filesystem adapters remain pending.
   Tuple/pair codecs now bind heterogeneous products directly, including empty/nested products, optional slots, const storage and copied callback products. Fixed arrays share exception-safe element construction. Automatic map/unordered-map codecs now preserve dictionaries or entry lists, including optional values, typed duplicate detection, enum/nested/callback values and protected traversal. Sum/set/remaining-sequence typemaps, custom container policies and ownership-bearing product elements remain pending.
4. Protected callbacks now have reentrancy, active-owner pins, thread/lifetime, allocation and shutdown tests. Shared-view facades demonstrate a native virtual diamond, checked down/crosscasts, identity and a retained Lua virtual override with cleanup tests. A series adapter and supplied Lua layer demonstrate arithmetic/bitwise/comparison/index/call protocols, conversions and native iteration with mutation checks. Extend class planning to overloads, template-specific nested types, automatic hierarchy/director/operator generation and ownership invalidation beyond explicit close, followed by advanced values and async adapters.
5. Record/store facades demonstrate nontrivial union transitions, bitfields, packed access, maps/variants and any/tuple storage with allocation-failure checks. C++20 facades demonstrate constexpr/consteval/constinit, constants/macros, template specializations/constraints, deduction, a selected literal and three-way comparison. The native task facade demonstrates promises/shared futures, cooperative cancellation, poll/wait/result, owner-thread completion dispatch and cancel/join cleanup. Add remaining value/buffer adapters, general instantiation/constant-evaluation generation, coroutine/generator adapters and executor/event-loop integration. Exercise a real small library and select the second backend to test shared model/planning reuse before expanding the language list.

## Release gates

- Every advertised feature needs declaration fixtures and executable native-loading tests, including incorrect input and post-error recovery.
- Ownership features need destructor, leak, double-close, borrowed-lifetime and sanitizer checks. Callback features need reentrancy, retention, shutdown and wrong-thread tests.
- Check integer boundaries, binary strings, overload ambiguity, nulls, platform widths and ABI compatibility in a maintained toolchain/platform matrix.
- Publish per-backend capability status and policy requirements. Partial generation remains explicit and records skipped declarations.
- Keep user code untouched; generated artifacts must be reproducible, inspectable, and replaceable. Stabilize model/plugin schemas only once multiple backends exercise them.

Automatic value-codec progress: supported `std::variant` signatures now preserve explicit alternative tags, duplicate types, monostate/optional nil states, nested containers, callback conversion and default-backed views. Allocation-failure tests cover native construction and protected Lua publication. Broader ownership-bearing alternatives, schema policies and other standard-library instantiations remain product work; family-level demo completion is recorded in the coverage audit.

Filesystem demo progress: reusable native-byte path helpers and a temporary-workspace facade now demonstrate binary file I/O, metadata, copied recursive directory snapshots, atomic staged replacement, rename/removal, shared ownership and last-owner cleanup. Native/Lua allocation-failure sweeps and Linux descriptor accounting cover resource unwinding. General path typemaps, arbitrary-root/permission/link/time policies, watching, durability and alternate-platform resource layers remain pending.

Container progress: automatic deque/list/forward-list, ordered/unordered set/multiset and multimap codecs now extend the existing recursive value machinery. Dense sequences preserve order; unique containers reject converted duplicates; multi-containers preserve multiplicity; boxes retain nil-capable set elements. Native/Lua allocation sweeps, callback/default-view tests and generator policy diagnostics cover each family. General automatic container-adaptor codecs, custom policy transport, node operations, live iterators and ownership-bearing elements remain pending.

Name-resolution progress: namespace aliases, explicit using-type imports, aliased template primaries and nested record-alias paths now retain canonical native identity without duplicate exports. Namespace/class lookup timing and imported-overload default safety have compiler fixtures; Lua examples exercise imported enums, class/readonly handles, strings, vectors and defaults. General using-directive/inherited/dependent lookup, imported-call export policies and cross-module identity remain pending.

Template/import and adaptor progress: explicit using imports of supported standard class templates now retain canonical primary identity and lexical argument lookup. A reusable native Worklist facade demonstrates stack, queue and min/max priority-queue behavior, copied extraction-order snapshots, clone/share lifetimes and defined pop/publication effects, with native/Lua failure and shutdown tests. General alias-template expansion, automatic adaptor codecs, custom comparators/allocators and concurrent queue scheduling remain pending.

Synchronization progress: native TLS get/set/probe, checked sequentially consistent atomic/CAS operations, bounded joined worker groups and mutex-guarded volatile storage now have reusable facades and executable Lua demonstrations. Native overlap/CAS-contention tests, partial thread-launch and worker-error allocation sweeps, publication failures and shutdown checks verify their lifetime/error contract. General memory-order/hardware typemaps, barriers/semaphores, executors and concurrent target-runtime entry remain pending.

Dependency/tooling progress: optional same-invocation Clang dependency capture now populates shared IR metadata and `--depfile` Make/Ninja output, with policy/database/source prerequisites and discovered-input overwrite protection. An installed CMake helper is used by the examples and handles tested Ninja escaping compatibility. Real incremental builds verify transitive/forced/context changes and no-op convergence. Module/PCH/VFS scanning, symlink retargeting, response-file expansion and versioned replay manifests remain pending.

Member-pointer progress: reusable typed selector/target facades now demonstrate integer/string data members, multiple-inheritance base adjustment, const/ref-qualified member-function pointers, virtual dispatch, native null data/method pointers, copied selectors and shared/readonly target lifetimes. Lua success/error tests, construction/copy/text allocation sweeps and VM-shutdown checks cover their cleanup contract. General member-pointer declarator modeling, automatic descriptor policies and bound-method callback transport remain pending.

ABI progress: a versioned C11 interface now fronts a native C++ library compiled with exceptions and RTTI disabled. Private compiler settings, opaque handles, allocator ownership, fixed-width checked values, binary byte lengths, status translation and a nonthrowing callback trampoline demonstrate the boundary. Static and dynamically loaded variants, symbol visibility, version/size rejection, partial allocation failures and Lua shutdown are executable tests. This remains an example shim; automatic C-shim generation, other calling conventions, cross-target validation and a second backend remain pending.

Modules/PCH progress: a named C++20 module, exported template partition, separate implementation and imported header unit now feed a normal C++ facade bound to Lua. An independent CMake PCH-backed facade supplies a checked operation. Real build-graph tests exercise no-op builds, partition/header-unit/PCH edits, private export boundaries and the explicit disabled capability; Lua and allocation-failure tests exercise results, binary strings, exceptions and RAII. Automatic module/PCH discovery, arbitrary graph/target replay and distributable compiler-compatible BMI manifests remain pending.

Declaration/storage progress: native facades now demonstrate an immovable object with shared stable identity, const storage/mutable inspections/static atomic data, local and unnamed types, anonymous-namespace/internal-linkage adaptation, `auto`/`decltype(auto)`, designated aggregate initialization, storage attributes and real multidimensional C arrays. Separate temporary arrays satisfy a native restricted-pointer operation's no-alias contract. Lua lifecycle/error tests and embedded allocation/publication/shutdown checks cover these mechanisms; general discovery/direction/restrict/static-variable policies remain pending.

Callable-result progress: native factories now return captured `std::function` values, shared-target bound members, free-function pointers and empty callables through an owned NativeCallable facade. Copy/share/clear operations preserve native callable semantics; a retained Lua callback variant demonstrates reentrant invocation and safe self-clear. A supplied Lua layer adds call/close protocols. Runtime errors, copy/creation/publication allocation sweeps and shutdown check capture/target/slot cleanup. General function-result codecs and move-only erasure remain product work. Copied class-valued callback transport is demonstrated by the frame adapter below.

Class-callback progress: a native `std::function<Record(Record)>` API now crosses a synchronous frame facade using copied owned record values and a supplied Lua `callback(record) -> record` protocol. Nested invocations restore their previous frame/result, helper arguments expire on exit, explicit copies can be retained, and results have independent ownership. Error/yield/readonly/reentrant-close tests, native strong-copy/depth tests, allocation sweeps and shutdown verify the contract. Automatic class-valued callback codecs and general borrowed/consuming policies remain pending.

Awaitable progress: a custom native C++20 co_await operation now schedules through an explicit owner-thread FIFO executor, with weak ready entries, shared task/driver ownership, suspended cancellation, stored completion/error results and prompt frame/callback release. A bounded Lua pumping layer demonstrates host-loop integration without implicit workers or cross-thread Lua entry. Runtime state/lifetime/error tests, wrong-thread rejection, scheduling under allocation exhaustion, native/Lua fault sweeps and shutdown cover the contract. General awaitable discovery, concurrent readiness/I/O integration and scheduler-policy generation remain pending.

Cross-module progress: independently generated interop_a/interop_b modules now share native object identity through a common native library and versioned weak-registry tickets. Import acquires independent shared ownership; live mutation, source close/GC/VM shutdown, readonly export restrictions, invalid/expired ticket rejection and module-table reload are executable tests in both loading orders. The embedded host sweeps registry/creation/import/publication failures and checks shutdown balances. General automatic shared userdata ABI/type registration remains pending; this is an explicit reusable adapter.

C# now has an initial compiled backend: a typed .NET API, shared native C ABI runtime, exact scalar/byte transport, policy-selected IDisposable/SafeHandle owners, properties, defaults, exception translation, namespace selection, CMake/CLI/dependency integration and Nix-backed runnable README examples. Runtime verification targets .NET 10 on Linux. Named enum bindings now preserve compiler-evaluated constants and expose typed parameters, results, constructors and properties. Signed and unsigned 64-bit endpoints, aliases, implicit increments, bool-backed native enums, native defaults, readonly fields, dependency enums and invalid input/result recovery have executable coverage in `tests/csharp_enums.hpp`, `tests/TestCSharp.cs` and the capability suite. Enum inputs and outputs require declared values; opaque/empty definitions, extended underlying integers and enum flag policies remain unsupported. Container/callback/class-value/inheritance mappings and Windows support remain future work.


## Fortran verification

Fortran now has a compiled Fortran 2018 backend over the shared private C ABI. `just fortran-test` builds the generated module sources and executes the native-only demonstration/verification facades with GNU Fortran. This initial backend covers scalar and explicit-close owned-handle APIs; it does not claim Lua feature parity.

| Inventory family | Executable evidence now | Remaining Fortran work |
| --- | --- | --- |
| Translation units/preprocessing | Shared compiler/database tests and two-output Fortran dependency regeneration | Fortran-specific module/PCH replay and cross-compilation |
| Names/identity/linkage | Flattened names, case-insensitive collision checks, 63-character limits, separate modules and C linkage | Nested/shared type identities across generated modules |
| Fundamental types | Signed int64 endpoints, checked unsigned ranges, bool, float/double, finite-number checks | Full unsigned 64-bit values, extended numbers and Unicode scalar types |
| Aliases/qualifiers | Scalar aliases, copied const-string inputs, const methods and readonly fields | General reference/volatile qualifiers |
| Enums/constants | Direct enum/variable capability rejection; constant-expression facade | Typed enums/flags and constant exports |
| Free functions | Typed positional/keyword arguments, native defaults, void subroutines and native exceptions | Overload selection and varargs |
| Pointers/references | Private registry IDs, closed/uninitialized receiver rejection and shared assignment aliases | Borrowed/class-valued inputs/results and buffers |
| Arrays/strings | Explicit byte lengths, empty/long/binary/UTF-8 strings, embedded NULs and trailing blanks | Arrays, spans/views, encoding policies and recursive codecs |
| Records/storage | Mutable/readonly/string field accessors and native pimpl facades | Aggregates, unions, bitfields and layout adapters |
| Construction/destruction | Explicit init/close, repeated close, failure-preserving reinit, module shutdown and process-exit destruction | Automatic ownership scopes, factories and copy/move policies |
| Members/access | Instance/static methods, access policies, private rejection and accessor-name collision checks | Ref-qualified members and static data |
| Inheritance/polymorphism | Native hierarchy/crosscast facade; direct base/abstract class guards | Native hierarchy identity and reverse overrides |
| Operators/conversions | Native Series get/set/scale/text methods | Fortran operator/assignment adapters and iteration |
| Templates/concepts | Native maximum/Box/variadic/constrained-sum instantiations | Automatic concrete instantiation policies |
| Standard library | Four native worklist orders and empty-state errors | Recursive containers, optional/variant/products, time/filesystem |
| Callbacks/reverse bindings | Function-pointer/std::function and callback-policy rejection | Callback ownership, reentrancy, thread entry and reverse calls |
| Errors | Optional error status, typed native codes/messages, fatal no-status path and repeated recovery | Allocation-fault injection, status/expected/nested-error adapters |
| Compile-time C++ | Native constant/concept facades; immediate-function rejection | Broader compiler-negative fixtures |
| Concurrency/async | Native wait/result/failure/cancel/close and shutdown/exit worker joins | Fortran async protocols and concurrent handle access |
| ABI/platform extensions | `ISO_C_BINDING` structs/procedures and linked native shared libraries | Compiler/data-model/platform matrix and disabled-exception facades |
| Documentation/tooling | CLI, CMake helper, Nix commands, compiled example and runnable README runner | Installed/package distribution and capability manifests |

Evidence: `src/fortran.cpp`, `src/fortran_runtime.hpp`, `tests/test_fortran.f90`, `tests/fortran_values.hpp`, `tests/test_fortran_capabilities.cpp`, `tests/test_dependencies.cpp` and `cmake/FortranExample.cmake`. Handles require explicit close: intrinsic assignment aliases the native ID, closing an alias invalidates all copies, and scope exit does not release an owner. Fortran integer transport is signed 64-bit; unsigned values outside that representable range are rejected. Errors use optional per-module `rosetta_error` status objects, or `error stop` when omitted. Capability tests cover 26 strict/partial rejection cases, policies/access, defaults, deterministic output, name limits/collisions, line wrapping, CLI artifacts and input protection.
