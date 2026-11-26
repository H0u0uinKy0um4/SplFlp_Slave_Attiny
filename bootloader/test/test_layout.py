#!/usr/bin/env python3
"""Host check that every consumer reads the same persisted-layout record.

    python3 bootloader/test/test_layout.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "bootloader" / "tools"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "tools"))

import generate_layout  # noqa: E402
import install  # noqa: E402
import layout  # noqa: E402

record = json.loads((ROOT / "layout" / "persisted_layout.json").read_text())
assert record["schema"] == 1

# The Python view must be the record, not a fallback copy.
assert layout.APP_START == record["flash"]["app_start"]
assert layout.BOOT_SECTION_SIZE == record["flash"]["bootloader_size"]
assert layout.FLASH_END == record["flash"]["flash_end"]
assert layout.FLASH_PAGE_SIZE == record["flash"]["flash_page_size"]
assert layout.LAYOUT["flag_addr"] == record["eeprom"]["boot_flag_addr"]
assert layout.LAYOUT["flag_value"] == record["eeprom"]["boot_flag_value"]

# The host tools must use that same view.
assert install.APP_START == layout.APP_START
assert install.BOOT_SECTION_SIZE == layout.BOOT_SECTION_SIZE
assert install.FLASH_END == layout.FLASH_END
assert install.FLASH_PAGE_SIZE == layout.FLASH_PAGE_SIZE
assert install.APP_MAX_SIZE == record["flash"]["app_max_size"]
assert install.EEPROM_BOOT_FLAG_ADDR == layout.LAYOUT["flag_addr"]
assert install.FUSES[8] == record["flash"]["bootloader_pages"]

# Both generated C headers must be reproducible from the record.
for index, target in enumerate(generate_layout.TARGETS):
    guard = "PERSISTED_LAYOUT_H" if index == 0 else "BOOT_PERSISTED_LAYOUT_H"
    expected = generate_layout.render(record, guard)
    assert target.read_text() == expected, f"stale generated header: {target}"

print("OK: persisted layout is shared by C builds and host tools")
