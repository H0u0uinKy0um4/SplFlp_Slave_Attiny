#!/usr/bin/env python3
"""Read-only diagnostic dump of an OTA module over UPDI.

When a module "is silent" (no homing, no answer to m<id>d) the cause is one of
a small number of states, and they are all visible without guessing:

  * the boot flag at EEPROM 255 is armed -> the bootloader stays in update
    mode and never runs the application (it only speaks the binary protocol);
  * there is no application at 0x0800 -> same, the bootloader stays put;
  * the BOOTEND fuse does not match the address the app was linked for ->
    the app starts but its interrupt vectors point at bootloader code;
  * the app runs but is not homing / not answering for its own reasons
    (auto-home disabled, unexpected module id, wiped EEPROM).

This tool prints all of that: fuses, the first bytes of the application at
0x0800, and the EEPROM fields the boot policy and the app depend on. It never
writes anything.

Requires megaTinyCore's bundled pymcuprog (the same package prog.py uses) and
a UPDI adapter on the module.

Usage:
  python3 dump_eeprom.py
  python3 dump_eeprom.py --port /dev/ttyUSB1
  python3 dump_eeprom.py --raw            # hexdump all 256 EEPROM bytes
  python3 dump_eeprom.py --json           # machine-readable
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
import install  # noqa: E402  (path is prepared above; same directory)

REPO_ROOT = Path(__file__).resolve().parents[2]
DEVICE = "attiny1616"

# Fallbacks only: the real values are read out of the headers below so the
# tool cannot silently drift from the firmware it is inspecting.
LAYOUT_DEFAULTS: dict[str, int] = {
    "init": 0,
    "home_offset": 1,
    "total_steps": 3,
    "module_id": 5,
    "auto_home": 6,
    "map_chars": 140,
    "map_format": 204,
    "init_value": 0x5D,
    "format_value": 0x02,
    "flag_addr": 255,
    "flag_value": 0xB0,
}

APP_START = 0x0800


# --- Shared constants, read from the firmware headers ---------------------------

def _numeric(pattern: str, text: str) -> int | None:
    match = re.search(pattern, text)
    if not match:
        return None
    return int(match.group(1), 0)


def load_layout() -> tuple[dict[str, int], list[str]]:
    """Pull the EEPROM map and boot-flag values out of the firmware headers."""
    layout = dict(LAYOUT_DEFAULTS)
    warnings: list[str] = []

    handler = REPO_ROOT / "include" / "ModuleHandler.h"
    protocol = REPO_ROOT / "include" / "OTAProtocol.h"

    try:
        text = handler.read_text(encoding="utf-8")
    except OSError as exc:
        warnings.append(f"cannot read {handler} ({exc}); using built-in EEPROM map")
    else:
        fields = {
            "init": r"ADDR_INIT\s*=\s*(\w+)",
            "home_offset": r"ADDR_HOME_OFFSET\s*=\s*(\w+)",
            "total_steps": r"ADDR_TOTAL_STEPS\s*=\s*(\w+)",
            "module_id": r"ADDR_MODULE_ID\s*=\s*(\w+)",
            "auto_home": r"ADDR_AUTO_HOME\s*=\s*(\w+)",
            "map_chars": r"ADDR_MAP_CHARS\s*=\s*(\w+)",
            "map_format": r"ADDR_FORMAT\s*=\s*(\w+)",
            "init_value": r"INIT_VALUE\s*=\s*(\w+)",
            "format_value": r"FORMAT_VALUE\s*=\s*(\w+)",
        }
        for key, pattern in fields.items():
            value = _numeric(pattern, text)
            if value is None:
                warnings.append(f"{handler.name}: {key} not found, using {layout[key]:#04x}")
            else:
                layout[key] = value

    try:
        text = protocol.read_text(encoding="utf-8")
    except OSError as exc:
        warnings.append(f"cannot read {protocol} ({exc}); using built-in boot flag")
    else:
        for key, pattern in {"flag_addr": r"OTA_EEPROM_BOOT_FLAG_ADDR\s+(\w+)",
                             "flag_value": r"OTA_BOOT_FLAG_VALUE\s+(\w+)"}.items():
            value = _numeric(pattern, text)
            if value is None:
                warnings.append(f"{protocol.name}: {key} not found, using {layout[key]:#04x}")
            else:
                layout[key] = value

    return layout, warnings


# --- Decoding (pure functions, no hardware) -------------------------------------

def decode(eeprom: bytes, fuses: bytes, app_head: bytes, layout: dict[str, int]) -> dict:
    def u16(offset: int) -> int:
        return eeprom[offset] | (eeprom[offset + 1] << 8)

    bootend = fuses[8] if len(fuses) > 8 else None
    flag = eeprom[layout["flag_addr"]]
    head_word = app_head[0] | (app_head[1] << 8) if len(app_head) >= 2 else 0xFFFF

    state = {
        "fuses": fuses.hex(" ").upper(),
        "osccfg": fuses[2] if len(fuses) > 2 else None,
        "syscfg0": fuses[5] if len(fuses) > 5 else None,
        "bootend": bootend,
        "bootend_bytes": None if bootend is None else bootend * 256,
        "app_head": app_head.hex(" ").upper(),
        "app_word": head_word,
        "app_present": head_word != 0xFFFF,
        "eeprom_init": eeprom[layout["init"]],
        "eeprom_initialised": eeprom[layout["init"]] == layout["init_value"],
        "module_id": eeprom[layout["module_id"]],
        "auto_home": eeprom[layout["auto_home"]],
        "home_offset": u16(layout["home_offset"]),
        "total_steps": u16(layout["total_steps"]),
        "map_format": eeprom[layout["map_format"]],
        "boot_flag": flag,
        "boot_flag_armed": flag == layout["flag_value"],
        "eeprom_size": len(eeprom),
    }
    state["verdicts"] = _verdicts(state, layout)
    return state


def _flag_armed_text(state: dict, layout: dict[str, int]) -> str:
    return (f"boot flag at EEPROM {layout['flag_addr']} is armed "
            f"({state['boot_flag']:#04x}): the bootloader stays in update mode and "
            f"the application never runs. It only speaks the binary protocol, so it "
            f"does not home and does not answer ASCII. Clear it with "
            f'{{"action":"reset","module":<id>}} on <prefix>/command/firmware '
            f"(the bootloader handles RESET and clears the flag).")


def _verdicts(state: dict, layout: dict[str, int]) -> list[tuple[str, str]]:
    """Return (level, text) pairs; level is 'error', 'warn' or 'ok'."""
    out: list[tuple[str, str]] = []

    bootend = state["bootend"]
    bootend_ok = bootend is not None and state["bootend_bytes"] == APP_START
    if bootend is None:
        out.append(("error", "could not read the BOOTEND fuse"))
    elif not bootend_ok:
        out.append((
            "error",
            f"BOOTEND={bootend:#04x} puts the application at "
            f"{state['bootend_bytes']:#06x}, but the app is linked for "
            f"{APP_START:#06x}: its interrupt vectors are not relocated, so it "
            f"crashes on the first interrupt. Write fuses "
            f"2:0x02 5:0xc5 8:0x08 (no erase needed)."))

    # A wiped EEPROM is not a fault: the app rewrites every field in
    # firstBoot() on this very boot, so its stale values explain nothing.
    if not state["eeprom_initialised"]:
        out.append((
            "info",
            f"EEPROM init byte is 0x{state['eeprom_init']:02X} "
            f"(expected 0x{layout['init_value']:02X}): the EEPROM is blank or "
            f"unformatted, so the app writes defaults (module id 3, auto-home on) "
            f"on this boot and the EEPROM values above are not meaningful."))
        if state["boot_flag_armed"]:
            out.append(("error", _flag_armed_text(state, layout)))
        elif state["app_present"] and bootend_ok:
            out.append((
                "ok",
                f"boot flag clear, application present at {APP_START:#06x}: the "
                f"bootloader listens for 500 ms after reset, then jumps to the app."))
        return out

    if state["boot_flag_armed"]:
        out.append(("error", _flag_armed_text(state, layout)))
    elif not state["app_present"]:
        out.append((
            "error",
            f"no application at {APP_START:#06x} (reads 0x{state['app_word']:04X}): "
            f"the bootloader stays in update mode. Install/flash the app."))
    elif bootend_ok:
        out.append((
            "ok",
            f"boot flag clear, application present at {APP_START:#06x}: the "
            f"bootloader listens for 500 ms after reset, then jumps to the app."))

    if state["auto_home"] != 1:
        out.append((
            "warn",
            f"auto-home byte is {state['auto_home']} (not 1): the app will NOT home "
            f"at power-up. If the module is also silent, look for a different cause; "
            f"if it only fails to home, this is the cause."))
    if state["total_steps"] == 0:
        out.append((
            "warn",
            "steps/rev is 0: moves are refused and homing gives up immediately."))
    if state["map_format"] != layout["format_value"]:
        out.append((
            "warn",
            f"map format byte is 0x{state['map_format']:02X} "
            f"(expected 0x{layout['format_value']:02X}): the app will migrate the map "
            f"on the next boot."))

    out.append((
        "info",
        f"module id {state['module_id']}, auto-home {state['auto_home']}, "
        f"home offset {state['home_offset']}, steps/rev {state['total_steps']} "
        f"- query it with m{state['module_id']}d"))
    return out


def hexdump(data: bytes, width: int = 16) -> str:
    lines = []
    for offset in range(0, len(data), width):
        chunk = data[offset:offset + width]
        hex_part = " ".join(f"{byte:02X}" for byte in chunk)
        text = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
        lines.append(f"  {offset:03d} 0x{offset:02X}: {hex_part:<{width * 3 - 1}}  {text}")
    return "\n".join(lines)


def report(state: dict, layout: dict[str, int]) -> str:
    levels = {"error": "!!", "warn": " ?", "ok": "  ", "info": "  "}
    lines = [
        f"Fuses   : {state['fuses']}",
        f"          OSCCFG={state['osccfg']:#04x} (20 MHz)   "
        f"SYSCFG0={state['syscfg0']:#04x} (EESAVE=1)   "
        f"BOOTEND={state['bootend']:#04x} "
        f"({state['bootend_bytes']} bytes of boot section)",
        f"App     : @{APP_START:#06x} = {state['app_head']}   "
        f"({'present' if state['app_present'] else 'BLANK'})",
        f"EEPROM  : init=0x{state['eeprom_init']:02X}  id={state['module_id']}  "
        f"auto-home={state['auto_home']}  home-offset={state['home_offset']}  "
        f"steps/rev={state['total_steps']}  map=0x{state['map_format']:02X}  "
        f"boot-flag=0x{state['boot_flag']:02X}",
        "",
    ]
    lines += [f"{levels[level]} {text}" for level, text in state["verdicts"]]
    return "\n".join(lines)


# --- Hardware access ------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read-only UPDI dump of an OTA module: fuses, app header and EEPROM.")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="UPDI serial port (default: %(default)s)")
    parser.add_argument("--baud", default="230400", help="UPDI baud rate (default: %(default)s)")
    parser.add_argument("--prog", default=None, help="path to megaTinyCore's prog.py")
    parser.add_argument("--raw", action="store_true", help="hexdump the whole EEPROM")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument("-v", "--verbose", action="store_true", help="pymcuprog INFO logging")
    args = parser.parse_args()

    layout, warnings = load_layout()

    try:
        prog_py = install.find_prog_py(args.prog)
    except install.InstallError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    libs = prog_py.parent / "libs"
    if not libs.is_dir():
        print(f"error: {libs} not found; --prog must point at megaTinyCore's tools/prog.py",
              file=sys.stderr)
        return 1

    sys.path.insert(0, str(libs))
    try:
        import pymcuprog.pymcuprog_main as pymcu
        from pymcuprog.pymcuprog import setup_logging
    except ImportError as exc:
        print(f"error: cannot import the bundled pymcuprog ({exc})", file=sys.stderr)
        return 1

    import logging
    setup_logging(user_requested_level=logging.INFO if args.verbose else logging.ERROR)

    backend = pymcu.Backend()
    session = False
    try:
        connection = pymcu._setup_tool_connection(
            argparse.Namespace(tool="uart", uart=args.port))
        backend.connect_to_tool(connection)
        device = pymcu._select_target_device(
            backend, argparse.Namespace(device=DEVICE, tool="uart"))
        status = pymcu._start_session(backend, device, argparse.Namespace(
            interface="updi", clk=str(args.baud), high_voltage=False,
            user_row_locked_device=False, chip_erase_locked_device=False,
            packpath=False))
        if status != pymcu.STATUS_SUCCESS:
            print(f"error: cannot start a UPDI session on {args.port}", file=sys.stderr)
            return 1
        session = True
        pymcu._action_ping(backend)

        fuses = bytes(backend.read_memory(pymcu.MemoryNames.FUSES, 0, 0, None)[0].data)
        app_head = bytes(backend.read_memory(
            pymcu.MemoryNames.FLASH, APP_START, 4, None)[0].data)
        eeprom = bytes(backend.read_memory(pymcu.MemoryNames.EEPROM, 0, 0, None)[0].data)
    except Exception as exc:  # noqa: BLE001 - report anything the tool raises
        print(f"error: {exc}", file=sys.stderr)
        print("  check the UPDI wiring, the port and that the module is powered", file=sys.stderr)
        return 1
    finally:
        if session:
            try:
                backend.end_session()
            except Exception:  # noqa: BLE001 - best effort on the way out
                pass
        try:
            backend.disconnect_from_tool()
        except Exception:  # noqa: BLE001 - best effort on the way out
            pass

    if not eeprom:
        print("error: the device returned an empty EEPROM read", file=sys.stderr)
        return 1
    if len(eeprom) <= layout["flag_addr"]:
        print(f"error: EEPROM is {len(eeprom)} bytes, the boot flag sits at "
              f"{layout['flag_addr']}", file=sys.stderr)
        return 1

    state = decode(eeprom, fuses, app_head, layout)

    if args.json:
        print(json.dumps({k: v for k, v in state.items() if k != "verdicts"}
                         | {"verdicts": [{"level": lvl, "text": txt}
                                         for lvl, txt in state["verdicts"]]},
                         indent=2))
    else:
        print(f"UPDI {args.port} @ {args.baud}  ({DEVICE})\n")
        print(report(state, layout))
        for warning in warnings:
            print(f"  (note: {warning})")
        if args.raw:
            print("\nEEPROM:")
            print(hexdump(eeprom))

    return 0


if __name__ == "__main__":
    sys.exit(main())
