#include "motor_control.h"
#include "hardware_config.h"

// ESP32 PWM resolution (8-bit = 0-255)
#define PWM_RESOLUTION 8

void motorInit() {
    // Setup motor driver pins
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);

    // Set direction (forward)
    digitalWrite(PIN_MOTOR_IN1, HIGH);
    digitalWrite(PIN_MOTOR_IN2, LOW);

    // Configure LEDC PWM on enable pin (ESP32 API)
    ledcAttach(PIN_MOTOR_ENA, PWM_FREQ_HZ, PWM_RESOLUTION);
    ledcWrite(PIN_MOTOR_ENA, 0);  // Start stopped
}

void motorSetSpeed(uint8_t pwm) {
    ledcWrite(PIN_MOTOR_ENA, pwm);
}

void motorStop() {
    ledcWrite(PIN_MOTOR_ENA, 0);
}
