#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>

void motorInit();
void motorSetSpeed(uint8_t pwm);
void motorStop();

#endif
