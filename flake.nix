{
  description = "Rosetta C++ binding generator development environment";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.systems.url = "github:nix-systems/default";
  inputs.flake-utils = {
    url = "github:numtide/flake-utils";
    inputs.systems.follows = "systems";
  };

  outputs =
    { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        lib = pkgs.lib;
        rWithR6 = pkgs.rWrapper.override { packages = [ pkgs.rPackages.R6 ]; };
        # SBCL plus the ASDF systems generated Common Lisp interfaces load.
        # sbcl.withPackages' wrapper currently fails nixpkgs' makeWrapper
        # check, so shells export the registry it would set.
        lispSystems =
          let
            closure = libs: lib.unique (lib.concatMap (p: [ p ] ++ closure (p.lispLibs or [ ])) libs);
          in
          closure [ pkgs.sbclPackages.cffi pkgs.sbclPackages.trivial-garbage ];
        lispHook = ''
          export CL_SOURCE_REGISTRY="${lib.concatMapStringsSep ":" (p: "${p}//") lispSystems}"
          export ASDF_OUTPUT_TRANSLATIONS="${lib.concatMapStringsSep ":" (p: "${p}:${p}") lispSystems}"
        '';
        # nixpkgs' compiler wrappers add -fmacro-prefix-map for C-family
        # dependencies to NIX_CFLAGS_COMPILE. gfortran rejects that flag with a
        # warning, which -Werror makes fatal, so shells with gfortran drop it.
        fortranHook = ''
          NIX_CFLAGS_COMPILE="$(printf '%s' "''${NIX_CFLAGS_COMPILE-}" | sed 's/-fmacro-prefix-map=[^ ]*//g')"
          export NIX_CFLAGS_COMPILE
        '';

        # README chunk shells. MEP runs each chunk with its language's usual
        # command (lua, Rscript, python3, ...) inside `nix develop .#readme-NAME`.
        # Entering the shell builds that backend's modules in
        # build/readme/NAME (never another backend's CMake cache), then points
        # the interpreter at them through its own startup mechanism, so the
        # chunk sees the names the README uses.
        backendOptions = {
          lua = "EXAMPLES";
          r = "R_EXAMPLES";
          python = "PYTHON_EXAMPLES";
          ruby = "RUBY_EXAMPLES";
          octave = "OCTAVE_EXAMPLES";
          julia = "JULIA_EXAMPLES";
          java = "JAVA_EXAMPLES";
          csharp = "CSHARP_EXAMPLES";
          fortran = "FORTRAN_EXAMPLES";
          lisp = "COMMON_LISP_EXAMPLES";
        };
        cmakeOptions =
          name:
          lib.concatStringsSep " " (
            lib.mapAttrsToList (
              backend: option: "-DROSETTA_BUILD_${option}=${if backend == name then "ON" else "OFF"}"
            ) backendOptions
            ++ lib.optional (name == "lua") "-DROSETTA_BUILD_MODULE_EXAMPLE=OFF"
          );
        chunkShell =
          {
            name,
            runtime,
            targets,
            setup,
          }:
          pkgs.mkShell {
            name = "rosetta-readme-${name}";
            packages = [ pkgs.clang pkgs.cmake pkgs.ninja pkgs.nlohmann_json pkgs.flock ] ++ runtime;
            shellHook =
              lib.optionalString (builtins.elem pkgs.gfortran runtime) fortranHook
              + lib.optionalString (builtins.elem pkgs.sbcl runtime) lispHook
              + ''
                rosetta_root=$PWD
                until [ -f "$rosetta_root/flake.nix" ] && [ -f "$rosetta_root/CMakeLists.txt" ]; do
                  if [ "$rosetta_root" = / ]; then
                    echo "readme-${name}: enter this shell from inside the Rosetta repository" >&2
                    exit 1
                  fi
                  rosetta_root=$(dirname "$rosetta_root")
                done
                export ROSETTA_README_BUILD="$rosetta_root/build/readme/${name}"
                mkdir -p "$ROSETTA_README_BUILD"
                # The lock serializes concurrent chunk builds and is released
                # before the chunk runs. Build output goes to stderr so the
                # chunk's stdout stays its result.
                (
                  flock 9 || exit 1
                  if [ ! -f "$ROSETTA_README_BUILD/CMakeCache.txt" ]; then
                    cmake -S "$rosetta_root" -B "$ROSETTA_README_BUILD" -G Ninja \
                      -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=OFF \
                      ${cmakeOptions name} || exit 1
                  fi
                  cmake --build "$ROSETTA_README_BUILD" --parallel 2 --target ${targets}
                ) 9>"$ROSETTA_README_BUILD/.build.lock" >&2 || exit 1
              ''
              + setup;
          };

        # Rscript reads R_PROFILE_USER; .First runs once the default packages
        # (methods, for S4) are attached.
        rProfile = pkgs.writeText "rosetta-readme.Rprofile" ''
          .First <- function() {
              build <- Sys.getenv("ROSETTA_README_BUILD")
              interfaces <- file.path(build, "generated", "r")
              for (name in c("r_demo", "r_verify")) {
                  dyn.load(file.path(build, paste0(name, .Platform$dynlib.ext)))
                  source(file.path(interfaces, paste0(name, ".R")), local = globalenv())
              }
              for (system in c("s3", "s4")) {
                  target <- new.env(parent = globalenv())
                  for (name in c("r_demo", "r_verify"))
                      source(file.path(interfaces, system, paste0(name, ".R")), local = target)
                  assign(paste0("r_", system), list(demo = target$r_demo, verify = target$r_verify),
                         envir = globalenv())
              }
              rm(".First", envir = globalenv())
          }
        '';
        # Python imports sitecustomize from PYTHONPATH at startup.
        pythonPrelude = pkgs.writeTextDir "sitecustomize.py" ''
          import builtins
          import python_demo as demo
          import python_verify as verify

          builtins.demo = demo
          builtins.verify = verify
          builtins.Counter = demo.r_demo__Counter
          builtins.Series = verify.r_verify__Series
          builtins.Worklist = verify.r_verify__Worklist
          builtins.AsyncTask = verify.r_verify__AsyncTask
        '';
        # Required through RUBYOPT.
        rubyPrelude = pkgs.writeText "rosetta-readme.rb" ''
          require 'ruby_demo'
          require 'ruby_verify'
          Counter = RubyDemo::R_demo__Counter
          Series = RubyVerify::R_verify__Series
          Worklist = RubyVerify::R_verify__Worklist
          AsyncTask = RubyVerify::R_verify__AsyncTask
        '';
        # Octave runs OCTAVE_SITE_INITFILE in the top-level workspace.
        octavePrelude = pkgs.writeText "rosetta-readme-octaverc" ''
          addpath(getenv('ROSETTA_README_BUILD'));
          demo = octave_demo();
          verify = octave_verify();
          Counter = demo.r_demo__Counter;
          Series = verify.r_verify__Series;
          Worklist = verify.r_verify__Worklist;
          AsyncTask = verify.r_verify__AsyncTask;
        '';
        # The Common Lisp chunk command loads this before the chunk.
        lispPrelude = pkgs.writeText "rosetta-readme.lisp" ''
          (load (concatenate 'string (sb-ext:posix-getenv "ROSETTA_README_BUILD") "/lisp_demo.lisp"))
          (load (concatenate 'string (sb-ext:posix-getenv "ROSETTA_README_BUILD") "/lisp_verify.lisp"))
        '';
        # Julia loads config/startup.jl from the first depot in JULIA_DEPOT_PATH.
        juliaStartup = pkgs.writeText "startup.jl" ''
          include(joinpath(ENV["ROSETTA_README_BUILD"], "julia_demo.jl"))
          include(joinpath(ENV["ROSETTA_README_BUILD"], "julia_verify.jl"))
          const verify = julia_verify
          const Series = verify.r_verify__Series
          const Worklist = verify.r_verify__Worklist
          const AsyncTask = verify.r_verify__AsyncTask
        '';
        # javac reads CLASSPATH, but MEP launches `java -cp CLASSES Main`, and
        # -cp replaces CLASSPATH. This java adds it back, plus the JNI path.
        javaWithReadmeApi = pkgs.writeShellScriptBin "java" ''
          args=()
          while [ $# -gt 0 ]; do
            case $1 in
              -cp | -classpath | --class-path) args+=("$1" "$2:$CLASSPATH"); shift 2 ;;
              *) args+=("$1"); shift ;;
            esac
          done
          exec ${pkgs.jdk21}/bin/java -Xcheck:jni -Djava.library.path="$ROSETTA_README_BUILD" "''${args[@]}"
        '';
        # MEP runs `dotnet run CHUNK.cs`; MSBuild imports this file into that
        # build through CustomAfterMicrosoftCommonTargets.
        csharpTargets = pkgs.writeText "rosetta-readme.targets" ''
          <Project>
            <ItemGroup>
              <Compile Include="$(ROSETTA_README_BUILD)/generated/csharp/CSharpDemo.cs" />
              <Compile Include="$(ROSETTA_README_BUILD)/generated/csharp/CSharpVerify.cs" />
              <None Include="$(ROSETTA_README_BUILD)/csharp/*CSharp*.so;$(ROSETTA_README_BUILD)/csharp/*CSharp*.dylib"
                    CopyToOutputDirectory="PreserveNewest" Visible="false" />
            </ItemGroup>
          </Project>
        '';

        developmentPackages = [
          pkgs.clang
          pkgs.cmake
          pkgs.ninja
          pkgs.just
          pkgs.pkg-config
          pkgs.nlohmann_json
          pkgs.lua5_4
          pkgs.python3
          pkgs.ruby
          pkgs.dotnet-sdk_10
          pkgs.gfortran
          pkgs.jdk21
          pkgs.julia-bin
          pkgs.octave
          pkgs.sbcl
          rWithR6
        ];
      in
      {
        devShells.default = pkgs.mkShell {
          packages = developmentPackages;
          shellHook = fortranHook + lispHook;
        };
        devShells.readme-shell = pkgs.mkShell { packages = developmentPackages; shellHook = fortranHook + lispHook; };
        devShells.readme-lua = chunkShell {
          name = "lua";
          runtime = [ pkgs.lua5_4 ];
          targets = "rosetta_demo";
          setup = ''
            export LUA_CPATH_5_4="$ROSETTA_README_BUILD/?.so;;"
            export LUA_INIT_5_4='demo = require("demo")'
          '';
        };
        devShells.readme-r = chunkShell {
          name = "r";
          runtime = [ rWithR6 ];
          targets = "rosetta_r_demo rosetta_r_verify";
          setup = ''
            export R_PROFILE_USER=${rProfile}
          '';
        };
        devShells.readme-python = chunkShell {
          name = "python";
          runtime = [ pkgs.python3 ];
          targets = "rosetta_python_demo rosetta_python_verify";
          setup = ''
            export PYTHONPATH="${pythonPrelude}:$ROSETTA_README_BUILD"
          '';
        };
        devShells.readme-ruby = chunkShell {
          name = "ruby";
          runtime = [ pkgs.ruby ];
          targets = "rosetta_ruby_demo rosetta_ruby_verify";
          setup = ''
            export RUBYLIB="$ROSETTA_README_BUILD" RUBYOPT="-r${rubyPrelude}"
          '';
        };
        devShells.readme-octave = chunkShell {
          name = "octave";
          runtime = [ pkgs.octave ];
          targets = "rosetta_octave_demo rosetta_octave_verify";
          setup = ''
            export OCTAVE_SITE_INITFILE=${octavePrelude}
          '';
        };
        devShells.readme-julia = chunkShell {
          name = "julia";
          runtime = [ pkgs.julia-bin ];
          targets = "rosetta_julia_demo rosetta_julia_verify";
          setup = ''
            mkdir -p "$ROSETTA_README_BUILD/julia-depot/config"
            ln -sf ${juliaStartup} "$ROSETTA_README_BUILD/julia-depot/config/startup.jl"
            # The trailing colon keeps Julia's default depots after this one.
            export JULIA_DEPOT_PATH="$ROSETTA_README_BUILD/julia-depot:"
          '';
        };
        devShells.readme-java = chunkShell {
          name = "java";
          runtime = [ pkgs.jdk21 ];
          targets = "rosetta_java_classes";
          setup = ''
            export CLASSPATH="$ROSETTA_README_BUILD/RosettaExamples.jar"
            export PATH="${javaWithReadmeApi}/bin:$PATH"
          '';
        };
        devShells.readme-csharp = chunkShell {
          name = "csharp";
          runtime = [ pkgs.dotnet-sdk_10 ];
          targets = "rosetta_csharp_demo rosetta_csharp_verify";
          setup = ''
            export CustomAfterMicrosoftCommonTargets=${csharpTargets}
            export DOTNET_CLI_HOME="$ROSETTA_README_BUILD/dotnet-home" DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
          '';
        };
        devShells.readme-fortran = chunkShell {
          name = "fortran";
          runtime = [ pkgs.gfortran ];
          targets = "rosetta_fortran_demo rosetta_fortran_verify";
          # MEP compiles with plain `gfortran CHUNK.f90 -o BIN`; nixpkgs'
          # gfortran wrapper adds these flags, and its linker wrapper appends
          # NIX_LDFLAGS after the chunk's object.
          setup = ''
            export NIX_CFLAGS_COMPILE="$NIX_CFLAGS_COMPILE -I$ROSETTA_README_BUILD/fortran-modules"
            export NIX_LDFLAGS="$NIX_LDFLAGS $ROSETTA_README_BUILD/librosetta_fortran_demo.a $ROSETTA_README_BUILD/librosetta_fortran_verify.a -L$ROSETTA_README_BUILD -lrosetta_fortran_demo_native -lrosetta_fortran_verify_native -rpath $ROSETTA_README_BUILD"
          '';
        };
        devShells.readme-lisp = chunkShell {
          name = "lisp";
          runtime = [ pkgs.sbcl ];
          targets = "rosetta_lisp_demo rosetta_lisp_verify";
          # MEP has no built-in Common Lisp runner, so the chunk's `cmd` runs
          # sbcl with this prelude.
          setup = ''
            export ROSETTA_README_PRELUDE=${lispPrelude}
          '';
        };
        # Used by `just lua-repl`, including when called outside nix develop.
        devShells.lua-repl = pkgs.mkShell {
          name = "rosetta-lua-repl";
          packages = developmentPackages;
        };
        # Used by `just r-repl`, including when called outside nix develop.
        devShells.r-repl = pkgs.mkShell {
          name = "rosetta-r-repl";
          packages = developmentPackages;
        };
      }
    );
}
