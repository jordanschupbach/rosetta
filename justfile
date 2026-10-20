set positional-arguments

build_dir := "build"
repl_build_dir := build_dir + "/lua-repl"
r_repl_build_dir := build_dir + "/r-repl"

# Show development commands.
default:
    @just --list

# Configure CMake; extra arguments pass through (e.g. just configure -DCMAKE_CXX_COMPILER=clang++).
configure *args:
    cmake -S . -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug "$@"

# Build the generator, C++ tests and generated Lua module.
build: configure
    cmake --build {{build_dir}} --parallel

# Run the C++ suite, CLI smoke test and native Lua integration.
test: build
    ctest --test-dir {{build_dir}} --output-on-failure

# Run the reusable native synchronization helper under ThreadSanitizer.
concurrency-tsan:
    cmake -E make_directory {{build_dir}}/sanitizers
    clang++ -std=c++17 -g -O1 -fsanitize=thread -fno-omit-frame-pointer -pthread -I examples tests/test_native_concurrency.cpp -o {{build_dir}}/sanitizers/native_concurrency_tsan
    TSAN_OPTIONS=halt_on_error=1 {{build_dir}}/sanitizers/native_concurrency_tsan

# Build and execute the Lua example.
demo: build
    ctest --test-dir {{build_dir}} --output-on-failure -R '^lua_integration$'

# Enter Nix and run Lua runtime, native, CLI and shared frontend/build tests.
lua-test:
    nix develop path:. --command just _lua-test

[private]
_lua-test:
    cmake -S . -B {{build_dir}}/lua-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_MODULE_EXAMPLE=ON -DROSETTA_BUILD_EXAMPLES=ON -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/lua-tests --parallel
    ctest --test-dir {{build_dir}}/lua-tests --output-on-failure --parallel

# Build and test the R6 example (requires R and the R6 package).
r-demo:
    just configure -DROSETTA_BUILD_R_EXAMPLES=ON
    cmake --build {{build_dir}} --parallel --target rosetta_r_demo
    ctest --test-dir {{build_dir}} --output-on-failure -R '^r_integration$'

# Enter Nix and run R runtime, capability, CLI and shared frontend/build tests.
r-test:
    nix develop path:. --command just _r-test

[private]
_r-test:
    cmake -S . -B {{build_dir}}/r-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=ON -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/r-tests --parallel
    ctest --test-dir {{build_dir}}/r-tests --output-on-failure --parallel

# Enter Nix and run Python runtime, capability, CLI and shared compiler/build tests.
python-test:
    nix develop path:. --command just _python-test

[private]
_python-test:
    cmake -S . -B {{build_dir}}/python-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=ON -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/python-tests --parallel
    ctest --test-dir {{build_dir}}/python-tests --output-on-failure --parallel

# Enter Nix, build the Python extensions, and open Python with demo and verify loaded.
python-repl:
    nix develop path:. --command just _python-repl

[private]
_python-repl:
    cmake -S . -B {{build_dir}}/python-repl -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=ON -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/python-repl --parallel --target python-repl

# Enter Nix and run Ruby runtime, capability, CLI and shared compiler/build tests.
ruby-test:
    nix develop path:. --command just _ruby-test

[private]
_ruby-test:
    cmake -S . -B {{build_dir}}/ruby-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=ON -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/ruby-tests --parallel
    ctest --test-dir {{build_dir}}/ruby-tests --output-on-failure --parallel

# Enter Nix, build Ruby extensions, and open IRB with examples loaded.
ruby-repl:
    nix develop path:. --command just _ruby-repl

[private]
_ruby-repl:
    cmake -S . -B {{build_dir}}/ruby-repl -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=ON -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/ruby-repl --parallel --target ruby-repl

# Enter Nix and run Octave runtime, capability, CLI and shared compiler/build tests.
octave-test:
    nix develop path:. --command just _octave-test

[private]
_octave-test:
    cmake -S . -B {{build_dir}}/octave-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=ON -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/octave-tests --parallel
    ctest --test-dir {{build_dir}}/octave-tests --output-on-failure --parallel

# Enter Nix, build Octave extensions, and open Octave with examples loaded.
octave-repl:
    nix develop path:. --command just _octave-repl

[private]
_octave-repl:
    cmake -S . -B {{build_dir}}/octave-repl -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=ON -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/octave-repl --parallel --target octave-repl

