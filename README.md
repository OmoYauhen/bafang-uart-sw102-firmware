
This is a fork of [anszom/SW102_LCD](https://github.com/anszom/SW102_LCD),
ported from the **TSDZ2** motor's UART protocol to the **Bafang** display
UART protocol used by BBS02 / BBSHD mid-drive motors. It runs on the
[Bafang SW102 display](https://github.com/OpenSource-EBike-firmware/SW102_LCD_Bluetooth/wiki)
and speaks the same wire protocol as a stock Bafang display, so it works
against either stock Bafang firmware or [`bbs-fw`](https://github.com/danielnilsson9/bbs-fw)
(the open-source app-MCU replacement).

Upstream lineage: `casainho/Color_LCD` → `anszom/SW102_LCD` (new UI) →
this fork (Bafang wire protocol).

## Demo

![video](sw102.gif)

Features:

- runs on the Bafang SW102 handlebar display
- talks the Bafang display UART protocol (1200 baud, `0x11`/`0x16` category bytes)
- polls the motor for status, current, battery, speed, temperature, voltage, moving state
- writes assist level (PAS), lights, and walk-assist state back to the motor
- built-in support for `bbs-fw`'s field hijacks (motor temperature via `READ_RANGE`, battery voltage ×10 via `READ_CALORIES`)
- new UI with smooth graphics, graphs, and 50 fps update rate
- desktop emulator build for developing without hardware — see [`firmware/SW102/README.md`](firmware/SW102/README.md)
- assist level defined as percentage (100 % = motor matches rider power)

Known issues / rough edges:

- pedal cadence is stubbed at 99 — BBSHD doesn't report RPM in its display protocol, still deciding how to synthesise or hide it
- BT connectivity hasn't been tested
- 850/860 (colour LCD) targets in the tree still speak TSDZ2 and are not covered by this port
- flashing on real hardware not yet exercised — emulator-verified end-to-end but the SW102 hardware hasn't been touched
- startup boost menu removed (never worked, per casainho's wiki)
- virtual throttle removed

## Installation

If you have already installed casainho's SW102 software & bootloader, you can
switch to this version and back via bluetooth. See the
[wiki page](https://github.com/OpenSourceEBike/TSDZ2_wiki/wiki/Flash-the-firmware-on-SW102)
for flashing instructions. OTA update binaries are available on the
[releases page](releases/).

If you have the factory SW102 software installed, you will need to disassemble
the device [as described here](https://github.com/OpenSourceEBike/TSDZ2_wiki/wiki/Flash-the-bootloader-and-firmware-on-SW102-using-SWD),
and flash the bootloader. Afterwards you can switch to this version using the
procedure described above.

To build from source, see [firmware/SW102/README.md](firmware/SW102/README.md)
for instructions (Nix + `nix-shell` for reproducible builds, or system Qt5 for
Debian/Ubuntu/Fedora/macOS).

## Usage

The main screen is explained in the video above. The following elements are
visible:

- left bar: assist level (0..9 — Bafang has nine discrete PAS levels)
- right bar: instantaneous motor power
- top: battery icon & percentage (read directly from the motor)
- central large display: speed
- central small display: extra information (odometer, trip distance, trip
  time, average speed, pedal power, motor power)
- bottom indicators: WALK (walk assist), BRK (braking), lights

The following key actions are available:

- **UP/DOWN**: adjust assist level
- **PWR**: toggle lights
- hold **PWR**: turn off
- **M**: cycle extra information display
- hold **M**: enter configuration menu
- hold **UP**: toggle Street Mode (if enabled in configuration)
- hold **DOWN**: Walk Assist (if enabled in configuration)

In the configuration screen, you use UP/DOWN for navigation, M to accept,
and PWR to go back.

## Compatibility

### Motor firmware

- **Bafang stock BBS02 / BBSHD firmware** — should work; the display speaks
  the same protocol Bafang's own displays use.
- **`bbs-fw` on BBS02 / BBSHD** — designed against this codebase; supports
  the field hijacks (motor temperature via `READ_RANGE`, battery voltage via
  `READ_CALORIES`) that `bbs-fw` implements.
- **TSDZ2** — **not compatible**. This fork replaces the TSDZ2 wire protocol
  entirely (baud, framing, handshake, direction of first packet all differ).
  Use [upstream anszom/SW102_LCD](https://github.com/anszom/SW102_LCD) if
  you're on TSDZ2.

### Config format compatibility

Note that the EEPROM-persisted config format is inherited from upstream and
has not been reshaped for BBSHD yet. Some fields (torque-sensor calibration
table, TSDZ2-specific tunables) are still present but do nothing on BBSHD.
Cleaning this up is on the TODO list.

### Assist level definition

Assist level is defined as a percentage: 100% means the motor gives roughly
the same power as the rider, 50% half, 200% double. The display remaps its
level number (0..9) to Bafang's non-monotonic wire codes on the fly
(`0x00 0x01 0x0B 0x0C 0x0D 0x02 0x15 0x16 0x17 0x03`).

### 850 / 860 displays

Although this repo contains code for the 8xx displays, it's leftover from the
fork and still speaks TSDZ2. The 8xx code should compile but has not been
ported to Bafang.

## Development

### Source changes relative to the anszom/SW102_LCD fork

The "backend" changes concentrate in `firmware/common/src/state.c` and
`firmware/common/include/uart.h`:

- new Bafang RX byte-level state machine (`uart_prime_rx(len)` API,
  caller-declared reply length, no CRC — checksums are opcode-specific)
- new TX round-robin over seven READ opcodes at ~100 ms/opcode
- WRITE_PAS / WRITE_LIGHTS with change-detection guards (writes only on
  user-initiated state changes)
- field mapping from parsed Bafang values → `rt_vars` / `ui8_g_battery_soc`

The UI, screen framework, and blitting code are inherited from
`anszom/SW102_LCD` unchanged.

### Emulator build

The firmware supports an "emulator" build, where it's compiled as a regular
Linux app driving a Qt5 window. This allows for a much quicker development
cycle.

```
cd firmware/SW102
nix-shell
make -f Makefile.emu
./emu
```

For desktop development without a real motor, use the included BBSHD
motor emulator (Python 3, no dependencies):

```
python3 -u tools/bbshd_mock.py --verbose --speed 20 --batt 60
# prints e.g. "Slave device: /dev/pts/N"

# in another shell:
SW102_UART_PORT=/dev/pts/N ./emu
```

See [`firmware/SW102/tools/BBSHD_MOCK.md`](firmware/SW102/tools/BBSHD_MOCK.md)
for the mock's protocol coverage and options.
