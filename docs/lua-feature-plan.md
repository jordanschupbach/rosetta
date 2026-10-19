# Lua feature coverage

The Lua demonstration goal is complete: every C++ feature family in the roadmap has an executable direct binding or an explicit facade/protocol mechanism, with its lifecycle, error behavior and limitations documented and tested. The [completion audit](lua-coverage-audit.md) links the implementations and evidence. The broader Rosetta roadmap remains the inventory of language features, tooling, and future backends; a representative example does not mean all combinations of that feature are supported.

## Contract

- **Automatic**: Rosetta discovers the C++ declaration and generates the binding.
- **Facade**: an ordinary, explicit C++ wrapper selects an instantiation or adapts an interface; Rosetta generates bindings for the wrapper. Facades must not depend on Lua so other backends can reuse them.
- **Lua protocol**: a generated or supplied Lua layer provides the target-language behavior, such as `__add` or `__close`, over native operations.
- **Policy required**: ownership, callback retention, encodings, thread entry, and other facts absent from a signature need explicit configuration. Reject unresolved policies rather than guess.

All four mechanisms need executable success/failure tests. No checklist item is finished merely because a wrapper can hide its C++ implementation. Its lifecycle and error behavior must also be exposed and demonstrated from Lua.

## Feature matrix

| C++ family | Demonstrated mechanism | Implemented scope and remaining product work |
| --- | --- | --- |
| Scalars, free functions, namespaces, C linkage | Native calls, strict conversions, range errors and post-error recovery | Automatic scalar functions plus explicit UTF/wide characters, exact u64/128-bit decimal and finite long-double adapters implemented; general extended typemaps and name/linkage gaps pending |
| Aliases and type identity | Resolve aliases in parameters/results and their defining scopes; preserve public spelling and declaration identity | Scalar/enum/value/record aliases, namespace aliases, explicit type imports, function signatures, class members/access/noexcept metadata, nested template arguments, cv qualifiers and pointer/reference children implemented; explicit native-ticket exchange demonstrates identity/ownership across independent modules; using-directive/inherited lookup, dependent and complex declarator types pending |
| Enums | Scoped/unscoped tables, aliases, evaluated expressions, negative values and integer limits; checked enum parameters | Automatic namespace/global/public nested enums, recursive value/callback codecs and explicit flag bases implemented; anonymous/template-specific declarations, general selection and boxed transport pending |
| Constants and preprocessing | Export constant expressions; facade for function-like macros and conditional APIs | Explicit constexpr/constinit, object-like/function-like macro and conditional feature facades implemented; automatic constant selection and general preprocessing configuration pending |
| Overloads/defaults/variadics | Explicitly named facades first, then generated dispatch with ambiguity diagnostics; typed adapters for C varargs | Automatic trailing-default calls and metadata for supported non-overloaded functions/members/constructors; named overload adapters, typed 0–3 argument C varargs, initializer-list adaptation and supplied Lua dispatch implemented; general overload generation pending |
| Templates | Function/class/variadic concrete instantiations, then non-type/alias/template-template parameters, specialization and constraints | Concrete facades demonstrate function/class/variadic templates, non-type/alias/template-template/variable templates, full/partial specialization, CTAD, dependent names, concepts/requires and SFINAE; automatic selection/instantiation pending |
| Strings/arrays/buffers | Binary strings, fixed arrays, pointer/length views and mutation; explicit encodings | Automatic byte/value codecs, checked buffer/span facades and explicit UTF-8/16/32, char8_t/wchar_t adapters implemented; general direction/encoding policies pending |
| Classes/members | Constructor, methods, fields through accessors, static state, nested types, const access and access control | Automatic selected public classes (including nested types), methods/fields/readonly/access checks and out-of-class definition merging; explicit overloaded/ref-qualified/friend adapters implemented; automatic overload handling and template-specific discovery pending |
| Ownership/RAII | Owned userdata with GC/close, copy/move, borrowed children, invalidation, smart pointers, destructor counts | Automatic handles/borrows/unique/shared plus explicit consuming slots, shared promotion, stateful custom deleters, aligned placement allocation, weak locking and immovable shared-identity facades implemented; generic transfer/deleter/weak typemaps pending |
| Pointers/references | Opaque handles, null checks, out/in-out parameters, lifetime-checked borrows | Automatic selected-class inputs/borrows and explicit pointer-length, pointer-to-pointer output, scalar in-out, checked span/view and typed data/member-function selectors implemented; automatic direction inference and arbitrary pointer arithmetic pending |
| Inheritance/virtuals | Base conversion, virtual dispatch, multiple/virtual inheritance via C++ accessors, safe downcasts and reverse overrides | Shared-view facades cover an abstract virtual diamond, checked down/crosscasts, shared virtual-base identity, virtual calls and a retained Lua virtual override; automatic hierarchy/director generation pending |
| Operators/conversions | Named native operations plus Lua arithmetic/comparison/index/call/iteration protocols | Named series adapters and Lua protocols cover checked arithmetic, truncating/floor division and remainder, bitwise/comparison/index/call/conversion/iteration; C++20 facades add a selected literal and three-way comparison. Automatic operator selection pending |
| Records/unions/layout | Field/bitfield accessors, tagged union adapters, packed records kept behind handles | Explicit facades cover union lifetimes/cloning, bitfields, packed/aligned storage, local/anonymous types, designated aggregates and no-unique-address traits; automatic layout/union adapters pending |
| Standard library | Vector/map/tuple/optional/variant/any access, custom ownership and mutation rules | Automatic strings/vector/deque/list/forward_list/array/optional/tuple/pair, ordered/unordered sets/multisets/maps/multimaps, variant/monostate and selected-class unique/shared ownership; explicit facades add variant/any stores, weak ownership, checked chrono durations/time points and owned filesystem workspaces. Stack/queue/priority-queue facades implemented; custom policies and ownership-bearing elements pending |
| Callbacks | Lua closure invoked by native code; protected error translation, reentrancy, retention, shutdown, wrong-thread rejection | Policy-controlled call-only/retained `std::function` inputs, copied value arguments/results, protected invocation, coroutine retention, self-unregistration, active-owner pins and off-thread release implemented; returned/bound callable handles, class-value frame callbacks and explicit reverse virtual overrides demonstrated; direct C pointer/codecs and automatic director generation remain product work |
| Errors | Exceptions, unknown throws, typed/status/expected-style adapters, callback errors and allocation failures | Automatic exception boundaries plus explicit expected-style result facade with typed/domain/system/status/error-code failures, nested causes, callback capture and repeatable rethrow implemented; generic generated mappings and C++23 std::expected pending |
| Compile-time C++ | Runtime-accessible results of constexpr/consteval, traits, deduction and constrained instantiation | C++20 native facades expose constexpr/consteval/constinit results, traits, deduction and constrained instantiations; compiler tests reject invalid immediate calls and constraints. General generated constant evaluation/reflection pending |
| Async/concurrency | Future/worker operations through poll/wait/cancel handles; coroutine results; Lua callbacks dispatched on the owning thread | Worker/future tasks, C++20 pull generators and an atomic/mutex/volatile/TLS state facade demonstrate cancellation, cleanup, owner-thread dispatch, coroutine interop and joined native contention; a custom co_await operation, owner-thread FIFO executor and bounded pumping helpers are implemented; arbitrary scheduler/I/O integration remains product work |
| ABI/platforms | Compiler-specific and nonportable cases behind C++ facades and explicit capabilities | Linux baseline, aligned storage, wchar/numeric capabilities and versioned C shim with exception/RTTI-disabled native static/shared libraries tested; broader platform validation pending |
| Tooling/modules/documentation | Selection/policy manifests, dependency tracking, compilation database and modules integration, per-feature examples | CMake/CTest/just/REPL, strict class/lifetime policies, compilation-database selection, compiler depfiles and an installed CMake generation helper implemented; explicit native module/partition/header-unit and PCH facades implemented; automatic module/PCH discovery, manifests and richer documentation pending |

This matrix includes every feature family in `roadmap.md`. Each family now has its required demonstration and executable success/error/lifecycle evidence. The detailed inventory there remains authoritative for individual subfeatures and broader product milestones. Universal automatic C++ coverage, every feature combination, additional language backends and a full platform matrix remain open product work.

The [detailed coverage audit](lua-coverage-audit.md) maps roadmap inventory families to native implementations and executable evidence, closes the demo mechanisms, and records general automatic-generation and platform work separately.

## Completed demo acceptance gates

1. **Initial type foundation and enums (demo mechanism verified).** Canonical resolution and source locations do not leak Clang nodes into the model. Named enums export constant tables and reject invalid values before casting. Template facades demonstrate selected instantiations. Enum/alias/compiler fixtures and Lua runtime checks pass. A complete structured type graph remains part of the broader foundation work.
2. **Call planning and value conversion (demo mechanism verified).** Conversions and native calls use C++ exceptions for cleanup, and result publication runs through `lua_pcall` in a thunk with no owned C++ values. Values, nested errors and injected Lua allocation failures are tested. Named call adapters and supplied Lua rules now demonstrate overload/default/varargs selection and ambiguity; trailing-default metadata/calls are generated automatically; general automatic overload metadata/dispatch and typemap planning remain product work. Native allocations are counted across conversion/native/result failures.
3. **Class and ownership policies (demo mechanism verified).** Versioned policies select classes/members, constructor exposure and anchored borrows. Constructors, methods, field accessors, close/GC/`__close`, readonly views and closed-owner checks work. Copies, moves, unique/shared factories, explicit consuming slots, custom deleters, aligned storage and weak locking have executable demos with destructor/allocation, failure, shutdown and sanitizer checks. Automatic overloads/template-specific nested discovery, general transfer/deleter/weak typemaps and arbitrary native invalidation remain open.
4. **Callbacks and reverse calls (demo mechanism verified).** Explicit `call`/`retained` policies bind `std::function` inputs. Lua argument publication and execution run under protected calls; result conversion and error propagation use C++ cleanup. Registry ownership retains closures and originating coroutines, call-only scopes revoke stale callbacks, queued off-thread releases drain on the owner thread, and VM shutdown disables remaining native copies. Receiver/argument owner chains are pinned against close during native calls. Tests cover reentrancy, self-unregistration, error/yield/return-type failures, thread rejection, GC release, registration/allocator failures and post-shutdown native invocation. Returned native callable objects now have an owned facade and Lua call protocol. A synchronous frame adapter now demonstrates copied class-valued callback arguments/results. Raw C function-pointer generation, automatic class-valued callback codecs, automatic virtual directors and arbitrary concurrent dispatch remain pending.
5. **Object protocols and polymorphism (demo mechanism verified).** Shared-view C++ facades demonstrate a virtual diamond, abstract interfaces, checked down/crosscasts, virtual-base identity, virtual dispatch and a Lua-backed virtual override. A native series plus supplied Lua layer demonstrates arithmetic/bitwise/comparison/index/call/string protocols and fail-fast native iteration. C++20 facades add a selected literal and three-way comparison. Tests cover invalid casts, independent close/GC, callback errors, reentrant close, mutation, overflow, allocation failure and shutdown. Automatic hierarchy/director/operator generation and additional protocol mappings remain product work.
6. **Advanced value and compile-time adapters (demo mechanism verified).** Tagged-record/store facades cover unions, bitfields, packed storage, maps, variants, any and tuples. C++20 facades add specializations/constraints, constants and immediate evaluation. Buffer facades add pointer/span access, weak views, mutation invalidation, pointer-to-pointer output and scalar in-out adaptation. Tests cover types/bounds, binary values, snapshots, copies, failure preservation, compiler rejection, GC and shutdown. The named container families now have automatic codecs or explicit Worklist facades. Other standard-library instantiations, general direction/encoding policies and automatic instantiation/constant-evaluation generation remain product work.
7. **Concurrency and async adapters (demo mechanism verified).** A worker/promise/shared-future facade supports polling, bounded wait, result/error retrieval, cancellation and owner-thread at-most-once dispatch. A native C++20 generator adds lazy yields, repeatable value access, retained exceptions, early cancellation and frame cleanup, with a supplied Lua iterator and coroutine-close interop. Tests cover races, errors, reentrancy, wrong-thread task dispatch, partial allocation, publication retry, GC and shutdown. A custom co_await operation and owner-thread FIFO executor now demonstrate bounded cooperative pumping, cancellation and frame cleanup. Arbitrary awaiter discovery and concurrent/I/O event-loop integration remain pending.
8. **Coverage audit (verified).** All 21 roadmap families map to implementations, tests and explicit limitations in the [audit](lua-coverage-audit.md). Returned callables, class-valued callbacks, awaitable/executor integration and independent-module identity exchange have executable ownership/error/shutdown checks. Remaining individual subfeatures and unsupported combinations retain concrete product tasks or capability limitations. Full universal automatic C++ support and additional language backends remain broader product milestones.

## Current executable examples

Run `just demo` for the assertions or `just lua-repl` to explore interactively:

```lua
demo.demo__apply(demo.demo__Operation.multiply, 6, 7) -- 42
demo.demo__operation_identity(demo.demo__Operation.add)
demo.demo__Boundary.minimum == math.mininteger
demo.demo__template_max_int(20, 42) -- selected function template
demo.demo__template_box_int(42) -- selected class template facade
demo.demo__template_sum_three(10, 20, 12) -- parameter-pack facade
demo.demo__echo("a\0b") -- binary-safe copied string
demo.demo__reverse_words({"one", "two"}) -- copied vector of strings
demo.demo__matrix_identity({{1, 2}, {}, {42}}) -- nested values
demo.demo__fixed_three({1, 2, 3}) -- fixed-size array
demo.demo__optional_text(nil) -- empty optional
local c <close> = demo.demo__Counter.new(20, "example")
c:add(22); c:value() -- 42
local child <close> = c:point() -- native member, anchored to c
child:set_x(20); child:set_y(22); child:sum() -- 42
local view <close> = c:readonly() -- rejects mutating methods/accessors
local unique <close> = demo.demo__make_counter(42)
local shared <close> = demo.demo__make_shared_counter(42)
local other <close> = demo.demo__share_counter(shared)
shared:close(); other:value() -- shared ownership keeps the native object alive
demo.demo__invoke_callback(function(v) return v + 2 end, 40) -- synchronous closure
local events <close> = demo.demo__EventSource.new()
events:set_callback(function(v) return v + 2 end) -- explicitly retained
events:emit(40) -- 42
events:clear_callback() -- release retained Lua references
local entity <close> = demo.demo__make_diamond("diamond", 42)
local named <close> = entity:as_named()
local value <close> = named:as_value() -- checked crosscast
value:value() -- virtual call, returns 42
local scripted <close> = demo.demo__make_scripted("Lua override", 40, function(v) return v + 2 end)
local virtual <close> = scripted:as_value()
virtual:value() -- C++ virtual override calls the retained Lua closure
-- Series is preloaded by the REPL's supplied Lua protocol layer:
local s <close> = Series({20, 21})
local doubled <close> = s * 2
doubled[2] -- 42
for index, value in pairs(doubled) do print(index, value) end
local task <close> = demo.demo__AsyncTask.new(21, 0, false)
task:wait(2000); task:result() -- 42, no callbacks invoked by wait
task:on_complete(function(state, value) print(state, value) end)
task:dispatch() -- explicit delivery on the Lua thread
local record <close> = demo.demo__TaggedRecord.new()
record:set_text("a\0b"); record:text() -- binary string in a native union
record:set_integer(42) -- destroys the previous active union member
local store <close> = demo.demo__ValueStore.new()
store:put_integer("answer", record:integer()) -- native map of variants
store:scratch_pair(42, "tuple inside any")
store:pair_integer() -- 42
demo.demo__immediate_answer() -- selected consteval result, 42
demo.demo__template_pattern(40) -- non-type template parameter N=3
demo.demo__constrained_sum({10, 20, 12}) -- selected constrained template
demo.demo__compare_ints(7, 2) -- C++20 three-way comparison, returns 1
local buffer <close> = demo.demo__ByteBuffer.new("a\0b")
local view <close> = buffer:slice(1, 2, false) -- zero-based byte offset
view:write(0, "42"); buffer:read() -- "a42"
demo.demo__decode_u16("\42\0") -- pointer/length + out parameter, returns 42
demo.demo__out_buffer(3, 255) -- owned pointer-to-pointer output, copied to string
```

