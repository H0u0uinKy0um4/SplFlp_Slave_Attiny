#pragma once

#include <stdint.h>

// Opt-in breadcrumb tracing for bench diagnosis (build with
// -DOTA_DEBUG_MARKERS).  Every marker is written to EEPROM bytes that the
// firmware otherwise leaves alone: the calibration map ends at 204 and the
// OTA boot flag is at 255, so 248..254 are free.
//
// The markers survive the installer's chip erase because SYSCFG0 has
// EESAVE=1, which is exactly what makes them readable with dump_eeprom.py
// after a normal power-up:
//
//   244  0x77 once app .init3 is entered, written *before* other EEPROM use
//   245  low byte of the app's first word as read by the bootloader at 0x0800
//   246  high byte of that word
//   247  0xAA if the bootloader decided to stay in update mode, 0x55 if not
//   248  RSTCTRL.RSTFR as seen by the bootloader on this boot
//   249  app loop heartbeat (0x9B once loop() is running)
//   250  bootloader boot counter
//   251  0x5A once the bootloader has reached the jump to the app
//   252  app boot counter (incremented in .init3)
//   253  last app stage reached on this boot
//   254  last app stage reached on the previous app boot
//
// Stage numbers are ordered by execution: if the trace stops at N, the app
// died between stage N and stage N+1.

#define DBG_EE_RAW           244
#define DBG_EE_RESET_FLAGS   248
#define DBG_EE_LOOP_ALIVE    249
#define DBG_EE_BL_BOOTS      250
#define DBG_EE_BL_JUMPED     251
#define DBG_EE_APP_BOOTS     252
#define DBG_EE_APP_STAGE     253
#define DBG_EE_APP_PREV      254

#define DBG_RAW_MARK_VALUE   0x77
#define DBG_BL_JUMPED_VALUE  0x5A
#define DBG_LOOP_ALIVE_VALUE 0x9B

#define DBG_STAGE_INIT3      1   /* app .init3: reset-flag hook ran          */
#define DBG_STAGE_CTOR       2   /* global constructors ran (before main)    */
#define DBG_STAGE_SETUP      3   /* setup() entered                          */
#define DBG_STAGE_RS485      4   /* setupRS485() returned                    */
#define DBG_STAGE_BROADCAST  5   /* waitOnBroadCast() returned               */
#define DBG_STAGE_HOMED      6   /* home() returned                          */
#define DBG_STAGE_LOOP       7   /* loop() entered the first time            */

#ifdef OTA_DEBUG_MARKERS

// Safe from .init3: no globals, no Arduino APIs, only the stack.
void debugMark(uint8_t stage);

// Set once loop() has been entered (EEPROM 249).
void debugLoopAlive(void);

// Write without the read-compare (still waits for the NVM to go idle).
void debugMarkRaw(uint8_t index, uint8_t value);

// Visible proof that .init3 executed: steps the motor coils for ~1 s before
// any EEPROM access.  No-op in a normal build.
void debugMotorTick(void);

#else

#define debugMark(stage) ((void)0)
#define debugLoopAlive() ((void)0)
#define debugMarkRaw(index, value) ((void)0)
#define debugMotorTick() ((void)0)

#endif
