# Slave RS485 protocol

This document describes the module firmware behaviour after the calibration
rework. It is the contract the ESP32 master and the Home Assistant integration
should rely on.

All commands are ASCII lines terminated by `\n` (a 50 ms silence also ends a
line). `*` in the id field addresses every module.

## Coordinate system

```
                 hall entering edge = pos 0
                          |
   ... -------------------+---- flap 0 ---- flap 1 ---- ... ---- flap 63 --+
                          |<-------------- totalStepsPerRev -------------->|
                          |<-- homeOffset -->|
```

- `pos` (`currentStepPos`) is the number of half-steps counted from the hall
  entering edge, modulo `totalStepsPerRev`. The origin is the **hall edge**,
  not flap 0.
- On every hall entering edge the firmware sets `pos = 0`. This is the only
  drift correction; `totalStepsPerRev` only has to be close enough for the
  wrap and the fallback positions.
- `homeOffset` is the `pos` at which flap 0 is centred. It is one number, not
  a coordinate system. Changing it never moves the position map.- `map[i]` is the `pos` at which flap i is centred, or `0xFFFF` when unset.
  The fallback for an unset entry is
  `homeOffset + round(i * totalStepsPerRev / 64)`, modulo
  `totalStepsPerRev`.

Legacy EEPROM images (map stored relative to flap 0) are migrated once on the
first boot of this firmware: every entry has `homeOffset` added and the
coordinate format is tagged at EEPROM address 204.

## Motion

- Motion is **forward only**. A nudge left takes the long way around. This
  keeps backlash identical for every measurement.
- Moves are **blocking**: the slave runs the whole step loop before it parses
  the next command. The RS485 parser is therefore stalled while a module turns,
  so the master must not query a module immediately after a movement command.
  The master defers that refresh by `MOVE_SETTLE_MS` (5 s) instead.
- Position is **re-referenced on every hall crossing**. `pos` is incremented
  once per half-step and wraps at `totalStepsPerRev`; when a move's forward
  path crosses the hall magnet (or passes within `HALL_WATCH_TOLERANCE` steps
  of it, half a flap), the firmware watches for the entering edge. On that
  edge `pos` is set to `0` exactly like a home and the move's remaining
  distance is recomputed from the true position, so the move still lands
  exactly on target even when steps were missed earlier. Drift therefore
  cannot survive a magnet crossing. Moves that never pass the magnet stay
  open loop. Re-home after a mechanical disturbance or a wrong revolution
  count.
- A move that starts while the reel already sits on the magnet still works:
  the watch is armed with the current hall state, so only the *entering*
  edge (inactive -> active) re-references the position. The leaving edge
  never does.
- If the position is unknown (boot without auto-home, a failed home, or a
  calibration), the next move homes first and then continues to the requested
  target. A failed home leaves the position unknown and the move is skipped.
- `busy` is always `0` by the time `s` is answered because moves are blocking.
  It is kept in the response for protocol compatibility.

## Commands

### Movement

| Command | Meaning |
| --- | --- |
| `m<id>-<char>` | Move to the flap showing `<char>`. |
| `m<id>+<index>` | Move to flap index `0..63`. |
| `m<id>g<pos>` | Move to an absolute `pos` (`0..totalStepsPerRev-1`, wrapped). |
| `m<id>h` | Home: seek the entering edge, then move to `homeOffset` (flap 0). |
| `m<id>c` | Measure one revolution between two entering edges, store `totalStepsPerRev`, then go to flap 0. **Blocking for several seconds**; manual diagnostic only. |
| `m<id>r<index>` | Record the current `pos` as `map[index]`. Index 0 also sets `homeOffset`. Refused while the position is unknown. |

The movement commands return immediately; use `s` to observe progress.

### Configuration and query

