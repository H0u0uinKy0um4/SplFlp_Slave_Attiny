"""Python view of the canonical persisted-layout record.

No regex scraping and no fallback defaults: if the record is missing or
malformed the tools fail loudly instead of inspecting a different layout.
"""

from __future__ import annotations

import json
from pathlib import Path

RECORD_PATH = Path(__file__).resolve().parents[2] / "layout" / "persisted_layout.json"


def load_record() -> dict:
    try:
        record = json.loads(RECORD_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"error: cannot read persisted layout {RECORD_PATH}: {exc}") from exc
    if record.get("schema") != 1 or "eeprom" not in record or "flash" not in record:
        raise SystemExit(f"error: unsupported persisted layout record in {RECORD_PATH}")
    return record


RECORD = load_record()
EEPROM = RECORD["eeprom"]
FLASH = RECORD["flash"]
PROTOCOL = RECORD["protocol"]

# Flat view compatible with the tool reports and the generated C names.
LAYOUT = {
    "init": EEPROM["init_addr"],
    "home_offset": EEPROM["home_offset_addr"],
    "total_steps": EEPROM["total_steps_addr"],
    "module_id": EEPROM["module_id_addr"],
    "auto_home": EEPROM["auto_home_addr"],
    "map_start": EEPROM["map_start_addr"],
    "map_chars": EEPROM["map_chars_addr"],
    "map_format": EEPROM["map_format_addr"],
    "fw_version": EEPROM["fw_version_addr"],
    "fw_version_slot": EEPROM["fw_version_slot"],
    "init_value": EEPROM["init_value"],
    "format_value": EEPROM["map_format_value"],
    "flag_addr": EEPROM["boot_flag_addr"],
    "flag_value": EEPROM["boot_flag_value"],
    "flag_clear_value": EEPROM["boot_flag_clear_value"],
    "fw_bus_compat": PROTOCOL["fw_bus_compat"],
}

APP_START = FLASH["app_start"]
BOOT_SECTION_SIZE = FLASH["bootloader_size"]
FLASH_END = FLASH["flash_end"]
FLASH_PAGE_SIZE = FLASH["flash_page_size"]
APP_MAX_SIZE = FLASH["app_max_size"]
APP_MAX_PAGES = FLASH["app_max_pages"]
BOOTLOADER_VERSION = PROTOCOL["bootloader_version"]
