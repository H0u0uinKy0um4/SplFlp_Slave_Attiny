#include <SoftwareSerial.h>
#include "RS485Handler.h"
#include "ModuleHandler.h"
#include "api/WCharacter.h"
#include <Arduino.h>
#include <stdlib.h>


SoftwareSerial rs485(RS485_RX, RS485_TX);

long processNumber(const char*& arg, const char terminal='\0')
{
  if (*arg == '*') {
    arg++;
    return (*arg == terminal || *arg == '\0') ? NUM_ALL : NUM_ERROR;
  }

  if (!isDigit(*arg)) return NUM_ERROR; 
  long sum=0;

  while (*arg != terminal && *arg != '\0') {
    if (!isDigit(*arg)) return NUM_ERROR;
    sum = sum * 10 + (*arg - '0');
    if (sum > 65535L) return NUM_ERROR;         // overflow cap
    arg++;
  }
  if(*arg != terminal) sum = NUM_ERROR;
  if (*arg != '\0') arg++;
  return sum;
}

void setupRS485()
{
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);
  rs485.begin(9600);
}

void sendRS485(String & message) {
  delay(10);                       // master turnaround
  digitalWrite(RS485_DE, HIGH);
  delayMicroseconds(100);          // driver enable settle
  rs485.print(message + "\n");     // blocking; stop bit already sent on return
  delay(1);                        // small guard, not 100 ms
  digitalWrite(RS485_DE, LOW);
}


// Software reset into the bootloader.  The boot flag (EEPROM 255) is armed
// by requestBootloader() before this is called, so the bootloader stays in
// update mode after the reset.
static void softwareReset() {
  noInterrupts();
  _PROTECTED_WRITE(RSTCTRL_SWRR, RSTCTRL_SWRE_bm);
  for (;;) {}                      // reset takes effect within a few cycles
}


static void writeMapPosition(const char* arg) {
    const char* colon = strchr(arg, ':');
    if (colon) {
        const char* idxArg = arg;
        long idx = processNumber(idxArg, ':');
        const char* posArg = colon + 1;
        long pos = processNumber(posArg);
        if (idx >= 0 && pos >= 0) attiny.setMapPosition((int)idx, (int)pos);
    }
}

static void writeFlapChar(const char* arg) {
    const char* p = arg;
    long idx = processNumber(p, ':');
    if (idx >= 0 && *p != '\0') attiny.setFlapChar((int)idx, *p);
}

void runCommand(char cmd, const char* arg,bool isBroadcast=false) {
  String message;
  bool enterBootloader = false;
  switch (cmd) {   
    case '-': if(*arg) attiny.moveToFlap(*arg); break;
    case '+': if (isDigit(*arg)) attiny.moveToIndex(atoi(arg)); break;
    case 'h': attiny.home(); break;
    case 'c': attiny.Calibrate();  break;
    case 'd': attiny.getConfig(message); break;
    case 'l': attiny.getDrum(message); break;
    case 'o': {
      long v = processNumber(arg);
      if (v >= 0) attiny.setHomeOffset((int)v);
      break;
    }
    case 't': attiny.setStepsPerRev(processNumber(arg)); break;
    case 'g': attiny.moveToStep(processNumber(arg)); break;
    case 'i': attiny.setModuleId(processNumber(arg)); break;
    case 'a': attiny.setAutoHome(processNumber(arg)); break;
    case 'e': attiny.deleteMapPosition(processNumber(arg)); break;
    case 'w': writeMapPosition(arg); break;
    case 'f': writeFlapChar(arg); break; 
    case 'r': {
      // Record the current position as a map entry.  Index 0 redefines where
      // flap 0 sits, which also updates the homing offset.
      if (isDigit(*arg)) {
        long idx = atol(arg);
        if (idx >= 0 && idx < 64 && attiny.isPositionKnown()) {
          int cur = attiny.getCurrentStepPos();
          attiny.setMapPosition((int)idx, cur);
          if (idx == 0) {
            attiny.setHomeOffset(cur);
          }
          message = "m" + String(attiny.getId()) + "r:" + String(idx) + ":" + String(cur);
        } else {
          message = "m" + String(attiny.getId()) + "r:" + String(idx) + ":-1";
        }
      }
      break;
    }
    case 's': {
      // State query: pos:busy:position_known.  Moves are blocking, so busy is
      // always 0 by the time the command is parsed.
      message = "m" + String(attiny.getId()) + "s:" + String(attiny.getCurrentStepPos()) +
                ":0:" + String(attiny.isPositionKnown() ? 1 : 0);
      break;
    }
    case 'b': {
      // Enter the bootloader for an OTA update.  Unicast only: a broadcast
      // would drop every module into update mode and stall the display.
      if (!isBroadcast) {
        bool ok = attiny.requestBootloader();
        message = "m" + String(attiny.getId()) + "b:" + (ok ? "1" : "0");
        if (ok) enterBootloader = true;
      }
      break;
    }
    
    default: break; 
  }
    if (message.length() && !isBroadcast) {
    sendRS485(message);
  }
    if (enterBootloader) {
      // sendRS485() blocks until the reply is fully shifted out; give the
      // master a moment to parse it before the bus goes quiet for the reset.
      delay(100);
      softwareReset();
    }
}


void processLine(const char* line) {
  if (line[0] != 'm') return;
  const char* p = line + 1;
 
  bool isBroadcast  = false;
  bool hasDigits = false;
  int  id        = 0;
 
  while (*p == '*' || isDigit(*p)) {
    if (*p == '*'){
      isBroadcast = true;
    }
    else { hasDigits = true; if (id < 1000) id = id * 10 + (*p - '0'); }
    p++;
  }
 
  if (*p == '\0') return;                                   // no command letter
  bool forMe = isBroadcast || (hasDigits && id == attiny.getId());
  if (!forMe) return;
 
  runCommand(*p, p + 1,isBroadcast);
}

void loadCommand() {
  static char          line[24];
  static uint8_t       len = 0;
  static unsigned long lastByteMs = 0;
 
  while (rs485.available()) {
    char c = rs485.read();
    lastByteMs = millis();
 
    if (c == '\n' || c == '\r') {                 // end of line -> execute
      if (len > 0) { line[len] = '\0'; len = 0; processLine(line); }
      continue;
    }
    if (len == 0 && c != 'm') continue;           // wait for start marker
    if (len < sizeof(line) - 1) line[len++] = c;
    else len = 0;                                  // too long: drop it
  }
 
  // No terminator received: run what we have after a short silence
  if (len > 0 && millis() - lastByteMs > CMD_TIMEOUT_MS) {
    line[len] = '\0';
    len = 0;
    processLine(line);
  }
}
