{ pkgs ? import <nixpkgs> {} }:

# Dev shell for both builds:
#   - desktop emulator (`make -f Makefile.emu` -> ./emu), and
#   - on-target firmware (`make _build/nrf51822_sw102.hex`).
# On any machine that has Nix installed, from the repo root:
#
#     nix-shell          # or: nix develop   (flake.nix reuses this shell)
#     make -f Makefile.emu && ./emu
#
# See docs/BUILD.md for full build/flash/OTA instructions and the mock motor.

pkgs.mkShell {
  buildInputs = with pkgs; [
    # emulator (Qt5 desktop app)
    gcc
    gnumake
    pkg-config
    qt5.qtbase
    qt5.qtserialport
    qt5.qtwayland
    python3
    # on-target firmware (nRF51 / Cortex-M0)
    gcc-arm-embedded
    srecord
  ];

  shellHook = ''
    # nixpkgs' Qt5SerialPort .pc only exposes the QtSerialPort/ subdir; the
    # header itself includes <QtSerialPort/qserialportglobal.h> which needs
    # the parent include directory. Add it explicitly.
    export CFLAGS="-I${pkgs.qt5.qtserialport.dev}/include"
    export CXXFLAGS="$CFLAGS"

    # Platform plugins (xcb, wayland) ship in the -bin output, not -dev.
    export QT_PLUGIN_PATH="${pkgs.qt5.qtbase.bin}/lib/qt-${pkgs.qt5.qtbase.version}/plugins:${pkgs.qt5.qtwayland.bin}/lib/qt-${pkgs.qt5.qtwayland.version}/plugins"

    # Default to wayland; override to xcb if you prefer, or unset entirely
    # to let Qt auto-detect (may warn if both are unavailable).
    export QT_QPA_PLATFORM="''${QT_QPA_PLATFORM:-wayland}"

    # Point the SDK Makefile at the Nix arm-none-eabi toolchain.
    export GNU_INSTALL_ROOT="${pkgs.gcc-arm-embedded}"

    echo "swang-stodva dev shell ready."
    echo "  emu:      make -f Makefile.emu && ./emu"
    echo "  firmware: make _build/nrf51822_sw102.hex"
    echo "  mock:     python3 -u tools/bbshd_mock.py --verbose --speed 18"
    echo "  mock (sweep speed to test the UI):"
    echo "            python3 -u tools/bbshd_mock.py --speed-wave sine --speed-min 0 --speed-max 45"
    echo "            then in another shell: SW102_UART_PORT=/dev/pts/N ./emu"
  '';
}
