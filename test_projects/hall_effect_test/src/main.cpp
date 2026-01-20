/**
 * LED Display Test - Simple Hardware Validation
 *
 * Purpose: Test LED strip and hall effect sensor before using full POV firmware
 *
 * Hardware: 41 LEDs total
 * - LED 0: Level shifter (3.3V→5V, always dark)
 * - ARM1 (inside): 13 LEDs, physical 1-13, normal wiring
 * - ARM2 (middle): 13 LEDs, physical 14-26, normal wiring
 * - ARM3 (outside): 14 LEDs, physical 27-40, REVERSED wiring
 *
 * Behavior:
 * - Walks LEDs: ARM1 inside→outside, ARM2 inside→outside, ARM3 inside→outside
 * - Hall effect sensor triggers color changes (Red -> Green -> Blue -> White)
 * - Serial output shows hall triggers and color values
 */

#include <Arduino.h>
#include <NeoPixelBus.h>

// Pin Definitions
#define LED_DATA D10     // Blue wire - SPI Data (MOSI)
#define LED_CLOCK D8     // Purple wire - SPI Clock (SCK)
#define HALL_PIN D5      // Brown wire - Hall effect sensor

// LED Configuration
#define NUM_LEDS 41     // 1 level shifter + 13 ARM1 + 13 ARM2 + 14 ARM3
#define CYCLE_DELAY 100 // ms delay between LED updates (adjust for speed)

// Walk order: ARM1 inside→out, ARM2 inside→out, ARM3 inside→out
// LED0 (level shifter) excluded - always dark
// ARM3 is wired outside→inside, so we reverse it to walk inside→out
const uint16_t WALK_ORDER[] = {
    // ARM1 (inside): physical 1-13, normal wiring
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    // ARM2 (middle): physical 14-26, normal wiring
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    // ARM3 (outside): physical 27-40, REVERSED wiring (so walk 40→27)
    40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27
};
const uint16_t WALK_LENGTH = sizeof(WALK_ORDER) / sizeof(WALK_ORDER[0]);

// NeoPixelBus setup for HD107/APA102 (DotStar compatible)
// Using BGR color order and hardware SPI
NeoPixelBus<DotStarBgrFeature, DotStarSpiMethod> strip(NUM_LEDS, LED_CLOCK, LED_DATA);

// State variables
volatile bool hallTriggered = false;
volatile uint32_t lastHallTriggerTime = 0;
const uint32_t DEBOUNCE_MS = 200; // Debounce time for hall sensor

uint16_t walkIndex = 0;
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
    Serial.printf("Total LEDs: %d (1 level shifter + 13 + 13 + 14)\n", NUM_LEDS);
    Serial.printf("Walk length: %d LEDs\n", WALK_LENGTH);
    Serial.printf("Pin Config: Data=%d, Clock=%d, Hall=%d\n", LED_DATA, LED_CLOCK, HALL_PIN);
    Serial.println("----------------------------------------");
    Serial.println("ARM1 (inside):  13 LEDs, physical 1-13");
    Serial.println("ARM2 (middle):  13 LEDs, physical 14-26");
    Serial.println("ARM3 (outside): 14 LEDs, physical 27-40 (reversed)");
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

    // Clear all LEDs (including level shifter at 0)
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, RgbColor(0, 0, 0));
    }

    // Turn on current LED with current color
    uint16_t physicalLed = WALK_ORDER[walkIndex];
    strip.SetPixelColor(physicalLed, currentColor);

    // Update LED strip
    strip.Show();

    // Move to next LED in walk order
    walkIndex = (walkIndex + 1) % WALK_LENGTH;

    // Delay for visible cycling
    delay(CYCLE_DELAY);
}
