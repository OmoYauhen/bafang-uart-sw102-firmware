# SW102_Display

This is an alternate version of the SW102 firmware. Most of the settings are compatible with casainho's version, so you may refer
to the original wiki pages, but please keep in mind that they describe a different version of the firmware:
- https://github.com/OpenSource-EBike-firmware/Color_LCD/wiki/Bafang-LCD-SW102
- https://github.com/OpenSource-EBike-firmware/SW102_LCD_Bluetooth/wiki

## How to build on Windows

TBD

## How to build on Linux

* Extract https://launchpad.net/gcc-arm-embedded/4.9/4.9-2015-q3-update/+download/gcc-arm-none-eabi-4_9-2015q3-20150921-linux.tar.bz2 into /usr/local/gcc-arm-none-eabi-4_9-2015q3.
* Run "make"

## Running the desktop emulator

`Makefile.emu` builds the firmware as a native Linux/Qt5 application so you
can develop the state machine, UI, and protocol code without flashing the
SW102. See `../SW102/src/emu/` for the hardware-abstraction shims that
replace the nRF51 peripherals.

### With Nix (recommended, one-shot)

From this directory:

```
nix-shell
make -f Makefile.emu
./emu
```

`shell.nix` here brings in Qt5 (base + serialport + wayland platform
plugin), gcc, make, pkg-config, and python3 for the mock. Works on any
machine that has Nix installed — NixOS, Nix on macOS, Nix on any Linux.

### Without Nix

Install these system packages, then run `make -f Makefile.emu`:

- Debian/Ubuntu: `qtbase5-dev qtbase5-dev-tools libqt5serialport5-dev pkg-config build-essential python3`
- Fedora: `qt5-qtbase-devel qt5-qtserialport-devel pkgconfig gcc-c++ python3`
- macOS + Homebrew: `qt@5 pkg-config python3`

Set `QT_QPA_PLATFORM=xcb` (X11) or `wayland` if Qt doesn't pick your session
type automatically.

### Motor UART

The emulator opens a serial port at 19200 baud (TSDZ2). Two ways to
connect:

1. **Real motor.** Plug a Bafang programming cable into your motor and
   your PC. The emu auto-scans `/dev/ttyUSB*` and grabs the first match.

2. **Mock motor (recommended for development).** Run the Python BBSHD
   emulator in `tools/bbshd_mock.py`, then point the emu at its pty:

   ```
   python3 -u tools/bbshd_mock.py --verbose --speed 18 --batt 78
   # prints e.g. "Slave device: /dev/pts/5"

   SW102_UART_PORT=/dev/pts/5 ./emu
   ```

   See `tools/BBSHD_MOCK.md` for full options and protocol coverage.

### What the emulator prints

- Every frame of the OLED is saved as `out/NNNN.pgm` (create the `out/`
  directory first if it doesn't exist). Handy for regression tests.
- Buttons on the SW102 are mapped to keys inside the emulator window:
  - `↑ / ↓` — UP / DOWN
  - `M` — mode/menu
  - `P` — power

## Debugging bluetooth linux

### Using a NRF dongle

Use https://www.nordicsemi.com/Software-and-Tools/Development-Tools/nRF-Connect-for-desktop
Install this https://github.com/NordicSemiconductor/nrf-udev

Use this command to BLE update a target:
nrfutil dfu ble -ic NRF52 -p /dev/ttyACM0 --help

### Using a regular BLE dongle

Install this fork of nrfutil https://github.com/anszom/pc-nrfutil

Use this command to BLE update a target:
nrfutil  dfu ble-native -pkg sw102-otaupdate-xxx.zip  -a (your target BLE address)

### Post-installation

Note that the bootloader used in the open-source firmware has an (issue)[https://github.com/OpenSourceEBike/SW102_LCD_Bluetooth-bootloader/pull/3] which was only recently fixed. In order to avoid problems, when activating the display *for the first time after flashing* you may need to hold the power button for a long time (up to 10 seconds) until you see the boot animation. Otherwise, the bootloader's processing may be interrupted and the SW102 will return to DFU mode. In this case, please re-run the DFU procedure.
