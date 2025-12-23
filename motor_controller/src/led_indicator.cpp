#include "led_indicator.h"
#include "motor_speed.h"
#include "motor_control.h"
#include "hardware_config.h"
#include <Arduino.h>

// RGB color structure
struct RGBColor {
    uint8_t r, g, b;
};

// Internal state for blinking
static unsigned long lastBlinkTime = 0;
static bool blinkState = false;

// Helper to set RGB color (handles active-LOW polarity internally)
static void setRGB(uint8_t r, uint8_t g, uint8_t b) {
    analogWrite(PIN_LED_R, 255 - r);  // Invert for active-LOW
    analogWrite(PIN_LED_G, 255 - g);
    analogWrite(PIN_LED_B, 255 - b);
}

// Calculate "glowing steel" color gradient based on speed position
static RGBColor calculateRunningColor(int pos) {
    // Clamp position to valid range
    if (pos < SPEED_MIN_POS) pos = SPEED_MIN_POS;
    if (pos > SPEED_MAX_POS) pos = SPEED_MAX_POS;

    // Normalize position to 0.0-1.0 range
    float normalizedPos = (float)(pos - SPEED_MIN_POS) /
                          (float)(SPEED_MAX_POS - SPEED_MIN_POS);

    RGBColor color;
    color.r = 255;  // Always full red (glowing hot base)

    // Green ramps up linearly across full range (red→yellow)
    color.g = (uint8_t)(normalizedPos * 255);

    // Blue only starts ramping in upper 40% of range (yellow→white)
    if (normalizedPos > 0.6f) {
        float bluePos = (normalizedPos - 0.6f) / 0.4f;  // Remap 0.6-1.0 to 0-1
        color.b = (uint8_t)(bluePos * 255);
    } else {
        color.b = 0;
    }

    return color;
}

void ledInit() {
    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);

    // Start with LED off
    setRGB(0, 0, 0);
}

void ledLoop() {
    MotorState state = getMotorState();

    if (state == MotorState::STOPPED) {
        // Blink red at 500ms interval
        if (millis() - lastBlinkTime >= LED_BLINK_INTERVAL_MS) {
            lastBlinkTime = millis();
            blinkState = !blinkState;
            setRGB(blinkState ? 255 : 0, 0, 0);  // Red on/off
        }
    } else if (state == MotorState::BRAKING) {
        // Solid orange during active brake
        setRGB(255, 128, 0);
    } else {  // RUNNING
        // Solid color gradient based on speed position
        int pos = getPosition();
        RGBColor color = calculateRunningColor(pos);
        setRGB(color.r, color.g, color.b);
    }
}

void ledShowStopped() {
    lastBlinkTime = millis();  // Reset blink timer
    blinkState = false;
}

void ledShowRunning() {
    // No-op - state is now queried from motor controller
}
