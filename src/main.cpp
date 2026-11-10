#include <Arduino.h>
#include "ModuleHandler.h"
#include "RS485Handler.h"

SplitFlapModule attiny;

void setup(){
    setupRS485();
    attiny.waitOnBroadCast();
    if(attiny.getAutoHome())attiny.home();
}

void loop()
{
    loadCommand();
}