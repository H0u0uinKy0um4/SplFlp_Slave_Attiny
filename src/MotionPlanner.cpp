#include "MotionPlanner.h"

MotionPlanner::MotionPlanner(CalibrationPort& calibration, MotionPort& motor)
  : calibration_(calibration), motor_(motor) {}

void MotionPlanner::calibrate() {
  const long measured = motor_.countPerRev();
  if (measured > 0 && measured < 65535)
    calibration_.storeTotalSteps((uint16_t)measured);
  motor_.homeMotor();
}

void MotionPlanner::home() {
  motor_.homeMotor();
}

int MotionPlanner::indexForFlap(char flap) const {
  for (int i = 0; i < DRUM_SIZE; ++i)
    if (calibration_.flapAt(i) == flap) return i;
  return -1;
}

uint16_t MotionPlanner::targetForIndex(int index) const {
  const uint16_t steps = calibration_.totalStepsPerRev();
  const uint16_t stored = calibration_.mapAt(index);
  if (stored != 0xFFFF && stored < steps) return stored;

  // Flap 0 sits at homeOffset; the drum is evenly spaced in hall-edge
  // coordinates. Round to the nearest half-step rather than truncating.
  const long offset = calibration_.homeOffset();
  return (uint16_t)((offset + ((long)index * steps + DRUM_SIZE / 2) / DRUM_SIZE)
                    % steps);
}

bool MotionPlanner::validTarget(int steps) const {
  const long total = calibration_.totalStepsPerRev();
  return total > 0 && steps >= 0 && steps < total;
}

void MotionPlanner::moveToFlap(char flap) {
  const int index = indexForFlap(flap);
  if (index >= 0) moveToIndex(index);
}

void MotionPlanner::moveToIndex(int index) {
  if (index < 0 || index >= DRUM_SIZE) return;
  if (calibration_.totalStepsPerRev() == 0) return;

  const uint16_t target = targetForIndex(index);
  moveToStep(target);
  if (motor_.positionKnown()) motor_.setCurrentFlapIndex(index);
}

void MotionPlanner::moveToStep(int steps) {
  if (!validTarget(steps)) return;

  // A move before the first successful home must establish the hall reference
  // first; otherwise the distance below has no meaning.
  if (!motor_.positionKnown()) {
    motor_.homeMotor();
    if (!motor_.positionKnown()) return;
  }

  long distance = (long)steps - motor_.position();
  if (distance < 0) distance += calibration_.totalStepsPerRev();

  // Arm mid-move re-referencing when the forward path crosses the hall magnet
  // (or comes within tolerance of it). beginHallWatch() samples the current
  // hall state, so a move starting on the magnet cannot see a false edge.
  const bool watchHall =
      (long)motor_.position() + distance + HALL_WATCH_TOLERANCE >=
      (long)calibration_.totalStepsPerRev();
  if (watchHall) motor_.beginHallWatch();

  while (distance > 0) {
    motor_.stepOne(watchHall);
    --distance;
    if (watchHall && motor_.hallReferenced()) {
      // The entering edge zeroed the position; recompute from the true one.
      distance = (long)steps - motor_.position();
      if (distance < 0) distance = 0; // landed on the edge itself
    }
  }

  motor_.releaseMotor();
  // Do not overwrite the position: stepOne() and the hall edge already put the
  // corrected landing spot there.
}
