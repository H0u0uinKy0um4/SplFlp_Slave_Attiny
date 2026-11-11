# OTA build and upload guide

How to build a new slave application image, ship it over RS485 from Home
Assistant, and prove that the module really runs the new build.

This is the day-to-day recipe. The wire protocol, boot policy and recovery
diagnostics are in [`../PROTOCOL.md`](../PROTOCOL.md); the version rules are in
[`VERSIONING.md`](VERSIONING.md).

## 1. Bump the release

Edit `include/FirmwareVersion.h`:

```c
#define FW_COMPAT 1        // leave this unless the contract broke
#define FW_RELEASE "0.1"   // bump for every uploaded image
```

`FW_VERSION` is composed automatically (`1.0.1` for the example above). Keep
the whole version at most 7 characters. The first boot after flashing copies
it to EEPROM 205..212 only if it changed.

## 2. Build the OTA image

```bash
cd ~/GIT_Personal/PlatformIO/SplFlp_Slave_Attiny

# Drop stale objects and any leftover debug flags before a production build.
pio run -t clean -e ATtiny1616_OTA
pio run -e ATtiny1616_OTA
```

Success looks like:

```
Flash: [======    ]  59.9% (used 9809 bytes from 16384 bytes)
Building .pio/build/ATtiny1616_OTA/firmware.hex
```

The app region is 14336 bytes (`0x0800..0x3FFF`); the linker fails if the
build no longer fits. Debug builds with `-DOTA_DEBUG_MARKERS` are opt-in and
must not be uploaded by accident: always clean before a production build.

## 3. Check the artifact

```bash
python3 bootloader/tools/flash_mqtt.py --module 3 --dry-run
```

It parses the same `firmware.hex`, prints size/pages/CRC-32 and the MQTT
messages it would publish, and writes nothing. `--dry-run` also works without
a broker or `mosquitto_pub`. Expected: starts at `0x0800`, at most 14336 bytes.
The CRC-32 printed here must match the `crc32` in the master log later.

## 4. Upload from Home Assistant

1. Open the split-flap panel -> the hardware page with the **MODULE FIRMWARE**
   card.
2. Enter the module id (or pick it from the datalist). The line under the
   fields shows the version that module currently reports.
3. Choose `.pio/build/ATtiny1616_OTA/firmware.hex`.
4. Press **FLASH MODULE** and confirm.

The image is uploaded to the master first (a few seconds), then written over
RS485. The display pauses for roughly 25 seconds.

Master log on success:

```
OTA: collecting an image for module 3
OTA: flashing 9809 bytes (154 pages, crc32 12259e53) to module 3
OTA: asking module 3 to enter the bootloader
m3b:1
OTA: bootloader ready (version 1, staying=1)
OTA: module 3 verified the image and rebooted
OTA: session complete
```

After the reboot the module auto-homes (13-15 seconds of RS485 deafness) and
then answers again. The panel shows it briefly offline during homing; this is
normal.

If the master reports an unsupported bootloader protocol, stop: the module and
master do not share the OTA generation. Update the master (or reinstall the
bootloader with `install.py`) before retrying. The session fails before writing
any page, so the module stays recoverable.

## 5. Prove the update took

Any one of these closes the loop; together they are conclusive:

| Where | What you see |
| --- | --- |
| Master log | `crc32` of the bytes actually written, then `verified the image and rebooted`. |
| Panel -> module inspector | `FIRMWARE` stat shows the new version after the post-OTA refresh. |
| Panel -> firmware card | "Installed firmware: `<version>`" for the selected module. |
| RS485 console | `m3v` -> `m3v:1:1.0.1`; `m3s` -> `m3s:<pos>:0:1:1:1.0.1`. |
| UPDI | `python3 bootloader/tools/dump_eeprom.py` prints `Version : 1.0.1` and `compat=1`. |

If the version stays old after `session complete`, the module either never
rebooted into the new app or the app predates version tracking. Check
`position_known`/`m<id>s` first, then the UPDI dump.

## Cancelling and recovery

The full state machine and diagnostic table are in
[`../PROTOCOL.md`](../PROTOCOL.md), "Boot policy" and "Diagnosing a silent
module". The short version:

- Cancel before the first page: the master resets; the old app boots.
- Cancel mid-session or power cut: the boot flag stays armed, the module waits
  in the bootloader, and a re-flash from the panel recovers it.
- Module silent with the flag armed: panel **BOOT INTO APP** (RESET) or
  `dump_eeprom.py --clear-boot-flag`.
- A power cut after a verified FINISH leaves the flag clear; the new app boots
  normally.

Never "fix" a module by uploading `ATtiny1616_OTA` over UPDI with PlatformIO:
that removes the bootloader. Use `install.py` for any UPDI flash.

## Bootloader changes (rare)

Only when `bootloader/src/` or the boot policy changed:

```bash
cd bootloader
pio run                        # must stay within the 2048-byte boot section

cd ..
python3 bootloader/tools/install.py --yes
```

`install.py` writes fuses first (EESAVE keeps the EEPROM calibration), then
bootloader + current OTA app, clears the boot flag and resets the module.
`--keep-boot-flag` skips the clear. See `bootloader/README.md` for the design
rules that must not be regressed.

## Master and Home Assistant changes

The master reports the slave's `compat`/`fw_version` and the panel displays
them. If either side changed:

- master: rebuild and flash the ESP32 (`pio run -t upload` in `SplFlp_Master`);
- HA integration: reload the integration (or restart HA). The panel is served
  fresh through the versioned ES-module imports.

A slave-only change needs neither: it is exactly the hex -> panel -> done path.

## Quick reference

```bash
# Production OTA image
cd ~/GIT_Personal/PlatformIO/SplFlp_Slave_Attiny
pio run -t clean -e ATtiny1616_OTA && pio run -e ATtiny1616_OTA

# Validate without touching the bus
python3 bootloader/tools/flash_mqtt.py --module 3 --dry-run

# Read the persisted version over UPDI (also while the module is silent)
python3 bootloader/tools/dump_eeprom.py

# Read it over RS485 from the master console
m3v
m3s
```
