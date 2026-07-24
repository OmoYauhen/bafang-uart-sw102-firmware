{
  description = "swang-stodva — open-source Bafang display firmware for the SW102";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        # Cosmetic package label only; the release version of record lives in
        # common/Makefile.common (VERSION_STRING), managed by the release workflow.
        version = "0.0.1-alpha";

        # Desktop Qt5 emulator (Makefile.emu -> ./emu).
        emu = pkgs.stdenv.mkDerivation {
          pname = "swang-stodva-emu";
          inherit version;
          src = ./.;
          nativeBuildInputs = [ pkgs.pkg-config pkgs.qt5.wrapQtAppsHook ];
          buildInputs = [ pkgs.qt5.qtbase pkgs.qt5.qtserialport ];
          # qtserialport's .pc only exposes the QtSerialPort/ subdir; the header
          # pulls in <QtSerialPort/...> which needs the parent dir (see shell.nix).
          CFLAGS = "-I${pkgs.qt5.qtserialport.dev}/include";
          CXXFLAGS = "-I${pkgs.qt5.qtserialport.dev}/include";
          enableParallelBuilding = true;
          buildPhase = ''
            runHook preBuild
            make -f Makefile.emu
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            install -Dm755 emu $out/bin/swang-stodva-emu
            runHook postInstall
          '';
          meta.mainProgram = "swang-stodva-emu";
        };

        # On-target firmware for the nRF51x22 (Cortex-M0) -> signed-flashable .hex.
        firmware = pkgs.stdenv.mkDerivation {
          pname = "swang-stodva-firmware";
          inherit version;
          src = ./.;
          nativeBuildInputs = [ pkgs.gcc-arm-embedded pkgs.gnumake ];
          dontFixup = true; # output is Intel HEX + map, no host ELF to fix up
          enableParallelBuilding = true;
          buildPhase = ''
            runHook preBuild
            export GNU_INSTALL_ROOT=${pkgs.gcc-arm-embedded}
            make GNU_INSTALL_ROOT=$GNU_INSTALL_ROOT _build/nrf51822_sw102.hex
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            install -Dm644 _build/nrf51822_sw102.hex $out/nrf51822_sw102.hex
            install -Dm644 _build/nrf51822_sw102.map $out/nrf51822_sw102.map || true
            runHook postInstall
          '';
        };
      in {
        # Reuse the classic shell.nix so `nix develop` and `nix-shell` match.
        devShells.default = import ./shell.nix { inherit pkgs; };

        packages = {
          inherit emu firmware;
          default = emu;
        };

        apps.emu = flake-utils.lib.mkApp { drv = emu; };
      });
}
