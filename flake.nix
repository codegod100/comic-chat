{
  description = "Microsoft Comic Chat — Qt6 port and development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        comic-chat-qt = pkgs.stdenv.mkDerivation rec {
          pname = "comic-chat-qt";
          version = "0.1.0-unstable";

          src = pkgs.lib.fileset.toSource {
            root = ./.;
            fileset = pkgs.lib.fileset.unions [
              ./qt-port
              ./v1.0-pre-modern/comicart
            ];
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
          ];

          dontUseCmakeConfigure = true;

          configurePhase = ''
            runHook preConfigure
            cmake -S qt-port -B build \
              -GNinja \
              -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_INSTALL_PREFIX=$out \
              -DCOMIC_ART_DIR=$PWD/v1.0-pre-modern/comicart
            runHook postConfigure
          '';

          buildPhase = ''
            runHook preBuild
            cmake --build build
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            cmake --install build
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "Qt6 port of Microsoft Comic Chat";
            homepage = "https://github.com/microsoft/comic-chat";
            license = licenses.mit;
            platforms = platforms.linux;
            mainProgram = "comic-chat-qt";
          };
        };
      in
      {
        packages = {
          default = comic-chat-qt;
          inherit comic-chat-qt;
        };

        apps.default = flake-utils.lib.mkApp {
          drv = comic-chat-qt;
          name = "comic-chat-qt";
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.qtbase
            qt6.qttools
          ];

          shellHook = ''
            echo "Comic Chat Qt port"
            echo "  nix build          # build the package"
            echo "  nix run            # run comic-chat-qt"
            echo "  cmake -G Ninja -B qt-port/build -DCMAKE_BUILD_TYPE=Debug"
            echo "  cmake --build qt-port/build && ./qt-port/build/comic-chat-qt"
          '';
        };
      }
    );
}
