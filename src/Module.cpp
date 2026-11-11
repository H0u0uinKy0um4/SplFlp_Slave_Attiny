#include "ModuleHandler.h"
#include <Arduino.h>
#include "RS485Handler.h"
#include <SoftwareSerial.h>
#include <string.h>
#include <stdlib.h>
#include "DebugMark.h"


SplitFlapModule::SplitFlapModule()
{
    debugMark(DBG_STAGE_CTOR);
    if (checkFirstBoot()) loadOnBoot();
    else firstBoot();
    loadFlapChars();
    // Persist FW_VERSION so it can be read over UPDI even while the module is
    // silent, and reported over RS485 once the bus is up.  Runs after the
    // config load so firstBoot() cannot overwrite the slot with defaults.
    saveFirmwareVersion();
}

void SplitFlapModule::waitOnBroadCast()
{
    delay(id * BROADCAST_DELAY);
}










