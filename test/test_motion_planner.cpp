// Host tests for the calibration-and-motion module. Compile with:
//   g++ -std=c++11 -Wall -Werror -Iinclude test/test_motion_planner.cpp
//     src/MotionPlanner.cpp -o /tmp/test_motion_planner
//   /tmp/test_motion_planner

#include "MotionPlanner.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

struct FakeCalibration : CalibrationPort {
  uint16_t offset = 2832;
  uint16_t total = 4096;
  uint16_t positions[MotionPlanner::DRUM_SIZE];
  char flaps[MotionPlanner::DRUM_SIZE];
  int saveCalls = 0;
  uint16_t savedSteps = 0;

  FakeCalibration() {
    for (int i = 0; i < MotionPlanner::DRUM_SIZE; ++i) positions[i] = 0xFFFF;
    std::memset(flaps, 0, sizeof(flaps));
  }

  uint16_t homeOffset() const override { return offset; }
  uint16_t totalStepsPerRev() const override { return total; }
  uint16_t mapAt(int index) const override {
    return index >= 0 && index < MotionPlanner::DRUM_SIZE ? positions[index]
                                                          : 0xFFFF;
  }
  char flapAt(int index) const override {
    return index >= 0 && index < MotionPlanner::DRUM_SIZE ? flaps[index] : '\0';
  }
  void storeTotalSteps(uint16_t steps) override {
    ++saveCalls;
    savedSteps = steps;
    total = steps;
  }
};

struct FakeMotor : MotionPort {
  uint16_t pos = 0;
  uint16_t wrap = 4096;
  bool known = true;
  int flap = -1;
  int steps = 0;
  int homes = 0;
  int releases = 0;
  int watches = 0;
  long measured = 4096;
  int hallAtStep = -1; // 1-based step on which the entering edge is seen
  bool referenced = false;

  uint16_t position() const override { return pos; }
  void setPosition(uint16_t value) override { pos = value; }
  bool positionKnown() const override { return known; }
  void setPositionKnown(bool value) override { known = value; }
  int currentFlapIndex() const override { return flap; }
  void setCurrentFlapIndex(int value) override { flap = value; }
  void homeMotor() override {
    ++homes;
    known = true;
    pos = wrap == 0 ? 0 : 2832 % wrap;
    flap = 0;
  }
  void releaseMotor() override { ++releases; }
  void beginHallWatch() override {
    ++watches;
    referenced = false;
  }
  bool hallReferenced() const override { return referenced; }
  void stepOne(bool watchHall) override {
    ++steps;
    if (watchHall && steps == hallAtStep) {
      pos = 0; // Motor::stepBackward zeroes on the entering edge
      referenced = true;
    }
    pos = (uint16_t)((pos + 1) % wrap);
  }
  long countPerRev() override {
    known = false;
    return measured;
  }
};

int checks = 0;

void expect(const char* name, const MotionPlanner& planner,
            const FakeMotor& motor, int steps, uint16_t pos, int watches = 0) {
  if (motor.steps != steps || motor.position() != pos || motor.watches != watches) {
    std::printf("FAIL %s: steps=%d (want %d), pos=%u (want %u), watches=%d (want %d)\n",
                name, motor.steps, steps, motor.position(), pos, motor.watches,
                watches);
    assert(false);
  }
  ++checks;
}

} // namespace

int main() {
  {
    FakeCalibration calibration;
    FakeMotor motor;
    MotionPlanner planner(calibration, motor);
    planner.moveToIndex(2);
    // Unset map entry: homeOffset + round(2 * 4096 / 64) = 2960.
    expect("fallback target", planner, motor, 2960, 2960);
    assert(motor.flap == 2);
    assert(motor.releases == 1);
  }
  {
    FakeCalibration calibration;
    calibration.positions[2] = 100;
    FakeMotor motor;
    MotionPlanner planner(calibration, motor);
    planner.moveToIndex(2);
    expect("stored map wins", planner, motor, 100, 100);
  }
  {
    FakeCalibration calibration;
    FakeMotor motor;
    motor.pos = 4000;
    MotionPlanner planner(calibration, motor);
    planner.moveToStep(100);
    // Forward-only with wrap: 100 - 4000 = -3900 -> 196 steps.
    expect("forward wrap and hall watch", planner, motor, 196, 100, 1);
  }
  {
    FakeCalibration calibration;
    FakeMotor motor;
    motor.pos = 4000;
    motor.hallAtStep = 5;
    MotionPlanner planner(calibration, motor);
    planner.moveToStep(100);
    // Edge re-references pos to 1, so only 99 more steps are needed.
    expect("mid-move re-reference", planner, motor, 104, 100, 1);
    assert(motor.referenced);
  }
  {
    FakeCalibration calibration;
    FakeMotor motor;
    motor.pos = 4000;
    motor.hallAtStep = 5;
    MotionPlanner planner(calibration, motor);
    planner.moveToStep(0);
    // Landing on the edge itself must not wrap a whole revolution.
    expect("target is the hall edge", planner, motor, 5, 1, 1);
  }
  {
    FakeCalibration calibration;
    FakeMotor motor;
    motor.known = false;
    MotionPlanner planner(calibration, motor);
    planner.moveToStep(100);
    // Homing puts us at homeOffset, then 1364 forward steps reach 100.
    expect("home before first move", planner, motor, 1364, 100, 1);
    assert(motor.homes == 1);
  }
  {
    FakeCalibration calibration;
    FakeMotor motor;
    MotionPlanner planner(calibration, motor);
    planner.moveToIndex(64);
    planner.moveToIndex(-1);
    planner.moveToStep(-1);
    planner.moveToStep(4096);
    expect("invalid targets are ignored", planner, motor, 0, 0);
    assert(motor.homes == 0);
  }
  {
    FakeCalibration calibration;
    calibration.flaps[1] = 'A';
    calibration.positions[1] = 50;
    FakeMotor motor;
    MotionPlanner planner(calibration, motor);
    planner.moveToFlap('A');
    expect("flap character resolves to index", planner, motor, 50, 50);
    assert(motor.flap == 1);

    planner.moveToFlap('Z');
    assert(motor.steps == 50); // unknown flap is silent
  }
  {
    FakeCalibration calibration;
    FakeMotor motor;
    MotionPlanner planner(calibration, motor);
    planner.calibrate();
    assert(calibration.saveCalls == 1);
    assert(calibration.savedSteps == 4096);
    assert(motor.homes == 1);

    motor.measured = -1;
    planner.calibrate();
    assert(calibration.saveCalls == 1); // failed measurement is not stored
    assert(motor.homes == 2);
  }

  std::printf("OK: %d calibration/motion cases\n", checks);
  return 0;
}
