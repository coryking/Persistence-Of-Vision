#ifndef MOTOR_SPEED_H
#define MOTOR_SPEED_H

#include <stdint.h>

void motorSpeedInit();

void togglePower();
void speedUp();
void speedDown();

uint8_t getCurrentPWM();
bool isEnabled();
int getPosition();

#endif // MOTOR_SPEED_H
