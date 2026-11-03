#include "ModuleHandler.h"
#include <Arduino.h>
#include "RS485Handler.h"
#include <string.h>
#include <stdlib.h>

void SplitFlapModule::Calibrate()
{
    stepper.countPerRev();
    saveTotalSteps();
    stepper.homeModule();
}
void SplitFlapModule::home()
{
    stepper.homeModule();
}


void SplitFlapModule::moveToIndex(int index)
{
    if (index < 0 || index >= 64) return;
    if (stepper.currentFlapIndex == index) return;

    uint16_t targetPosition = getMapPosition(index);
    if (targetPosition == 0xFFFF) {
        targetPosition =((long)index * stepper.totalStepsPerRev) / 64;
    }

    moveToStep(targetPosition);
    stepper.currentFlapIndex = index;
}


void SplitFlapModule::moveToStep(int steps)
{
    if (stepper.totalStepsPerRev <= 0) return;
    if (steps < 0 || steps >= stepper.totalStepsPerRev) return;


    long distance = (long)steps - stepper.currentStepPos;
    // Always move forward, wrapping around the reel
    if (distance < 0)distance += stepper.totalStepsPerRev;
    while (distance > 0) {
        stepper.stepBackward(1);
        distance--;
    }

    stepper.releaseMotor();
    stepper.currentStepPos = steps;
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