#include "motor_control.h"
#include "hardware_config.h"

// ESP32 PWM resolution (8-bit = 0-255)
#define PWM_RESOLUTION 8

// State machine variables
static MotorState motorState = MotorState::STOPPED;
static unsigned long brakeStartTime = 0;

// Active brake: short motor terminals through L298N for fast stop
static void motorBrake() {
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
    ledcWrite(PIN_MOTOR_ENA, 255);  // Full power for maximum braking force
}

// Coast mode: disconnect motor terminals
static void motorCoast() {
    ledcWrite(PIN_MOTOR_ENA, 0);
}

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

    motorState = MotorState::STOPPED;
}

void motorSetSpeed(uint8_t pwm) {
    if (pwm > 0) {
        // Transition to RUNNING state
        if (motorState != MotorState::RUNNING) {
            // Restore forward direction (in case we were braking)
            digitalWrite(PIN_MOTOR_IN1, HIGH);
            digitalWrite(PIN_MOTOR_IN2, LOW);
            motorState = MotorState::RUNNING;
        }
        ledcWrite(PIN_MOTOR_ENA, pwm);
    } else {
        // Request to stop - engage brake if currently running
        if (motorState == MotorState::RUNNING) {
            motorBrake();
            motorState = MotorState::BRAKING;
            brakeStartTime = millis();
        }
        // If already BRAKING or STOPPED, do nothing
    }
}

void motorLoop() {
    // Check if brake timer has expired
    if (motorState == MotorState::BRAKING) {
        if (millis() - brakeStartTime >= BRAKE_DURATION_MS) {
            motorCoast();
            motorState = MotorState::STOPPED;
        }
    }
}

MotorState getMotorState() {
    return motorState;
}

void motorStop() {
    ledcWrite(PIN_MOTOR_ENA, 0);
}
