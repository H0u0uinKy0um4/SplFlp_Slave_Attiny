# Split-Flap Slave Firmware

The ATtiny module firmware for the split-flap display: it owns module motion, calibration and the bus language shared with the master. This glossary names the domain concepts that the firmware and its tooling use.

## Language

**ASCII command**:
A human-readable instruction on the RS485 bus that names one operation for one module or every module, written as a command character with an optional argument.
_Avoid_: request, packet, message

**OTA frame**:
A binary RS485 unit used only while a module is in bootloader mode, escaped so a raw `m` never appears on the wire and cannot start an **ASCII command** on a bystander module.
_Avoid_: packet, message, command

**persisted layout**:
The canonical record of EEPROM addresses, marker values and flash boundaries shared by the application, the bootloader and the host tools.
_Avoid_: header constants, magic numbers, EEPROM map

**firmware identity**:
The bus compatibility generation and this repository's release, composed into the version reported on the bus and stored in the EEPROM slot by the **persisted layout**.
_Avoid_: build date, version string

**position map**:
The 64-entry table that gives the hall-edge position of each flap, with a computed fallback when an entry is unset.
_Avoid_: calibration table, lookup