Enum tables contain Lua integers and can be modified by Lua code. Parameter validation uses the original C++ declarations and explicit policies; changing the table cannot change what the native wrapper accepts. Unconfigured enums accept only named values. The flag policy below accepts combinations of selected bits. Enum constants outside Lua's integer range fail module loading rather than silently lose precision. Bool-backed enums also use integer 0/1 for constants, parameters and results.

### Explicit flag policies

An enum is not inferred to be a bitmask from its values or name. Add its canonical qualified identity and an explicit basis to the version-1 policy:

```json
{
  "version": 1,
  "enums": {
    "demo::Feature": { "flags": ["classes", "templates", "callbacks"] }
  }
}
```

This section composes with existing class and callback policies and does not select classes or export dependency enums. Basis arrays must be nonempty and name distinct existing enumerators; enum names use the model's canonical qualified identity, not a typedef alias. Unknown enums, incomplete definitions, unknown basis names and unsupported policy keys fail generation, including in partial-output mode.

The native binding compiler evaluates basis constants and requires each to be positive, single-bit and representable in Lua's integer type. Zero, negative values (including sign-bit-only values), composites and oversized bits receive compile-time diagnostics. A named composite can remain in the enum declaration/table but should not be listed as a basis term. The accepted mask is the OR of the named basis constants; aliases of the same bit do not expand it. This avoids interpreting a negative all-bits sentinel as permission for arbitrary values. Signed sign-bit flags need a separate explicit bit-pattern/encoding adapter; this policy does not yet supply one.

Parameters are checked against the native underlying integer range before validating their unsigned bit pattern against that mask. Zero is accepted as the empty set even when unnamed. Any bit outside the mask fails before the C++ enum cast. Named constants excluded from the basis remain visible in the exported table but are rejected as inputs when they contain excluded bits. Enum result publication keeps the existing integer-range behavior; this policy does not silently strip native result bits or validate return membership.

```lua
local Feature = demo.demo__Feature
local enabled = demo.demo__combine_features(Feature.classes, Feature.templates)
assert(enabled == 3 and demo.demo__has_features(enabled, Feature.templates))
assert(demo.demo__feature_identity(Feature.classes | Feature.callbacks) == 5)
```

`tests/test_enum_flags.lua` checks every bit pattern for the selected unsigned/signed byte examples, aliases, excluded named bits, unnamed zero, wide exact integers, bool-backed enums, strict Lua inputs and unchanged closed-enum rules. The native compile test builds generated source as C++17 with warnings as errors and verifies zero/composite/negative/oversized basis diagnostics. Generator fixtures check schema/reference errors and preservation of class-selection behavior. The embedded host checks cleanup when unknown bits follow an owned string conversion and when publishing valid flag-derived string results fails. Recursive enum products/callbacks are implemented below; anonymous/template-specific declaration discovery, renaming/general selection and boxed out-of-range enum transport remain pending.

### Enums in value codecs and callbacks

Enum arguments/results compose with automatic vector, array, optional, tuple and pair codecs. Their representation remains Lua integers, including bool-backed enums and exact wide flag values. The emitter collects enum types recursively from call and callback signatures and emits module-local codec specializations that reuse the direct enum validator. No per-container extraction facade is required. Dependency enum definitions supply validators without exporting extra dependency tables.

```lua
local Operation = demo.demo__Operation
local values = demo.demo__operation_vector({Operation.add, Operation.multiply})
assert(values[2] == Operation.multiply)
assert(demo.demo__enum_callback(function(_) return Operation.multiply end, Operation.add) == Operation.multiply)
```

Lua-to-C++ enum reads validate range and named values or the explicit flag mask at every nested position, including callback results. Optional enum slots accept nil under their enclosing codec's normal rules. C++-to-Lua enum publication preserves native values with integer-range checks; it does not strip unknown bits or enforce input membership on native output/callback arguments. Thus a native result can be inspected even if it would fail validation when passed back to C++.

A definition with named enumerators is required anywhere Lua values must be decoded into an enum. Incomplete enum parameters, including nested inputs and callback results, receive generation diagnostics. A fixed-underlying forward declaration can still be used on native output paths, including native-to-Lua callback arguments: its codec publishes the known integer representation, while its read entry point is disabled. Compiler fixtures exercise this distinction, as well as dependency definitions and specialization placement. Public nested enum/class discovery is described below; anonymous and template-specific declarations remain separate frontend work.

`examples/enum_values.cpp` supplies normal native APIs over these standard types. `tests/test_enum_values.lua` checks flag/closed-enum vectors, arrays, optional/product fields, bool/wide transport, copied enum callbacks, invalid callback results, unknown native output values and error recovery. The embedded host verifies later-element cleanup after owned strings, native exceptions, late out-of-range enum publication, partial native allocation and partial Lua/callback publication. Existing callback lifetime, thread and yield restrictions still apply.

## Value conversion rules

`std::string` and `std::string_view` use raw bytes and preserve embedded NULs. Strings own copies; input views borrow the Lua string only for the native call and must not be retained. Returned views and C strings must remain valid until the wrapper copies them into Lua. These automatic byte mappings perform no encoding conversion; the explicit Unicode adapters below validate selected encodings. Const-reference scalar/sequence inputs are backed by temporary native values and must not escape; their reference results are copied. Selected-class references instead borrow the existing native object under the class policies below. Mutable scalar/value references and rvalue-reference arguments are rejected until an explicit mutation/transfer policy exists.

Supported vector/array elements include scalars, enums, strings/views and supported nested value types. Inputs must be dense tables with exactly integer keys `1..n`; holes and extra keys are errors. `__index`, `__len`, and `__pairs` are bypassed. The fixed extent of `std::array` must match. Native calls receive copies and never mutate the original Lua table through these adapters. `std::optional<T>` maps nil to empty and non-nil to a supported copied value. Nested optionals and optional sequence elements are rejected because nil would make the representation ambiguous. Custom allocators/traits, direct wide-string codecs, other standard-library instantiations and general template types need explicit adapters. The Unicode facades demonstrate wide-string operations; the container/value sections describe the additional automatic codecs.

`tests/test_lua_values.cpp` links the generated bindings into an embedded Lua VM, counts native string/vector allocations independently of Lua, and repeatedly forces Lua result allocation failure. It also exercises later-argument conversion errors after a string was constructed, exceptions from native code, integer result overflow, and partial vector conversion. Every call must release its native allocations and leave the Lua state usable.

## Compiler dependencies and incremental builds

`--depfile PATH` requests compiler-discovered dependencies from the same Clang invocation that produces the declaration AST. The frontend adds a private `-MD` capture with a fixed target, then resolves/deduplicates the reported regular-file inputs. Active transitive and forced includes, including system headers, are recorded; inactive preprocessor branches are not guessed. The shared model and IR expose these compiler inputs as `dependencies`, empty when collection was not requested. Parse-context flags retain the user's semantic options without temporary paths or internal capture flags. `parse(..., directory, collect_dependencies=true)` makes the same metadata available to other backends.

The CLI emits an absolute-target Make/Ninja rule for the generated source. Besides compiler inputs it includes the binding policy and, when selected, the compilation database and translation-unit source. Paths are sorted/deduplicated and spaces/hash/dollar characters are escaped. Compiler-relative names resolve against the selected parse working directory and supported direct `-working-directory` overrides, independently of where outputs are written. Forwarded working-directory forms are rejected during collection. Canonical file identity also prevents generated source, IR or depfile outputs from overwriting discovered headers, including through hard links; existing input/policy/database path guards still apply. Parsing, generation, protected-path and depfile-format failures leave previous managed outputs unchanged. This is not a filesystem-wide transaction across unrelated output write failures.

```sh
rosetta api.hpp --module api -o build/api.cpp \
  --dump-ir build/api.json --depfile build/api.d \
  --policy api.policy.json -- -std=c++20
```

`cmake/Rosetta.cmake` provides `rosetta_generate_lua`. It is used by both repository examples and installed under `share/rosetta/cmake/Rosetta.cmake`. The function generates source/optional IR; the caller owns its native module target and runtime linkage.

```cmake
include(path/to/Rosetta.cmake)
rosetta_generate_lua(
    HEADER "${CMAKE_CURRENT_SOURCE_DIR}/api.hpp"
    MODULE api OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/api.cpp"
    IR "${CMAKE_CURRENT_BINARY_DIR}/generated/api.json"
    POLICY "${CMAKE_CURRENT_SOURCE_DIR}/api.policy.json"
    FLAGS -std=c++20)
```

`EXECUTABLE` selects a Rosetta executable/target, defaulting to the project's `rosetta` target or a discovered installed program. `CLANG` selects the compiler adapter. `COMPILE_COMMANDS`, `TRANSLATION_UNIT` and `COMMAND_INDEX` select the existing compilation-database context; index zero is preserved. `DEPFILE` overrides its default `${OUTPUT}.d`. Header/policy/unit paths are relative to the current source directory; generated outputs and the database path are relative to the current binary directory. The helper passes literal argument lists, tracks the generator/compiler/policy/context files and wires its depfile into CMake.

Some installed CMake versions decode hash/dollar escapes but lose them while rewriting Ninja depfiles. The helper probes the installed transformation rather than inferring behavior from a version number. If escaping is lost, a function-local `CMP0116` compatibility setting permits direct Ninja consumption of Rosetta's absolute paths; callers retain their own policy settings. The integration test uses the actual helper and exercises filenames containing spaces, hash, dollar and colon characters, including a no-op build after regeneration. Other generators use their standard CMake depfile support; the development regression gate uses Ninja when it is available.

Collection reserves direct compiler dependency-output flags such as `-MD`, `-MF`, `-MT` and `-MP`, rejecting conflicts instead of mixing output rules. Depfile paths cannot represent control characters or literal backslashes through the supported consumers; output targets containing a colon are rejected. Reported inputs must be native regular files. General virtual-filesystem, symlink-retarget detection, PCH/module dependency scanning, response-file expansion and replay manifests remain separate work.

`tests/test_dependencies.cpp` verifies real compiler discovery, forced/inactive includes, relative database working directories, IR metadata, escaped-path round trips, malformed/conflicting output options, input/hard-link protection and failure preservation. A separate CMake/Ninja fixture proves that an included-header edit changes the regenerated canonical signature, a newly introduced leaf header becomes a tracked dependency, and forced-include/policy/database edits regenerate while unchanged builds stay idle. The installed CLI/helper also run this fixture. Generator/runtime sanitizer checks cover the changed core and generated modules.

## Namespace aliases and imported names

Namespace aliases and explicit `using source::Type` imports now resolve to the original declaration identity. Aliased/qualified template primaries retain lexical lookup for their arguments: `::std::vector<Count>` selects global `std::vector` while resolving `Count` in the declaring scope. Chained/nested namespace aliases, inline-namespace imports and nested types qualified through a record alias/import use the same resolution path. Explicit class-template imports such as `using std::vector`, `using std::optional` and `using std::tuple` preserve the original template primary; their supported concrete instances reuse existing value codecs. Template arguments still resolve in the declaring scope, including through inline or namespace-qualified imports. This does not instantiate arbitrary alias-template patterns or make unknown template families supported. A nearer real namespace takes precedence over an outer alias with the same name.

`examples/demo.hpp` declares namespace aliases to `demo` and `std`, then imports `Count`, `Operation`, `Counter` and `NestedOwner` into `demo_names`. `examples/names.cpp` supplies ordinary native functions through those spellings. In the REPL, `demo.demo_names__imported_count()` returns 42; `demo.demo_names__imported_operation(demo.demo__Operation.multiply)` uses the original enum codec; `imported_counter(counter)` accepts the original class handle, including a readonly borrow; `imported_nested` uses the canonical nested enum. Imported strings/enum vectors retain their normal value and lifetime rules. `using_vector`, `using_optional` and `using_tuple` demonstrate direct imports of the corresponding standard class templates.

Aliases do not create declarations or runtime ownership. Enum/class tables remain under their canonical original names, class policies still select the original qualified class, and userdata keeps one native metatable identity. `using source::function` participates in C++ lookup without creating another exported function. Use an ordinary named facade to deliberately expose an imported callable, as the independent scalar module's `baseline_import::invoke` demonstrates. Dependencies contribute canonical record/enum/alias identities without becoming selected class exports or enum tables. Public source spellings remain in IR alongside canonical types.

Namespace-level signatures resolve at their declaration point, so a later import cannot retroactively change a previously declared return type. Record signatures resolve when the enclosing class context is complete, allowing nested method bodies to use later enclosing-class aliases while excluding later namespace imports. Out-of-class member definitions resolve before canonical redeclaration merging. Default-call safety is checked after discovery: imported function and function-template overloads are included because emitted short calls see the completed header. Ambiguous/unsupported default overload groups continue to require explicit call adapters.

Generator fixtures check chained/scoped aliases, imported scalars/enums/records, aliased standard templates, inline/dependency imports, nested record aliases, IR identity, lexical shadowing, declaration order, complete class contexts and imported overload rejection. `tests/test_names.lua` executes imported defaults/values, enum validation, class/readonly/closed handles and absence of duplicate tables. The embedded host runs it with native allocation accounting and sanitizer coverage. General using-directive lookup, inherited/dependent lookup, alias-template member discovery, imported-call re-export/rename policies, anonymous namespaces and cross-module registration remain pending. This implements selected name-resolution mechanisms rather than full C++ lookup.

## Class policies and lifetime rules

`examples/demo.policy.json` is passed to the demo generator automatically by CMake. For another API:

```sh
build/rosetta api.hpp --module api --policy api.policy.json -o build/api.cpp
```

```json
{
  "version": 1,
  "classes": {
    "api::Counter": {
      "ownership": "owned",
      "methods": ["value", "add", "readonly"],
      "fields": ["label"],
      "returns": {"readonly": "borrow_self"}
    }
  }
}
```

`classes` explicitly selects declarations by their real qualified C++ names; other record declarations are omitted. Without a class policy, record binding is rejected. Every selected class requires a complete definition and a public nonthrowing destructor (also checked by a generated C++ type-trait assertion). `methods` and `fields` are optional allowlists; when omitted, public named nonoperator methods and supported public fields are considered. Unknown names, private members requested explicitly, misspelled keys, duplicate allowlist entries, unsupported policy versions and mismatched return rules are errors. Constructors can be disabled with `"constructors": false`; otherwise one supported non-copy/move constructor or an implicit default constructor is exposed as `Class.new`. Overloaded constructors/methods still require named C++ adapters.

Lua-created objects own their native allocation. By-value class results produce independent owned userdata, using move construction when available or copying when appropriate. By-value inputs require a public copy constructor; move-only inputs need a future consuming policy. `std::unique_ptr<T>` results transfer ownership after Lua allocation succeeds. `std::shared_ptr<T>` results/inputs retain shared ownership; only userdata actually backed by a shared pointer can supply a shared input. Passing nil supplies an empty shared pointer. Borrowed/raw-owned userdata are never implicitly promoted to shared ownership. Unique pointer inputs, custom deleters/allocators, shared pointers to const types, and weak pointers are not supported yet.

Native class pointers/references passed as arguments borrow the existing object for the call, including native mutation through nonconst signatures. Native code must not retain raw pointers/references without a separate lifetime policy. Nullable pointer inputs accept nil. Methods returning a class pointer/reference must opt into `borrow_self`; static/free pointer/reference results are rejected until an argument/global-owner policy is implemented. A borrowed result retains the actual Lua receiver in a userdata user value, forming an owner chain. Owners remain reachable through GC; explicit close of any owner invalidates descendant handles before they can dereference native storage. Closing a borrow never deletes its target. Const results and borrows from readonly owners reject mutating methods, field setters, and mutable native arguments. A policy author must guarantee the returned address stays valid until its owner closes; arbitrary native member replacement/reallocation is not tracked yet.

