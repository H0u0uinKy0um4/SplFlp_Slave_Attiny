// Reset-flag bookkeeping, overriding megaTinyCore's weak init_reset_flags().
//
// The core's version (cores/megatinycore/main.cpp, run from .init3 before
// main()) reads RSTCTRL.RSTFR, clears it and then forces a SOFTWARE RESET if
// it found no flags at all, assuming a chip with zero flags did not come
// through a real reset.
//
// That assumption does not hold when a bootloader hands over to the app: a
// bootloader that clears RSTFR before jumping makes the app reset itself, the
// bootloader clears the flags again on the next pass, and the module loops
// forever - no setup(), no homing, no RS485 replies.  Our OTA bootloader no
// longer clears RSTFR (bootloader/src/bootloader.c, start_app()), but the app
// keeps this override so a hand-over we do not control cannot brick it at the
// bench.
//
// Same bookkeeping as the core, minus the forced reset: the flags are cleared
// after being copied, so resetCause()-style queries behave as before.
//
// The signature/name must stay identical to the core's declaration (plain C++
// linkage, global scope) or this stops being an override.

#include <Arduino.h>

void init_reset_flags()
{
    uint8_t flags = RSTCTRL.RSTFR;
    RSTCTRL.RSTFR = flags;
    GPIOR0 = flags;
}
