# Split-Flap OTA bootloader

A small, proven RS485 bootloader for the ATtiny1616 split-flap slave. It is
kept here as a reusable reference; the full wire protocol, boot policy and
recovery diagnostics live in [`../PROTOCOL.md`](../PROTOCOL.md)
("OTA bootloader").

**Status: validated on hardware.** The happy path was exercised end to end:
the master asked the application to enter the bootloader, streamed a 154-page
image, the module verified the CRC-32, reset itself, and booted the new
application. Cancelled sessions and power cuts are recoverable over RS485 by
design; the recovery paths are documented in PROTOCOL.md.

## What is proven

These are the decisions that made it work. Do not "simplify" them without
re-testing the failure modes that produced them.

| Area | Working choice | Why it matters |
| --- | --- | --- |
| Boot section | `BOOTEND=8` -> 2 KB at `0x0000` | Fits the bootloader (~1.7 KB) and leaves 14336 bytes for the app. |
| App start | `0x0800` | The app must be linked with `ATtiny1616_OTA` (`text_section_start=0x800`). |
| Hand-over | `jmp APP_START_BYTE` (`0x0800`) | GNU as `jmp` operands are **byte** addresses; `APP_START_BYTE/2` jumps to `0x0400` inside the bootloader. Disassembly must be `0C 94 00 04`. |
| Reset flags | Never clear `RSTCTRL.RSTFR` before the jump | megaTinyCore's `.init3` resets the chip when it sees no flags, producing a ~500 ms boot loop before `setup()`. |
| Software reset | `_PROTECTED_WRITE(RSTCTRL_SWRR, RSTCTRL_SWRE_bm)` | A plain store to `SWRR` is silently ignored; the bootloader would ACK and then spin forever. |
| Bus pins | PA5 TX, PA6 DE, PA7 RX (Arduino 1/2/3) | The bootloader must use the application's real pins, not PA1..PA3. |
| Bus turnaround | 10 ms before replying | The master's RS485 transceiver needs the release time; replying sooner loses the first bytes. |
| Boot flag | EEPROM 255, `0xB0` = stay in update mode | Set before the first page write, so a power cut mid-flash is recoverable. Cleared only by a verified FINISH or RESET. |
| EEPROM | `EESAVE=1` | A UPDI chip erase must not destroy calibration (and the boot flag must survive it). |
| Flashing | Bootloader writes only `0x0800..0x3FFF` | A failed RS485 update can never remove the bootloader or fuses. UPDI stays the last resort. |
| Framing | Escape-encode every byte after the sync | A raw `m` must never appear in binary traffic, or bystander modules would start parsing a command. |
| Master echo | Drain the half-duplex echo after sending | The echo starts with the same `0xAA` as a real reply; without draining, the master can parse its own frame and miss the slave. |

## Memory and fuses

```
0x0000  +---------------------------+
        | bootloader (2 KB)         |  BOOTEND = 8
0x0800  +---------------------------+
        | application (14336 B)     |  224 pages x 64 B
0x3FFF  +---------------------------+

EEPROM  0..204    application calibration / drum
        205..212  application version (8 bytes, optional)
        244..254  OTA_DEBUG_MARKERS (marker builds only)
        255       OTA boot flag (0xB0 = stay in update mode)
```

Fuses written by `tools/install.py`:

| Fuse | Value | Meaning |
| --- | --- | --- |
| OSCCFG | `0x02` | 20 MHz internal oscillator |
| SYSCFG0 | `0xC5` | factory `0xC4` plus `EESAVE` (UPDI pin config preserved) |
| BOOTEND | `0x08` | 2 KB boot section |

## Protocol in one screen

```text
[0xAA][addr][cmd][len][payload ...][crc16 hi][crc16 lo]
```

