#pragma once

// Version model
// ------------
// The system has two independent compatibility domains (docs/VERSIONING.md):
//
//   BUS_COMPAT  the slave <-> master side: the RS485 application protocol and
//               the bootloader/OTA contract. This firmware carries it.
//   MQTT_COMPAT the master <-> Home Assistant side: the MQTT status/command
//               JSON schema. The master carries it, not the slave.
//
// The master therefore publishes two compat numbers; the slave publishes only
// the bus one. RELEASE is this repository's own release inside its generation.
//
// The wire version is "<BUS_COMPAT>.<RELEASE>", so the compatibility
// generation is readable from the number itself: 1.4.2 means bus generation 1,
// release 4.2. The compile-time construction makes it impossible for the two
// to drift.

#define SF_STRINGIFY_(value) #value
#define SF_STRINGIFY(value) SF_STRINGIFY_(value)

// Bump this only for a breaking slave <-> master change. Keep it in lockstep
// with MASTER_BUS_COMPAT (master include/Version.h). The MQTT generation is
// independent and lives in the master and Home Assistant.
#define FW_BUS_COMPAT 1

// Bump this for every image you upload. The version is how a module proves
// which application actually runs after an OTA.
#define FW_RELEASE "0.1"

// Full application version, e.g. "1.0.1". Keep it at most 7 characters:
// at most one compat digit and a short release, because the EEPROM slot is
// 8 bytes including the terminating NUL and the value travels inside a
// colon-separated RS485 line (no spaces, colons or newlines).
#define FW_VERSION SF_STRINGIFY(FW_BUS_COMPAT) "." FW_RELEASE

static_assert(FW_BUS_COMPAT >= 1 && FW_BUS_COMPAT <= 9,
              "FW_BUS_COMPAT must be one digit so FW_VERSION fits in the EEPROM slot");
static_assert(sizeof(FW_VERSION) <= 8, "FW_VERSION must fit in 7 characters");
