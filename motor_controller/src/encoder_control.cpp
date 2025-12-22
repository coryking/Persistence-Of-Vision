#include "encoder_control.h"
#include "hardware_config.h"
#include <Arduino.h>
#include <RotaryEncoder.h>

// Encoder instance
static RotaryEncoder encoder(PIN_ENCODER_CLK, PIN_ENCODER_DT, RotaryEncoder::LatchMode::FOUR3);

// Position tracking
static int currentPosition = 0;
static int lastPosition = 0;

// Button debouncing
static bool lastButtonState = HIGH;  // Pullup, so HIGH when not pressed
static unsigned long lastDebounceTime = 0;
static const unsigned long DEBOUNCE_DELAY = 50;  // ms

void encoderInit() {
    pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
}

void encoderLoop() {
    encoder.tick();  // Poll encoder hardware

    // Read and clamp position
    currentPosition = encoder.getPosition();
    if (currentPosition < ENCODER_MIN_POS) {
        currentPosition = ENCODER_MIN_POS;
        encoder.setPosition(currentPosition);
    }
    if (currentPosition > ENCODER_MAX_POS) {
        currentPosition = ENCODER_MAX_POS;
        encoder.setPosition(currentPosition);
    }
}

int encoderGetPosition() {
    return currentPosition;
}

void encoderSetPosition(int pos) {
    currentPosition = pos;
    lastPosition = pos;
    encoder.setPosition(pos);
}

bool encoderPositionChanged() {
    if (currentPosition != lastPosition) {
        lastPosition = currentPosition;
        return true;
    }
    return false;
}

bool encoderButtonPressed() {
    bool currentButtonState = digitalRead(PIN_ENCODER_SW);

    // Detect falling edge (button pressed)
    if (currentButtonState == LOW && lastButtonState == HIGH) {
        if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
            lastDebounceTime = millis();
            lastButtonState = currentButtonState;
            return true;
        }
    }

    lastButtonState = currentButtonState;
    return false;
}
