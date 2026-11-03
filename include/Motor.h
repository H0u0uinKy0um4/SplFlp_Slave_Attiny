
#pragma once
#include "Motor.h"
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
    void stepBackward(int steps);
    long countPerRev();
    void homeModule();
    bool debugMagnet();

    uint16_t currentStepPos = 0;   // Current motor position in half-steps (0 = flap 0)
    int currentPhase = 0;      // Current index into halfStepSequence[8]
    int currentFlapIndex = -1; // Which flap is currently showing (-1 = unknown)

    uint16_t homeOffset = 2832; // Steps from Hall sensor trigger edge → flap 0 position
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
};
