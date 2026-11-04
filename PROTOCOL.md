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
  a coordinate system. Changing it never moves the position map.
- `map[i]` is the `pos` at which flap i is centred, or `0xFFFF` when unset.
  The fallback for an unset entry is
  `homeOffset + round(i * totalStepsPerRev / 64)`, modulo
  `totalStepsPerRev`.

Legacy EEPROM images (map stored relative to flap 0) are migrated once on the
first boot of this firmware: every entry has `homeOffset` added and the
coordinate format is tagged at EEPROM address 204.

## Motion

- Motion is **forward only**. A nudge left takes the long way around. This
  keeps backlash identical for every measurement.
- Moves are **non-blocking**: `update()` advances at most one half-step per
  millisecond, so the RS485 parser keeps running while modules move. A burst
  of commands to many modules no longer overflows the input buffer.
- A move is shorter than one revolution and may cross at most one hall edge.
  On that edge the remaining distance is recomputed from `pos = 0`.
- A move that would need a second edge (target outside the real revolution,
  usually a wrong `totalStepsPerRev`) stops, marks the position unknown and
  reports the failure through the state query. It never silently claims
  success.
- If the position is unknown (boot without auto-home, or a failed move), the
  next move homes first and then continues to the requested target.

## Commands

### Movement

| Command | Meaning |
| --- | --- |
| `m<id>-<char>` | Move to the flap showing `<char>`. |
| `m<id>+<index>` | Move to flap index `0..63`. |
| `m<id>g<pos>` | Move to an absolute `pos` (`0..totalStepsPerRev-1`, wrapped). |
| `m<id>h` | Home: seek the entering edge, then move to `homeOffset` (flap 0). |
| `m<id>c` | Measure one revolution between two entering edges, store `totalStepsPerRev`, then go to flap 0. **Blocking for several seconds**; manual diagnostic only. |
| `m<id>r<index>` | Record the current `pos` as `map[index]`. Index 0 also sets `homeOffset`. Refused while busy or while the position is unknown. |

The movement commands return immediately; use `s` to observe progress.

### Configuration and query

| Command | Meaning |
| --- | --- |
| `m<id>d` | Config and position map (unchanged format, see below). |
| `m<id>s` | State: `m<id>s:<pos>:<busy>:<position_known>` |
| `m<id>l` | Raw 64-character drum, one character per flap, no prefix (a leading space is significant). |
| `m<id>o<pos>` | Store `homeOffset`. |
| `m<id>t<steps>` | Store `totalStepsPerRev`. |
| `m<id>w<index>:<pos>` | Store one map entry. |
| `m<id>e<index>` / `m<id>e*` | Delete one map entry / clear the whole map. |
| `m<id>f<index>:<char>` | Store one drum character. |
| `m<id>i<n>` | Store the module id. |
| `m<id>a<0\|1>` | Auto-home at boot. |

`d` keeps its existing wire format so an older master can still parse it:

```
m<id>:<homeOffset>:<totalSteps>:<autoHome>:<index>=<pos>,<index>=<pos>,...
```

The position map is empty when no entry is set; the colon before it is always
present. New state (`pos`, `busy`) is exposed through `s`, not by extending
`d`.

`r` response: `m<id>r:<index>:<pos>` on success, `m<id>r:<index>:-1` when the
record was refused.

`busy` is 1 while a move or home is in progress. `position_known` is 0 after
a failed move or a boot without auto-home.

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
