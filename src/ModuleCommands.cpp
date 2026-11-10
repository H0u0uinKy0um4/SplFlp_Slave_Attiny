#include "ModuleHandler.h"
#include <Arduino.h>
#include "RS485Handler.h"
#include <string.h>
#include <stdlib.h>

void SplitFlapModule::Calibrate()
{
    long measured = stepper.countPerRev();
    // Store the measured revolution; the old code measured but discarded it.
    if (measured > 0 && measured < 65535) {
        stepper.totalStepsPerRev = (uint16_t)measured;
        saveTotalSteps();
    }
    stepper.homeModule();
}
void SplitFlapModule::home()
{
    stepper.homeModule();
}


void SplitFlapModule::moveToIndex(int index)
{
    if (index < 0 || index >= 64) return;
    if (stepper.totalStepsPerRev <= 0) return;

    uint16_t targetPosition = getMapPosition(index);
    if (targetPosition == 0xFFFF || targetPosition >= stepper.totalStepsPerRev) {
        // Fallback for an unset entry: flap 0 is at homeOffset, and the drum is
        // evenly spaced in the hall-edge coordinate system.
        targetPosition = (uint16_t)(
            ((long)stepper.homeOffset +
             ((long)index * stepper.totalStepsPerRev + 32) / 64) %
            stepper.totalStepsPerRev);
    }

    moveToStep(targetPosition);
    if (stepper.positionKnown) stepper.currentFlapIndex = index;
}


void SplitFlapModule::moveToStep(int steps)
{
    if (stepper.totalStepsPerRev <= 0) return;
    if (steps < 0 || steps >= stepper.totalStepsPerRev) return;

    // A move requested before the first successful home must establish the
    // hall reference first; otherwise the distance below is meaningless.
    if (!stepper.positionKnown) {
        stepper.homeModule();
        if (!stepper.positionKnown) return;
    }

    long distance = (long)steps - stepper.currentStepPos;
    // Always move forward, wrapping around the reel
    if (distance < 0)distance += stepper.totalStepsPerRev;

    // Mid-move re-referencing: when the forward path crosses the hall magnet
    // (or comes within HALL_WATCH_TOLERANCE steps of it, half a flap), watch
    // for the entering edge.  On the edge the position is zeroed exactly like
    // a home and the remaining distance is recomputed from that true
    // position, so this move still lands exactly on target even if steps were
    // missed earlier.  Moves that never pass the magnet stay open loop.
    bool watchHall = (long)stepper.currentStepPos + distance +
                        Motor::HALL_WATCH_TOLERANCE >=
                    (long)stepper.totalStepsPerRev;
    if (watchHall) stepper.beginHallWatch();

    while (distance > 0) {
        stepper.stepBackward(1, watchHall);
        distance--;
        if (watchHall && stepper.hallReferenced) {
            // The entering edge re-referenced the position mid-move; the
            // distance computed from the stale position is now meaningless.
            distance = (long)steps - stepper.currentStepPos;
            if (distance < 0) {
                // The target is the entering edge itself (pos 0) and this
                // step just crossed it, so an exact landing is impossible at
                // half-step granularity. Accept the crossing instead of
                // wrapping a whole revolution, which would cross the edge
                // again and never terminate.
                distance = 0;
            }
        }
    }

    stepper.releaseMotor();
    // currentStepPos is already the landing spot: it was incremented by the
    // move loop and, when the hall edge was crossed, re-referenced to the true
    // position.  Overwriting it with the requested target would discard that
    // correction.
}

void SplitFlapModule::moveToFlap(char flap)
{
    for (int i = 0; i < 64; i++) {
        if (flapChars[i] == flap) {
            moveToIndex(i);
            return;
        }
    }
}