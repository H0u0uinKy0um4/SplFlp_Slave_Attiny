#pragma once

#include <stdint.h>

// Arduino-free ports. The module below owns the calibration record's meaning
// and every target-to-step decision; these interfaces only read/write state
// and drive hardware.

class CalibrationPort {
public:
  virtual ~CalibrationPort() {}
  virtual uint16_t homeOffset() const = 0;
  virtual uint16_t totalStepsPerRev() const = 0;
  virtual uint16_t mapAt(int index) const = 0; // 0xFFFF means unset
  virtual char flapAt(int index) const = 0;
  virtual void storeTotalSteps(uint16_t steps) = 0;
};

class MotionPort {
public:
  virtual ~MotionPort() {}
  virtual uint16_t position() const = 0;
  virtual void setPosition(uint16_t pos) = 0;
  virtual bool positionKnown() const = 0;
  virtual void setPositionKnown(bool known) = 0;
  virtual int currentFlapIndex() const = 0;
  virtual void setCurrentFlapIndex(int index) = 0;
  virtual void homeMotor() = 0;
  virtual void releaseMotor() = 0;
  virtual void beginHallWatch() = 0;
  virtual bool hallReferenced() const = 0;
  virtual void stepOne(bool watchHall) = 0;
  virtual long countPerRev() = 0;
};

// One deep module for calibration and motion: it owns homing policy, the
// map/fallback target decision, forward-only distance, hall-watch policy and
// mid-move re-referencing. Callers name a flap, index or step target; they
// never manipulate motor fields or individual map entries to get there.
class MotionPlanner {
public:
  static const int DRUM_SIZE = 64;
  static const int HALL_WATCH_TOLERANCE = 32;

  MotionPlanner(CalibrationPort& calibration, MotionPort& motor);

  void calibrate();
  void home();

  void moveToFlap(char flap);
  void moveToIndex(int index);
  void moveToStep(int steps);

  uint16_t targetForIndex(int index) const;
  int indexForFlap(char flap) const;

private:
  CalibrationPort& calibration_;
  MotionPort& motor_;

  bool validTarget(int steps) const;
};
