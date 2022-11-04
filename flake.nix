{
  description = "Flake utils demo";

  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = nixpkgs.legacyPackages.${system}; in
      {
        devShells = {
          default = pkgs.mkShell {
            buildInputs = [
              pkgs.cargo
              pkgs.libclang.lib
            ];
            shellHook = ''
              export LIBCLANG_PATH=${pkgs.libclang.lib}/lib
            '';
          };
        };
      }
    );
}