Owned objects, shared handles, and borrows all provide idempotent `close`, Lua 5.4 `__close`, and `__gc`. Closing an owned/shared handle releases only that handle's ownership; closing another shared handle can leave the native object alive. A borrow anchored to the explicitly closed handle still becomes invalid even if an independent shared owner survives. Userdata carry a module-local type tag, so another selected class or unrelated userdata cannot be cast to the native class. Cross-module type unification and automatic inheritance casts remain pending; the explicit hierarchy adapters below keep casts inside C++.

Supported fields get `get_name`/`set_name` accessors; const fields and fields containing borrowed string/view storage get no setter. Bitfields and unsupported storage need explicit C++ accessor methods. Nested aliases used in public signatures are resolved, while general nested class discovery and operator protocol generation remain future work. Properties do not bypass C++ access control.

`tests/test_objects.lua` checks native state, destructor counts, copy independence, readonly enforcement, cross-class child borrows, GC retention, closed-owner/closed-intermediate checks, shared ownership, wrong-class inputs, and close during Lua error unwinding. The embedded VM runs this script too, then closes the VM while native objects remain reachable to verify shutdown. It also injects failures for owned/unique/shared/move-only publication, borrowed methods, constructors, and C++ allocation after userdata creation; native resources must be released exactly once.

## Public nested declarations

Named public classes/structs and enums inside ordinary records are discovered recursively. Private/protected type paths are excluded even when a deeper descendant is public, including when their definitions appear later outside the class. Public nested enum tables are exported independently of class selection; nested classes require the same explicit owned-class policy as top-level classes. Dependency class bodies can supply public enum/alias metadata without exporting their classes or enum tables.

Lua keeps the existing flattened names. `demo::NestedOwner::Item` becomes `demo.demo__NestedOwner__Item`; its `State` enum becomes `demo.demo__NestedOwner__Item__State`. These are type/class tables, not fields on an owner instance. A nested class has no implicit outer-instance lifetime; construct it independently with `Item.new`, or use an explicitly configured borrow from an owner:

```lua
local Owner, Item = demo.demo__NestedOwner, demo.demo__NestedOwner__Item
local independent <close> = Item.new(41)
assert(independent:shifted() == 42)
local owner <close> = Owner.new(41)
local borrowed <close> = owner:item()
assert(borrowed:value() == 41)
```

The demo policy selects both types and declares `item`/`readonly_item` as `borrow_self`. `NestedOwner::Access` uses the ordinary explicit flag policy, and `Mode`/`Item::State` use closed-enum validation, including nested value/callback codecs. `Item::Tags::Mark` demonstrates a deeper public enum whose containing `Tags` class is not selected. `Hidden::Visible` and `Hidden::Deep::Visible` remain inaccessible and unexported. Readonly borrows, explicit close, owner retention, active callback pins and VM shutdown use existing class semantics.

The frontend retains Clang declaration contexts internally so forward declarations and definitions outside a class resolve to the original scope/access. Canonical member redeclarations merge after alias resolution, preserving declaration access and collecting later visible defaults; `Item::shifted` demonstrates a default introduced on its out-of-class inline definition. The shared model/IR adds an `owner` identity to records and enums. Existing `namespaces` arrays contain lexical qualification components, including enclosing classes for nested declarations; `owner` distinguishes the enclosing record from namespace-only scope. No Clang AST IDs escape into the model.

Generator fixtures cover qualified definitions, private/protected paths, deeper private-owner chains, member redeclarations/defaults, enclosing aliases, dependency enums, child-only class selection and owner IR. `tests/test_nested.lua` executes owned and borrowed nested instances, class enum methods/fields, flag masks, recursive enum codecs, independent/readonly lifetimes, callback close protection, strict errors, overflow and GC. The embedded host sweeps partial parent/child construction, denies borrow publication, checks live/allocation counts and closes a VM with an owner and nested borrow alive. Native object layouts remain inside C++.

Local/anonymous types, nested template instantiation, inherited type lookup and public aliases of inaccessible canonical types need more modelling or explicit accessible facades. This discovery does not infer lifetime relationships merely from lexical nesting, bypass private access or automatically bind class templates.

## Extended numeric adapters

`examples/numeric_library.hpp` and `examples/numerics.cpp` expose selected native numeric operations using decimal strings. This avoids routing large values through Lua numbers or exposing compiler-specific numeric ABI layouts. Ordinary numeric bindings keep their existing strict range checks; these are explicit alternative interfaces, not automatic coercions.

`u64_decimal`/`u64_add` support the full native `uint64_t` range. Signed/unsigned 128-bit identity, addition and multiplication use actual native extended integers when `int128_available()` is true. Parsing validates every decimal digit and checks the sign-specific range before multiplication/addition; formatting handles signed minimum without negating it. Arithmetic checks limits before executing an overflowing operation. Leading zeros normalize away and integer -0 becomes 0; whitespace, plus signs, nondecimal data and numeric Lua values are rejected.

128-bit integers are a [compiler extension](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fint128.html). The adapter checks the compiler/type-size capability and can be disabled with `ROSETTA_DISABLE_INT128=1` when compiling native code. Disabled/unavailable builds retain the named API and produce a capability error. The separate `numeric_capability` test compiles this disabled path and verifies that u64/floating adapters still work; this does not substitute for testing other real platforms.

Finite `long double` operations parse locale-independent decimal text and format scientific text with `max_digits10` precision using standard `from_chars`/`to_chars`. Identity, addition, multiplication, comparison and sign-bit access preserve the native floating type rather than converting through double. `long_double_precision` reports native radix precision, and `long_double_bounds` returns lowest, maximum, minimum-normal and minimum-subnormal values. Tests include negative zero, subnormals and a precision distinction beyond binary64 when available. Arithmetic follows native floating rounding; it is not exact decimal arithmetic. This selected policy rejects malformed/nonfinite input, out-of-range conversions and detected arithmetic overflow.

`tests/test_numerics.lua` checks full-range endpoints, malformed strings, signed-minimum operations, overflow and post-error recovery. An independent Lua digit-cell implementation verifies 256 deterministic addition/multiplication cases without relying on Lua's full-width numeric precision. Native floating limits and canonical formatting are round-tripped. The embedded host sweeps native input/formatting allocation failures and injects Lua metadata-publication failure, checking string/vector cleanup. Generic numeric typemaps, arbitrary-precision values, long-double nonfinite/payload transport, floating-environment policies and broader compiler/ABI validation remain pending.

## Unicode and wide-character adapters

`examples/unicode_library.hpp` implements explicit conversions using native `std::u8string`, `std::u16string`, `std::u32string` and `std::wstring`; `examples/unicode.cpp` exposes selected string/sequence facades. These functions have no Lua dependency and do not change the existing raw-byte codecs. Encoding is selected by the API name, not inferred from `std::string`.

UTF-8 validation follows [Unicode §3.9 and Table 3-7](https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/): reject invalid/truncated continuations, non-shortest forms, surrogate encodings and values above U+10FFFF. UTF-16 rejects unpaired surrogates. Scalar inputs exclude surrogate code points but accept noncharacters. Errors are fail-fast; no replacement characters are inserted.

`unicode_decode`/`unicode_encode` convert UTF-8 bytes to/from copied dense sequences of scalar integers through `char32_t` storage. `unicode_utf16`/`unicode_from_utf16` use integer code units through `char16_t` strings, checking unit range before narrowing. These sequences represent code-unit values, not endian-dependent byte serialization. UTF-8 error offsets are zero-based byte positions. Empty input, embedded U+0000, supplementary scalars and U+FEFF as ordinary data are preserved.

`unicode_char8` validates and round-trips through an actual C++20 `char8_t` string. `unicode_wide` and `unicode_wide_length` use an explicit platform policy: represent Unicode as UTF-16 when `wchar_t` is a suitable 16-bit type, or scalar values when it is a suitable 32-bit type; other representations raise a capability error. `unicode_wchar_bits` exposes the native width. The current Linux/Clang tests exercise 32-bit `wchar_t`; UTF-16 conversion itself is tested independently. No system locale, normalization, case folding or grapheme segmentation is inferred. `unicode_scalar_count` counts scalar values, so a combining sequence can count as multiple values.

`tests/test_unicode.lua` checks every one of the 1,112,064 Unicode scalar values in bounded batches against Lua's separate UTF-8 encoder, then round-trips through the selected native encodings. It also tests malformed sequences, integer/unit bounds, binary NULs, empty inputs, BOM preservation and raw-byte codec independence. The embedded host runs that script with allocation accounting, sweeps allocation failures through intermediate strings/vectors, injects Lua publication failures and checks cleanup after long valid prefixes ending in malformed data. Automatic wide/UTF typemaps, arbitrary legacy encodings, normalization/grapheme policies and broader platform validation remain pending.

## Native coroutine generator adapter

`examples/coroutine_library.hpp` defines a real C++20 coroutine using `co_yield`, `co_return`, a promise and an owning coroutine handle. `examples/coroutines.cpp` exposes it through `Generator.new(start, count, prefix, fail_after)`. The native code has no Lua dependency. This is a synchronous pull generator; it does not schedule background work or implement arbitrary awaitable protocols.

The coroutine initially suspends before entering its body. `advance()` resumes to the next yield and returns true, or returns false at normal completion. Yielded strings contain a binary-safe prefix followed by the checked integer `start + offset`. `value()` copies the current yield without advancing and can be retried if copying/publication fails. Reading before the first yield or after exhaustion raises an error. Empty generators still need their first resume before reporting `done()`.

Counts must be nonnegative. `fail_after` is -1 for no forced error, or a position in `[0, count]`; zero fails before the first yield and `count` fails after the last yield on the next resume. Arithmetic overflow also fails the coroutine. Native failures are captured in the promise, unwind body locals and are rethrown on repeated `advance`/`value` calls. A failed coroutine is terminal; explicit close releases its retained frame/exception.

`cancel()` destroys a pending/suspended frame immediately, returns true once and makes `advance` return false and `value` report cancellation. A completed/failed coroutine returns false from cancel and retains its frame until close/GC. The promise/frame and body-local scope have separate counters: completed bodies can be gone while parameters/promise still occupy a final-suspended frame. Generated close/GC/`__close` destroy the frame once, including early exit, exceptions and VM shutdown. Resume/access operations require serialized use.

`examples/demo_generators.lua`, preloaded as `Generators` in the REPL, provides `Generators.values(handle)` for generic-for loops. It retains the handle and tracks whether a yield is still unpublished. If value publication raises, retrying that same iterator returns the same yield without another resume. It releases its handle reference on exhaustion; early exit releases it through iterator GC. Use an explicit `<close>` handle for deterministic cleanup, and do not interleave independent consumers on the same generator.

The Lua coroutine example calls `coroutine.yield` only after native advance/value calls have returned. A `<close>` generator local inside a suspended Lua coroutine is destroyed by `coroutine.close`, including its suspended C++ body/frame. This does not enable yielding from a Lua callback across a native C call boundary.

`tests/test_coroutines.lua` covers lazy/empty iteration, binary values, repeated reads, failure positions, overflow, cancellation before/after first resume, iterator retention, Lua coroutine close, GC and frame/body allocation counts. The embedded host sweeps partial frame/parameter construction failures, injects native allocation failure during resume and Lua failure during value publication, arms iterator failure after advancement to test retry without skipping, and closes a VM with a yielded frame alive. General coroutine typemaps and arbitrary concurrent resume remain product work. The native awaitable/executor section demonstrates explicit owner-thread co_await scheduling and bounded pumping.

## Transfer, custom-deleter and weak-ownership adapters

`examples/ownership_library.hpp` supplies native unique/shared/weak slot types. `examples/ownership.cpp` exposes selected `ResourceSlot`, `SharedResource` and `WeakResource` facades through ordinary generated class bindings. Resource allocation, placement construction, destruction, stateful deleters and control-block ownership stay inside C++; the same native interfaces can be reused by other backends.

`make_resource(text, tag, fail)` creates a unique resource; `empty_resource()` creates an empty destination. A resource uses 64-byte aligned storage and placement construction of a noncopyable object containing a binary string. The custom deleter destroys the object, returns aligned storage and records its tag. Forced construction failure releases storage without invoking a complete object's destructor/deleter. `alignment`/`aligned` check the selected allocation mechanism without exposing addresses or assuming Lua userdata have native alignment.

`source:move_to(destination)` requires a present source, a different empty destination and valid open handles. It moves a native unique pointer including its deleter state with no allocation. On success the source stays open but empty; the destination owns the same resource. Rejected self/occupied/closed/wrong-type destinations do not consume it. `reset()` releases a slot's ownership and is idempotent; generated `close` destroys the facade. An empty slot's value access raises an error.

`source:promote_to(shared)` takes an existing empty `SharedResource.new()` destination. It allocates an empty shared control block before moving the unique resource, then commits without throwing. Failed allocation preserves the source/destination. Both transfer APIs return no newly allocated Lua object, so result-userdata allocation cannot fail after consuming the source. This is an explicit slot protocol, not automatic consuming `T&&`/`unique_ptr<T>` argument generation.

`shared:clone()` creates an independent facade sharing control ownership. `shared:reset()`/`close()` release that facade's share. `shared:weak()` creates a nonowning weak facade. `weak:lock()` returns a newly owning shared facade, or nil when expired; a live locked facade keeps the resource alive. Weak references retain only the control block, not the resource. Empty shared slots can also produce expired weak handles. The stateful custom deleter travels with the resource through moves, promotion, clones and locks and executes exactly once at last-owner release. Operations use the demo's serialized native-access contract.

`ResourceSlot.live()` counts complete native resources, not facade handles. `allocations`/`releases` count aligned storage, including failed construction; `deleted` counts complete resources processed by the custom deleter, and `last_deleter_tag` exposes retained deleter state. These counters make ownership transitions observable rather than relying on Lua GC timing.

`tests/test_ownership_transfer.lua` covers binary data, alignment, constructor failure, rejection/consumption rules, deleter state, reset/close/GC, shared aliases and weak expiry/locking. The embedded host counts both ordinary and aligned C++ allocations, sweeps partial resource construction and weak locking, injects Lua publication failures, verifies allocation-free moves and promotion under Lua allocation failure, and verifies failed control-block allocation preserves the source. VM shutdown leaves unique/shared/weak handles alive and checks all resources and storage are released. General transfer/allocator/deleter/weak-pointer policies and arbitrary native ownership invalidation remain pending.

## Structured error adapters

`examples/error_library.hpp` defines a reusable `Result<T>` over `std::variant<T, Error>`, with typed kind/code/category/message data, copied cause messages and an optional `std::exception_ptr`. It is an expected-style C++20-compatible adapter, not a generated `std::expected` typemap. `examples/errors.cpp` exposes the selected `int` specialization as owned `IntResult` userdata. Opaque template forward declarations in private implementation storage are allowed without requesting template export; full template definitions and public unsupported template signatures retain diagnostics.

`ok()` discriminates the result. `value()` returns success or rethrows the captured native exception; status-only errors synthesize a typed domain exception. Repeated access is allowed and does not consume the result. `value_or(fallback)` provides an explicit fallback. Error accessors are safe on success: `ErrorKind.none`, code zero, empty category/message/causes. Results use normal close/GC/`__close`; access after close fails.

`parse_result` captures typed input errors with distinct malformed/range codes. `divide_result` captures typed arithmetic failures, while `status_divide_result` adapts the same operation through a native nonthrowing integer status and out parameter. Division checks zero and `INT_MIN / -1` before execution; successful integer division follows C++ truncation toward zero. `system_result` captures a thrown `std::system_error`, and `error_code_result` converts `std::error_code` directly. Native code/category values are retained rather than translated to guessed cross-platform errno constants.

`nested_result` stores a native nested exception and exposes a copied outer-to-inner cause sequence, bounded to eight entries. Inspecting causes does not rethrow into Lua; `value()` rethrows the original outer exception. `unknown_result` captures a nonstandard integer throw with an unknown kind/message, and `allocation_result` demonstrates a captured `std::bad_alloc`. Allocating error descriptions can itself fail: that failure goes through the existing generated exception boundary rather than promising an allocation-free result object.

