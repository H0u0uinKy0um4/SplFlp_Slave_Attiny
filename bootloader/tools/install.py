#!/usr/bin/env python3
"""One-shot UPDI installer for the RS485 OTA bootloader (ATtiny1616 slave).

Builds nothing itself; installs three things in a single UPDI session:

  1. Fuses (written FIRST, before any erase - see the safety notes below):
       OSCFG   (offset 2) = 0x02   20 MHz internal oscillator (factory value,
                                   set explicitly for determinism)
       SYSCFG0 (offset 5) = 0xC5   factory 0xC4 plus EESAVE=1: EEPROM survives
                                   chip erase; RSTPINCFG stays 01 (UPDI
                                   enabled) and CRCSEL keeps its factory bits
       BOOTEND (offset 8) = 0x08   2 KB boot section, application at 0x0800
  2. The bootloader image  (bootloader/.pio/build/ATtiny1616_Boot/firmware.hex)
  3. The OTA application image  (.pio/build/ATtiny1616_OTA/firmware.hex)

Why this cannot brick a module
------------------------------
  * prog.py (megaTinyCore's SerialUPDI tool) writes fuses BEFORE its
    chip-erase/write/verify sequence, so EESAVE=1 is already in force when
    the erase runs and EEPROM (module id, calibration, position map)
    survives.  Fuse order verified in prog.py's pymcuprog_basic().
  * UPDI stays enabled (RSTPINCFG untouched at 01): UPDI programming over
    PA0 always remains available as the last resort, no matter what the
    bootloader or a half-written application does.
  * The bootloader writes only the application region (>= 0x0800) and never
    fuses, so an RS485 flash can never remove the bootloader itself.
  * The two images are validated against the flash map and merged into a
    single Intel HEX, because prog.py's `-a write` chip-erases on every
    invocation: programming them as two separate runs would erase the
    bootloader again.  The app image must start exactly at 0x0800; a plain
    (bootloader-less) ATtiny1616 build starts at 0x0000 and is rejected
    because it would overwrite the bootloader.

Usage
-----
  python3 install.py [--port /dev/ttyUSB0] [--baud 230400]
                     [--boot-hex PATH] [--app-hex PATH] [--yes] [--dry-run]

Run `pio run` in bootloader/ and `pio run -e ATtiny1616_OTA` in the project
root first (or point --boot-hex/--app-hex at existing hex files).  Later
application updates do NOT use this script; they go over RS485 through the
master's OTA flasher.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

# --- Flash map (must match bootloader/src/protocol.h) ----------------------

BOOT_SECTION_SIZE = 2048          # BOOTEND = 8 -> 2 KB boot section
APP_START = 0x0800                # application base address
FLASH_END = 0x4000                # 16 KB total flash

# Fuse values written by this installer (offsets are prog.py's fuse offsets).
FUSES = {
    2: 0x02,   # OSCCFG: 20 MHz
    5: 0xC5,   # SYSCFG0: factory 0xC4 | EESAVE (UPDI pin config preserved)
    8: 0x08,   # BOOTEND: 2 KB boot section
}

DEVICE = "attiny1616"

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BOOT_HEX = REPO_ROOT / "bootloader" / ".pio" / "build" / "ATtiny1616_Boot" / "firmware.hex"
DEFAULT_APP_HEX = REPO_ROOT / ".pio" / "build" / "ATtiny1616_OTA" / "firmware.hex"
DEFAULT_MERGED_HEX = REPO_ROOT / "bootloader" / ".pio" / "build" / "ATtiny1616_Boot" / "install-combined.hex"


class InstallError(Exception):
    pass


# --- Intel HEX --------------------------------------------------------------

def parse_ihex(path: Path) -> list[tuple[int, bytes]]:
    """Parse an Intel HEX file into a list of (absolute address, data) records.

    Only the record types avr-objcopy emits are accepted (data, EOF,
    extended segment/linear address, start address).  Every record's
    checksum is verified.
    """
    records: list[tuple[int, bytes]] = []
    base = 0
    try:
        text = path.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as exc:
        raise InstallError(f"cannot read {path}: {exc}") from exc

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise InstallError(f"{path.name}:{lineno}: expected ':' record start")
        try:
            rec = bytes.fromhex(line[1:])
        except ValueError:
            raise InstallError(f"{path.name}:{lineno}: invalid hex digits") from None
        if len(rec) < 5 or len(rec) != 5 + rec[0]:
            raise InstallError(f"{path.name}:{lineno}: malformed record length")
        if sum(rec) & 0xFF:
            raise InstallError(f"{path.name}:{lineno}: checksum mismatch")
        addr = (rec[1] << 8) | rec[2]
        rtype = rec[3]
        data = rec[4:-1]
        if rtype == 0x00:
            records.append((base + addr, data))
        elif rtype == 0x01:
            break
        elif rtype == 0x02:
            if len(data) != 2:
                raise InstallError(f"{path.name}:{lineno}: bad extended segment record")
            base = ((data[0] << 8) | data[1]) << 4
        elif rtype == 0x04:
            if len(data) != 2:
                raise InstallError(f"{path.name}:{lineno}: bad extended linear record")
            base = ((data[0] << 8) | data[1]) << 16
        elif rtype in (0x03, 0x05):
            # Start-address hints (avr-objcopy emits type 03 with the entry
            # point): no flash content, ignore.
            pass
        else:
            raise InstallError(f"{path.name}:{lineno}: unsupported record type {rtype:#x}")
    if not records:
        raise InstallError(f"{path.name}: no data records")
    return records


def write_ihex(path: Path, records: list[tuple[int, bytes]]) -> None:
    """Write (address, data) records as a normalized Intel HEX file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii") as fh:
        for addr, data in sorted(records):
            if addr >= 0x10000 or addr + len(data) > 0x10000:
                raise InstallError(f"address {addr:#x} does not fit in 16-bit hex records")
            rec = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, 0x00]) + data
            rec += bytes([(-sum(rec)) & 0xFF])
            fh.write(":" + rec.hex().upper() + "\n")
        fh.write(":00000001FF\n")


