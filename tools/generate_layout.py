#!/usr/bin/env python3
"""Generate the C headers for the canonical persisted-layout record.

The JSON file is the single source of truth. Both C builds and the Python
tools read it (directly or through these generated headers), so a layout
change lands in one place instead of being mirrored in five files.

    python3 tools/generate_layout.py            # write the two headers
    python3 tools/generate_layout.py --check    # fail if they are stale
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORD = ROOT / "layout" / "persisted_layout.json"
TARGETS = (
    ROOT / "include" / "PersistedLayout.h",
    ROOT / "bootloader" / "src" / "persisted_layout.h",
)


def render(record: dict, guard: str) -> str:
    eeprom = record["eeprom"]
    flash = record["flash"]
    protocol = record["protocol"]

    pairs = [
        ("EEPROM size", "SF_EEPROM_SIZE", eeprom["size"]),
        ("EEPROM init address", "SF_EEPROM_INIT_ADDR", eeprom["init_addr"]),
        ("EEPROM init value", "SF_EEPROM_INIT_VALUE", eeprom["init_value"]),
        ("home offset", "SF_EEPROM_HOME_OFFSET_ADDR", eeprom["home_offset_addr"]),
        ("steps per revolution", "SF_EEPROM_TOTAL_STEPS_ADDR", eeprom["total_steps_addr"]),
        ("module id", "SF_EEPROM_MODULE_ID_ADDR", eeprom["module_id_addr"]),
        ("auto home", "SF_EEPROM_AUTO_HOME_ADDR", eeprom["auto_home_addr"]),
        ("position map", "SF_EEPROM_MAP_START_ADDR", eeprom["map_start_addr"]),
        ("drum characters", "SF_EEPROM_MAP_CHARS_ADDR", eeprom["map_chars_addr"]),
        ("map format tag", "SF_EEPROM_MAP_FORMAT_ADDR", eeprom["map_format_addr"]),
        ("map format value", "SF_EEPROM_MAP_FORMAT_VALUE", eeprom["map_format_value"]),
        ("firmware version", "SF_EEPROM_FW_VERSION_ADDR", eeprom["fw_version_addr"]),
        ("firmware version slot", "SF_FW_VERSION_SLOT", eeprom["fw_version_slot"]),
        ("debug markers first", "SF_DEBUG_MARKERS_START", eeprom["debug_markers_start"]),
        ("debug markers last", "SF_DEBUG_MARKERS_END", eeprom["debug_markers_end"]),
        ("boot flag", "SF_EEPROM_BOOT_FLAG_ADDR", eeprom["boot_flag_addr"]),
        ("boot flag armed value", "SF_BOOT_FLAG_VALUE", eeprom["boot_flag_value"]),
        ("boot flag cleared value", "SF_BOOT_FLAG_CLEAR_VALUE", eeprom["boot_flag_clear_value"]),
        ("bootloader section pages", "SF_BOOTLOADER_PAGES", flash["bootloader_pages"]),
        ("bootloader section bytes", "SF_BOOTLOADER_SIZE", flash["bootloader_size"]),
        ("application start byte", "SF_APP_START_BYTE", flash["app_start"]),
        ("flash page size", "SF_FLASH_PAGE_SIZE", flash["flash_page_size"]),
        ("flash total size", "SF_FLASH_TOTAL_SIZE", flash["flash_total_size"]),
        ("flash end", "SF_FLASH_END", flash["flash_end"]),
        ("maximum application size", "SF_APP_MAX_SIZE", flash["app_max_size"]),
        ("maximum application pages", "SF_APP_MAX_PAGES", flash["app_max_pages"]),
        ("bus compatibility generation", "SF_FW_BUS_COMPAT", protocol["fw_bus_compat"]),
        ("bootloader version", "SF_BOOTLOADER_VERSION", protocol["bootloader_version"]),
    ]

    lines = [
        "/* Generated from layout/persisted_layout.json by tools/generate_layout.py.",
        " * Do not edit by hand: change the record and regenerate. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    for comment, name, value in pairs:
        # The two generation numbers are stringified into wire versions, so
        # they must render as plain decimal digits, never as 0x01.
        text = str(value) if name in ("SF_FW_BUS_COMPAT", "SF_BOOTLOADER_VERSION") \
            else f"0x{value:02X}"
        lines.append(f"#define {name:36s} {text} /* {comment} */")
    lines += ["", f"#endif /* {guard} */", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify the generated headers match the record")
    args = parser.parse_args()

    record = json.loads(RECORD.read_text(encoding="utf-8"))
    stale = False
    for index, target in enumerate(TARGETS):
        guard = "PERSISTED_LAYOUT_H" if index == 0 else "BOOT_PERSISTED_LAYOUT_H"
        content = render(record, guard)
        if args.check:
            if not target.exists() or target.read_text(encoding="utf-8") != content:
                print(f"stale: {target.relative_to(ROOT)}")
                stale = True
        else:
            target.write_text(content, encoding="utf-8")
            print(f"wrote {target.relative_to(ROOT)}")

    if args.check:
        if stale:
            print("run: python3 tools/generate_layout.py")
            return 1
        print("persisted layout headers are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