# Enter Nix, build the bindings, and start Lua with `demo` already loaded.
lua-repl:
    nix develop path:.#lua-repl --command just _lua-repl

# Runs inside the Nix shell; kept separate to avoid recursively entering Nix.
[private]
_lua-repl:
    cmake -S . -B {{repl_build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DROSETTA_BUILD_EXAMPLES=ON -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{repl_build_dir}} --parallel --target lua-repl

# Enter Nix and start R with R6, S3 and S4 demos and feature bindings loaded.
r-repl:
    nix develop path:.#r-repl --command just _r-repl

[private]
_r-repl:
    cmake -S . -B {{r_repl_build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=ON -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{r_repl_build_dir}} --parallel --target r-repl

# Run the generator; no arguments shows help (e.g. just run api.hpp --module api -o build/api.cpp).
run *args: build
    if [ "$#" -eq 0 ]; then set -- --help; fi; {{build_dir}}/rosetta "$@"

# Remove CMake build artifacts.
clean:
    cmake -E rm -rf {{build_dir}}

# Enter Nix and run Julia native/runtime and shared compiler/build tests.
julia-test:
    nix develop path:. --command just _julia-test

[private]
_julia-test:
    cmake -S . -B {{build_dir}}/julia-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=ON -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/julia-tests --parallel
    ctest --test-dir {{build_dir}}/julia-tests --output-on-failure --parallel

# Enter Nix, build Julia bindings, and open Julia with the examples loaded.
julia-repl:
    nix develop path:. --command just _julia-repl

[private]
_julia-repl:
    cmake -S . -B {{build_dir}}/julia-repl -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=ON -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/julia-repl --parallel --target julia-repl

# Enter Nix and run Java/JNI runtime, capability and shared compiler/build tests.
java-test:
    nix develop path:.#readme-java --command just _java-test

[private]
_java-test:
    cmake -S . -B {{build_dir}}/java-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=ON -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/java-tests --parallel
    ctest --test-dir {{build_dir}}/java-tests --output-on-failure --parallel

# Compile and run the Java example with its JNI libraries.
java-demo:
    nix develop path:.#readme-java --command just _java-demo

[private]
_java-demo:
    cmake -S . -B {{build_dir}}/java-demo -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=ON -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/java-demo --parallel --target java-demo

# Build C# bindings and run the .NET verification suite.
csharp-test:
    nix develop path:.#readme-csharp --command just _csharp-test

[private]
_csharp-test:
    cmake -S . -B {{build_dir}}/csharp-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=ON -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/csharp-tests --parallel
    ctest --test-dir {{build_dir}}/csharp-tests --output-on-failure --parallel

# Compile and run the C# example with its native libraries.
csharp-demo:
    nix develop path:.#readme-csharp --command just _csharp-demo

[private]
_csharp-demo:
    cmake -S . -B {{build_dir}}/csharp-demo -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=ON -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/csharp-demo --parallel --target csharp-demo

# Build Fortran bindings and run runtime, capability and tooling checks.
fortran-test:
    nix develop path:.#readme-fortran --command just _fortran-test

[private]
_fortran-test:
    cmake -S . -B {{build_dir}}/fortran-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=ON -DROSETTA_WARNINGS_AS_ERRORS=ON -DBUILD_TESTING=ON
    cmake --build {{build_dir}}/fortran-tests --parallel
    ctest --test-dir {{build_dir}}/fortran-tests --output-on-failure --parallel

# Compile and run the Fortran example.
fortran-demo:
    nix develop path:.#readme-fortran --command just _fortran-demo

[private]
_fortran-demo:
    cmake -S . -B {{build_dir}}/fortran-demo -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DROSETTA_BUILD_EXAMPLES=OFF -DROSETTA_BUILD_R_EXAMPLES=OFF -DROSETTA_BUILD_PYTHON_EXAMPLES=OFF -DROSETTA_BUILD_RUBY_EXAMPLES=OFF -DROSETTA_BUILD_OCTAVE_EXAMPLES=OFF -DROSETTA_BUILD_JULIA_EXAMPLES=OFF -DROSETTA_BUILD_JAVA_EXAMPLES=OFF -DROSETTA_BUILD_CSHARP_EXAMPLES=OFF -DROSETTA_BUILD_FORTRAN_EXAMPLES=ON -DBUILD_TESTING=OFF
    cmake --build {{build_dir}}/fortran-demo --parallel --target fortran-demo
