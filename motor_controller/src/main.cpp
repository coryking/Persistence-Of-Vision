#include <Arduino.h>
#include "motor_control.h"
#include "encoder_control.h"
#include "led_indicator.h"
#include "hardware_config.h"

// Motor state
bool motorEnabled = false;

void setup() {
    Serial.begin(115200);
    Serial.println("POV Motor Controller - RP2040");

    motorInit();
    encoderInit();
    ledInit();

    ledShowStopped();  // Start in stopped mode
    Serial.println("Ready. Press button to start motor, turn encoder to control speed.");
}

void loop() {
    // Update subsystems
    encoderLoop();
    ledLoop();

    // Handle button press
    if (encoderButtonPressed()) {
        motorEnabled = !motorEnabled;

        if (motorEnabled) {
            ledShowRunning();
            encoderSetPosition(1);  // Start at minimum speed
            motorSetSpeed(positionToPWM(encoderGetPosition()));
            Serial.println("Motor ON");
        } else {
            ledShowStopped();
            motorStop();
            Serial.println("Motor OFF");
        }
    }

    // Update motor speed if position changed
    if (motorEnabled && encoderPositionChanged()) {
        int pos = encoderGetPosition();
        uint8_t pwm = positionToPWM(pos);
        motorSetSpeed(pwm);

        // Log status (application logic, not motor module concern)
        float pwmPercent = (pwm / 255.0f) * 100.0f;
        float rpm = -8170.97 + 205.2253*pwmPercent - 0.809611*pwmPercent*pwmPercent;
        Serial.printf("Pos: %d, PWM: %d (%.1f%%), Est. RPM: %.0f\n",
                      pos, pwm, pwmPercent, rpm);
    }

    delay(10);
}
