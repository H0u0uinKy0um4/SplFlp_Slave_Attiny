#include "RS485Handler.h"
#include "AsciiCommand.h"
#include "CommandPorts.h"
#include "FirmwareIdentity.h"
#include "ModuleHandler.h"

#include <Arduino.h>
#include <SoftwareSerial.h>

SoftwareSerial rs485(RS485_RX, RS485_TX);

namespace {

// The reply adapter streams bytes: the bus sees the same text as before, but
// firmware never assembles a long line in RAM.
class SerialReplyWriter : public ReplyWriter {
public:
  void write(const char* text) override { begin(); rs485.print(text); }
  void writeChar(char c) override { begin(); rs485.print(c); }
  void writeNumber(long value) override {
    // avr-libc's printf pulls in several KB; format integers by hand.
    char buffer[16];
    uint8_t len = 0;
    unsigned long magnitude = value < 0 ? (unsigned long)(-(value + 1)) + 1
                                        : (unsigned long)value;
    if (magnitude == 0) {
      buffer[len++] = '0';
    } else {
      char digits[12];
      uint8_t count = 0;
      while (magnitude != 0) {
        digits[count++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
      }
      while (count != 0) buffer[len++] = digits[--count];
    }
    if (value < 0) buffer[len++] = '-';
    buffer[len] = '\0';
    write(buffer);
  }
  void endReply() override {
    if (!active_) return;
    rs485.print('\n');
    delay(1);                       // small guard, not 100 ms
    digitalWrite(RS485_DE, LOW);
    active_ = false;
  }

private:
  bool active_ = false;

  void begin() {
    if (active_) return;
    delay(10);                      // master turnaround
    digitalWrite(RS485_DE, HIGH);
    delayMicroseconds(100);         // driver enable settle
    active_ = true;
  }
};

class ArduinoControl : public CommandControl {
public:
  void delayMs(unsigned long ms) override { delay(ms); }
  void reset() override {
    noInterrupts();
    _PROTECTED_WRITE(RSTCTRL_SWRR, RSTCTRL_SWRE_bm);
    for (;;) {}                      // reset takes effect within a few cycles
  }
};

// Converts the module's String-based interface to the plain-C++ ModulePort.
class ArduinoModulePort : public ModulePort {
public:
  explicit ArduinoModulePort(SplitFlapModule& module) : module_(module) {}

  int id() const override { return module_.getId(); }
  uint16_t currentStepPos() const override { return module_.getCurrentStepPos(); }
  bool positionKnown() const override { return module_.isPositionKnown(); }
  uint16_t homeOffset() const override { return module_.getHomeOffset(); }
  uint16_t totalStepsPerRev() const override { return module_.getTotalStepsPerRev(); }

  void writeConfig(ReplyWriter& out) const override {
    String text;
    module_.getConfig(text);
    out.write(text.c_str());
  }
  void writeDrum(ReplyWriter& out) const override {
    String text;
    module_.getDrum(text);
    out.write(text.c_str());
  }
  void writeFirmwareVersion(ReplyWriter& out) const override {
    String text;
    module_.getFirmwareVersion(text);
    out.write(text.c_str());
  }
  int busCompat() const override { return FirmwareIdentity::build().compat(); }
  bool requestBootloader() override { return module_.requestBootloader(); }
  void moveToFlap(char flap) override { module_.moveToFlap(flap); }
  void moveToIndex(int index) override { module_.moveToIndex(index); }
  void home() override { module_.home(); }
  void calibrate() override { module_.Calibrate(); }
  void setHomeOffset(int value) override { module_.setHomeOffset(value); }
  void setStepsPerRev(int value) override { module_.setStepsPerRev(value); }
  void moveToStep(int steps) override { module_.moveToStep(steps); }
  void setModuleId(int value) override { module_.setModuleId(value); }
  void setAutoHome(int value) override { module_.setAutoHome(value); }
  void deleteMapPosition(int index) override { module_.deleteMapPosition(index); }
  void setMapPosition(int index, int position) override {
    module_.setMapPosition(index, position);
  }
  void setFlapChar(int index, char c) override { module_.setFlapChar(index, c); }

private:
  SplitFlapModule& module_;
};

SerialReplyWriter serialReply;
ArduinoControl arduinoControl;
AsciiCommand* command = 0;

} // namespace

void setupRS485(SplitFlapModule& module) {
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);
  rs485.begin(9600);

  static ArduinoModulePort modulePort(module);
  static AsciiCommand asciiCommand(modulePort, serialReply, arduinoControl);
  command = &asciiCommand;
}

void loadCommand() {
  static char          line[24];
  static uint8_t       len = 0;
  static unsigned long lastByteMs = 0;

  while (rs485.available()) {
    char c = rs485.read();
    lastByteMs = millis();

    if (c == '\n' || c == '\r') {                 // end of line -> execute
      if (len > 0) { line[len] = '\0'; len = 0; command->handleLine(line); }
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
    command->handleLine(line);
  }
}
