#!/usr/bin/env python3
"""
BBSHD motor emulator (mock).

Speaks the Bafang display UART protocol as documented in ~/git/bbs-fw
(extcom.c) and in ~/Sync/projects/PET/bbshd.md. Opens a pty pair; prints
the slave device path so a display firmware (or a real Bafang display)
can be pointed at it.

Usage:
    python3 bbshd_mock.py                       # keeps running until Ctrl-C
    python3 bbshd_mock.py --verbose             # log every packet
    python3 bbshd_mock.py --speed 25 --batt 82  # set fake state

Then point the SW102 emu at the printed device path:
    SW102_UART_PORT=/dev/pts/N ./emu
"""

import argparse
import math
import os
import pty
import select
import struct
import sys
import termios
import time

# ---- Bafang UART protocol constants (source: bbs-fw/src/firmware/extcom.c) ----

CAT_READ  = 0x11   # display -> motor: query
CAT_WRITE = 0x16   # display -> motor: command

# Read opcodes (motor -> display replies documented below)
OP_READ_STATUS    = 0x08  # reply: 1B status code
OP_READ_CURRENT   = 0x0A  # reply: amp_x2, amp_x2 (checksum degenerate)
OP_READ_BATTERY   = 0x11  # reply: percent, percent
OP_READ_SPEED     = 0x20  # reply: rpm_hi, rpm_lo, (sum + 0x20) & 0xFF
OP_READ_UNKNOWN1  = 0x21  # reply: 0x00 0x00 0x00
OP_READ_RANGE     = 0x22  # reply: hi, lo, sum  -- bbs-fw hijacks with motor temp or power
OP_READ_CALORIES  = 0x24  # reply: hi, lo, sum  -- bbs-fw hijacks with battery voltage x10
OP_READ_UNKNOWN3  = 0x25  # reply: 5x 0x00
OP_READ_MOVING    = 0x31  # reply: 0x31 moving / 0x30 still, echoed as chksum

# Write opcodes (display -> motor commands)
OP_WRITE_PAS        = 0x0B  # 4B: cat, op, level, chk
OP_WRITE_MODE       = 0x0C  # 4B: cat, op, mode,  chk
OP_WRITE_LIGHTS     = 0x1A  # 3B: cat, op, state  (no checksum, per bbshd.md)
OP_WRITE_SPEED_LIM  = 0x1F  # 5B: cat, op, hi, lo, chk

# Bafang assist-level encoding is not monotonic (see bbshd.md §3.2)
BAFANG_LEVEL_TO_NAME = {
    0x00: "ASSIST_0",
    0x01: "ASSIST_1",
    0x0B: "ASSIST_2",
    0x0C: "ASSIST_3",
    0x0D: "ASSIST_4",
    0x02: "ASSIST_5",
    0x15: "ASSIST_6",
    0x16: "ASSIST_7",
    0x17: "ASSIST_8",
    0x03: "ASSIST_9",
    0x06: "ASSIST_PUSH",
}


# ---- Simulated motor state ---------------------------------------------------

class MotorState:
    def __init__(self, args):
        self.wheel_kph          = args.speed          # user-supplied initial
        # Optional time-varying speed, to exercise the display's speed UI
        # (digits, needle/graph, "moving" indicator) without a real wheel.
        self.speed_wave         = args.speed_wave     # None = fixed speed
        self.speed_min          = args.speed_min
        self.speed_max          = args.speed_max
        self.speed_period       = max(0.1, args.speed_period)
        self.battery_percent    = args.batt
        self.battery_voltage_v  = 52.0                # nominal for 14S
        self.motor_current_a    = 0.0
        self.motor_temp_c       = 24
        self.assist_level       = "ASSIST_1"
        self.lights_on          = False
        self.status_code        = 0x00                # 0 = normal
        # Wheel *perimeter* in mm, matching the firmware's
        # DEFAULT_VALUE_WHEEL_PERIMETER exactly (Bafang's own unit — not derived
        # from inches × π, which would produce a slightly different number
        # because Bafang's nominal "27.5\" wheel = 2100 mm" is a convention
        # that accounts for tire deflection, not a pure geometric circumference).
        self.wheel_perimeter_mm = args.perimeter      # default 2100
        self.t0 = time.monotonic()

    # bbs-fw formula from extcom.c process_bafang_display_read_speed:
    #   rpm = kph / (3 * pi * wheel_size_inch/10 * 25.4mm/inch * 1e-6 h/mm-... )
    # Simplified: rpm = kph * 60 / (pi * wheel_inch * 25.4e-3 * 60/1000)
    #                 = kph * 1000 / (pi * wheel_inch * 25.4e-3 * 60)
    # For 28": one rev = pi * 28 * 25.4mm = 2234 mm ≈ 2.234 m
    #   at 20 kph = 5.556 m/s → 5.556/2.234 = 2.487 rev/s = 149 rpm
    def wheel_rpm(self):
        # kph -> rpm using perimeter (mm): rpm = kph * 1e6 / (perimeter * 60)
        # Kept as an integer to match the on-wire uint16, which is how the real
        # motor MCU sees this value (integer Hall counts per unit time).
        return int(self.wheel_kph * 1_000_000.0 / (self.wheel_perimeter_mm * 60.0))

    def voltage_x10(self):
        return int(self.battery_voltage_v * 10)

    def current_amp_x2(self):
        # bbs-fw: uart_write((motor_battery_current_x10 * 2) / 10)
        return int((self.motor_current_a * 10 * 2) / 10) & 0xFF

    def is_moving(self):
        return self.wheel_kph > 0.1

    def tick(self, dt):
        # Slowly drop battery percent + voltage over time to prove reactivity
        self.battery_percent   = max(0, self.battery_percent - dt * 0.01)
        self.battery_voltage_v = 42.0 + (self.battery_percent / 100.0) * 16.4
        t = time.monotonic() - self.t0
        # Fake a gentle sinusoidal current draw for the "range" field
        self.motor_current_a = 5.0 + 3.0 * math.sin(t * 0.5)
        # Optionally sweep the wheel speed across [min, max] to test the UI.
        if self.speed_wave is not None:
            lo, hi = self.speed_min, self.speed_max
            phase = (t % self.speed_period) / self.speed_period   # 0..1
            if self.speed_wave == "sine":
                # smooth up-and-down: cos maps 0..1 -> lo..hi..lo
                frac = (1.0 - math.cos(phase * 2.0 * math.pi)) / 2.0
            elif self.speed_wave == "triangle":
                # linear up then down
                frac = 1.0 - abs(2.0 * phase - 1.0)
            else:  # "sawtooth": ramp up then snap back to lo
                frac = phase
            self.wheel_kph = lo + (hi - lo) * frac


