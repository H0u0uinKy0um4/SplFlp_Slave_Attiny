#pragma once
#include "ModuleHandler.h"
#include <SoftwareSerial.h>
#include <Arduino.h>

const int RS485_RX = 3;   // Receive data from bus (SoftwareSerial RX)
const int RS485_TX = 1;   // Transmit data to bus  (SoftwareSerial TX)
const int RS485_DE = 2;   // Driver Enable: HIGH = transmit, LOW = receive

extern SoftwareSerial rs485;

constexpr unsigned long CMD_TIMEOUT_MS = 50;

// Transport adapter setup: the command module and its other adapters are
// owned here; the caller supplies the module it should drive.
void setupRS485(SplitFlapModule& module);
void loadCommand();
