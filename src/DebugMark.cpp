// Bench breadcrumbs, compiled in only with -DOTA_DEBUG_MARKERS.
//
// Uses the mapped EEPROM window directly (the same write sequence as
// megaTinyCore's EEPROM library: store the byte, then issue
// NVMCTRL_CMD_PAGEERASEWRITE under the SPM CCP signature) so it is safe even
// from .init3, which runs before the C++ runtime and the EEPROM library.

#include "DebugMark.h"

#ifdef OTA_DEBUG_MARKERS

#include <avr/io.h>

static uint8_t ee_read(uint8_t index)
{
    return *(volatile uint8_t *)(MAPPED_EEPROM_START + index);
}

static void ee_write(uint8_t index, uint8_t value)
{
    if (ee_read(index) == value)
        return;                              // no wear on identical value

    while (NVMCTRL_STATUS & (NVMCTRL_FBUSY_bm | NVMCTRL_EEBUSY_bm))
        ;
    *(volatile uint8_t *)(MAPPED_EEPROM_START + index) = value;
    _PROTECTED_WRITE_SPM(NVMCTRL_CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
    while (NVMCTRL_STATUS & (NVMCTRL_FBUSY_bm | NVMCTRL_EEBUSY_bm))
        ;
}

void debugMark(uint8_t stage)
{
    if (stage == DBG_STAGE_INIT3) {
        uint8_t boots = ee_read(DBG_EE_APP_BOOTS);
        boots = (boots == 0xFF) ? 1 : (uint8_t)(boots + 1);
        ee_write(DBG_EE_APP_PREV, ee_read(DBG_EE_APP_STAGE));
        ee_write(DBG_EE_APP_BOOTS, boots);
    }
    ee_write(DBG_EE_APP_STAGE, stage);
}

void debugMarkRaw(uint8_t index, uint8_t value)
{
    while (NVMCTRL_STATUS & (NVMCTRL_FBUSY_bm | NVMCTRL_EEBUSY_bm))
        ;
    *(volatile uint8_t *)(MAPPED_EEPROM_START + index) = value;
    _PROTECTED_WRITE_SPM(NVMCTRL_CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
    while (NVMCTRL_STATUS & (NVMCTRL_FBUSY_bm | NVMCTRL_EEBUSY_bm))
        ;
}

void debugMotorTick(void)
{
    /* IN1..IN4 = PB0..PB3 (Arduino pins 9, 8, 7, 6).  Eight steps with a
       dwell long enough to be seen/heard, so "did .init3 run?" can be
       answered without trusting EEPROM at all.

       This runs inside the naked .init3 function, before any C frame
       exists: no volatile locals here.  __builtin_avr_delay_cycles() is a
       constant inline loop and keeps the step counter in a register. */
    PORTB.DIR |= 0x0F;
    for (uint8_t step = 0; step < 8; step++) {
        PORTB.OUT = (uint8_t)((PORTB.OUT & 0xF0) | (uint8_t)(1 << (step & 3)));
        __builtin_avr_delay_cycles(2000000UL);      /* 100 ms at 20 MHz */
    }
    PORTB.OUT &= 0xF0;                 /* de-energised, pins stay outputs */
}

void debugLoopAlive(void)
{
    ee_write(DBG_EE_LOOP_ALIVE, DBG_LOOP_ALIVE_VALUE);
}

#endif
