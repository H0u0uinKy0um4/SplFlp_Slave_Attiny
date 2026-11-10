
#include "Motor.h"

Motor::Motor()
{
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
    pinMode(HALL_PIN, INPUT_PULLUP);
}

void Motor::releaseMotor()
{
    digitalWrite(IN1, 0);
    digitalWrite(IN2, 0);
    digitalWrite(IN3, 0);
    digitalWrite(IN4, 0);
}

bool Motor::hallActive()
{
    return (digitalRead(HALL_PIN) == LOW);
}

void Motor::applyStep(const uint8_t *step)
{
    digitalWrite(IN1, step[0]);
    digitalWrite(IN2, step[1]);
    digitalWrite(IN3, step[2]);
    digitalWrite(IN4, step[3]);
}

void Motor::stepBackward(int steps, bool watchHall)
{
    for (int k = 0; k < steps; k++) {
        // Mid-move re-referencing: while a move crosses the hall magnet, watch
        // for the entering edge (inactive -> active) and zero the position
        // exactly like homeModule() does.  This is the only drift correction;
        // without it missed steps accumulate forever.  See PROTOCOL.md.
        if (watchHall) {
            bool hallNow = hallActive();
            if (hallNow && !hallWatchLast) {
                currentStepPos = 0;
                hallReferenced = true;
            }
            hallWatchLast = hallNow;
        }

        currentPhase--;
        if (currentPhase < 0) currentPhase = 7;

        applyStep(halfStepSequence[currentPhase]);
        delay(stepDelay);

        // Track absolute position, wrapping at the revolution boundary
        currentStepPos++;
        if (currentStepPos >= totalStepsPerRev) currentStepPos = 0;
    }
}

// Arm mid-move hall re-referencing.  Sampling the current hall state as the
// baseline prevents a false entering edge when a watched move starts while
// the reel already sits on the magnet (the first edge seen would then be the
// leaving edge, which must not re-zero the position).
void Motor::beginHallWatch()
{
    hallWatchLast = hallActive();
    hallReferenced = false;
}


long Motor::countPerRev()
{
  // Measuring moves the reel arbitrarily, so the position is no longer known
  // until the caller homes afterwards.
  positionKnown = false;
  long safety = 0;

  // Phase 1: If already on the magnet, move off it first
  while (hallActive() && safety < 4000) {
    stepBackward(1);
    safety++;
    delay(5);
  }

  // Phase 2: Find the entering edge of the magnet
  safety = 0;
  while (!hallActive() && safety < 5000) {
    stepBackward(1);
    safety++;
  }
  if (!hallActive()) return -1;  // Hall never triggered: do not store a guess.

  // Phase 3: Find the trailing edge of the magnet (so we start counting from a
  // clean edge).  Capped so a stuck sensor cannot hang the firmware.
  safety = 0;
  while (hallActive() && safety < 5000) {
    stepBackward(1);
    safety++;
  }
  if (hallActive()) return -1;

  // Phase 4: Count steps for one full revolution (trailing edge → next trailing
  // edge).  Both loops are capped for the same reason.
  long measuredSteps = 0;
  safety = 0;
  while (!hallActive() && safety < 5000) {
    stepBackward(1);
    measuredSteps++;
    safety++;
  }
  if (!hallActive()) return -1;
  safety = 0;
  while (hallActive() && safety < 5000) {
    stepBackward(1);
    measuredSteps++;
    safety++;
  }
  if (hallActive()) return -1;
  return measuredSteps;
}

void Motor::homeModule() {
  positionKnown = false;

  // Leave the magnet first so the reference is always the same entering edge,
  // even when homing starts with the reel already on the hall magnet.
  long safety = 0;
  while (hallActive() && safety < (long)totalStepsPerRev + 500) {
    stepBackward(1);
    safety++;
  }

  // Seek the entering edge of the magnet.  Bail out after slightly more than
  // one revolution so a broken or missing sensor cannot hang the module.
  safety = 0;
  while (!hallActive() && safety < (long)totalStepsPerRev + 500) {
    stepBackward(1);
    safety++;
  }

  if (!hallActive()) {
    // Never claim a known position when the reference was not found.
    releaseMotor();
    return;
  }

  // Advance the calibrated offset from the entering edge to flap 0.
  stepBackward(homeOffset);

  // In the hall-edge coordinate system flap 0 sits at homeOffset.
  currentStepPos   = totalStepsPerRev > 0
                         ? homeOffset % totalStepsPerRev
                         : homeOffset;
  currentFlapIndex = 0;
  positionKnown    = true;
  releaseMotor();
}