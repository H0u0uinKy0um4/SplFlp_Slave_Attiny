#pragma once
#include "ModuleHandler.h"
#include <SoftwareSerial.h>
#include <Arduino.h>

const int RS485_RX = 3;   // Receive data from bus (SoftwareSerial RX)
const int RS485_TX = 1;   // Transmit data to bus  (SoftwareSerial TX)
const int RS485_DE = 2;   // Driver Enable: HIGH = transmit, LOW = receive

extern SoftwareSerial rs485;
extern SplitFlapModule attiny;

constexpr unsigned long CMD_TIMEOUT_MS = 50;

void setupRS485();
void readRS485(String & message);
void sendRS485(String & message);
void runCommand(char cmd, const char* arg, bool isBroadcast = false);
void loadCommand();
void processLine(const char* line);