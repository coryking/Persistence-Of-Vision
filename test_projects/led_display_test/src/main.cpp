/**
 * LED Display Test - Simple Hardware Validation
 *
 * Purpose: Test LED strip and hall effect sensor before using full POV firmware
 *
 * Behavior:
 * - Cycles through all LEDs (0 -> NUM_LEDS) with moving dot pattern
 * - Hall effect sensor triggers color changes (Red -> Green -> Blue -> White)
 * - Serial output shows hall triggers and color values
 *
 */

#include <Arduino.h>
#include <NeoPixelBus.h>

// Pin Definitions
#define LED_DATA D10     // Blue wire - SPI Data (MOSI)
#define LED_CLOCK D8     // Purple wire - SPI Clock (SCK)
#define HALL_PIN D5      // Brown wire - Hall effect sensor

// LED Configuration
#define NUM_LEDS 33     // 11 LEDs per arm × 3 arms
#define CYCLE_DELAY 100 // ms delay between LED updates (adjust for speed)

// NeoPixelBus setup for SK9822/APA102 (DotStar)
// Using BGR color order and hardware SPI
NeoPixelBus<DotStarBgrFeature, DotStarSpiMethod> strip(NUM_LEDS, LED_CLOCK, LED_DATA);

// State variables
volatile bool hallTriggered = false;
volatile uint32_t lastHallTriggerTime = 0;
const uint32_t DEBOUNCE_MS = 200; // Debounce time for hall sensor

uint16_t currentLed = 0;
uint8_t currentColorIndex = 0;

// Color palette (RGB)
const RgbColor colors[] = {
    RgbColor(255, 0, 0),   // Red
    RgbColor(0, 255, 0),   // Green
    RgbColor(0, 0, 255),   // Blue
    RgbColor(255, 255, 255) // White
};
const uint8_t NUM_COLORS = sizeof(colors) / sizeof(colors[0]);

RgbColor currentColor = colors[0]; // Start with Red

/**
 * Hall Effect Sensor ISR
 * Triggers on falling edge (magnet detected)
 */
void IRAM_ATTR hallISR() {
    uint32_t now = millis();

    // Simple debouncing - ignore triggers within DEBOUNCE_MS
    if (now - lastHallTriggerTime > DEBOUNCE_MS) {
        hallTriggered = true;
        lastHallTriggerTime = now;
    }
}

void setup() {
    // Initialize Serial
    Serial.begin(115200);
    delay(2000); // Wait for serial connection

    Serial.println("========================================");
    Serial.println("LED Display Test - Hardware Validation");
    Serial.println("========================================");
    Serial.printf("NUM_LEDS: %d\n", NUM_LEDS);
    Serial.printf("Pin Config: Data=%d, Clock=%d, Hall=%d\n", LED_DATA, LED_CLOCK, HALL_PIN);
    Serial.println("----------------------------------------");

    // Initialize LED strip
    strip.Begin();
    strip.Show(); // Initialize all LEDs to off

    Serial.println("NeoPixelBus initialized (SK9822/APA102)");

    // Configure hall effect sensor
    pinMode(HALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallISR, FALLING);

    Serial.printf("Hall effect sensor configured (GPIO %d, FALLING edge)\n", HALL_PIN);
    Serial.println("----------------------------------------");
    Serial.printf("Starting LED cycle with color: Red [%d,%d,%d]\n",
                  currentColor.R, currentColor.G, currentColor.B);
    Serial.println("Pass magnet over hall sensor to change colors!");
    Serial.println("========================================\n");
}

void loop() {
    // Check if hall sensor was triggered
    if (hallTriggered) {
        hallTriggered = false;

        // Cycle to next color
        currentColorIndex = (currentColorIndex + 1) % NUM_COLORS;
        currentColor = colors[currentColorIndex];

        // Print color change to serial
        const char* colorNames[] = {"Red", "Green", "Blue", "White"};
        Serial.printf("\n*** Hall triggered! New color: %s [%d,%d,%d] ***\n\n",
                      colorNames[currentColorIndex],
                      currentColor.R, currentColor.G, currentColor.B);
    }

    // Clear all LEDs
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, RgbColor(0, 0, 0)); // Off
    }

    // Turn on current LED with current color
    strip.SetPixelColor(currentLed, currentColor);

    // Update LED strip
    strip.Show();

    // Move to next LED (wrap around at NUM_LEDS)
    currentLed = (currentLed + 1) % NUM_LEDS;

    // Delay for visible cycling
    delay(CYCLE_DELAY);
}