# ---- Wire protocol helpers ---------------------------------------------------

def chk8(*bytes_iter):
    """1-byte modulo-256 sum, used as Bafang's write-request checksum."""
    total = 0
    for b in bytes_iter:
        total = (total + b) & 0xFF
    return total


def handle_read(op, state, verbose):
    """Return the reply bytes for a READ (0x11) request, or None if unknown."""
    if op == OP_READ_STATUS:
        return bytes([state.status_code])

    if op == OP_READ_CURRENT:
        v = state.current_amp_x2()
        return bytes([v, v])  # data + degenerate 1B checksum

    if op == OP_READ_BATTERY:
        pct = int(state.battery_percent) & 0xFF
        return bytes([pct, pct])

    if op == OP_READ_SPEED:
        rpm = state.wheel_rpm() & 0xFFFF
        hi, lo = (rpm >> 8) & 0xFF, rpm & 0xFF
        weird_chk = (hi + lo + 0x20) & 0xFF   # per bbs-fw
        return bytes([hi, lo, weird_chk])

    if op == OP_READ_UNKNOWN1:
        return bytes([0x00, 0x00, 0x00])

    if op == OP_READ_RANGE:
        # bbs-fw hijacks this to show motor temp OR instantaneous power
        val = state.motor_temp_c & 0xFFFF
        hi, lo = (val >> 8) & 0xFF, val & 0xFF
        return bytes([hi, lo, (hi + lo) & 0xFF])

    if op == OP_READ_CALORIES:
        # bbs-fw hijacks this to show battery voltage x10
        val = state.voltage_x10() & 0xFFFF
        hi, lo = (val >> 8) & 0xFF, val & 0xFF
        return bytes([hi, lo, (hi + lo) & 0xFF])

    if op == OP_READ_UNKNOWN3:
        return bytes([0x00] * 5)

    if op == OP_READ_MOVING:
        v = 0x31 if state.is_moving() else 0x30
        return bytes([v, v])

    if verbose:
        print(f"  UNKNOWN READ opcode 0x{op:02x}", file=sys.stderr)
    return None


