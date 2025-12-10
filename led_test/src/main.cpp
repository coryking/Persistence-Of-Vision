#include <Arduino.h>
#include <FastLED.h>

// Hardware Configuration
// Seeed XIAO ESP32S3: D8=GPIO7 (data), D10=GPIO9 (clock)
// ESP32-S3-Zero: GPIO5 (data), GPIO4 (clock), GPIO13 (hall)
#define NUM_LEDS 33
#define DATA_PIN D7    //  (orange wire)
#define CLOCK_PIN D9   // (green wire)
#define HALL_PIN D10    // (yellow wire)

// SK9822/APA102 with BGR color order
CRGB leds[NUM_LEDS];

// Hall sensor ISR state
volatile uint8_t currentColor = 0;  // 0=red, 1=green, 2=blue

// ISR for hall effect sensor
void IRAM_ATTR hallISR() {
    currentColor = (currentColor + 1) % 3;
}

void setup() {
    // Initialize FastLED for SK9822/APA102
    FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, NUM_LEDS);
    FastLED.setBrightness(50);  // Dim for USB power

    // Clear all LEDs
    FastLED.clear();
    FastLED.show();

    // Configure hall sensor pin and interrupt
    pinMode(HALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallISR, FALLING);
}

void loop() {
    // Cycle through each LED one at a time
    for (int i = 0; i < NUM_LEDS; i++) {
        // Clear all LEDs
        FastLED.clear();

        // Choose color based on currentColor (updated by ISR)
        CRGB color;
        switch (currentColor) {
            case 0:
                color = CRGB::Red;
                break;
            case 1:
                color = CRGB::Green;
                break;
            case 2:
                color = CRGB::Blue;
                break;
        }

        // Light up current LED with current color
        leds[i] = color;
        FastLED.show();

        delay(500);  // Half second per LED
    }
}
