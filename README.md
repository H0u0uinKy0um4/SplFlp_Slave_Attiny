# Split-Flap slave firmware (ATtiny1616)

Application and OTA bootloader for one split-flap module. The module speaks an
ASCII RS485 protocol to the ESP32 master and can be reflashed over that same
bus, without UPDI.

This repository is the **canonical source** for the wire protocol, the OTA
bootloader and the cross-repo version model. The other two repositories link
here instead of copying those details.

## Build targets

| Environment | Links at | Purpose | How to install |
| --- | --- | --- | --- |
| `ATtiny1616_OTA` | `0x0800` | OTA application image (normal build for updates) | RS485 from Home Assistant |
| `ATtiny1616` | `0x0000` | Plain application without a bootloader | Only via `bootloader/tools/install.py` |
| `bootloader/` (subproject) | `0x0000` | 2 KB boot section | UPDI, `bootloader/tools/install.py` |

`ATtiny1616_OTA` is **build-only** on purpose. Uploading it with PlatformIO
over UPDI chip-erases the boot section.

## Quick start

```bash
# Normal application image for an OTA
pio run -t clean -e ATtiny1616_OTA
pio run -e ATtiny1616_OTA
# artifact: .pio/build/ATtiny1616_OTA/firmware.hex -> Home Assistant panel

# One-time install / recovery of bootloader + app + fuses over UPDI
python3 bootloader/tools/install.py --yes

# Read-only on-chip diagnostics (works while the module is silent)
python3 bootloader/tools/dump_eeprom.py
```

## Repository map

| Path | Contents |
| --- | --- |
| `src/`, `include/` | Application: motion, calibration, EEPROM, RS485 parser, reset hook |
| `bootloader/` | 2 KB OTA bootloader and its UPDI/bench tools |
| `PROTOCOL.md` | **Canonical** RS485 + OTA wire contract and recovery diagnostics |
| `docs/OTA_BUILD.md` | Day-to-day build, upload and "did it take?" recipe |
| `docs/VERSIONING.md` | **Canonical** cross-repo compatibility/release model |
| `bootloader/README.md` | Reusable bootloader design notes and integration checklist |

## Version

```c
// include/FirmwareVersion.h
#define FW_BUS_COMPAT 1      // slave <-> master compatibility generation
#define FW_RELEASE "0.1"     // this firmware's release inside it
#define FW_VERSION "1.0.1"   // composed, persisted in EEPROM 205..212
```

`m<id>v` reports `m<id>v:<bus_compat>:<version>`; `m<id>s` ends with the same
two fields. The master adds a separate MQTT compatibility generation for the
Home Assistant side. See [`docs/VERSIONING.md`](docs/VERSIONING.md) for the
two-axis model, the rules and the bump procedure.

## Hard rules

- Never upload the `ATtiny1616_OTA` build over UPDI; use `install.py`.
- Never clear `RSTCTRL.RSTFR` before the bootloader's jump to the app.
- Software reset must use the CCP-protected `RSTCTRL.SWRR` write.
- The bootloader writes only `0x0800..0x3FFF`; it never touches fuses or EEPROM.
- UPDI is the last resort, not the normal update path.
- Moves are blocking; do not query a module immediately after a move.
