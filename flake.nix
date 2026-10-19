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
        developmentPackages = [
          pkgs.clang
          pkgs.cmake
          pkgs.ninja
          pkgs.just
          pkgs.nlohmann_json
          pkgs.lua5_4
          pkgs.python3
          pkgs.ruby
          pkgs.octave
          (pkgs.rWrapper.override { packages = [ pkgs.rPackages.R6 ]; })
        ];
      in
      {
        devShells.default = pkgs.mkShell {
          packages = developmentPackages;
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