def image_extent(records: list[tuple[int, bytes]]) -> tuple[int, int]:
    """Return (lowest address, one-past-highest address) of the records."""
    lo = min(addr for addr, _ in records)
    hi = max(addr + len(data) for addr, data in records)
    return lo, hi


def image_bytes(records: list[tuple[int, bytes]], start: int, end: int) -> bytes:
    """Materialise the image byte range [start, end), gaps filled with 0xFF."""
    out = bytearray([0xFF]) * (end - start)
    for addr, data in records:
        for i, byte in enumerate(data):
            pos = addr + i - start
            if 0 <= pos < len(out):
                out[pos] = byte
    return bytes(out)


# --- Validation --------------------------------------------------------------

def validate_boot(records: list[tuple[int, bytes]], name: str) -> None:
    lo, hi = image_extent(records)
    if lo != 0x0000:
        raise InstallError(
            f"{name}: boot image must start at 0x0000 (reset vector), starts at {lo:#06x}")
    if hi > APP_START:
        raise InstallError(
            f"{name}: boot image is {hi} bytes, overflows the {BOOT_SECTION_SIZE}-byte "
            f"boot section (fits only below {APP_START:#06x}). Shrink the bootloader.")


def validate_app(records: list[tuple[int, bytes]], name: str) -> None:
    lo, hi = image_extent(records)
    if lo != APP_START:
        raise InstallError(
            f"{name}: app image must start exactly at {APP_START:#06x} (ATtiny1616_OTA "
            f"build), starts at {lo:#06x}. A plain ATtiny1616 build would overwrite "
            "the bootloader and is refused.")
    if hi > FLASH_END:
        raise InstallError(
            f"{name}: app image ends at {hi:#06x}, past the end of flash ({FLASH_END:#06x})")


# --- prog.py discovery ---------------------------------------------------------

