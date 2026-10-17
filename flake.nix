{
  description = "AniParser Electron application";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    let
      forEachSystem =
        fn:
        nixpkgs.lib.genAttrs nixpkgs.lib.platforms.linux (
          system: fn system nixpkgs.legacyPackages.${system}
        );
    in
    {
      devShells = forEachSystem (
        system: pkgs: rec {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              cmake
              ninja
              gcc

              boost
              nlohmann_json
              cpr
              curl
              ncurses
              notcurses

              openssl
              pkg-config
            ];

            shellHook = ''
                        echo "C++ HTTP environment ready"
              echo "Compiler: $(g++ --version | head -n1)"
            '';
          };
        }
      );
    };
}
