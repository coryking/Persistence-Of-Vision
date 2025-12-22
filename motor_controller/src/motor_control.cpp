#include "motor_control.h"
#include "hardware_config.h"

void motorInit() {
    // Setup motor driver pins
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_ENA, OUTPUT);

    // Set direction (forward)
    digitalWrite(PIN_MOTOR_IN1, HIGH);
    digitalWrite(PIN_MOTOR_IN2, LOW);

    // Configure PWM on enable pin
    analogWriteFreq(PWM_FREQ_HZ);    // 25kHz
    analogWriteRange(PWM_MAX_VALUE);  // 8-bit (0-255)
    analogWrite(PIN_MOTOR_ENA, 0);    // Start stopped
}

void motorSetSpeed(uint8_t pwm) {
    analogWrite(PIN_MOTOR_ENA, pwm);
}

void motorStop() {
    analogWrite(PIN_MOTOR_ENA, 0);
}

// Convert encoder position (0-40) to PWM value (0-255)
uint8_t positionToPWM(int pos) {
    if (pos <= ENCODER_MIN_POS) return 0;  // OFF

    // Clamp to valid range
    if (pos > ENCODER_MAX_POS) pos = ENCODER_MAX_POS;

    // Linear map: position 1-40 → PWM_MIN-PWM_MAX
    float pwmPercent = PWM_MIN_PERCENT +
                       ((float)(pos - 1) / (ENCODER_MAX_POS - 1)) *
                       (PWM_MAX_PERCENT - PWM_MIN_PERCENT);

    return (uint8_t)((pwmPercent / 100.0f) * PWM_MAX_VALUE);
}
