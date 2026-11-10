#include <Arduino.h>
#include "ModuleHandler.h"
#include "RS485Handler.h"
#include "DebugMark.h"

SplitFlapModule attiny;

void setup(){
    debugMark(DBG_STAGE_SETUP);
    setupRS485();
    debugMark(DBG_STAGE_RS485);
    attiny.waitOnBroadCast();
    debugMark(DBG_STAGE_BROADCAST);
    if(attiny.getAutoHome())attiny.home();
    debugMark(DBG_STAGE_HOMED);
}

void loop()
{
    static bool firstLoop = true;
    if (firstLoop) {
        firstLoop = false;
        debugMark(DBG_STAGE_LOOP);
        debugLoopAlive();
    }
    loadCommand();
}