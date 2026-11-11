# Versioning and compatibility

Split-Flap is three repositories with two independent contracts:

| Repository | Role | Version constants |
| --- | --- | --- |
| `SplFlp_Slave_Attiny` | ATtiny1616 application + OTA bootloader | `include/FirmwareVersion.h`, `bootloader/src/protocol.h` |
| `SplFlp_Master` | ESP32-C3 RS485/MQTT bridge + OTA flasher | `include/Version.h` |
| `SplitflapRebuild` | Home Assistant integration + panel | `custom_components/splitflap_os/const.py`, `manifest.json`, `frontend/constants.js` |

This document is the single source of truth for how those versions relate.
The wire protocol itself is specified in [`../PROTOCOL.md`](../PROTOCOL.md).

## Two compatibility axes

The system has two compatibility domains because two different peers talk to
each other:

| Axis | Peer contract | Carried by |
| --- | --- | --- |
| **BUS_COMPAT** | RS485 application protocol and the OTA/bootloader entry contract | Slave + master |
| **MQTT_COMPAT** | MQTT status/command JSON schema | Master + Home Assistant |

The master is the bridge, so it carries **both** numbers. The slave carries
only the bus generation; Home Assistant carries only the MQTT generation.

Each repository also has a **RELEASE** inside its generations. The published
version is composed from the compat numbers that apply to that repository:

```
slave    <bus>.<release>             1.0.1      bus 1, release 0.1
master   <bus>.<mqtt>.<release>      1.1.0.0    bus 1, mqtt 1, release 0.0
HA       <mqtt>.<release>            1.0.1      mqtt 1, release 0.1
```

The leading numbers are compatibility generations; the trailing part is the
release (minor.patch). A consumer can decide compatibility from the leading
numbers alone. Explicit fields are also published, so nobody has to parse the
string to check.

`BUS_COMPAT` and `MQTT_COMPAT` are deliberately small, slow-moving numbers.
They are not the app version and not a date. Bump one only when an old peer
would misbehave, not merely because behavior changed.

## Current matrix

| Component | Version | BUS_COMPAT | MQTT_COMPAT | Where it is reported |
| --- | --- | --- | --- | --- |
| Slave application | `1.0.1` | `1` | — | `m<id>v:<bus>:<version>`, `m<id>s`, UPDI dump |
| Master firmware | `1.1.0.0` | `1` | `1` | MQTT status `version`, `bus_compat`, `mqtt_compat` |
| Home Assistant integration | `1.0.1` | — | `1` | panel header / connection info, WebSocket `version`, `mqtt_compat` |
| OTA bootloader protocol | `1` | separate | — | PING reply byte 0 |

### Why the master version has four components

`1.1.0.0` is not a mistake:

```
1 . 1 . 0 . 0
│   │   └─┬─┘
│   │     └── master release 0.0
│   └──────── MQTT_COMPAT 1 (master <-> Home Assistant)
└──────────── BUS_COMPAT 1 (slave <-> master)
```

The first two numbers are compatibility generations; the rest is the master's
own release. The status payload repeats them explicitly as `bus_compat` and
`mqtt_compat`.

### The OTA protocol is a separate axis

`BOOTLOADER_VERSION` (slave) / `OTA_PROTOCOL_VERSION` (master) is the **binary
OTA wire generation**, not the application version. It changes only when the
frame layout, escaping, CRC, command numbers or boot policy change. The master
refuses a session before writing any page when the module reports anything
else, so an incompatible bootloader can never be written to.

A `BUS_COMPAT` bump does **not** automatically change the OTA protocol. This
keeps an application-protocol change from forcing a bootloader reflash.

## What is a breaking change?

### BUS_COMPAT (slave + master)

Bump this in the slave and master together when:

- an RS485 command is removed, renamed, or changes the meaning/position of a
  reply field;
- the module-id rules change;
- the OTA frame layout, escaping, CRC algorithm, command numbers, boot policy,
  flash layout or boot-flag semantics change.

### MQTT_COMPAT (master + Home Assistant)

Bump this in the master and HA together when:

- a command topic/action is removed or renamed;
- a required status field is removed or renamed;
- the OTA message schema changes incompatibly;
- a WebSocket/snapshot field the panel requires changes shape.

### Release only

These are additive and bump `RELEASE` only:

- a new command or a new optional status field;
- appending fields to an existing reply (older peers ignore the tail);
- bug fixes, performance work, UI changes;
- changing default settings or calibration values.

When in doubt, ask: "can the previous release still work with the new peer?"
If yes, it is a release. If no, bump the compatibility number for that domain.

Adding the explicit `bus_compat`/`mqtt_compat` fields while keeping the old
`compat` aliases is additive, so it did not require an MQTT bump.

## Runtime checks

Compatibility is visible and checkable at every layer; it is never inferred
from a build date.

