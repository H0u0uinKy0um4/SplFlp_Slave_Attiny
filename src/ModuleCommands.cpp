#include "ModuleHandler.h"

// The module's command methods are thin delegations: all coordinate, homing
// and hall-watch decisions live in MotionPlanner.

void SplitFlapModule::Calibrate()
{
    motion_.calibrate();
}

void SplitFlapModule::home()
{
    motion_.home();
}

void SplitFlapModule::moveToFlap(char flap)
{
    motion_.moveToFlap(flap);
}

void SplitFlapModule::moveToIndex(int index)
{
    motion_.moveToIndex(index);
}

void SplitFlapModule::moveToStep(int steps)
{
    motion_.moveToStep(steps);
}

// --- CalibrationPort -------------------------------------------------------

uint16_t SplitFlapModule::homeOffset() const
{
    return stepper.homeOffset;
}

uint16_t SplitFlapModule::totalStepsPerRev() const
{
    return stepper.totalStepsPerRev;
}

uint16_t SplitFlapModule::mapAt(int index) const
{
    return getMapPosition(index);
}

char SplitFlapModule::flapAt(int index) const
{
    if (index < 0 || index >= DRUM_SIZE) return '\0';
    return flapChars[index];
}

void SplitFlapModule::storeTotalSteps(uint16_t steps)
{
    stepper.totalStepsPerRev = steps;
    saveTotalSteps(); // EEPROM via the existing store
}

// --- MotionPort ------------------------------------------------------------

uint16_t SplitFlapModule::position() const
{
    return stepper.currentStepPos;
}

void SplitFlapModule::setPosition(uint16_t pos)
{
    stepper.currentStepPos = pos;
}

bool SplitFlapModule::positionKnown() const
{
    return stepper.positionKnown;
}

void SplitFlapModule::setPositionKnown(bool known)
{
    stepper.positionKnown = known;
}

int SplitFlapModule::currentFlapIndex() const
{
    return stepper.currentFlapIndex;
}

void SplitFlapModule::setCurrentFlapIndex(int index)
{
    stepper.currentFlapIndex = index;
}

void SplitFlapModule::homeMotor()
{
    stepper.homeModule();
}

void SplitFlapModule::releaseMotor()
{
    stepper.releaseMotor();
}

void SplitFlapModule::beginHallWatch()
{
    stepper.beginHallWatch();
}

bool SplitFlapModule::hallReferenced() const
{
    return stepper.hallReferenced;
}

void SplitFlapModule::stepOne(bool watchHall)
{
    stepper.stepBackward(1, watchHall);
}

long SplitFlapModule::countPerRev()
{
    return stepper.countPerRev();
}
