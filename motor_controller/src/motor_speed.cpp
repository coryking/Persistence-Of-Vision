#include "motor_speed.h"
#include "hardware_config.h"

static bool motorEnabled = false;
static int speedPreset = 1;

static uint8_t positionToPWM(int pos) {
    float percent = PWM_MIN_PERCENT +
                    ((float)(pos - 1) / 9.0f) *
                    (PWM_MAX_PERCENT - PWM_MIN_PERCENT);
    return (uint8_t)((percent / 100.0f) * PWM_MAX_VALUE);
}

void motorSpeedInit() {
    motorEnabled = false;
    speedPreset = 1;
}

void togglePower() {
    motorEnabled = !motorEnabled;
    if (motorEnabled) {
        speedPreset = 1;
    }
}

bool motorPowerOn() {
    if (motorEnabled) {
        return false;  // Already on, no state change
    }
    motorEnabled = true;
    speedPreset = 1;
    return true;
}

bool motorPowerOff() {
    if (!motorEnabled) {
        return false;  // Already off, no state change
    }
    motorEnabled = false;
    return true;
}

void speedUp() {
    if (speedPreset < SPEED_MAX_POS) {
        speedPreset++;
    }
}

void speedDown() {
    if (speedPreset > SPEED_MIN_POS) {
        speedPreset--;
    }
}

uint8_t getCurrentPWM() {
    if (!motorEnabled) {
        return 0;
    }
    return positionToPWM(speedPreset);
}

bool isEnabled() {
    return motorEnabled;
}

int getSpeedPreset() {
    return speedPreset;
}
