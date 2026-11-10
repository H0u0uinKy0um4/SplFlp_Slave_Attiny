
#pragma once
#include <Arduino.h>

// Stepper motor coil drive pins (connected to ULN2003 or similar driver board)
#define IN1 9
#define IN2 8
#define IN3 7
#define IN4 6

#define HALL_PIN 4

class Motor
{
public:
    Motor();
    void releaseMotor();
    bool hallActive();
    void applyStep(const uint8_t *step);
    // watchHall re-references currentStepPos on the hall entering edge, so the
    // caller must only enable it for a move whose forward path crosses the
    // magnet (see SplitFlapModule::moveToStep).  Call beginHallWatch() right
    // before the first watched step so a move that starts on the magnet cannot
    // produce a false entering edge.
    void stepBackward(int steps, bool watchHall = false);
    void beginHallWatch();
    long countPerRev();
    void homeModule();

    // Set to true when a watched move re-referenced currentStepPos on the hall
    // entering edge.  Cleared by beginHallWatch(), so it is meaningful only
    // while that move is running.  The caller uses it to recompute the
    // remaining distance from the true position.
    bool hallReferenced = false;

    // A watched move re-references its position whenever the forward path
    // passes within this many steps of pos 0, so small drift cannot hide the
    // crossing.  Half a flap on the stock 4096-step drum.
    static constexpr int HALL_WATCH_TOLERANCE = 32;

    // Position is measured from the hall entering edge (pos 0).  Flap 0 sits at
    // homeOffset, so after homing currentStepPos == homeOffset.
    uint16_t currentStepPos = 0;   // Current motor position in half-steps
    int currentPhase = 0;      // Current index into halfStepSequence[8]
    int currentFlapIndex = -1; // Which flap is currently showing (-1 = unknown)
    bool positionKnown = false; // False until a successful home establishes pos 0

    uint16_t homeOffset = 2832; // Steps from Hall sensor entering edge → flap 0 position
    uint16_t totalStepsPerRev = 4096;    // Total half-steps in one full reel revolution

    static constexpr int stepDelay = 1;

    static constexpr uint8_t halfStepSequence[8][4] = {
        {1, 0, 0, 0}, // Phase 0: coil A only
        {1, 1, 0, 0}, // Phase 1: coils A+B
        {0, 1, 0, 0}, // Phase 2: coil B only
        {0, 1, 1, 0}, // Phase 3: coils B+C
        {0, 0, 1, 0}, // Phase 4: coil C only
        {0, 0, 1, 1}, // Phase 5: coils C+D
        {0, 0, 0, 1}, // Phase 6: coil D only
        {1, 0, 0, 1}  // Phase 7: coils D+A
    };

private:
    // Last hall sample while a watched move is running.  Only meaningful
    // between beginHallWatch() and the end of that move.
    bool hallWatchLast = false;
};