def parse_display_frames(buf, state, verbose):
    """
    Consume bytes from `buf` (a mutable bytearray).
    For each complete display->motor packet, produce a reply.
    Returns list of bytes to write back to the display.
    """
    replies = []
    while buf:
        cat = buf[0]

        # --- READ (2 bytes total: cat, opcode, no checksum) ---
        if cat == CAT_READ:
            if len(buf) < 2:
                return replies  # incomplete, wait for more
            op = buf[1]
            del buf[0:2]
            reply = handle_read(op, state, verbose)
            if reply is not None:
                replies.append(reply)
                if verbose:
                    print(f"  RX  READ  op=0x{op:02x} -> TX {reply.hex(' ')}", file=sys.stderr)
            continue

        # --- WRITE (variable length) ---
        if cat == CAT_WRITE:
            if len(buf) < 2:
                return replies
            op = buf[1]

            if op == OP_WRITE_PAS:
                if len(buf) < 4: return replies
                level, ck = buf[2], buf[3]
                del buf[0:4]
                if chk8(cat, op, level) != ck:
                    if verbose: print(f"  WRITE_PAS bad checksum", file=sys.stderr)
                    continue
                name = BAFANG_LEVEL_TO_NAME.get(level, f"UNKNOWN(0x{level:02x})")
                state.assist_level = name
                if verbose: print(f"  RX  WRITE_PAS  level=0x{level:02x} → {name}", file=sys.stderr)
                # bbs-fw does NOT ACK write_pas per extcom.c (returns 4, no uart_write)
                continue

            if op == OP_WRITE_MODE:
                if len(buf) < 4: return replies
                mode, ck = buf[2], buf[3]
                del buf[0:4]
                if chk8(cat, op, mode) == ck:
                    if verbose: print(f"  RX  WRITE_MODE mode=0x{mode:02x}", file=sys.stderr)
                continue

            if op == OP_WRITE_LIGHTS:
                if len(buf) < 3: return replies
                state_byte = buf[2]
                del buf[0:3]
                state.lights_on = (state_byte == 0xF1)
                if verbose: print(f"  RX  WRITE_LIGHTS {'ON' if state.lights_on else 'OFF'}", file=sys.stderr)
                continue

            if op == OP_WRITE_SPEED_LIM:
                if len(buf) < 5: return replies
                # bbs-fw ignores this, just drops the bytes
                del buf[0:5]
                if verbose: print(f"  RX  WRITE_SPEED_LIM  (ignored)", file=sys.stderr)
                continue

            # unknown write opcode — advance one byte and resync
            if verbose: print(f"  UNKNOWN WRITE opcode 0x{op:02x} (drop)", file=sys.stderr)
            del buf[0]
            continue

        # Not a category byte — drop and resync
        if verbose:
            print(f"  drop resync byte 0x{cat:02x}", file=sys.stderr)
        del buf[0]

    return replies


# ---- Main --------------------------------------------------------------------

def configure_baud(fd, baud):
    """Set the slave PTY to raw + 1200 baud (mostly cosmetic; PTYs don't
    really rate-limit, but the display firmware may verify baud settings)."""
    attrs = termios.tcgetattr(fd)
    # ispeed, ospeed at index 4, 5
    baud_map = {1200: termios.B1200, 19200: termios.B19200}
    if baud in baud_map:
        attrs[4] = baud_map[baud]
        attrs[5] = baud_map[baud]
    # raw mode
    attrs[0] = 0        # iflag
    attrs[1] = 0        # oflag
    attrs[3] = 0        # lflag
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def main():
    ap = argparse.ArgumentParser(description="BBSHD motor emulator (Bafang UART).")
    ap.add_argument("--speed", type=float, default=0.0, help="fixed wheel kph (ignored if --speed-wave is set)")
    ap.add_argument("--speed-wave", choices=["sine", "triangle", "sawtooth"], default=None,
                    help="sweep speed over time instead of a fixed value, to test the speed UI")
    ap.add_argument("--speed-min", type=float, default=0.0, help="min kph for --speed-wave (default 0)")
    ap.add_argument("--speed-max", type=float, default=45.0, help="max kph for --speed-wave (default 45)")
    ap.add_argument("--speed-period", type=float, default=20.0,
                    help="seconds for one full --speed-wave cycle (default 20)")
    ap.add_argument("--batt", type=float, default=90.0, help="initial battery %%")
    ap.add_argument("--baud", type=int, default=1200, help="Bafang baud (default 1200)")
    ap.add_argument("--perimeter", type=int, default=2100,
                    help="wheel perimeter in mm (default 2100, matches firmware default)")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)
    configure_baud(slave, args.baud)

    # We keep the slave fd around so the pty doesn't close when the other side
    # detaches. We do all our I/O on the master.
    print(f"BBSHD mock motor running.")
    print(f"  Slave device: {slave_name}")
    print(f"  Baud:         {args.baud}")
    print(f"  Point the SW102 emulator at it:")
    print(f"    SW102_UART_PORT={slave_name} ./emu")
    print(f"  Point a real display via socat:")
    print(f"    socat -d -d {slave_name} /dev/ttyUSB0,b{args.baud},raw,echo=0")
    print()

    state = MotorState(args)
    rx_buf = bytearray()
    last_tick = time.monotonic()

    try:
        while True:
            # Wait up to 100 ms for master fd input
            r, _, _ = select.select([master], [], [], 0.1)
            now = time.monotonic()
            state.tick(now - last_tick)
            last_tick = now

            if master in r:
                try:
                    chunk = os.read(master, 256)
                except OSError:
                    chunk = b""
                if chunk:
                    rx_buf.extend(chunk)
                    if args.verbose:
                        print(f"RX <- {chunk.hex(' ')}", file=sys.stderr)

            replies = parse_display_frames(rx_buf, state, args.verbose)
            for reply in replies:
                os.write(master, reply)
                if args.verbose:
                    print(f"TX -> {reply.hex(' ')}", file=sys.stderr)

    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
