#pragma once

// Version model
// ------------
// The system has two independent version axes (docs/VERSIONING.md):
//
//   COMPAT   one integer shared by the slave app, the ESP32 master and the
//            Home Assistant integration. It changes only when a cross-repo
//            contract changes in an incompatible way (RS485 lines, OTA
//            framing, MQTT status/command schema). All three must be equal;
//            a mismatch is reported, never silently ignored.
//   RELEASE  the per-repo release inside that generation. Slave, master and
//            HA move independently.
//
// The wire version is "<COMPAT>.<RELEASE>" so the compatibility generation
// is readable from the number itself: 1.4.2 means compatibility generation 1,
// release 4.2. The compile-time construction below makes it impossible for
// the two to drift.

#define SF_STRINGIFY_(value) #value
#define SF_STRINGIFY(value) SF_STRINGIFY_(value)

// Bump this only for a breaking cross-repo change. Keep it in lockstep with
// MASTER_COMPAT (master include/Version.h) and COMPAT_VERSION (HA const.py).
#define FW_COMPAT 1

// Bump this for every image you upload. The version is how a module proves
// which application actually runs after an OTA.
#define FW_RELEASE "0.0"

// Full application version, e.g. "1.0.0". Keep it at most 7 characters:
// at most one compat digit and a short release, because the EEPROM slot is
// 8 bytes including the terminating NUL and the value travels inside a
// colon-separated RS485 line (no spaces, colons or newlines).
#define FW_VERSION SF_STRINGIFY(FW_COMPAT) "." FW_RELEASE

static_assert(FW_COMPAT >= 1 && FW_COMPAT <= 9,
              "FW_COMPAT must be one digit so FW_VERSION fits in the EEPROM slot");
static_assert(sizeof(FW_VERSION) <= 8, "FW_VERSION must fit in 7 characters");
