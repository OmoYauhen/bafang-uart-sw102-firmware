{ pkgs ? import <nixpkgs> {} }:

# Dev shell for building and running the desktop emulator (`Makefile.emu`).
# On any machine that has Nix installed, from firmware/SW102/:
#
#     nix-shell
#     make -f Makefile.emu
#     ./emu
#
# See README.md > "Running the desktop emulator" for the mock-motor setup.

pkgs.mkShell {
  buildInputs = with pkgs; [
    gcc
    gnumake
    pkg-config
    qt5.qtbase
    qt5.qtserialport
    qt5.qtwayland
    python3
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

    echo "emu dev shell ready."
    echo "  build:  make -f Makefile.emu"
    echo "  run:    ./emu"
    echo "  mock:   python3 -u tools/bbshd_mock.py --verbose --speed 18"
    echo "  mock (sweep speed to test the UI):"
    echo "          python3 -u tools/bbshd_mock.py --speed-wave sine --speed-min 0 --speed-max 45"
    echo "          then in another shell: SW102_UART_PORT=/dev/pts/N ./emu"
  '';
}
