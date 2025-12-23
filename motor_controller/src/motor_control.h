#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>

enum class MotorState {
    RUNNING,
    BRAKING,
    STOPPED
};

void motorInit();
void motorSetSpeed(uint8_t pwm);
void motorStop();
void motorLoop();
MotorState getMotorState();

#endif