`status_result` supplies explicit status detail, including binary strings. `message()` preserves its full binary data through normal string codecs. Rethrown exceptions use the existing bounded C-string exception-message boundary, which may truncate long/embedded-NUL detail. `callback_result` captures synchronous Lua callback failures into `ErrorKind.callback`, including argument/result conversion failures inside invocation. Input conversion failures before entering the native adapter remain ordinary Lua errors. Stored exceptions do not retain the callback closure or registry references. Result conversion cannot roll back earlier native side effects.

`tests/test_errors.lua` covers all sources, success/default metadata, wrong inputs, nested snapshots, binary detail, repeatable value/rethrow, fallback, callback release, GC and closed access. The embedded host sweeps native allocation failures during error capture/result construction, injects Lua result/message/cause publication failures, verifies a stored status survives failed exception construction and closes a VM with nested exceptions/results alive. Generic typed Lua error objects, generated exception policies, arbitrary `expected`/result typemaps and cross-module exception registration remain pending.

## Automatic tuples and pairs

Supported `std::tuple<T...>` and `std::pair<First, Second>` values map automatically to one Lua table with one-based positional slots. The C++ type supplies the arity and element types; this is not a multiple-return convention. Empty `std::tuple<>` maps to `{}`, distinct from a void result. The frontend now represents zero-argument template instances without a phantom empty type argument. Native definitions in `examples/products.cpp` use ordinary tuples/pairs directly, including a pair with a const string key; they require no hand-written extraction facade.

```lua
local value = demo.demo__product_identity({'binary\0text', 42})
assert(value[1] == 'binary\0text' and value[2] == 42 and value[3] == nil)
local pair = demo.demo__pair_identity({'key', true})
assert(pair[1] == 'key' and pair[2] == true)
assert(next(demo.demo__empty_product({})) == nil)
```

Elements can use supported scalar/value codecs, including nested tuples/pairs, strings/views, vectors, arrays and unambiguous optionals. Strings own copied native bytes; input views borrow pinned Lua strings for the call. The usual input-reference rules apply to the outer product: const references receive temporary values; mutable references and reference elements require separate policies and are rejected. Class handles, raw pointers and custom tuple-like types are not yet product codecs. Nested ambiguous optionals remain unsupported. Existing sequence rules continue to prohibit optional elements directly inside vectors/arrays.

