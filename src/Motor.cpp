
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

void Motor::stepBackward(int steps)
{
    // static bool lastHallState = false; // Persists across calls to detect rising edge

    for (int k = 0; k < steps; k++) {
        /*
        bool hallNow = hallActive();

        if (hallNow && !lastHallState) {
            currentStepPos = totalStepsPerRev - homeOffset;
        }
        lastHallState = hallNow;
        */

        currentPhase--;
        if (currentPhase < 0) currentPhase = 7;

        applyStep(halfStepSequence[currentPhase]);
        delay(stepDelay);

        // Track absolute position, wrapping at the revolution boundary
        currentStepPos++;
        if (currentStepPos >= totalStepsPerRev) currentStepPos = 0;
    }
}

long Motor::countPerRev()
{
  long safety = 0;

  // Phase 1: If already on the magnet, move off it first
  while (hallActive() && safety < 4000) {
    stepBackward(1);
    safety++;
    delay(5);
  }

  // Phase 2: Find the leading edge of the magnet
  safety = 0;
  while (!hallActive() && safety < 5000) {
    stepBackward(1);
    safety++;
  }

  // Phase 3: Find the trailing edge of the magnet (so we start counting from a clean edge)
  while (hallActive()) {
    stepBackward(1);
  }

  // Phase 4: Count steps for one full revolution (trailing edge → next trailing edge)
  int measuredSteps = 0;
  while (!hallActive() && measuredSteps < 5000) {
    stepBackward(1);
    measuredSteps++;
  }
  while (hallActive()) {
    stepBackward(1);
    measuredSteps++;
  }
  return measuredSteps;
}

void Motor::homeModule() {
  long safety = 0;

  // Step until the Hall sensor triggers, or bail after slightly more than one
  // full revolution (prevents infinite loops if sensor is broken/missing).
  while (!hallActive() && safety < (totalStepsPerRev + 500)) {
    stepBackward(1);
    safety++;
  }

  // Advance the calibrated offset to reach flap 0
  stepBackward(homeOffset);

  // We're now at flap 0 — reset tracking
  currentStepPos   = 0;
  currentFlapIndex = 0;
  releaseMotor();
}