def find_prog_py(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise InstallError(f"prog.py not found at {path}")
        return path
    core_dir = Path.home() / ".platformio"
    path = core_dir / "packages" / "framework-arduino-megaavr-megatinycore" / "tools" / "prog.py"
    if not path.is_file():
        raise InstallError(
            f"prog.py not found at {path}; pass --prog /path/to/prog.py "
            "(megaTinyCore's tools/prog.py)")
    return path


# --- Main ----------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install the RS485 OTA bootloader + OTA app + fuses in one UPDI session.")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="UPDI serial port (default: %(default)s)")
    parser.add_argument("--baud", default="230400", help="UPDI baud rate (default: %(default)s)")
    parser.add_argument("--boot-hex", type=Path, default=DEFAULT_BOOT_HEX,
                        help="bootloader hex (default: %(default)s)")
    parser.add_argument("--app-hex", type=Path, default=DEFAULT_APP_HEX,
                        help="OTA app hex (default: %(default)s)")
    parser.add_argument("--merged-hex", type=Path, default=DEFAULT_MERGED_HEX,
                        help="where to write the merged image (default: %(default)s)")
    parser.add_argument("--prog", default=None, help="path to megaTinyCore's prog.py")
    parser.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    parser.add_argument("--dry-run", action="store_true",
                        help="parse, validate and merge only; do not touch hardware")
    args = parser.parse_args()

    try:
        boot_hex: Path = args.boot_hex
        app_hex: Path = args.app_hex
        for path in (boot_hex, app_hex):
            if not path.is_file():
                raise InstallError(
                    f"{path} not found.\n"
                    "  Build first:\n"
                    "    cd bootloader && pio run\n"
                "    cd .. && pio run -e ATtiny1616_OTA")

        boot_records = parse_ihex(boot_hex)
        app_records = parse_ihex(app_hex)
        validate_boot(boot_records, boot_hex.name)
        validate_app(app_records, app_hex.name)

        _, boot_hi = image_extent(boot_records)
        app_lo, app_hi = image_extent(app_records)
        app_image = image_bytes(app_records, app_lo, app_hi)
        app_crc = zlib.crc32(app_image) & 0xFFFFFFFF

        write_ihex(args.merged_hex, boot_records + app_records)

        print(f"Bootloader : {boot_hex}")
        print(f"             {boot_hi} bytes (0x0000..{boot_hi - 1:#06x}), "
              f"boot section is {BOOT_SECTION_SIZE} bytes")
        print(f"OTA app    : {app_hex}")
        print(f"             {len(app_image)} bytes ({app_lo:#06x}..{app_hi - 1:#06x}), "
              f"app region is {FLASH_END - APP_START} bytes")
        print(f"             CRC-32 (what a later RS485 flash must reproduce): "
              f"{app_crc:08X}")
        print(f"Merged     : {args.merged_hex}")
        print("Fuses      : " + ", ".join(f"{offset}={value:#04x}" for offset, value in sorted(FUSES.items()))
              + "  (EESAVE=1, UPDI pin config preserved, 2 KB boot section)")

        prog_path = find_prog_py(args.prog)
        cmd = [
            sys.executable, str(prog_path),
            "--tool", "uart",
            "--device", DEVICE,
            "--uart", args.port,
            "-b", args.baud,
            "--fuses", *[f"{offset}:{value:#04x}" for offset, value in sorted(FUSES.items())],
            "-f", str(args.merged_hex),
            "-a", "write",
            "-v",
        ]

        if args.dry_run:
            print("\nDry run: nothing was written. Would run:")
            print("  " + " ".join(cmd))
            return 0

        if not args.yes:
            print(f"\nAbout to install bootloader + app + fuses on the module at {args.port}.")
            answer = input("EEPROM (module id, calibration) is preserved. Proceed? [y/N] ")
            if answer.strip().lower() not in ("y", "yes"):
                print("Aborted, nothing was written.")
                return 1

        print("\nRunning (fuses are written before the chip erase, so EEPROM survives):")
        print("  " + " ".join(cmd) + "\n")
        result = subprocess.run(cmd)
        if result.returncode != 0:
            raise InstallError(
                "prog.py failed (exit code {}). If it failed before the flash write, "
                "the chip may be erased but EEPROM is intact and UPDI works: "
                "fix the cause and re-run this script.".format(result.returncode))

        print(
            "\nInstalled. On the next reset the bootloader starts the application "
            "after a 500 ms boot window.\n"
            "Module id and calibration EEPROM were preserved "
            "(verify with the master's scan / config query).")
        return 0
    except InstallError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
