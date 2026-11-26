// Host tests for the ASCII command module. Compile with:
//   g++ -std=c++11 -Wall -Werror -Iinclude test/test_ascii_command.cpp
//     src/AsciiCommand.cpp -o /tmp/test_ascii_command
//   /tmp/test_ascii_command

#include "AsciiCommand.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct CapturingReply : ReplyWriter {
  std::string text;
  int endings = 0;

  void write(const char* value) override { text += value; }
  void writeChar(char value) override { text += value; }
  void writeNumber(long value) override {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    text += buffer;
  }
  void endReply() override { ++endings; }
};

struct RecordingControl : CommandControl {
  unsigned long delayed = 0;
  int resets = 0;
  void delayMs(unsigned long ms) override { delayed += ms; }
  void reset() override { ++resets; }
};

struct FakeModule : ModulePort {
  std::string calls;
  bool bootResult = true;

  int id() const override { return 10; }
  uint16_t currentStepPos() const override { return 123; }
  bool positionKnown() const override { return true; }
  uint16_t homeOffset() const override { return 2832; }
  uint16_t totalStepsPerRev() const override { return 4096; }

  void writeConfig(ReplyWriter& out) const override {
    out.write("m10:2832:4096:1:5=100");
  }
  void writeDrum(ReplyWriter& out) const override {
    out.write("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$&()-+=;q:%'.,/?*roygbpw");
  }
  void writeFirmwareVersion(ReplyWriter& out) const override {
    out.write("1.0.2");
  }
  int busCompat() const override { return 1; }
  bool requestBootloader() override {
    calls += "requestBootloader;";
    return bootResult;
  }
  void moveToFlap(char flap) override {
    calls += "moveToFlap("; calls += flap; calls += ");";
  }
  void moveToIndex(int index) override {
    calls += "moveToIndex(" + std::to_string(index) + ");";
  }
  void home() override { calls += "home;"; }
  void calibrate() override { calls += "calibrate;"; }
  void setHomeOffset(int value) override {
    calls += "setHomeOffset(" + std::to_string(value) + ");";
  }
  void setStepsPerRev(int value) override {
    calls += "setStepsPerRev(" + std::to_string(value) + ");";
  }
  void moveToStep(int steps) override {
    calls += "moveToStep(" + std::to_string(steps) + ");";
  }
  void setModuleId(int value) override {
    calls += "setModuleId(" + std::to_string(value) + ");";
  }
  void setAutoHome(int value) override {
    calls += "setAutoHome(" + std::to_string(value) + ");";
  }
  void deleteMapPosition(int index) override {
    calls += "deleteMapPosition(" + std::to_string(index) + ");";
  }
  void setMapPosition(int index, int position) override {
    calls += "setMapPosition(" + std::to_string(index) + "," +
             std::to_string(position) + ");";
  }
  void setFlapChar(int index, char flap) override {
    calls += "setFlapChar(" + std::to_string(index) + ",";
    calls += flap;
    calls += ");";
  }
};

int checks = 0;

void check(const char* line, const char* reply, const char* calls,
           unsigned long delay = 0, int resets = 0) {
  FakeModule module;
  CapturingReply out;
  RecordingControl control;
  AsciiCommand command(module, out, control);

  command.handleLine(line);

  if (out.text != reply || module.calls != calls ||
      control.delayed != delay || control.resets != resets) {
    std::printf("FAIL %s\n  reply:   '%s' (want '%s')\n  calls:   '%s' (want '%s')\n"
                "  delay:   %lu (want %lu)\n  resets:  %d (want %d)\n",
                line, out.text.c_str(), reply, module.calls.c_str(), calls,
                control.delayed, delay, control.resets, resets);
    assert(false);
  }
  ++checks;
}

} // namespace

int main() {
  // All 18 command cases, addressed to this module.
  check("m10-A", "", "moveToFlap(A);");
  check("m10+5", "", "moveToIndex(5);");
  check("m10h", "", "home;");
  check("m10c", "", "calibrate;");
  check("m10d", "m10:2832:4096:1:5=100", "");
  check("m10l", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$&()-+=;q:%'.,/?*roygbpw", "");
  check("m10o42", "", "setHomeOffset(42);");
  check("m10t100", "", "setStepsPerRev(100);");
  check("m10g200", "", "moveToStep(200);");
  check("m10i7", "", "setModuleId(7);");
  check("m10a1", "", "setAutoHome(1);");
  check("m10e3", "", "deleteMapPosition(3);");
  check("m10e*", "", "deleteMapPosition(64);");
  check("m10w2:100", "", "setMapPosition(2,100);");
  check("m10f5:A", "", "setFlapChar(5,A);");
  check("m10r4", "m10r:4:123", "setMapPosition(4,123);");
  check("m10r0", "m10r:0:123", "setMapPosition(0,123);setHomeOffset(123);");
  check("m10s", "m10s:123:0:1:1:1.0.2", "");
  check("m10v", "m10v:1:1.0.2", "");
  check("m10b", "m10b:1", "requestBootloader;", 100, 1);

  // Wildcards are rejected for scalar commands; no write reaches the module.
  check("m10o*", "", "");
  check("m10t*", "", "");
  check("m10g*", "", "");
  check("m10i*", "", "");
  check("m10a*", "", "");

  // Addressing, broadcast and invalid input.
  check("m11h", "", "");
  check("m*h", "", "home;");
  check("m*b", "", "");
  check("m10r99", "m10r:99:-1", "");
  check("m10r", "", "");
  check("h", "", "");
  check("m10z", "", "");

  std::printf("OK: %d ASCII command cases\n", checks);
  return 0;
}
