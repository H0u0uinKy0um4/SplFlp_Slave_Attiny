#include "ModuleHandler.h"
#include <Arduino.h>
#include "RS485Handler.h"
#include <SoftwareSerial.h>
#include <string.h>
#include <stdlib.h>


SplitFlapModule::SplitFlapModule()
{
    if (checkFirstBoot()) loadOnBoot();
    else firstBoot();
    loadFlapChars();
}

void SplitFlapModule::waitOnBroadCast()
{
    delay(id * BROADCAST_DELAY);
}










