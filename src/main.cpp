#include <Arduino.h>
#include "ModuleHandler.h"
#include "RS485Handler.h"

SplitFlapModule attiny;

int parseState = 0;        // State machine position for incoming serial parsing
int lastSerialTime;
String buffer     = "";      // Accumulates digit characters during command parsing
String idBuffer   = "";      // Accumulates digit characters of the incoming module ID
bool   idWildcard = false;   // True when '*' was seen in the ID field (broadcast match)

void setup(){
    setupRS485();
    attiny.waitOnBroadCast();
    if(attiny.getAutoHome())attiny.home();
}

void loop()
{
    loadCommand();
}