Everything after `0xAA` is escape encoded: `0x5C` -> `5C 00`, `m` (`0x6D`) ->
`5C 01`. Fields are big-endian. CRC-16/CCITT-FALSE (poly `0x1021`, init
`0xFFFF`, no reflection, no final XOR) covers `addr`+`cmd`+`len`+payload. The
image is verified with CRC-32 IEEE.

| Command | Value | Payload | Meaning |
| --- | --- | --- | --- |
| PING | `0x01` | 0 | ACK `[ota_protocol_version, module_id, staying]` |
| START_UPDATE | `0x02` | `size:u16`, `crc32:u32` | Arm the boot flag and begin a session |
| WRITE_PAGE | `0x03` | `page:u16`, 64 bytes | Write one 64-byte page |
| FINISH | `0x04` | `crc32:u32` | Verify image, clear flag, reset |
| RESET | `0x05` | 0 | Clear flag and reset (recovery) |

`staying=1` means full update mode (flag armed or app erased); `staying=0`
means the bootloader is in its 500 ms boot window with a valid app. The master
accepts either, then arms the flag with START_UPDATE.

## Integration checklist for another project

1. Keep the bootloader and application **separate build environments**. The
   app must be linked above the boot section; a normal `0x0000` build will
   overlap it.
2. Reserve the boot section in the linker (`text_section_start` / `BOOTEND`).
3. Use the application's real bus pins and DE polarity.
4. Put the boot flag in EEPROM above every application field, and make
   "armed" mean "stay in update mode indefinitely".
5. Clear the flag **only** after a verified image and immediately before the
   reboot. Never clear it on entry or on a page write.
6. Keep the hand-over jump as a plain `jmp` to the byte address of the app.
   Do not clear `RSTCTRL.RSTFR`.
7. Implement software reset with the CCP signature.
8. Escape-encode binary traffic so it can never look like an application
   command.
9. Retry every page in the master and make START_UPDATE idempotent for the
   session (the bootloader accepts a repeated START_UPDATE only as defined by
   its session state).
10. Provide a UPDI recovery tool that can read fuses, the app header and the
    boot flag, and clear the flag without erasing calibration.
11. Test three failure modes on the bench before trusting it:
    cancel before the first page, cancel mid-flash, and power cut mid-flash.

## Build and install

```bash
cd bootloader
pio run                 # must stay within 2048 bytes

cd ..
python3 bootloader/tools/install.py --yes
```

`install.py` writes fuses first, then bootloader + current OTA application in
one UPDI session, clears the boot flag, resets the module and verifies the
flag byte. `--keep-boot-flag` skips the clear.

**Never** upload the `ATtiny1616_OTA` application directly over UPDI: a UPDI
upload chip-erases the boot section. The only UPDI path for this stack is
`install.py`.

## Debugging

Build with `-DOTA_DEBUG_MARKERS` only when diagnosing; the markers are
breadcrumbs in EEPROM 244..254 and are documented in `src/bootloader.c` and
[`../PROTOCOL.md`](../PROTOCOL.md). A production image must be rebuilt clean
without the flag:

```bash
cd ..
pio run -t clean -e ATtiny1616_OTA
pio run -e ATtiny1616_OTA
```

Read the on-chip state without writing:

```bash
python3 bootloader/tools/dump_eeprom.py
```

The dump prints fuses, the app header at `0x0800`, the persisted application
version and the boot flag, then states a verdict. It is the first thing to run
when a module is silent.

## Files

| File | Purpose |
| --- | --- |
| `src/bootloader.c` | Boot policy, frame handling, flash writes, hand-over |
| `src/protocol.h` | Shared protocol constants and EEPROM addresses |
| `src/codec.h` | CRC-16/CRC-32 and escape codec |
| `tools/install.py` | One-time UPDI install of bootloader + app + fuses |
| `tools/flash_mqtt.py` | Bench OTA driver via the master's MQTT firmware topic |
| `tools/dump_eeprom.py` | Read-only UPDI diagnostics and boot-flag recovery |
