# Versioning and compatibility

Split-Flap is three repositories that must interoperate:

| Repository | Role | Version constants |
| --- | --- | --- |
| `SplFlp_Slave_Attiny` | ATtiny1616 application + OTA bootloader | `include/FirmwareVersion.h`, `bootloader/src/protocol.h` |
| `SplFlp_Master` | ESP32-C3 RS485/MQTT bridge + OTA flasher | `include/Version.h` |
| `SplitflapRebuild` | Home Assistant integration + panel | `custom_components/splitflap_os/const.py`, `manifest.json`, `frontend/constants.js` |

This document is the single source of truth for how those versions relate.
The wire protocol itself is specified in [`../PROTOCOL.md`](../PROTOCOL.md).

## Two axes

Every repository carries two independent numbers:

| Axis | Meaning | Changes when |
| --- | --- | --- |
| **COMPAT** | The shared contract generation. One integer, identical in all three repositories. | A contract changes in a way an older peer cannot handle. |
| **RELEASE** | The repository's own release inside that generation. | Any release of that repository, including non-breaking fixes. |

The human-facing version is composed from both:

```
<compat>.<release>          examples: 1.0.0   1.4.2   2.0.0
   |        |
   |        +-- per-repo release (minor.patch)
   +----------- compatibility generation (major)
```

So `1.4.2` means "compatibility generation 1, release 4.2". A consumer can
decide compatibility from the **first number alone**; the rest is informative.
There is no separate hidden compatibility flag.

`COMPAT` is deliberately a small, slow-moving number. It is not the app
version and it is not a date. Bump it only when an old peer would misbehave,
not merely because behavior changed.

## Current matrix

| Component | Version | COMPAT | Where it is reported |
| --- | --- | --- | --- |
| Slave application | `1.0.0` | `1` | `m<id>v:<compat>:<version>`, `m<id>s`, UPDI dump |
| Master firmware | `1.0.0` | `1` | MQTT status `version`, `compat` |
| Home Assistant integration | `1.0.0` | `1` | panel header / connection info, WebSocket `version`, `compat` |
| OTA bootloader protocol | `1` | `1` (separate) | PING reply byte 0 |

The OTA bootloader has its own **OTA protocol generation**
(`BOOTLOADER_VERSION` / `OTA_PROTOCOL_VERSION`). It is a wire framing version,
not the application version. The master refuses to start a session when the
bootloader reports anything else, so an incompatible bootloader can never be
written to.

## What is a breaking change?

Bump `COMPAT` in all three repositories in the same coordinated change when
any of these changes:

- **Slave ASCII contract**: removing or renaming a command, changing a reply
  field's meaning or position, changing the module-id rules.
- **OTA contract**: frame layout, escaping, CRC algorithm, command numbers,
  boot policy, flash layout, boot flag semantics.
- **MQTT contract**: removing a command topic/action, renaming a required
  status field, changing the OTA message schema.
- **Home Assistant contract**: a WebSocket field or snapshot shape that the
  panel requires (the panel and integration ship together, but a stale panel
  must at least be detectable).

These are **not** breaking and only bump `RELEASE`:

- adding a new command or a new optional status field;
- appending fields to an existing reply (older peers ignore the tail);
- bug fixes, performance work, UI changes;
- changing default settings or calibration values.

When in doubt, ask: "can the previous release still work with the new peer?"
If yes, it is a release. If no, it is a compatibility bump.

## Runtime checks

Compatibility is visible and checkable at every layer; it is never inferred
from a build date.

### Slave

```text
m3v   -> m3v:1:1.0.0        compat 1, application 1.0.0
m3s   -> m3s:<pos>:0:1:1:1.0.0
```

An old application returns an empty version. The master keeps the module
online and reports the version as unknown instead of inventing one.

The UPDI dump reads the persisted copy independently of the bus:

```bash
python3 bootloader/tools/dump_eeprom.py
# Version : 1.0.0   compat=1 (tree expects 1)
```

### Master

The master publishes its own generation and each module's reported values in
the retained status payload:

```json
{
  "state": "online",
  "version": "1.0.0",
  "compat": 1,
  "modules": [
    {
      "id": 3,
      "fw_version": "1.0.0",
      "fw_compat": 1,
      "fw_compatible": true
    }
  ]
}
```

`fw_compatible` is omitted when the module does not report a version. The
master does not hide a mismatch: the module stays usable, but HA can warn.

During OTA the `ota` object carries the bootloader protocol generation the
module answered with. A mismatch fails the session before `START_UPDATE`, so
no page is written.

### Home Assistant

The integration compares the master's `compat` with its own `COMPAT_VERSION`.
The panel:

- shows `v<version>` in the header and the master version in the connection
  info;
- shows a warning banner when the master reports a different generation;
- shows "master version unknown" for firmware that predates this protocol,
  rather than treating it as compatible.

The check is advisory for normal display traffic: a mismatch must never block
recovery, OTA cancellation, or a module reset. It is authoritative only where
writing is irreversible (the OTA session refuses an unknown bootloader
protocol before writing).

## Bumping a release

For a non-breaking change, touch only the repository that changed:

1. **Slave**: raise `FW_RELEASE` in `include/FirmwareVersion.h` (for example
   `"0.0"` -> `"0.1"`). Rebuild and upload. The EEPROM slot updates on the
   first boot after the OTA.
2. **Master**: raise `MASTER_RELEASE` in `include/Version.h`, rebuild and
   flash the ESP32.
3. **Home Assistant**: raise `VERSION` in `const.py` and `manifest.json` and
   every `?v=` import in `frontend/`. `tests/test_versions.py` enforces the
   lockstep and the major/compat relationship.

`COMPAT` stays unchanged, so old and new peers remain compatible.

## Bumping compatibility

A compatibility bump is a coordinated release across all three repositories:

1. Raise `FW_COMPAT`, `MASTER_COMPAT` and `COMPAT_VERSION` to the same new
   integer.
2. Reset each `RELEASE` as appropriate (usually to `0.0`); the composed
   versions become `<new>.0.0`.
3. Bump the OTA protocol generation only if the bootloader framing or boot
   policy changed, not merely because the application protocol changed.
4. Update this document's matrix and every version reference in the docs.
5. Build and test all three repositories. Reinstall bootloader + app through
   `bootloader/tools/install.py` if the bootloader changed; otherwise a normal
   OTA is enough for the slaves.
6. Tag the coordinated point in all three repositories, for example
   `compat-2` and `v2.0.0`.

Do not publish a release whose major number disagrees with its repository's
`COMPAT` constant. The checks in the code and `tests/test_versions.py` exist to
make that visible immediately.

## Git tags

Versions live in source so they are readable from a checkout and from a
running device. Tags are the historical index:

```bash
# one per repository, same format
git tag -a v1.0.0 -m "slave 1.0.0, compat 1"

# when the shared generation changes, tag the coordinated point
git tag -a compat-2 -m "cross-repo compatibility generation 2"
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
mosquitto_sub -t 'splitflap/status' -C 1 | jq '{version, compat, modules}'

# HA: source of truth and frontend lockstep
cd SplitflapRebuild
python3 tests/test_versions.py
```
