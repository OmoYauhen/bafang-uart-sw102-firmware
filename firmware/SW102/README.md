# SW102_Display

This is a fork of [anszom/SW102_LCD](https://github.com/anszom/SW102_LCD)
ported from the TSDZ2 wire protocol to the Bafang display UART protocol used
by BBS02 / BBSHD mid-drive motors. See the top-level [README](../../README.md)
for lineage, compatibility notes, and feature list.

Most of the display-side settings are still compatible with upstream
`anszom/SW102_LCD` (and by extension `casainho/Color_LCD`), so these wiki
pages remain a useful reference for the UI / configuration menus, though
they describe a different motor-side protocol:
- https://github.com/OpenSource-EBike-firmware/Color_LCD/wiki/Bafang-LCD-SW102
- https://github.com/OpenSource-EBike-firmware/SW102_LCD_Bluetooth/wiki

## Building the on-target firmware

The `Makefile` (as opposed to `Makefile.emu`) cross-compiles the firmware
for the SW102's nRF51x22 (Cortex-M0) and produces `_build/nrf51822_sw102.hex`,
suitable for flashing via SWD (ST-Link + OpenOCD) or for wrapping into a
signed OTA DFU zip.

### Toolchain

You need `arm-none-eabi-gcc` and `arm-none-eabi-newlib`. Modern versions
work fine — this fork has been built with GCC 15.2 (nixpkgs
`gcc-arm-embedded`); the upstream README's ancient 4.9/2015q3 pin is no
longer required.

* **NixOS / Nix (any platform)**:
  ```
  nix shell nixpkgs#gcc-arm-embedded nixpkgs#gnumake nixpkgs#python3
  export GNU_INSTALL_ROOT=$(dirname $(dirname $(which arm-none-eabi-gcc)))
  ```
* **Debian / Ubuntu**: `sudo apt install gcc-arm-none-eabi python3 make`
* **Fedora**: `sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-newlib python3 make`
* **macOS + Homebrew**: `brew install --cask gcc-arm-embedded && brew install python3`

### Building `.hex`

```
cd firmware/SW102
make -f Makefile clean_project
make -f Makefile _build/nrf51822_sw102.hex
```

Expected size on the current `main`: roughly 48 KB `.text` + 300 B `.data`
+ 6 KB `.bss`, comfortably within the nRF51x22's 256 KB flash / 16 KB RAM.
Warnings from the newlib stubs (`_close is not implemented`) are benign —
those syscalls are unused after link-time garbage collection.

### Packaging the DFU (OTA) zip

Wraps the `.hex` into a signed Nordic DFU package that can be delivered
over BLE to a running bootloader. Uses Nordic's current `nrfutil` (Rust
rewrite, v8.x) with the `nrf5sdk-tools` subcommand — the same CLI shape
the SDK 12.3 Makefile expected. `firmware/SW102/prebuilt/private.key` is
the signing key checked into the repo.

**On NixOS**, `nrfutil` requires accepting the unfree Segger JLink
license, and the Nordic subcommand binaries are generic-linux ELFs that
need an FHS environment. Both are handled here:

```
mkdir -p _release
nix-shell --impure \
  --arg config '{ allowUnfree = true; segger-jlink.acceptLicense = true; }' \
  -p nrfutil steam-run --run '
    # one-time: install the nrf5sdk-tools subcommand into ~/.nrfutil
    steam-run nrfutil install nrf5sdk-tools

    # then package the zip
    steam-run nrfutil nrf5sdk-tools pkg generate \
      --application _build/nrf51822_sw102.hex \
      --key-file prebuilt/private.key \
      --application-version 27 \
      --hw-version 51 \
      --sd-req 0x87 \
      _release/sw102-otaupdate-$(git rev-parse --short HEAD).zip
  '
```

**On non-NixOS Linux** (Debian/Ubuntu/Fedora), install `nrfutil` from
Nordic's release page, then drop the `steam-run` wrapper:

```
nrfutil install nrf5sdk-tools
nrfutil nrf5sdk-tools pkg generate \
  --application _build/nrf51822_sw102.hex \
  --key-file prebuilt/private.key \
  --application-version 27 \
  --hw-version 51 \
  --sd-req 0x87 \
  _release/sw102-otaupdate-$(git rev-parse --short HEAD).zip
```

Fields:

- `--application-version` — **bump this for every release** you flash. The
  bootloader rejects packages whose version is not strictly greater than
  what's currently installed unless a debug mode is set. Track it
  alongside `VERSION_STRING` in `../common/Makefile.common`.
- `--hw-version 51` — nRF51 family.
- `--sd-req 0x87` — CRC of SoftDevice s130 2.0.1 (matches
  `nRF5_SDK_12.3.0/components/softdevice/s130/hex/`). Change this only if
  you rebuild against a different SoftDevice.
- `--key-file` — must match the public key baked into the installed
  bootloader. The one in `prebuilt/private.key` pairs with the prebuilt
  bootloader in the same directory.

The resulting zip contains `manifest.json`, `nrf51822_sw102.dat` (init
packet), and `nrf51822_sw102.bin` — deliverable to a bootloader via
`nrfutil dfu ble ...` or any BLE DFU app (see the "Debugging bluetooth
linux" section below).

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

The emulator opens a serial port at 1200 baud (Bafang display UART).
Two ways to connect:

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
