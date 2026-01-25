#ifndef MOTOR_SPEED_H
#define MOTOR_SPEED_H

#include <stdint.h>

void motorSpeedInit();

void togglePower();
bool motorPowerOn();   // Idempotent - returns true if state changed (was off)
bool motorPowerOff();  // Idempotent - returns true if state changed (was on)
void speedUp();
void speedDown();

uint8_t getCurrentPWM();
bool isEnabled();
int getSpeedPreset();

#endif // MOTOR_SPEED_H
