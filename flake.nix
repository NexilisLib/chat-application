{
  description = "Chat application: nexilis-based server and ncurses client";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          name = "chat-application-devshell";
          packages = with pkgs; [
            cmake
            gcc
            gnumake
            pkg-config
            ncurses
            boost
          ];

          shellHook = ''
            export CMAKE_PREFIX_PATH="${pkgs.boost.dev}:${pkgs.ncurses.dev}:${pkgs.ncurses}:$CMAKE_PREFIX_PATH"
            echo "Welcome to the chat-application dev shell"
            echo "Build the client and server with:"
            echo "  cd client && mkdir -p build && cd build && cmake .. && make"
            echo "  cd server && mkdir -p build && cd build && cmake .. && make"
          '';
        };
      });
}
