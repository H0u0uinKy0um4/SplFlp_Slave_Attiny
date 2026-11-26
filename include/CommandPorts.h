#pragma once

#include <stdint.h>

// The ASCII command module is plain C++: every Arduino dependency lives behind
// one of these interfaces, so host tests cross the same seam as the firmware.

class ReplyWriter {
public:
  virtual ~ReplyWriter() {}
  virtual void write(const char* text) = 0;
  virtual void writeChar(char c) = 0;
  virtual void writeNumber(long value) = 0;
  virtual void endReply() = 0;
};

class CommandControl {
public:
  virtual ~CommandControl() {}
  virtual void delayMs(unsigned long ms) = 0;
  virtual void reset() = 0;
};

class ModulePort {
public:
  virtual ~ModulePort() {}
  virtual int id() const = 0;
  virtual uint16_t currentStepPos() const = 0;
  virtual bool positionKnown() const = 0;
  virtual uint16_t homeOffset() const = 0;
  virtual uint16_t totalStepsPerRev() const = 0;
  virtual void writeConfig(ReplyWriter& out) const = 0;
  virtual void writeDrum(ReplyWriter& out) const = 0;
  virtual void writeFirmwareVersion(ReplyWriter& out) const = 0;
  virtual int busCompat() const = 0;
  virtual bool requestBootloader() = 0;
  virtual void moveToFlap(char flap) = 0;
  virtual void moveToIndex(int index) = 0;
  virtual void home() = 0;
  virtual void calibrate() = 0;
  virtual void setHomeOffset(int value) = 0;
  virtual void setStepsPerRev(int value) = 0;
  virtual void moveToStep(int steps) = 0;
  virtual void setModuleId(int value) = 0;
  virtual void setAutoHome(int value) = 0;
  virtual void deleteMapPosition(int index) = 0;
  virtual void setMapPosition(int index, int position) = 0;
  virtual void setFlapChar(int index, char c) = 0;
};