| Command | Meaning |
| --- | --- |
| `m<id>d` | Config and position map (unchanged format, see below). |
| `m<id>s` | State: `m<id>s:<pos>:<busy>:<position_known >` |
| `m<id>l` | Raw 64-character drum, one character per flap, no prefix (a leading space is significant). |
| `m<id>o<pos>` | Store `homeOffset`. |
| `m<id>t<steps>` | Store `totalStepsPerRev`. |
| `m<id>w<index>:<pos>` | Store one map entry. |
| `m<id>e<index>` / `m<id>e*` | Delete one map entry / clear the whole map. |
| `m<id>f<index>:<char>` | Store one drum character. |
| `m<id>i<n>` | Store the module id. |
| `m<id>a<0\|1>` | Auto-home at boot. |
| `m<id>b` | Arm the OTA boot flag (EEPROM 255) and reset into the bootloader. Replies `m<id>b:1` first, resets ~100 ms later. Unicast only — a broadcast arms nothing and gets no reply. See [OTA bootloader](#ota-bootloader). |

`d` keeps its existing wire format so an older master can still parse it:

```
m<id>:<homeOffset>:<totalSteps>:<autoHome>:<index>=<pos>,<index>=<pos>,...
```

The position map is empty when no entry is set; the colon before it is always
present. New state (`pos`, `busy`) is exposed through `s`, not by extending
`d`.

`r` response: `m<id>r:<index>:<pos>` on success, `m<id>r:<index>:-1` when the
record was refused.

`busy` is always `0` because moves are blocking. `position_known` is 0 after a
failed home, a boot without auto-home, or before the first move.

## Typical calibration sequence

1. `m<id>h` — establish the hall reference and land on flap 0.
2. `m<id>s` — wait until `busy` is 0 and `position_known` is 1.
3. Nudge with `m<id>+<index>` / `m<id>g<pos>` until the flap is centred.
4. `m<id>r<index>` — store the current position. Repeat as needed.
5. `m<id>d` — read back the map. Fit the map in Home Assistant only if the
   residuals are larger than about a quarter flap; otherwise the fallback is
   good enough.

`r` is intentionally dumb: it stores exactly the position the firmware
reports. All fitting, outlier rejection and `N_fit` estimation belongs to
Home Assistant, where the samples can be collected without blocking the bus.

## OTA bootloader

The bootloader lives in a 2 KB boot section at flash `0x0000` (BOOTEND fuse
= 8) and writes only the application region `0x0800–0x3FFF` (14336 bytes,
224 pages of 64 bytes). It never touches fuses or the EEPROM calibration
data, so an RS485 update can never remove the bootloader itself; UPDI
programming stays available as the last resort at all times. The
application must be built with the `ATtiny1616_OTA` environment (linked at
`0x0800`); a plain build would overlap the bootloader and is rejected by
`bootloader/tools/install.py`.

### Boot policy

- EEPROM 255 holds the boot flag. `0xB0` = stay in update mode.
- Flag set (by `m<id>b`, or by START_UPDATE before the first page write):
  stay in the bootloader indefinitely. A power cut mid-session therefore
  re-enters the bootloader and the master can simply re-flash.
- Flag clear and a valid application present: listen for 500 ms (any frame
  addressed to this module extends the window), then jump to the
  application at `0x0800`.
- Flag clear and the application looks erased (first word `0xFFFF`): stay in
  update mode, ready for a first RS485 flash.
- The flag is cleared only by a verified FINISH (flash CRC-32 matches) or by
  CMD_RESET. A cancelled or interrupted session leaves the module in the
  bootloader, ready for a re-flash.

### Framing

All OTA traffic uses 9600 baud 8N1 on the same bus pins as the application
(PA1 TX, PA3 RX, PA2 DE). A frame is:

```
[0xAA][addr][cmd][len][payload ... ][crc_hi][crc_lo]
```

Everything after the `0xAA` sync marker is **escape encoded**: `0x5C` is
sent as `5C 00`, and `0x6D` (`m`) is sent as `5C 01`. A raw `m` therefore
never appears inside OTA traffic, so the application's line parser (which
only starts a command line at `m`) can never be triggered on bystander
modules while firmware bytes stream past them.

- `addr` must be the module id; broadcast is ignored.
- `cmd`, `len` and all payload fields are big-endian.
- CRC-16/CCITT-FALSE over `addr`, `cmd`, `len` and the payload
  (polynomial 0x1021, init 0xFFFF, no reflection, no final XOR),
  transmitted big-endian.
- Image verification uses CRC-32 IEEE (the value Python's
  `binascii.crc32` / `zlib.crc32` produce over the raw application image,
  `0x0800`-based, gaps `0xFF`).

| Command | Value | Payload | Meaning |
| --- | --- | --- | --- |
| PING | `0x01` | 0 | Reply: ACK with `[version, module_id, staying]`. |
| START_UPDATE | `0x02` | 6 | `size:u16`, `crc32:u32`. Arms the boot flag. |
| WRITE_PAGE | `0x03` | 66 | `page:u16`, 64 data bytes. Accepted only in a session. |
| FINISH | `0x04` | 4 | `crc32:u32` (must match START_UPDATE). Verify, clear flag, reset. |
| RESET | `0x05` | 0 | Clear flag and reset (app must be intact). |

Responses use `0x81` (ACK) and `0x82` (NACK, `payload[0]` = error code:
1 CRC, 2 address, 3 sequence, 4 verify, 5 length) with the same framing.
The bootloader stays silent on CRC errors and noise; the master retries on
timeout (3 attempts per page).

### Master update sequence

1. Pause the display loop (the whole bus is used by the session).
2. `m<id>b` — wait for `m<id>b:1`, then ~100 ms + reset time. If the module
   is already in the bootloader (previous cancelled session or power cut
   mid-flash), this times out and the next step still finds it.
3. PING until it answers with `staying = 1` (retry, total ≤ 5 s).
4. START_UPDATE with the image size and CRC-32.
5. WRITE_PAGE for each 64-byte page (`0x0800`-based), retrying on timeout.
   A full flash takes roughly 20–25 s.
6. FINISH — on success the module verifies its flash, clears the flag and
   resets itself; no extra RESET is needed.
7. Wait for the module to boot, re-scan it, resume the display loop.

To abort: stop the session and leave the module in the bootloader unless
no page was written yet, in which case RESET returns it to its intact
application.

### Initial install (UPDI)

`bootloader/tools/install.py` performs the one-time install of bootloader +
OTA application + fuses in a single UPDI session. It merges both images
into one Intel HEX (prog.py chip-erases on every `-a write`) and writes the
fuses *before* the erase: OSCCFG = 0x02 (20 MHz), SYSCFG0 = 0xC5 (factory
0xC4 plus EESAVE, so EEPROM survives chip erase; UPDI pin config
preserved), BOOTEND = 0x08. Module id and calibration EEPROM are retained.
Later application updates never use UPDI again; they go over RS485 as
above.

`bootloader/tools/flash_mqtt.py` drives those later updates from the bench:
it validates the `ATtiny1616_OTA` build's `firmware.hex` (reusing
install.py's parser, so a 0x0000-linked plain build is refused), then
publishes `begin`/`chunk`/`flash` on the master's
`<prefix>/command/firmware` topic — chunks of 768 image bytes
(padding-free base64 per chunk), plus `--cancel` and `--reset` for the
recovery paths above and `--watch` to follow the `ota` status object.
