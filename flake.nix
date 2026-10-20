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
        chunkTools = [ pkgs.clang pkgs.cmake pkgs.ninja pkgs.nlohmann_json pkgs.python3 ];
        chunkShell = name: runtime: pkgs.mkShell {
          name = "rosetta-readme-${name}";
          packages = chunkTools ++ runtime;
        };
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
          (pkgs.rWrapper.override { packages = [ pkgs.rPackages.R6 ]; })
        ];
      in
      {
        devShells.default = pkgs.mkShell {
          packages = developmentPackages;
        };
        # README chunk runners select these explicitly; runtime builds live in
        # build/readme/<language> and never reuse another backend's CMake cache.
        devShells.readme-shell = pkgs.mkShell { packages = developmentPackages; };
        devShells.readme-lua = chunkShell "lua" [ pkgs.lua5_4 ];
        devShells.readme-r = chunkShell "r" [ (pkgs.rWrapper.override { packages = [ pkgs.rPackages.R6 ]; }) ];
        devShells.readme-python = chunkShell "python" [ ];
        devShells.readme-ruby = chunkShell "ruby" [ pkgs.ruby ];
        devShells.readme-octave = chunkShell "octave" [ pkgs.octave ];
        devShells.readme-csharp = chunkShell "csharp" [ pkgs.dotnet-sdk_10 pkgs.just ];
        devShells.readme-java = chunkShell "java" [ pkgs.jdk21 pkgs.just ];
        devShells.readme-fortran = chunkShell "fortran" [ pkgs.gfortran pkgs.just ];
        devShells.readme-julia = chunkShell "julia" [ pkgs.julia-bin ];
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