### Slave

```text
m3v   -> m3v:1:1.0.1        bus compat 1, application 1.0.1
m3s   -> m3s:<pos>:0:1:1:1.0.1
```

An old application returns an empty version. The master keeps the module
online and reports the version as unknown instead of inventing one.

The UPDI dump reads the persisted copy independently of the bus:

```bash
python3 bootloader/tools/dump_eeprom.py
# Version : 1.0.1   bus compat=1 (tree expects 1)
```

### Master

The master publishes both compat numbers and each module's bus values in the
retained status payload:

```json
{
  "state": "online",
  "version": "1.1.0.0",
  "bus_compat": 1,
  "mqtt_compat": 1,
  "modules": [
    {
      "id": 3,
      "fw_version": "1.0.1",
      "fw_bus_compat": 1,
      "fw_bus_compatible": true
    }
  ]
}
```

For older Home Assistant builds the master also keeps `compat` as an alias for
`mqtt_compat`, and `fw_compat`/`fw_compatible` as aliases for the bus values.
`fw_bus_compatible` is omitted when the module does not report a version. The
master does not hide a mismatch: the module stays usable, but HA can warn.

During OTA the `ota` object carries the bootloader protocol generation the
module answered with. A mismatch fails the session before `START_UPDATE`, so
no page is written.

### Home Assistant

The integration compares the master's `mqtt_compat` with its own `MQTT_COMPAT`.
The panel:

- shows `v<version>` in the header and the master version in the connection
  info;
- shows a warning banner when the master reports a different MQTT generation;
- shows "master version unknown" for firmware that predates this protocol,
  rather than treating it as compatible;
- shows each module's `fw_version` and `fw_bus_compat` in the firmware card.

The check is advisory for normal display traffic: a mismatch must never block
recovery, OTA cancellation, or a module reset. It is authoritative only where
writing is irreversible (the OTA session refuses an unknown bootloader
protocol before writing).

## Bumping a release

For a non-breaking change, touch only the repository that changed:

1. **Slave**: raise `FW_RELEASE` in `include/FirmwareVersion.h` (for example
   `"0.1"` -> `"0.2"`). Rebuild and upload. The EEPROM slot updates on the
   first boot after the OTA.
2. **Master**: raise `MASTER_RELEASE` in `include/Version.h`, rebuild and
   flash the ESP32.
3. **Home Assistant**: raise `VERSION` in `const.py` and `manifest.json` and
   every `?v=` import in `frontend/`. `tests/test_versions.py` enforces the
   lockstep and the major/MQTT_COMPAT relationship.

The compat numbers stay unchanged, so old and new peers remain compatible.

## Bumping a compatibility generation

Bump only the domain that broke:

| Change touches | Raise |
| --- | --- |
| RS485 or OTA contract | `FW_BUS_COMPAT`, `MASTER_BUS_COMPAT` |
| MQTT JSON schema | `MASTER_MQTT_COMPAT`, `MQTT_COMPAT` |
| Both | all four |

Then:

1. Reset the affected `RELEASE` values as appropriate (usually `0.0`).
2. Bump the OTA protocol generation only if the bootloader framing or boot
   policy changed, not merely because the application protocol changed.
3. Update this document's matrix and every version reference in the docs.
4. Build and test all three repositories. Reinstall bootloader + app through
   `bootloader/tools/install.py` if the bootloader changed; otherwise a normal
   OTA is enough for the slaves.
5. Tag the coordinated point in all three repositories, for example
   `bus-compat-2` or `mqtt-compat-2`, plus the release tag.

Do not publish a release whose leading numbers disagree with the repository's
`*_COMPAT` constants. The checks in the code and `tests/test_versions.py` exist
to make that visible immediately.

## Git tags

Versions live in source so they are readable from a checkout and from a
running device. Tags are the historical index:

```bash
# one per repository, same format
git tag -a v1.0.1 -m "slave 1.0.1, bus compat 1"

# when a shared generation changes, tag the coordinated point
git tag -a bus-compat-2 -m "slave <-> master compatibility generation 2"
git tag -a mqtt-compat-2 -m "master <-> HA compatibility generation 2"
```

A tag is not a substitute for the runtime fields: when diagnosing a module,
always read `m<id>v` or the UPDI dump, because a module may run an image that
was never tagged.

## Quick checks

```bash
# Slave: what does the running app report?
# (from the master console)
m3v

# Slave: persisted copy, works while the module is silent
cd SplFlp_Slave_Attiny
python3 bootloader/tools/dump_eeprom.py

# Master/HA: what did the retained status carry?
mosquitto_sub -t 'splitflap/status' -C 1 \
  | jq '{version, bus_compat, mqtt_compat, modules}'

# HA: source of truth and frontend lockstep
cd SplitflapRebuild
python3 tests/test_versions.py
```