Product slots can contain optional values. Missing keys read as nil for an optional slot, including interior slots; required slots still reject nil. Every stored key must be an integer in `1..arity`; extra numeric slots, string keys (including `table.pack`'s `n`), fractional keys and other metadata are rejected. Both input and output use raw table access, so metatable `__index`, `__pairs` and `__len` do not participate. Lua's length operator is not an arity check for products containing nil slots; read positions using the declared C++ signature. A tuple/pair always occupies a table even when every slot is nil, preserving its identity inside an optional or sequence.

Element reads construct native products left to right and unwind earlier owned values if a later conversion or native allocation fails. Fixed arrays reuse this construction path after their existing dense/exact-length validation, allowing const elements without assignment. Optional and product codecs remove storage constness only when selecting an element codec; the constructed native value retains its declared const element types. Published Lua tables are independent, mutable copies. Result publication stays under the protected bridge; borrowed views into default-created temporaries are copied within the native call's full expression.

Wrappers with borrowed text inputs establish a call scope that retains each decoded Lua string in the registry. This also protects nested views when a callback removes the original table fields and runs GC before native code finishes reading. Registry pin creation runs in a protected thunk; native allocation failure after acquiring a reference releases it before unwinding. Nested calls restore the enclosing view scope, and every success/error path releases its roots after result publication. This protects storage during the call; it does not authorize native retention afterward. Tests mutate tuple/vector input tables during callbacks, collect their original strings, perform a nested view call, and inspect registry string-root counts during and after the call. Fresh-VM allocation sweeps exercise registry growth while earlier decoded views/native vectors are alive, as well as cleanup of partially acquired pins.

Policy-controlled callbacks accept/return supported copied products as a single table, including optional slots. Reference products and borrowed callback results retain their existing capability restrictions. `tests/test_products.lua` exercises nested/optional/empty/const products, independent copies, raw access, strict shape/type failures, callback errors/yields, view defaults and post-error recovery. The standalone C++17 scalar module includes a product without callback/class policies. The embedded host checks native allocation balance across later-element failures, native exceptions and late publication range errors; allocation sweeps cover product/const-key-pair/default-view/callback construction and partial Lua publication. Other standard-library instantiations and broader ownership-bearing product elements remain product work.

## Automatic variants

`std::variant<T...>` uses a single Lua table `{index=alternative, value=payload}`. The required `index` is **one-based**, independent of the payload type. `demo.demo__variant_identity({index=3, value="text"})` returns the text alternative; `{index=1}` selects `std::monostate`, while `{index=5}` selects an empty optional. Duplicate C++ alternative types remain distinct: `duplicate_variant({index=2, value=42})` retains the second `int`. No type guessing or implicit alternative conversion occurs. Only raw `index` and `value` keys are accepted; metatable hooks are bypassed. Missing payload is nil and succeeds only for an alternative whose codec accepts nil.

Alternatives recursively use supported value codecs, including enums, strings, const values, products, containers and nested variants. Inputs construct by `std::in_place_index`; outputs retain the native active index. Empty alternatives cannot erase the outer tagged table, so vectors, maps and optional values can contain such variants without losing elements. Default `unordered_map` hashing accepts supported variant alternatives and preserves alternative identity. `std::monostate` alone maps to exactly one nil result; maps with monostate mapped values use entry lists. Sequences of monostate and optional<monostate> are rejected because nil would erase elements or engaged state. Use a tagged variant to preserve those states.

`variant_callback` uses the same representation in both directions under the callback lifetime policy. Borrowed text inputs remain pinned through the native call and reentrant callbacks; borrowed callback results remain unsupported. `variant_view()` exercises a view into a default-created temporary whose backing string survives protected result publication. C++ `valueless_by_exception` is rejected as a Lua error; it is never confused with a monostate alternative. The exception demo queries whether the native library preserved the old alternative or became valueless after a throwing emplacement, and tests the corresponding behavior.

`tests/test_variants.lua` checks all demo alternatives, duplicate/const alternatives, binary data, nested containers, nil states, hash keys, callbacks, raw shape validation, borrowed-input GC, throwing native calls, publication range failures and recovery. The C++17 scalar module checks a minimal variant without class/callback policies. The embedded host runs the full script with native allocation accounting and sweeps C++ and Lua allocation failures through string/const-string alternatives, callbacks and default-backed views. Class handles, reference alternatives, consuming ownership and schema/version policies remain separate planning work.

## Automatic maps

`std::map<Key, Value>` and `std::unordered_map<Key, Value>` use automatic copied-value codecs with their default comparator/hash/equality/allocator template arguments. The normal value, enum and callback machinery applies recursively. The Lua representation is selected from the C++ signature:

| Native shape | Lua representation | Reason |
| --- | --- | --- |
| String key, non-nil mapped value | Dictionary `{key=value}` | Keys and every mapped value have non-nil representations |
| Other key types, or nil-capable mapped values | Dense list of `{key, value}` entries | Typed keys use native value comparison and nil slots preserve empty optionals and monostate |

```lua
local dictionary = demo.demo__string_map({answer='42', ['binary\0key']='value\0'})
assert(dictionary.answer == '42')
local entries = demo.demo__optional_map({{'empty'}, {'present', '42'}})
assert(entries[1][1] == 'empty' and entries[1][2] == nil)
```

String dictionaries require actual string keys and use raw traversal/access, preserving binary keys/values and bypassing `__pairs`, `__index` and `__len`. Entry lists use the existing dense-sequence and fixed-pair rules. An optional or monostate key/mapped value can be absent from its entry table, while the entry table itself remains present in the outer list. Thus an empty mapped optional is not confused with a missing map entry. An empty map is `{}` in either representation; outer `optional<Map>` can still distinguish it from nil. The representation is not guessed from input shape.

Ordered maps serialize entry lists in native comparator order. Unordered iteration order is unspecified; Lua dictionaries have no ordering guarantee even for `std::map`. Duplicate entries are rejected using native comparator/equality semantics after conversion, including different Lua tables with equal tuple keys, enum aliases and positive/negative floating zero. They are never silently overwritten. Scalar/nested/enum/optional key and mapped codecs retain their normal strict validation; mutable references, ownership-bearing class values and pointer keys need separate policies.

Default unordered keys currently include unqualified scalar/enum types, strings/views, hash-compatible optionals and `vector<bool>`. Arbitrary products/containers lack general default `std::hash` support and need an explicit native adapter/hash policy. Ordered keys may use supported comparable products/containers; keys containing unordered maps are rejected because those have no default ordering. Custom comparator/hash/equality/allocator arguments remain explicit capability gaps. Only entries are serialized, not native policy state. Automatic publication currently requires trivially destructible native iterators, checked at compilation, so Lua errors cannot skip iterator cleanup on another standard-library configuration. A native copying facade can expose entries when these direct-codec requirements are unsuitable.

Reads build native maps with C++ unwinding for nodes, keys and mapped values. Each dictionary traversal step runs under a protected Lua thunk: decoding a view can allocate registry roots/trigger GC, and reentrant table mutation can invalidate a `next` key. Errors from traversal unwind C++ values before being raised in Lua. Mutation during conversion has no atomic-snapshot guarantee and is not rolled back. Output pushes stay inside protected publication; callbacks use the same codecs. Borrowed text fields retain registry pins for the call and must not be retained by native code.

`examples/maps.cpp` uses standard maps directly. `tests/test_maps.lua` exercises binary dictionaries, raw access, optional entries, tuple/enum/optional/bit-vector keys, native ordering/equality, strict shapes, duplicate rejection, copied callbacks, reentrant view GC, errors and recovery. A debug-hook regression removes/re-hashes the input before the second traversal step and verifies that the resulting Lua error releases native entries. The embedded host runs that script with allocation accounting and sweeps ordered/unordered/optional/view/callback conversions through native and Lua allocation failures, checking nodes, values and registry-root cleanup. Native exceptions and oversized enum-result publication also unwind. Multimaps, sets and linear sequences now have the codecs described below; general custom-policy/ownership-bearing container lowering remains pending.

## Automatic sets, multimaps and linear sequences

`std::deque<T>`, `std::list<T>` and `std::forward_list<T>` accept/return dense one-based Lua tables, preserving native sequence order and duplicates. Forward-list decoding inserts after a moving cursor rather than reversing its elements; publication traverses the native sequence without assuming it has `size()`. Recursive codecs support enums, binary strings, tagged variants, products and other supported containers. Direct nil-capable sequence elements remain ambiguous and are rejected; use tagged variants or tuple boxes to preserve empty states.

Sets use dense tables too, with an explicit distinction for nil-capable element types:

| Native signature | Lua representation | Duplicate rule |
| --- | --- | --- |
| `set<T>` / `unordered_set<T>` with non-nil `T` | `{value, value, ...}` | Duplicate converted native elements are errors |
| `set<optional<T>>` / `unordered_set<optional<T>>` | `{{value}, {}, ...}` | An empty box is one nullopt element; two empty boxes are duplicates |
| `multiset<T>` / `unordered_multiset<T>` | Same element shape as the unique set | Preserve repeated native elements |
| Set/multiset of `monostate` | `{{}, ...}` | Unique sets admit at most one empty value; multisets preserve its count |
| `multimap<K,V>` / `unordered_multimap<K,V>` | `{{key,value}, ...}` for every signature | Preserve repeated keys and values, including nil entry slots |

Boxing is selected by the declared native element type, never guessed from Lua input. A variant element is already a tagged non-nil table, so it does not acquire another box. Every stored key in an optional/monostate box must be its one allowed positional slot. Ordered sets serialize by native comparison; ordered multimaps serialize by key and retain insertion order among equivalent keys. Unordered output order is unspecified: compare element/entry counts rather than positions. Lua edits always modify copies. Unique-container duplicate checks prevent conversion from silently losing entries; use a multi-container when repeats are meaningful.

```lua
local sorted = demo.demo__ordered_set({'z', 'a'}) -- {'a', 'z'}
local repeats = demo.demo__multi_set({'b', 'a', 'a'}) -- {'a', 'a', 'b'}
local optional = demo.demo__optional_set({{'text'}, {}}) -- {{}, {'text'}}
local pairs = demo.demo__multi_map({{'key', 'first'}, {'key', 'second'}})
local sequence = demo.demo__deque_values({'binary\0', 'second', 'binary\0'})
```

Default native comparator/hash/equality/allocator policies are supported. Custom template policies still require an explicit adapter. Ordered keys/elements cannot contain an unordered associative container because those have no native ordering. Unordered keys/elements require supported `std::hash` types: supported scalars/enums, strings/views, bit vectors, and recursively hashable optionals/variants. Supported values may nest ordered containers, including a set of vectors, without exporting native iterators. Reference elements, class handles and consuming ownership retain their existing capability restrictions. Borrowed input text is pinned for the call, including through reentrant mutation/GC; borrowed callback results remain rejected. The `sequence_view()` demo publishes views into default-created temporaries within their native full expression.

Input shape checks and element access use raw Lua operations; metatable hooks cannot synthesize values or iteration order. Output traverses borrowed native elements under the existing protected publisher and requires trivially destructible native iterators, so Lua allocation/error jumps cannot skip owning iterator cleanup. Partial native construction unwinds blocks, nodes, buckets and earlier values. Policy-controlled sequence/set callbacks use the same codecs in both directions.

`examples/containers.cpp` uses ordinary container signatures. `tests/test_containers.lua` checks every new family, ordered and unordered multiplicity, nil boxes, nested values, binary data, enums, strict shapes, callback errors, native exceptions, late enum-result range errors, borrowed-input GC, default-backed views and recovery. The independent C++17 scalar module checks a set without class/callback policies. Generator fixtures reject ambiguous nil sequences, custom policies, unsupported native hashing/ordering and borrowed callback results. The embedded host runs the script with allocation accounting and sweeps native/Lua failures through every new family, callback conversions, view pinning and default construction/publication. Stack/queue/priority-queue facades are documented below; node extraction/merge, general live mutation/iteration, custom policy transport and ownership-bearing elements remain pending.

## Typed member-pointer selectors

`examples/member_library.hpp` holds real native pointers to integer/string data members and const/mutable member functions in a typed variant. `examples/members.cpp` exposes `demo::MemberObject` and `demo::MemberSelector` through existing owned-class policies. The helper has no Lua dependency. This explicit selection mechanism can be reused by another backend without transporting a C++ pointer representation or object layout.

```lua
local object <close> = demo.demo__MemberObject.new(20, 21, 'example', true)
local field <close> = demo.demo__MemberSelector.new(demo.demo__MemberChoice.right)
assert(field:read_integer(object) == 21)
field:write_integer(object, 22)
local method <close> = demo.demo__MemberSelector.new(demo.demo__MemberChoice.measure)
assert(method:invoke_read(object, 0) == 43) -- derived override adds one
```

| Selection | Native member category | Operations |
| --- | --- | --- |
| `left`, `right` | `int Object::*`, converted from two different base-class member pointers | `read_integer`, `write_integer` |
| `label` | `std::string Object::*` | `read_text`, `write_text` |
| `measure` | Virtual `int (Object::*)(int) const &` | `invoke_read` |
| `add` | `int (Object::*)(int) &` | `invoke_write` |
| `none`, `null_method` | Native null data/member-function pointers | `null()` is true; attempted application fails |

`kind()` returns `MemberKind.none`, `integer`, `text`, `const_method` or `mutable_method`. Native nulls are initialized using language-level nullptr values, never an assumed zero byte pattern. No numeric address, serialized member-pointer bytes or null representation crosses the boundary. Wrong operation categories and null applications throw before `std::invoke`; wrong/closed userdata is rejected by generated checks before native access.

`MemberObject.new(left,right,text,derived)` creates a native object with two field-bearing bases; the flag selects a final derived implementation of the virtual read method. Field pointers use legal C++ base-to-derived member-pointer conversion. The compiler therefore handles the right-base adjustment, and a copied virtual member-function pointer still dispatches to the actual derived override. `measure(offset)` adds both fields and the offset with checked intermediate arithmetic; the derived override adds one. `add(delta)` changes the left field only after checking its addition. Arithmetic/type failures preserve earlier state. Text reads return copied binary strings; writes prepare their input before swapping it into the selected field.

Selectors store a selection independent of any target. `copy()` copies the native typed pointer value into an independently owned selector. Closing the original selector preserves its copy; a selector can be reused with another valid target after an earlier target closes. It does not retain or authorize arbitrary target memory. `MemberObject.share()` creates another owning facade for the same native object. `readonly()` returns an owner-anchored const view: reads and const member calls accept it; field writes and mutable member calls reject it. Closing that view's original owner invalidates the borrow even if an independent shared owner still exists. Normal receiver/argument owner-chain pins apply during calls and result publication.

Both facades support close/GC/VM shutdown. `MemberObject.live()` counts native targets and `MemberSelector.live()` counts native selection values; these differ from Lua handle counts when targets are shared. Access is serialized by the caller; this adapter does not add synchronization. Raw member-pointer signatures remain rejected, including for selected classes; general declarator modeling, descriptor generation from policies, pointer conversions across arbitrary class hierarchies and first-class bound method callbacks remain pending.

`tests/test_members.lua` checks every selection, both null categories, multiple-inheritance field adjustment, virtual/ref-qualified method dispatch, binary values, readonly/wrong-kind/closed inputs, copied selectors, shared targets, checked failures, GC and recovery. The embedded host runs it with native allocation accounting, sweeps native/Lua failures through both target factories, every selector category, copying/sharing and binary-text read/replacement, and closes a VM with a readonly target view and selector alive. Generator fixtures verify that direct member-pointer signatures do not become unsafe wire values. Sanitizer validation covers the generated binding and native facade lifetimes.

## Native synchronization and qualified-storage adapters

`examples/concurrency_library.hpp` implements native checked atomics, mutex-guarded volatile storage, thread-local values and joined worker groups. `examples/concurrency.cpp` exposes the selected `demo::ConcurrentState` facade and TLS functions through ordinary scalar/product/container codecs. The native helper has no Lua dependencies and uses C++17 facilities, so another backend can reuse the operations and synchronization policy. Raw atomic/volatile fields remain unsupported automatic fields; generated accessors must not silently remove those semantics. Compiler/model fixtures retain the volatile qualifier and require explicit accessor facades for both storage types.

```lua
local state <close> = demo.demo__ConcurrentState.new(0, 42)
local completed = state:parallel_add(4, 100, 1)
assert(state:load() == 400 and state:register_load() == 42)
local result = state:compare_exchange(400, 500)
assert(result[1] == true and result[2] == 400)
```

The counter uses native `std::atomic<int>` with the default sequentially consistent memory order. `load`/`store` read/write it; `add(delta)` returns the previous count after a successful checked compare/exchange loop. Range tests occur before native addition and use the native limits without assuming a wider integer representation. Overflow throws without committing that attempted addition. `compare_exchange(expected, desired)` is strong and returns `{success, observed_previous}`: success stores desired and returns the matching old expected value; failure retains the counter and returns the actual observed value. `limits()` supplies native `{minimum,maximum}`; tests derive boundaries from this metadata.

`parallel_add(workers, iterations, delta)` creates 1–16 native threads, each attempting 0–10000 checked additions. Success returns each worker's completed count in creation order. Each worker uses its own TLS counter; no worker calls Lua or a target callback. A cooperative stop flag and protected first-exception slot collect worker failures. All started threads join before results are returned or an exception propagates, including failure while creating a later thread. Updates committed before a worker/creation error remain committed. Publication failure after success also preserves the committed native updates; the API does not promise transaction rollback or a scheduler deadline. The REPL call blocks until its bounded worker group joins; the existing task facade remains the separate asynchronous poll/wait/cancel example.

`register_load`, `register_store` and `register_exchange` explicitly access a native volatile unsigned integer under a mutex shared by every access. Exchange returns the previous value; `register_bits()` reports the native value width. Volatile preserves the explicit storage accesses; the mutex provides synchronization. This example uses ordinary native memory and does not claim hardware MMIO semantics, device barriers or external invalidation tracking. Ordinary integer conversions continue to reject values outside the native/target range. Counter and register operations have independent synchronization; the facade does not promise an atomic snapshot spanning both fields.

`thread_local_get`/`thread_local_set` access a native thread-local integer. `thread_local_probe(value)` returns `{caller_before, worker_initial, worker_after, caller_after}` from a fresh joined native thread. Worker TLS starts at zero and changing it does not change the caller. TLS follows native thread lifetime and library linkage, rather than Lua VM/coroutine identity. The functions deliberately expose that distinction; they do not infer a per-VM data policy. Parallel worker TLS changes likewise leave caller TLS unchanged.

`share()` returns another owning facade for the same native state. Its atomic/mutex operations support overlapping native callers; last-owner close/GC/shutdown releases the state after active bound calls finish. Lua itself still requires its host-thread rules; native synchronization does not authorize concurrent entry into a Lua VM. Methods do not retain running workers beyond a call, and sharing does not create a background executor.

`tests/test_concurrency.lua` covers TLS isolation, compare/exchange success/failure, contended positive/negative/zero adds, exact native limits and overflow, zero-work groups, bounds, unsigned/volatile access, shared close and GC. The embedded host runs the script with allocation accounting, overlaps native writers with worker groups, verifies one winner among competing compare/exchanges, and injects native/Lua failures through construction, partial thread launch, worker overflow and result publication. TLS probe thread creation/publication also has failure sweeps. Native state/capture/exception allocations balance, worker counts return to zero after errors and VM shutdown releases a live state. An independent C++17 `native_concurrency` CTest exercises the reusable helper without Lua. `just concurrency-tsan` compiles that test with ThreadSanitizer and checks overlapping counter/register access, TLS and worker-error collection; it passes on the development host. General memory-order/atomic typemaps, hardware access policies, barriers/semaphores, arbitrary scheduler integration and concurrent target-runtime entry remain product work. A cooperative owner-thread executor is demonstrated separately.

## Native container-adaptor worklists

`examples/worklist_library.hpp` stores real `std::stack<std::string>`, `std::queue<std::string>` and `std::priority_queue` instantiations behind a variant. `examples/worklists.cpp` exposes the selected `demo::Worklist` facade with the ordinary owned-class machinery. There are no Lua APIs in the native helper; another backend can bind the same ordering, lifetime and error contract. These are explicit selected instantiations, not general automatic container-adaptor typemaps.

`WorkOrder.stack` selects LIFO, `queue` selects FIFO, `highest` selects the default lexicographic max-priority queue and `lowest` selects a min-priority queue using `std::greater<std::string>`. The mode is fixed at construction and returned by `order()`. Comparisons use native byte-string order, without locale collation or an inferred numeric priority. Equal strings remain separate entries. All modes preserve empty strings and binary contents.

```lua
local tasks <close> = demo.demo__Worklist.new(demo.demo__WorkOrder.highest)
tasks:push('a'); tasks:push('z'); tasks:push('a')
assert(tasks:peek() == 'z' and tasks:size() == 3)
local snapshot = tasks:snapshot() -- {'z', 'a', 'a'}
assert(tasks:pop() == 'z')
```

`peek()` copies the next entry without mutation. `pop()` copies it before consuming it; both return nil for an empty worklist, distinct from an empty-string entry. `snapshot()` copies native storage and drains that copy in logical extraction order, returning an independent vector. It exposes neither the adaptor's protected storage nor live iterators. Snapshot/clone preparation failures leave the original unchanged. With default native string moves and the selected comparators, failed push preparation preserves the source. The native pop commits before Lua result publication; if Lua cannot publish the removed string, the removal remains committed. Clients requiring a separate inspection/commit protocol can use `peek()` followed by a later `pop()`.

`clone()` creates an independent native worklist with the same mode and entries. `share()` creates another owning handle for the same mutable state: changes are visible through both, and closing one preserves the state while another owner remains. Last-owner close/GC/VM shutdown release its containers and strings. Calls on a closed handle fail through normal generated checks. These adaptors have no internal synchronization or cross-thread Lua dispatch policy; use serialized calls.

`tests/test_worklists.lua` checks all four extraction orders, duplicates, nil/empty/binary distinctions, copied snapshots, independent clones, shared mutation/lifetime, strict mode/value errors, close and GC. The embedded host runs the script with allocation accounting, sweeps native/Lua failures through construction and every operation in every mode, verifies source preservation before commit, and closes a VM with queue/priority state alive. General element/comparator/allocator selection, automatic adaptor codecs, consuming move operations and concurrent work-queue scheduling remain pending.

## Chrono duration and time-point adapters

`examples/chrono_library.hpp` implements native `std::chrono::duration`, `duration_cast` and clock-specific `time_point` operations. `examples/chrono.cpp` exposes ordinary C++ facades in `demo.hpp`; the Lua emitter uses its existing value/class/default machinery. There are no Lua APIs in the native time library.

```lua
demo.demo__time_convert(42, 's', 'ms') -- 42000
demo.demo__time_convert(-1500, 'ms', 's', 'nearest') -- -2, ties to even
local epoch <close> = demo.demo__TimePoint.new(0) -- system-clock milliseconds
local later <close> = epoch:shifted(42)
assert(later:difference(epoch) == 42 and epoch:before(later))
local monotonic <close> = demo.demo__time_now(true) -- steady-clock milliseconds
```

`time_convert(ticks, from, to, rounding)` selects concrete signed `long long` duration types. Units are exactly `ns`, `us`, `ms`, `s`, `min`, `h`. Rounding defaults to `truncate` (toward zero); `floor`, `ceil`, `nearest` (ties to even) and `exact` are explicit alternatives. Exact conversion rejects any discarded fraction. Unknown names, embedded-NUL names and invalid Lua types fail. Unit scaling checks the native multiplication range before `duration_cast`. Reductions use integral remainders for rounding, including signed extrema, without rebuilding an unrepresentable source duration or converting through floating point. The supported unit ratios are nested integral factors.

`TimePoint.new(milliseconds, steady=false)` constructs a native point in the selected clock domain. `ticks()` returns its millisecond count; `steady()` identifies the domain. `shifted(delta_ms)` creates an independent point, `difference(other)` returns a signed millisecond duration, and `before(other)` compares without subtracting. Arithmetic checks bounds before native operations. Comparing/subtracting different clock domains raises an error; equal numeric ticks do not establish a relationship between clocks. The native variant retains `time_point<system_clock, Milliseconds>` and `time_point<steady_clock, Milliseconds>` types. Synthetic points can span the full millisecond representation without implying they fit a clock's default, finer-duration point type. Values are immutable through this facade, so failed operations preserve their inputs. Class close/GC/VM shutdown use the ordinary owned-handle rules; closing an input does not close a shifted result.

`time_now(steady=false)` samples the selected native clock and floors its duration to milliseconds. `time_clock_info(steady=false)` returns `{period_numerator, period_denominator, is_steady}` using the actual native clock period in seconds and an integer 0/1 flag. Steady ticks are relative to that clock's unspecified epoch and are not civil timestamps. System-clock samples can move backward; only steady-clock samples have a monotonicity test. The current adapter requires integral native clock representations that fit `long long` and periods compatible with the selected integral conversion path; unsupported representations receive a native compile-time capability diagnostic. The verified Lua/native configuration uses signed 64-bit ticks.

`tests/test_chrono.lua` tests all supported unit pairs, every rounding mode, positive/negative ties, exactness, scaling boundaries and signed extrema against separate Lua integer arithmetic. It tests clock metadata, same/cross-domain operations, extreme arithmetic, independent lifetime, close/GC and strict inputs. The embedded host runs that script with native allocation accounting, sweeps point construction/shift/clock-factory allocation failures, denies Lua result publication and closes a VM with a time point alive. Calendar/time-zone formatting, leap-second policies, floating duration representations, arbitrary periods/clocks and automatic chrono typemaps remain pending. These facades do not infer an epoch or scheduler policy for another library's raw counts.

## Automatic default arguments

Supported non-overloaded functions, constructors and methods accept omitted trailing arguments. The shared model and IR record their trailing `default_arguments` count, merged across canonical free-function redeclarations. Each supplied arity gets a generated native call. Short calls use the declared C++ name, so the compiler evaluates the original defaults in their defining scope on each invocation; defaults are never parsed or reimplemented in Lua. Complete calls retain exact signature selection. General overload groups remain unsupported, including ambiguous short calls; use named adapters for them. Default-call checks include directly declared overloads from included headers and private/deleted/template alternatives, even when those alternatives are not exported. More complex lookup through using declarations and dependent scopes remains part of general callable planning.

Try `demo.demo__scale(21)`, `demo.demo__text_then_integer()`, `demo.demo__Counter.new(41)` followed by `counter:add()`, or `demo.demo__Counter.adjusted(40)`. `default_text(prefix)` demonstrates a side-effecting default; passing an explicit value bypasses it. Enum defaults work. Explicit nil remains an actual argument: `optional_text()` returns the native default string while `optional_text(nil)` returns nil. A required string still rejects nil. Callback parameters retain their explicit lifetime-policy requirement, including on functions with defaults.

Conversions of supplied arguments precede the native call; failed conversion does not evaluate omitted defaults. Default expressions may throw or allocate, and their cleanup follows C++ rules. Native effects already performed are not rolled back if result publication fails. String views/C strings into default-created temporaries are copied under protected Lua publication within the same full expression, preserving the backing storage through the copy. This cannot repair a native function that already returns dangling storage. Default expressions and their dependencies must be available in the parsed/compiled header context.

Generator fixtures check merged redeclarations, constructor/method metadata, IR and ambiguity diagnostics. The demo and object/callback scripts execute zero/partial/full argument calls, strict errors, per-call side effects, static/member calls, throwing defaults and recovery. The independent scalar module verifies defaults without class/callback support. The embedded host sweeps native allocation failures and denies Lua publication allocation for allocating defaults and borrowed text results; native allocations must balance in every path. Overload ranking, expression/source metadata and general ref-qualified/dependent callable support remain pending.

## Overload, default and variadic adapters

`examples/call_library.hpp` defines real native overloads, deleted overloads, default expressions, C-style varargs, initializer lists and an object with overloaded constructors and const/ref-qualified methods. `examples/calls.cpp` selects concrete signatures with typed function/member pointers and ordinary C++ factories. These named adapters have no Lua dependency and are bound through the regular generator. Automatic discovery/dispatch of overload groups remains pending; direct unsupported overloads/varargs still receive capability diagnostics. Non-overloaded trailing defaults are generated automatically as described above.

`overload_integer`, `overload_real` and `overload_text` select integer, double and string overloads. `default_scale(value)` calls the native API with its factor omitted, evaluating the actual C++ default expression; `explicit_scale(value, factor)` passes both arguments. `default_factor_calls()` makes default-expression side effects observable. Rejected Lua conversion does not enter the native call, but a later arithmetic error does not roll back a default already evaluated. Arithmetic checks integer overflow.

`examples/demo_calls.lua` supplies explicit target dispatch rules, preloaded as `Calls` in the REPL. `Calls.choose` requires one argument: strings select the string overload, Lua integers select `int`, and Lua floating values select `double`. An integer outside the selected native range fails without falling back to double. Numeric strings stay strings; booleans/nil do not match. This is an explicit demo policy, not C++ overload resolution reproduced automatically in Lua.

`Calls.scale` dispatches on exact argument count; an explicit nil factor is not treated as an omitted default. `Calls.ambiguous` demonstrates two native integer types sharing Lua's integer representation. It checks native signed/unsigned limits exported by C++, selects a sole fitting candidate, reports an ambiguity when both fit and reports no match when neither fits. `Calls.signed`/`unsigned` give explicit selection. Tests cover the current Linux 32-bit-int/64-bit-Lua data model.

`variadic_sum` accepts a copied dense integer sequence of length 0–3 and calls a real C-varargs function with the exact count and promoted `int` arguments selected in C++. Larger or differently typed sequences are rejected. `initializer_sum` constructs a native three-element initializer list from a checked fixed array. Arbitrary runtime varargs, format-string APIs and automatic initializer-list typemaps require separate adapters/policies.

`make_call_default`, `make_call_number` and `make_call_text` select overloaded constructors. Text construction requires a complete decimal integer, rejecting embedded NUL/trailing text and overflow. The facade constructor is hidden from Lua. `adjust_mutable` and `adjust_readonly` select `&` and `const &` member overloads, preserving mutation versus read-only computation. `take` explicitly invokes an `&&` method, returns the old integer and resets the native object to zero; the facade remains usable. It is not a generic ownership-transfer policy. `as_integer` calls an explicit conversion, and `secret` uses an authorized native friend to read private data. An anchored readonly facade rejects mutation and expires when its owner closes.

`tests/test_calls.lua` exercises every selected call, exact Lua dispatch, ambiguity/no-match cases, default side effects, strict inputs, varargs limits, constructor errors, ref qualifiers, overflow recovery, readonly borrowing and GC. The embedded host runs it with native allocation accounting, sweeps factory/string-call allocation failures, injects Lua result-publication failure and shuts down with an object/readonly borrow alive. Generated signature selection/renaming policies, general overload ranking, default-expression source metadata and arbitrary calling conventions remain pending.

## Filesystem adapters

`examples/filesystem_library.hpp` implements a native temporary workspace without Lua dependencies. `examples/filesystem.cpp` exposes `demo::FileWorkspace` and four concrete path helpers through the existing class/value codecs. This is a reusable explicit adapter for `std::filesystem` paths, metadata and file operations, not an automatic path typemap. The current workspace resource implementation requires POSIX, matching the compiler process adapter; another host can replace its resource layer while retaining the facade and backend-independent policies.

`path_normalize`, `path_join`, `path_components` and `path_parts` expose native lexical normalization, joining, decomposition and `{parent, filename, extension}` results. These operations do not access disk. Joining an absolute child replaces its base on the POSIX host. Path strings use native bytes, reject embedded NUL and preserve UTF-8 bytes without inferring an encoding. Normalizing `../outside` is allowed lexically; workspace operations impose separate access rules.

```lua
local files <close> = demo.demo__FileWorkspace.new()
files:directories('notes/nested')
files:write('notes/nested/message', 'hello\0filesystem')
assert(files:read('notes/nested/message') == 'hello\0filesystem')
for _, name in ipairs(files:entries('', true)) do print(name) end
files:rename('notes/nested/message', 'notes/nested/renamed')
assert(files:erase('notes', true))
```

Each workspace creates a private temporary directory. `location()` copies its native absolute path for inspection. `exists()` accepts the empty path for the root; `entries(relative='', recursive=false)` returns a copied, sorted snapshot whose paths are relative to the workspace root. Recursive traversal does not follow symlinks. Lua edits cannot change directory contents or invalidate native iterators because no iterator crosses the binding boundary. `size` returns checked byte counts; `read`/`write` preserve binary data and empty files. This selected demo limits file contents to 16 MiB and relative paths/traversal to 32 components/directories.

Workspace paths reject absolute roots, dot/dot-dot components, embedded NUL and symbolic links in the selected path. Empty paths are allowed only for root queries/listing. `directories` creates intermediate directories; file writes require an existing parent. A write prepares a temporary sibling and renames it into place, preserving an existing destination if preparation fails and removing staging files on failure. Writes replace existing regular entries; `rename(from,to)` rejects an existing destination. `erase(relative, recursive=false)` removes a single entry, rejects nonempty directories, and returns false for missing entries; recursive erase removes a tree. Filesystem/status and native I/O errors use the ordinary exception boundary. Effects already committed by native filesystem operations are not rolled back after unrelated publication failures.

`share()` returns another owning facade for the same workspace. Closing one handle preserves files while another owner remains; last-owner close, GC and VM shutdown remove workspace contents and its root. Cleanup does not follow symlinks, including links introduced by external native code. Descriptor-based cleanup uses no C++ allocations, so failure-injection unwinding can still release files/directories. Destructors never throw; `cleanup_errors()` counts failed cleanups and `live()` counts active workspaces. Cleanup may fail if external code changes permissions or removes/replaces the root. These APIs assume exclusive workspace mutation and do not provide a security boundary against concurrent external path replacement. No process working-directory change, arbitrary-root access or retained file handle is exposed.

The installed standard library's directory iterator allocates inside a noexcept traversal routine; an injected allocation failure was observed to terminate there. The adapter therefore owns POSIX traversal resources directly and uses `std::filesystem` for paths/metadata rather than exposing that iterator. Both allocation and OS descriptor counts verify unwinding through snapshot conversion, native errors and Lua publication. `tests/test_filesystem.lua` tests path semantics, binary/UTF-8 data, nested snapshots, rename/removal errors, path restrictions, shared close, GC and on-disk cleanup. The embedded host also tests external symlink rejection/non-traversal, native/Lua allocation failures in construction, reads, atomic replacement, recursive traversal, directory creation, rename, erase and sharing, and VM shutdown with files alive. Automatic filesystem typemaps, arbitrary-root policies, permission/link operations, timestamps, watchers, durability guarantees and Windows resource support remain pending.

## Buffer and out-parameter adapters

`examples/buffer_library.hpp` and `examples/buffer.cpp` provide native pointer/span helpers and selected `ByteBuffer`/`ByteSlice` facades. They have no Lua dependency. Storage and generation updates are encapsulated in the native library, so mutation tracking can be reused by other backends. Lua sees owned handles and copied binary strings, never numeric addresses or mutable pointers into Lua strings.

`ByteBuffer.new(bytes)` copies binary input. Offsets and lengths are **zero-based byte counts**, unlike the series example's one-based element indices. Bounds checks reject negative values and use subtraction to avoid overflow. `write(offset, bytes)` updates existing bytes without changing size. Nonempty append and size-changing resize invalidate existing views by incrementing a generation after successful mutation. Empty append and same-size resize are no-ops. Allocation or validation failure preserves bytes and view validity. Buffers are limited to `INT_MAX` bytes for this concrete API.

`slice(offset, length, readonly)` returns an independently owned native view with a weak storage owner and captured generation. Each method locks the owner and checks the generation before acquiring a temporary C++ span/pointer. In-place writes are visible through other views. Readonly views reject mutation. Empty/end slices are supported without dereferencing empty storage. Structural changes report `invalidated`; releasing the last storage owner reports `owner expired`. Checks occur before pointer access.

`share()` supplies another owning facade for the same storage. Closing/collecting one owner leaves slices usable while another owner remains. Slices themselves do **not** retain storage or the Lua owner; this differs from `borrow_self`, which anchors a Lua receiver. Root/slice handles still use generated idempotent close/GC/`__close`. Native operations require serialized access; this adapter does not synchronize concurrent storage mutation.

`decode_u16(bytes)` adapts a native pointer/length input, pointer out parameter and scalar-reference consumed count to optional integer output. It reads two little-endian bytes, returns nil on short input and ignores trailing bytes. `decode_u16_with_count` exposes the same outputs as a copied `{value, consumed}` sequence, or `{}` when short. `out_buffer(size, fill)` adopts a native `unsigned char**` array output into `unique_ptr<unsigned char[]>`, copies it to a binary string and releases it on all paths. `inout_increment(value)` adapts a native `int&` update to a checked returned value. A slice's XOR operation demonstrates a `void*`/length native API. These are explicit selected directional adapters; signatures alone do not infer direction, encoding or ownership.

`tests/test_buffers.lua` covers binary/empty data, ranges, readonly mutation, shared owners, weak expiry, GC, no-op operations, mutation invalidation, optional/sequence outputs and scalar overflow. The embedded host injects failures during native output-array/string allocation, buffer growth, slice/string publication and Lua output publication, verifies preserved state/views and allocation balance, and closes a VM with storage/views alive. Unicode conversion has separate explicit adapters; general automatic direction/encoding policies, arbitrary pointer arithmetic and arbitrary external native invalidation remain pending.

## Compile-time and template adapters

The native `examples/compile_time_library.hpp` uses C++20. `examples/compile_time.cpp` supplies concrete runtime entry points declared in `demo.hpp`. The demo and embedded test host compile as C++20, and CMake passes the same standard to Clang when parsing the facade header. Rosetta's core, generator tests and independent scalar module continue to compile as C++17. These entry points use the normal backend and have no Lua-specific native code.

`constexpr_answer`, `immediate_answer` and `initialized_constant` export selected constexpr, consteval and constinit results. The immediate function is evaluated with constant arguments inside C++; Lua does not call an immediate-only function with runtime inputs or take its address. Direct consteval binding is rejected with a constant-expression-facade diagnostic. Type traits and a selected variable template become boolean results. Object-like and function-like macros get explicit functions, and `concepts_available` demonstrates conditional preprocessing through a feature-test macro. General macro discovery and automatic constant exports remain pending.

Template examples include non-type bias/array-size arguments, full/partial class specialization, variable and alias templates, a template-template container parameter, dependent `value_type`, a deduction guide/CTAD, concepts/requires and SFINAE. Native static assertions verify expected instantiations and unavailable constraint combinations. `constrained_sum` selects `int`, checking prefix-sum overflow; it does not infer template arguments from arbitrary Lua values. `template_pattern` selects a three-element array and checks range before addition. String alias-template results preserve binary data.

`literal_answer` exports one selected immediate user-defined literal. `compare_ints` uses an actual defaulted C++20 spaceship operator and maps ordering to -1/0/1 without subtracting possibly extreme integers. Automatic literal parsing and ordering-category typemaps remain pending.

`tests/test_compile_time.lua` exercises each facade, negative values, integer limits, binary strings, strict inputs, overflow and post-error recovery. The embedded host runs it with native allocation accounting. Compiler fixtures check valid C++20 facades and rejection of invalid concept/SFINAE instantiations and runtime arguments to consteval functions. The compile-time family remains partial: automatic instantiation manifests, general dependent-type modeling, generated constant-expression wrappers and future reflection support are broader tasks.

## Record and standard-library adapters

`examples/advanced_values.hpp` implements native storage without Lua dependencies. Selected `demo::TaggedRecord` and `demo::ValueStore` facades expose supported signatures to the regular generator. This demonstrates explicit emulation of otherwise unsupported types; Automatic variant/map/tuple codecs now cover supported value signatures; arbitrary union and any policies still require explicit adapters.

`TaggedRecord` owns a real union of `int` and nontrivial `std::string`, a discriminator, unsigned 3/5-bit fields and a packed byte/32-bit-counter structure. `set_integer`/`set_text` construct and destroy the active member in C++. Wrong-member reads raise an error before accessing inactive storage. String replacements are prepared before changing the active member, preserving the old value on allocation failure. Strings are binary-safe. `clone()` produces independent storage and copies fields. Bitfield setters validate ranges `[0, 7]` and `[0, 31]` before narrowing, and counter conversion is checked by the normal unsigned codec.

Packed storage remains behind C++ accessors: Lua never receives layout-dependent bytes, addresses or references. The demo uses compiler `#pragma pack` and a five-byte `static_assert` capability check. This is tested on the current Linux/Clang platform, not a portable wire format. The ownership example separately demonstrates aligned placement storage. Automatic union-tag policies, arbitrary layouts, general allocator policies and bitfield accessor generation remain pending.

`ValueStore` owns a `std::map<std::string, std::variant<int, std::string>>`. Explicit insertion methods select alternatives; `kind(key)` returns `ValueKind.missing`, `integer` or `text`. Reads of missing keys and wrong alternatives become Lua errors; wrong alternatives exercise native `std::bad_variant_access`. Empty and binary keys are valid. `keys()` returns a copied, sorted vector snapshot: Lua edits cannot mutate the map. Replacing/erasing entries does not invalidate Lua borrows because the facade returns copied values and exposes no map iterators.

The store also holds `std::any` with an explicit allowed set of integer, string or `std::tuple<int, std::string>`. Scratch setters select these concrete types; kind/accessor methods inspect them, and wrong casts exercise `std::bad_any_cast`. Tuple elements are copied through named accessors. Any replacements are prepared and swapped, preserving old values on failure. `clear_scratch` resets the value. This finite adapter neither exposes arbitrary native RTTI types to Lua nor infers conversions among them.

`tests/test_advanced_values.lua` covers repeated union transitions, tag/type errors, deep clones, bitfield/counter bounds, packed storage, binary data, map updates/erasure/snapshots, all scratch alternatives, GC and idempotent close. The embedded allocation-counting host runs the script, sweeps native allocation failures through union replacement/cloning, map insertion and any/tuple replacement, injects Lua result-publication failures and leaves active union/variant/any values alive for VM shutdown. Automatic union/any mappings, live map iteration, general container-adaptor typemaps, general weak-ownership policies and the remaining standard-library inventory remain pending.

## Async task adapter

`examples/async_task.hpp` implements a reusable native worker/promise/shared-future adapter without Lua dependencies. `demo::AsyncTask` exposes it as an owned class through the regular binding generator. `AsyncTask.new(value, delay_ms, fail)` starts a worker that waits for the delay and then computes checked `value * 2` or raises a native failure when `fail` is true. Delays and wait timeouts must be in `[0, 60000]` ms. The delay is interruptible through a condition variable; the adapter's work is bounded. Adapting an arbitrary blocking native operation will require its own cooperative-cancellation contract.

`state()` reports `TaskState.running`, `succeeded`, `failed` or `cancelled`. `ready()` polls without blocking, and `wait(timeout_ms)` returns whether the future is ready. Waiting never enters Lua or invokes callbacks. `result()` is nonblocking: it raises while running, returns the same successful value repeatedly, or rethrows the stored worker/cancellation exception repeatedly. Worker exceptions and checked-overflow failures use the usual generated Lua error boundary. `cancel()` returns true only for the first accepted request before terminal completion. A request races with completion under a mutex; accepted cancellation wins and is observable through `wait`/`state`/`result`.

`on_complete(function(state, value) ... end)` retains one handler, replacing a previous handler; nil clears it. It can be installed after completion. `dispatch()` explicitly delivers a ready completion record on the task's creating host thread and returns true when invoked. Pending, handler-free or already-delivered tasks return false. Completion values are the result on success and zero on failure/cancellation; use `state` or `result` to inspect an error. Native workers publish only C++ values/exception pointers and never touch Lua. This per-task dispatch boundary can be driven by a host loop, but there is no automatic global scheduler.

Delivery removes the handler and marks the event delivered before calling it. Reentrant dispatch cannot redeliver, self-unregistration is safe, and callback exceptions do not retry delivery or invalidate the future. A nonempty replacement after delivery is rejected. Receiver pins reject callback attempts to close an actively dispatched task. Other callback lifetime/thread rules apply, including explicit close to break self-capturing handler cycles. The native adapter also rejects dispatch/handler registration from a thread other than its creator.

Explicit close, GC and Lua shutdown request cancellation, join the worker and release the retained handler without calling it. There are no detached workers and no Lua callback work after VM shutdown. `AsyncTask.live()` and `workers()` expose native counts for tests; a running worker may not yet have started immediately after construction. Workers belong to their task, so keep the generated library loaded until tasks close. General background work with unbounded/noninterruptible joins needs a separate lifetime policy.

`tests/test_async.lua` covers success/failure/overflow, polling, timeout validation, cancellation/completion races, repeatable results/errors, explicit owner-thread delivery, at-most-once behavior, handler replacement/clearing, callback errors, reentrancy, close and GC. The embedded host checks native allocation balance, tests native wrong-thread dispatch, sweeps allocation failures through partial task/future/thread construction, and closes a VM with a pending task and retained handler. The separate generator adapter covers native coroutine lifetimes; generic `std::future`/awaitable typemaps, general executors, event-loop integration and arbitrary concurrent Lua dispatch remain pending.

## Operator and iterator adapters

`feature_library::Series` in `examples/feature_library.hpp` defines native C++ arithmetic (`+`, `-`, unary `-`, scalar `*`, `/`, `%`), bitwise operations, comparisons, indexing, call and explicit string/bool conversions. `demo::IntSeries` provides named, target-independent adapters selected through the normal class policy. `SeriesCursor` is a native iterator handle over shared storage. Other backends can bind these entry points without a Lua dependency.

`examples/demo_protocols.lua` supplies a Lua layer over generated userdata. Load it with `local Series = dofile('examples/demo_protocols.lua')(demo)`; `just lua-repl` preloads `Series`. `Series({20, 21})` creates a protocol object. The layer implements `+`, binary/unary `-`, multiplication in either scalar order, `/`, `//`, `%`, `&`, `|`, binary/unary `~`, shifts, value comparisons, `#`, numeric indexing/assignment, calls, `tostring`, `pairs` and Lua 5.4 `__close`. It does not modify generated userdata metatables. Native named methods remain usable directly through `demo.demo__IntSeries`.

Division accepts a series on the left and a checked native integer scalar on the right. The two rounding conventions are explicit:

| Series operation | Native named adapter | Rule | Result element for -7 and divisor 3 |
| --- | --- | --- | --- |
| `s / divisor` or `s:quotient(divisor)` | `quotient` invokes C++ `operator/` | Integer quotient truncated toward zero | -2 |
| `s:remainder(divisor)` | `remainder` invokes C++ `operator%` | Remainder has the dividend's sign | -1 |
| `s // divisor` or `s:floor_quotient(divisor)` | `floor_quotient` | Quotient rounded toward negative infinity | -3 |
| `s % divisor` or `s:floor_modulo(divisor)` | `floor_modulo` | Modulo has the divisor's sign | 2 |

`/` here exposes the native integer operation; it does not produce Lua floating-point fractions. `//` and `%` follow Lua's floor convention through native adapters. Corrections use only checked integer quotient/remainder, preserving exact endpoints. All four operations reject zero (even for empty series), and reject `INT_MIN` with divisor -1 before executing C++ `/` or `%`. This uniform overflow policy also applies to floor modulo, even though its mathematical result would be zero. Operations create independent results; failure after processing earlier elements leaves the source and its active cursors unchanged. Scalar-left division/modulo has no adapter and is rejected.

Indices are one-based and checked, including minimum integers and fractional keys. Out-of-range access raises an error; use `pairs` for iteration rather than `ipairs`, whose nil-terminated indexing contract differs. Assignment updates an existing element. Operations return independent owned series, `clone` copies, and comparisons are lexicographic by value. Equal contents do not imply Lua identity. Native arithmetic checks integer overflow before conversion and rejects unequal binary-operation lengths. Shifts reject negative values and counts outside `[0, numeric_limits<int>::digits)`; left shifts also check result range. There is no target-language layout assumption. The tested Linux toolchain uses 32-bit `int`. Lua truthiness cannot be overloaded: use `:nonempty()` for the C++ bool conversion, including for an empty series.

A cursor retains native storage independently of the series facade. Closing a series leaves existing cursors usable. Every successful element mutation increments a version; `next` checks it before accessing C++ iterators and reports `series mutated during iteration` if changed. `pairs` closes its cursor on exhaustion or iterator error. Early loop exit/body errors release abandoned cursors through GC. Explicitly close or use `<close>` for deterministic series cleanup; ordinary proxies release their hidden userdata through GC. Protocol tables follow normal Lua semantics, so `rawset` can bypass their regular indexing behavior; it does not grant access to the private native handle.

`tests/test_protocols.lua` covers every supplied protocol, strict types/bounds, empty values, ordering/identity, deep copies, overflow, shift validation, mutation, closed handles, iterator ownership and early-exit/error GC. It tests named adapters without protocols and checks division/remainder against Lua's separate 64-bit integer oracle using native endpoints, both divisor signs and deterministic signed samples. The embedded host runs the script with native allocation accounting, injects Lua failure when publishing operator/cursor/string results, sweeps partial native allocation failures for all four division operations, and closes a VM with native series/cursors still alive. Automatic operator-policy generation and arbitrary iterator categories remain pending; selected literal and three-way comparison adapters are documented with the compile-time examples.

## Hierarchy adapters

`examples/feature_library.hpp` defines an abstract `Entity`, virtual bases `Named` and `Valued`, a final diamond implementation, a named-only implementation and a final callback-backed implementation. `EntityView`, `NamedView` and `ValueView` in `examples/demo.hpp` are selected concrete facades without base classes. Each contains a shared pointer to a native interface; constructors reject null and are hidden from Lua by policy. Factories select concrete implementations. The facade interfaces have no Lua dependency and can be used by future backends.

`as_named` and `as_value` use `std::dynamic_pointer_cast`, returning nil on an incompatible runtime type. `NamedView::as_value` demonstrates a checked crosscast. `as_entity` uses the compiler's virtual-base conversion, and `same_entity` compares native `Entity` pointers after conversion. Separate Lua userdata can refer to one native object, so userdata equality does not imply native identity. Pointer offsets and C++ layouts stay inside C++.

Each facade holds independent shared ownership. Closing/collecting a root leaves other views usable; the native entity is destroyed after the last view closes or is collected. `EntityView.live()` counts virtual roots rather than facade objects. Close is idempotent. Methods on closed facades fail through normal generated checks; callbacks cannot close an active virtual-call facade.

`make_scripted(name, seed, value_override)` supplies a retained `std::function<int(int)>` to a C++ `Valued::value` override. Virtual dispatch calls the closure with the current seed; `set_value` changes the seed. Nil is rejected during construction. Callback errors and invalid results unwind through virtual dispatch and become Lua errors. The callback lifetime, thread and shutdown contract below applies, including explicit close for self-capturing cycles. This adapter exposes a selected override; automatic arbitrary interface implementations and general virtual directors remain pending.

`tests/test_hierarchy.lua` runs as a module and inside the embedded allocation-counting/sanitizer host. It covers identity, successful/failed casts, mutation, last-owner destruction, binary names, GC, retained closure release, virtual callback errors, strict results and close reentrancy. Embedded tests force Lua allocation failure when publishing factory/cast results and leave shared views plus a Lua override alive for shutdown. Direct hierarchy binding, arbitrary native borrow invalidation and automatic inherited-member discovery remain future work.

## Callback policies and lifetime rules

Callback lifetimes are explicit because `std::function` signatures do not say whether native code retains a copy. Add top-level rules to a version-1 policy:

```json
{
  "version": 1,
  "classes": {"api::EventSource": {"ownership": "owned"}},
  "callbacks": {
    "api::invoke": {"1": "call"},
    "api::EventSource::set_callback": {"1": "retained"}
  }
}
```

Keys identify real qualified callable names. Parameter indices are positive, one-based C++ parameter positions, excluding Lua's method receiver. Missing policies, stale/unselected callable names, non-callback parameter indices and unsupported lifetime values are errors. Empty callback-rule objects and noncanonical indices such as `0` or `01` are rejected. A policy containing only `callbacks` does not implicitly change class selection.

- `call` callbacks are valid only during that native wrapper call. Scope exit revokes them even if native code incorrectly retained a copy, and drops their Lua references. A later native invocation fails before touching the Lua state or a recycled registry reference.
- `retained` callbacks keep the Lua closure and the Lua coroutine that registered it alive until native code unregisters or destroys every copy. An event handler can unregister/replace itself when the native library keeps a callable snapshot alive during invocation, as the demo `EventSource::emit` does. Passing nil supplies an empty `std::function`; the native API decides whether that clears a handler or is an error.
- Native invocation must run on the host thread that registered the callback. Wrong-thread calls throw a C++ error before using any Lua API; they are not implicitly dispatched. Off-thread destruction instead queues an intrusive release record without allocating or touching Lua. Bound-call entry/exit drains releases on the owner thread. If Lua cannot reserve cleanup stack space, release is deferred until a later entry or VM shutdown.
- A VM token marks outstanding native copies dead during Lua shutdown. Copies that outlive `lua_close` reject invocation and can be destroyed on another thread without accessing the freed VM. The embedding application must serialize Lua access and keep generated native code loaded while any native callback copy exists; a C++ function object cannot safely outlive the shared library containing its code.

Callback arguments use supported value codecs: scalars, enums, strings/views/C strings, dense sequences, products and unambiguous optional values. Results must be owning/copyable supported values or void. Automatic codecs for native reference/pointer/view results, class-valued or nested callbacks, direct function pointers and returned native `std::function` objects remain product work. The returned-callable and class-value frame sections demonstrate explicit adapters for those two callable mechanisms. Borrowed native arguments must remain valid until they are copied into Lua. Callback functions may reenter bindings, but cannot yield across the native C call boundary. Lua errors (including non-string errors), invalid result types, numeric overflow and allocation failures become C++ exceptions with bounded messages. No error-object `__tostring` metamethod is invoked during conversion. The outer wrapper raises its final Lua error after C++ cleanup; native C++ can catch a callback exception deliberately.

Receivers and selected-class arguments pin their complete owner chains during native calls. Explicit close or `__close` of an active handle/owner fails instead of deleting native storage while a callback can still use it. Reentrant ordinary methods and native self-unregistration remain allowed. Pins unwind on both success and errors. Native destructors must not invoke throwing Lua callbacks; use an explicit finalize operation before nonthrowing destruction if notification is needed. Native `noexcept` APIs with callback parameters are rejected unless adapted through an error-status boundary. The demo's `c_style_callback` facade catches callback exceptions in a nonthrowing function-pointer/context trampoline and rethrows only after the C-style operation returns; it has no Lua dependency.

Retained handlers that capture their own Lua owner can create a registry → closure → owner → native callback cycle. Explicit `clear_callback` or `close` breaks it; GC alone is not promised to collect it. Tests demonstrate both noncapturing-handler GC and explicit cycle release. Callback errors do not roll back native side effects or unregister retained handlers automatically.

`tests/test_callbacks.lua` exercises capturing, nested/multiple/void callbacks, binary/container values, strict result validation, Lua errors, forbidden yields, wrong-thread calls, stale lifetimes, registry-reference reuse, originating-coroutine retention, self-unregistration, close protection and retained cycles. The embedded-VM cleanup test runs it under native allocation accounting, sweeps partial VM/reference-registration failures, injects Lua allocation failure in callback argument publication and callback bodies, forces C++ allocation failure while reading a callback result, and invokes/destroys native copies after VM shutdown. The small independent scalar-module test ensures callback additions do not impose policies or callback runtime dependencies on ordinary bindings.

## Versioned native ABI boundary

`examples/native_abi.h` is a C11-compatible interface to a separately compiled C++ library. `native_abi.cpp` requires exceptions and RTTI to be disabled at compile time; its implementation still uses virtual dispatch and template instantiations. CMake applies those options privately and builds static and shared variants from the same object file. The Lua module links the shared variant; the allocation-counting embedded host links the static variant. The enabled-exception facade in `native_boundary.cpp` keeps the generated bridge's error-handling requirements intact.

```lua
local n <close> = demo.demo__NativeBoundary.new(demo.demo__BoundaryOperation.add, 20, 'binary\0payload')
assert(n:with_callback(function(v) return v + 1 end, 21) == 42)
assert(n:payload() == 'binary\0payload')
local info = demo.demo__native_abi_info()
assert(info.version == 1 and info.flags == 3) -- exceptions and RTTI disabled
```

Versioned `rn_v1_*` C symbols use opaque native handles, fixed-width integers, explicit byte lengths and status codes. Creation checks the interface version and allocator structure size; capabilities report pointer/size widths and byte order. This is a process-local ABI requiring compatible target data models, not a portable serialized wire format. Payloads are copied into library-owned storage and copied back into Lua strings. Handles retain the caller's allocator callbacks/context; the caller must keep that context alive and supply normal maximum alignment. Native allocation callbacks return null on failure. Partial creation frees storage through the same allocator. The library imposes a 1 MiB payload limit and checks signed 32-bit operation ranges before writing results.

The synchronous C callback is `noexcept`. Its C++ trampoline captures exceptions, returns a failure status through the disabled-exception library, and rethrows only after control returns to the enabled-exception facade. Lua errors, invalid return types, range errors, attempted yields and reentrant close use the existing protected callback bridge. The native library never owns a Lua registry reference and never performs a Lua runtime jump. A shared facade keeps the same native handle alive independently of its original wrapper. Destroy must be synchronized with all concurrent handle use; this example provides no concurrent mutation or retained C callbacks.

`tests/test_native_abi.cpp` exercises static and dynamically loaded copies, public/hidden symbols, status/version checks, overflow, binary payload copies, callback failure preservation and partial allocator failures. The C11 header is compiled independently. `tests/test_native_boundary.lua` checks Lua behavior; the embedded host sweeps native/Lua allocation failures through construction, sharing, payload publication and callbacks, checks allocation balances, and leaves a live handle for VM-shutdown cleanup.

This is an explicit reusable example shim. Automatic C ABI generation, general calling-convention/export policies, Windows/macOS validation, cross compilation and a second-language backend remain roadmap work.

## Native modules, header units and precompiled headers

`examples/modules` provides a C++20 named module, an exported template partition, a separate implementation unit and an imported header unit. `facade.cpp` imports them and defines ordinary functions declared in `demo.hpp`. Rosetta parses that ordinary header and generates Lua bindings; the module's implementation types and BMI files stay in the native build. Lua exercises the selected partition template, header-unit result, copied binary strings, native exceptions and recovery:

```lua
assert(demo.demo__native_pch_shift(40) == 42)
if demo.demo__native_modules_available() then
    assert(demo.demo__native_module_shift(40) == 42)
    assert(demo.demo__native_header_unit_answer() == 42)
    assert(demo.demo__native_module_text('module\0', false) == 'module\0module\0')
end
```

`cmake/NativeModuleExample.cmake` explicitly builds Clang BMI/object dependencies, uses separate BMI directories per configuration, and compiles the partition and primary module into linkable objects. The header unit contributes declarations through its BMI. Native objects and BMIs use the same C++20, PIC, global compiler, build-configuration and sanitizer flags. This follows the separate BMI/object and explicit dependency model described in [Clang's module guide](https://clang.llvm.org/docs/StandardCPlusPlusModules.html). The example targets reuse these objects in both the Lua module and embedded allocation-counting host. This helper is an example build graph, not an installed universal module-discovery API.

The module example defaults on with a Clang native compiler. `ROSETTA_BUILD_MODULE_EXAMPLE=OFF` retains explicit disabled-capability functions; module operations then throw a descriptive error rather than silently substituting a header implementation. Other native compilers default off, and explicitly enabling this Clang-specific example with another compiler fails configuration. The generator itself remains C++17; the demo is C++20. Cross-compilation flags, compiler launchers, arbitrary module graphs, imported standard-library modules, automatic module selection and compiler/version-compatible BMI distribution remain future tooling work. BMIs must be rebuilt with the consuming compiler and configuration.

Separately, `examples/pch.cpp` is built in a native target using CMake `target_precompile_headers`. The PCH supplies the configuration macro used by the facade; omission fails compilation. Rosetta parses the ordinary public signature without consuming a compiler-specific PCH. This demonstrates linking an API whose implementation uses a PCH, not automatic extraction of declarations hidden inside PCH artifacts.

`tests/test_native_build.lua` executes the module/PCH paths and failure recovery in the standalone runtime and embedded host. Native RAII counters and native/Lua allocation-failure sweeps cover string construction, module exceptions and result publication. `tests/test_native_build.cpp` builds a real project in a path containing spaces, verifies a no-op build, edits a re-exported partition, header unit and PCH configuration and checks the changed executable results. It verifies separate Debug/Release BMI artifacts with Ninja Multi-Config and that an implementation-only type cannot be used by a module consumer and that disabling the module capability still builds and reports failure explicitly. These fixtures establish native build integration; direct module/PCH AST discovery and replay manifests remain pending.

## Native declaration and storage forms

`examples/native_forms.cpp` uses ordinary C++ facades for forms that should stay inside their defining translation unit or native object lifetime:

- `NativeStationary` owns a native object with deleted copy/move constructors and assignments. Shared facade handles preserve its native identity without copying or moving it. `same(other)` compares that identity without exposing an address. `readonly()` is an anchored borrow; closing its particular owner invalidates that borrow even when another shared facade keeps the native object alive.
- The native object has a const string member and a mutable inspection counter. `text()` uses a `decltype(auto)` getter returning a const reference, then copies it while the native owner is pinned. Calling this const operation increments the counter; a later string-copy or Lua-publication failure does not roll that native effect back. Counter overflow is checked. Per-object access follows the VM owner-thread rule. Static atomic `setting()`/`set_setting()` expose native static data shared by these objects, not a per-Lua-state setting. Tests restore the setting.
- `native_internal_add` adapts a local callable class, an unnamed aggregate and a standard-layout/trivial aggregate initialized with C++20 designated members. Native `auto`, `decltype`, `[[nodiscard]]` and `[[maybe_unused]]` remain compiler-resolved implementation details. An internal-linkage function in an anonymous namespace performs checked arithmetic. No local or anonymous C++ name needs to appear in generated source.
- `native_matrix_shift` copies a fixed two-by-three Lua value into a real multidimensional C array, passes it by array reference, flattens it into a separate buffer and invokes a restricted-pointer operation. Input and output arrays are distinct, satisfying the no-alias contract rather than guessing from Lua object identity. Overflow discards the private partial output; native errors and Lua-publication failures leave the input table unchanged. Both dimensions are validated by the existing nested fixed-array codecs.
- `native_storage_traits()` exports compile-time copy/move/aggregate/layout traits, an empty marker used with `[[no_unique_address]]`, and the restricted-pointer extension capability. No native layout, padding, address or raw pointer is transported. The Clang/GNU restricted qualifier stays inside the native implementation; other compilers report the capability as false and use the same copied-buffer operation.

```lua
local s <close> = demo.demo__NativeStationary.new('stable\0payload', false)
local shared <close> = s:share()
assert(s:same(shared))
assert(s:text() == 'stable\0payload')
assert(shared:inspections() == 1)
assert(demo.demo__native_internal_add(20,22) == 42)
assert(demo.demo__native_matrix_shift({{1,2,3},{4,5,6}},40)[1][2] == 42)
```

The public constructor's failure flag exercises partial native construction with an owned string. `tests/test_native_forms.lua` checks identity, readonly expiry, independent close, mutable/const/static behavior, binary data, overflow, malformed dimensions and recovery. The embedded host counts native allocations around the script, sweeps native/Lua failures through construction, sharing, readonly views and copied text, injects partial matrix publication failures, and leaves an anchored immovable object alive for VM shutdown. The native translation unit statically verifies that the object is neither copyable nor movable and that the chosen aggregate has the claimed storage traits. Direct local/anonymous-type discovery, arbitrary shape/direction inference, general restrict modelling and static-variable generation remain broader generator work.

## Returned native callable handles

`examples/callable_library.hpp` returns actual `std::function<std::string(int)>` values from native factories: a stateful captured functor, a `std::bind_front` member function retaining a shared target, a native free-function pointer and an empty function. `examples/callables.cpp` moves those results into an owned `NativeCallable` facade. The native library and facade have no Lua dependency; a future backend can bind the same handle operations. This is an explicit callable-result adapter, not general automatic `std::function` result generation.

```lua
local f <close> = demo.demo__make_native_capture('answer:',40)
assert(f:invoke(2) == 'answer:42')
local copy <close> = f:copy()
local shared <close> = f:share()
assert(f:same_slot(shared) and not f:same_slot(copy))
assert(shared:invoke(1) == 'answer:43')
assert(copy:invoke(1) == 'answer:43') -- independent copied capture
```

`copy()` copies the native function into a distinct slot. Stateful functor copies have independent prefixes/seeds; bound-method copies retain the same shared native target, as dictated by the native callable's captured shared pointer. `share()` retains the same mutable slot. `clear()` empties that slot for all its shared handles, while independent copies retain their own functions. `empty()` reports callable state; invoking an empty function throws `std::bad_function_call`, translated by the normal generated exception boundary. Closing a wrapper invalidates it and releases its slot ownership; other shared wrappers remain usable. Raw code addresses and member-pointer representations never cross the boundary.

Capture/bound invocations check arithmetic overflow and prepare the output string before changing native state. Native allocation/range failure leaves the seed unchanged. Once native result preparation succeeds, later Lua publication failure does not roll the update back. The native capture, bound target and slot expose live counters for cleanup verification. Calls use the owning Lua thread; these mutable function/target objects do not promise concurrent access.

`make_native_callback(function)` is an explicitly retained Lua callback input stored in the same facade; nil makes an empty callable. Copies preserve the generated callback state's registry ownership. The slot holds its native function through a shared snapshot during invocation, so a reentrant Lua callback can clear its slot without destroying the executing function or losing mutable native capture updates. Closing an active receiver remains rejected by owner pins. Retained callback cycles still require explicit clear/close; the adapter does not infer cycle collection. Callback errors, invalid returns, forbidden yields and closed-VM checks retain the existing bridge rules.

`examples/demo_callables.lua` adds optional Lua `__call` and `__close` protocols over handles. The REPL preloads it as `Callables`; try `f = Callables.capture('answer:',40); print(f(2)); f:close()`. Its factories include `capture`, `bound`, `pointer`, `empty`, `from_lua` and `wrap`, and instances provide `copy`, `share`, `clear`, `empty` and `close`. The underlying handle remains accessible as `handle`; its userdata owns native cleanup if a protocol table is garbage-collected.

`tests/test_callables.lua` checks native result factories, copy versus shared/bound identity, captured binary text, empty functions, arithmetic failure recovery, recursive callbacks, self-close rejection, self-clear plus GC, retained copies, callback errors and Lua protocols. The embedded host runs the script, sweeps native/Lua allocation failures through all factories and invoke/copy/share publication, and checks native allocation/capture/target/slot balances. VM-shutdown fixtures leave a native capture, bound member target and retained Lua callable alive. Generic callable signature transport, move-only callable erasure and class-valued callback conversion remain future work.

## Class-value callback adapter

`examples/class_callback_library.hpp` defines a real native `std::function<Record(Record)>` operation. `Record` owns a binary-safe string and integer and has counted copy/move/destruction. A synchronous frame adapter converts the class-valued input and result through ordinary C++ facade operations plus an existing call-only `std::function<void()>` notification. Neither the native operation nor its frame knows about Lua. This is explicit class-value adaptation; direct automatic class-valued callback codecs remain unsupported.

The REPL preloads `ClassCallbacks` from `examples/demo_class_callbacks.lua`:

```lua
local input <close> = demo.demo__CallbackRecord.new('record\0text',40)
local result <close> = ClassCallbacks.transform(input,function(record)
    record:set_number(record:number()+2)
    return record
end)
assert(input:number()==40 and result:number()==42)
```

The native operation receives a value copy of the input. The Lua helper obtains another owned copy for the callback and closes that temporary on callback exit, including errors. Mutations affect that copy, not the original input. Returning the argument is supported: its value is copied into the native result before the helper closes it. To retain an argument beyond the callback, call `record:copy()` and later close that independent handle. Saving the helper's original argument handle does not extend its validity after the helper closes it.

The callback must return an open `CallbackRecord` handle. Readonly inputs/results are accepted because transport copies values; mutable operations on a readonly handle still fail. The helper never closes an arbitrary result handle supplied by the caller. The caller owns that result handle according to normal close/GC rules. A fresh handle returned by a callback can be collected after its value is copied. Do not return a handle declared as a callback-local `<close>` variable: Lua closes it before the caller can read it; return the argument or another open copy instead. The returned native result is independently owned and survives closing its source handles.

For explicit control, construct `ClassCallbackFrame` and call `frame:transform(input, function() ... end)`. Inside the notification, `frame:argument()` returns an owned copied record and `frame:set_result(record)` copies the selected result. Repeated selection replaces the result only after the new value has copied successfully. Returning without selecting a result is an error. `argument()`/`set_result()` outside an active invocation are errors. `depth()` reports native nesting. `ClassCallbacks.with_frame(frame,input,callback)` applies the same Lua protocol using an existing frame.

Nested calls on the same frame use stack-scoped native invocations; returning or throwing restores the prior invocation and its selected result. A 64-level native nesting limit fails before replacing the current frame. The frame retains no callback or invocation record after the synchronous operation returns. Existing receiver/input owner pins reject reentrant close during invocation. Callback failures, wrong result types, nil/closed results, attempted yields, native allocation failure and Lua publication failure unwind native record/scope ownership normally. The VM owner-thread rule applies; a frame is not a concurrent dispatcher. Copy transport provides no general borrowed or consuming class-callback policy.

`tests/test_class_callbacks.lua` exercises copied arguments/results, native and helper retention rules, binary strings, mutation isolation, readonly values, explicit result replacement, nested calls, inactive/missing results, reentrant close, errors/yields and recovery. The embedded host independently tests the native result-copy strong guarantee and nesting-limit restoration, sweeps native/Lua allocation failures through record/frame construction and simple/error/nested transformations, checks record/frame/allocation balances and leaves owned records/frames for VM-shutdown cleanup.

## Native awaitable and cooperative executor

`examples/executor_library.hpp` supplies a real C++20 coroutine with a custom `Tick` awaiter. `co_await` schedules that coroutine back onto a native FIFO executor; the host explicitly calls `run_one()` on its creating thread. The implementation uses initial/final suspension, promise-owned result/error publication and explicit frame destruction following the [C++ coroutine lifetime model](https://eel.is/c++draft/dcl.fct.def.coroutine). It has no Lua dependency and starts no worker threads.

```lua
local executor <close> = demo.demo__AwaitExecutor.new()
local task <close> = executor:submit(40,2,'answer:',false,nil)
assert(Awaitables.result(executor,task,3)=='answer:42')
```

`submit(seed, steps, prefix, fail, callback)` creates an initially suspended task and queues its first execution. Steps must be nonnegative. Each iteration first awaits a queue turn, then transforms the integer with the retained callback, or increments it with checked arithmetic when callback is nil. A task with N steps needs N+1 dispatches; zero steps completes on the initial dispatch. Completion returns binary-safe `prefix .. decimal_integer`. The failure flag throws after the steps, exercising native error storage. Each coroutine owns its prefix and callback parameters until completion/cancellation/destruction.

`run_one()` skips dead/cancelled queue entries and resumes at most one runnable task. It returns true if a task resumed, including a step that stored a task failure; empty queues return false. `pending()` counts queued runnable tasks, excluding the currently executing task. Promise exceptions store native errors, callback errors, conversion errors and allocation failures in the task; they do not unwind across the coroutine boundary into Lua. Reentrant dispatch and wrong-thread operations fail before resuming a coroutine. Callback attempts to yield keep the existing protected-bridge prohibition.

`AwaitPhase` is `pending`, `succeeded`, `failed` or `cancelled`. `task:result()` copies the completed string, rethrows a stored failure, or reports pending/cancelled state. Repeated success/error inspection is supported. Native/Lua result-publication failures leave completed state intact. A later Lua allocation failure publishing a dispatch result does not roll back an already executed native step; inspect state or pump again. `task:share()` retains the same native task; closing one wrapper leaves other owners usable.

Cancellation is permitted while initially or subsequently suspended. It removes logical scheduling eligibility, destroys the frame, releases callback captures and marks the task cancelled; repeated/terminal cancellation returns false. Cancellation of the actively executing task is rejected. The ready queue holds weak task references; it cannot keep discarded tasks alive. Dispatch holds one strong task reference through resumption, so a callback may close its last Lua task wrapper safely: cleanup occurs when that dispatch releases the task, after it has suspended or completed. Completed frames are destroyed immediately, freeing callback registry captures even while a task handle retains its output/error. Destructors never execute callbacks.

`executor:share()` retains the same driver state. Tasks also retain the native executor state, so closing an executor wrapper does not cancel independently owned tasks. Keep a driver handle to continue pumping, or cancel/close tasks before discarding all drivers. Executor/task operations require the creating thread; global live/frame/allocation counters are metadata. Mutable executor/task access must be serialized; this is not a concurrent event queue. Retained Lua closures that capture their own task can create the existing registry-owner cycle; explicit cancel/close breaks it.

`examples/demo_awaitables.lua` is preloaded as `Awaitables` in the REPL. `drain(executor,budget)` performs at most the nonnegative integer budget of dispatches and returns `{count, pending}` as two Lua results. `result(executor,task,budget)` pumps until that task becomes terminal, the queue empties or the budget is exhausted, then calls `task:result()`. Budgets default to 1000. Pumping can advance other tasks in the same FIFO; supply the task's executor. The helpers introduce no hidden event loop, timers or thread entry. Other awaiters, I/O readiness sources and host event-loop integrations remain extension points.

`tests/test_awaitables.lua` checks FIFO turns, initial/awaiting/completed frames, callbacks, cancellation, shared ownership, native/callback/overflow/yield errors, repeatable results, reentrant dispatch/cancel rules, close during invocation, retained-capture release and bounded helpers. The embedded host rejects native operations from another thread, drives 128 tasks under native allocation exhaustion, sweeps construction/submission/dispatch/callback/result/share publication failures, checks task/executor/frame/body/allocation balances, and leaves a suspended native task with a retained Lua callback for VM shutdown. This demonstrates a selected awaitable/executor mechanism; arbitrary awaitable discovery and scheduler-policy generation remain broader work.

## Independently generated module interoperability

`examples/interop/a.hpp` and `b.hpp` define separate ordinary C++ facade APIs, each parsed into its own generated Lua module (`interop_a` and `interop_b`). Both link `rosetta_exchange`, a shared native library that owns the same opaque C++ `State` type and a process-local registry. This is an explicit native interoperability adapter: each module keeps its own userdata/class tags, and the shared library supplies checked identity and ownership exchange.

The REPL loads both modules and the supplied `Interop` helper:

```lua
local a <close> = Interop.new_a('shared\0text',40)
local b <close> = Interop.to_b(a)
assert(a:ticket()==b:ticket())
assert(b:shift(2)==42 and a:value()==42)
a:close()
assert(b:text()=='shared\0text')
```

Without the helper, construct `InteropA.interop_a__Record.new(text,value)` and import with `InteropB.interop_b__Record.from_ticket(source:ticket())`. Either module can create or import. Exported tickets use a fixed record-type/version prefix and a strictly parsed monotonically assigned integer ID. IDs are never reused in the running registry; registration failures may consume an ID. Malformed numbers, leading zeros, embedded NUL/trailing text, overflow, other type/version prefixes and expired/unknown IDs are rejected. A matching live ticket resolves the same native state rather than copying it: mutations and binary text changes are observed through every imported owner. `live()` and `tickets()` expose native instance and registered-record counts from either module.

Ticket strings hold no native ownership. Keep a source owner alive until import. Import acquires independent shared ownership, so closing/collecting a source handle or closing its entire Lua VM leaves imported owners valid. Last-owner destruction removes the weak registry entry without allocating. Tickets identify live objects within this process and shared-library instance; they are not persistent object serialization. Use the same shared native library and a compatible native compiler/standard-library ABI for both modules.

`ticket()` and `share()` require mutable handles. Readonly anchored views support inspection but cannot silently produce writable imports or shared mutable wrappers. A view expires when its particular facade owner closes, even if another module's imported owner keeps the underlying native object alive. Directly passing one module's userdata to a typed function in the other module is rejected by existing class-tag validation. The explicit ticket adapter is the supported exchange path; it does not merge unrelated metatables, infer a common object layout, or implement general automatic cross-module type registration.

The registry and native record data use native mutexes. No Lua operation runs under those locks. Native string copying/range checks complete before publication; errors unwind locks/temporary owners normally. Clearing `package.loaded` or collecting module tables is supported while native ownership remains elsewhere; keep generated/shared native code loaded until all its handles and finalizers are gone. This example does not promise manual native-library unloading with live handles.

CMake generates/compiles both modules and links the common shared library, with normal build-tree dependency loading. The `lua-repl` target depends on both generated modules. `tests/test_interop.lua` runs in fresh Lua processes with both loading orders and checks identity, shared mutation, direct foreign-handle rejection, readonly rules, independent close/GC, stale/type/version/format rejection and module-table collection/reload. The embedded host also transfers a ticket between independent Lua VMs, closes the source VM, and verifies the imported object remains alive. Native/Lua fault sweeps cover construction/registry insertion, imports, text/ticket publication, shared handles and readonly views, checking allocation/instance/registry balances. A final imported owner survives into the VM-shutdown fixture. General shared userdata ABI/type registration and foreign platform/loading matrices remain broader work.
