#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "types.h"
#include "RevolutionTimer.h"

// Hardware Configuration
#define NUM_LEDS 30
#define LEDS_PER_ARM 10
#define HALL_PIN D1

// Physical arm layout (from center outward): Inner, Middle, Outer
// LED index mapping (based on wiring observation)
#define INNER_ARM_START 10    // LEDs 10-19 (orange)
#define MIDDLE_ARM_START 0    // LEDs 0-9 (red) - triggers hall sensor
#define OUTER_ARM_START 20    // LEDs 20-29 (green)

// Phase offsets (degrees) - middle arm triggers hall sensor at 0°
#define MIDDLE_ARM_PHASE 0
#define INNER_ARM_PHASE 120
#define OUTER_ARM_PHASE 240

// POV Display Configuration
#define DEGREES_PER_ARC 4           // 4-degree line width
#define ARC_START_ANGLE 0           // Line starts at 0 degrees (hall trigger position)
#define ARC_GROWTH_INTERVAL_MS 50  // Milliseconds between each degree change (tune: 100-500ms)
#define WARMUP_REVOLUTIONS 20       // Number of revolutions before display starts
#define ROLLING_AVERAGE_SIZE 20     // Smoothing window size
#define ROTATION_TIMEOUT_US 2000000 // 2 seconds timeout to detect stopped rotation

// LED Configuration
#define LED_LUMINANCE 127 // 0-255

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
// Uses GPIO 7 (data) and GPIO 9 (clock) via hardware SPI
// Using 40MHz SPI (fastest for this LED count)
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// ISR state
volatile bool newRevolutionDetected = false;
volatile timestamp_t isrTimestamp = 0;

// Arm colors (solid RGB for visibility)
const RgbColor INNER_ARM_COLOR(255, 0, 0);    // Red (inner)
const RgbColor MIDDLE_ARM_COLOR(0, 255, 0);   // Green (middle)
const RgbColor OUTER_ARM_COLOR(0, 0, 255);    // Blue (outer)
const RgbColor OFF_COLOR(0, 0, 0);

// Arc animation state
int currentArcDegrees = DEGREES_PER_ARC;  // Start at 4 degrees
bool arcGrowing = true;                    // true = growing, false = shrinking
timestamp_t lastArcUpdateTime = 0;         // Track last animation update
const interval_t ARC_UPDATE_INTERVAL = ARC_GROWTH_INTERVAL_MS * 1000;  // Convert ms to microseconds
const int MIN_ARC_DEGREES = 4;
const int MAX_ARC_DEGREES = 180;

/**
 * ISR handler for hall effect sensor
 * Captures timestamp immediately and sets flag for processing in main loop
 */
void IRAM_ATTR hallSensorISR(void* arg) {
    isrTimestamp = esp_timer_get_time();
    newRevolutionDetected = true;
}

void setup()
{
    Serial.begin(115200);
    delay(2000); // Give serial time to initialize
    Serial.println("POV Display Initializing...");

    // Initialize LED strip
    Serial.println("Initializing LED strip...");
    strip.Begin();
    strip.SetLuminance(LED_LUMINANCE);  // 25% brightness
    strip.ClearTo(OFF_COLOR);           // Clear strip to black
    strip.Show();
    Serial.println("Strip initialized");

    // Setup hall effect sensor GPIO and ISR
    Serial.println("Setting up hall effect sensor...");
    gpio_num_t hallPin = static_cast<gpio_num_t>(HALL_PIN);

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;          // Triggers on FALLING EDGE
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << HALL_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;        // Pull-up enabled
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(hallPin, hallSensorISR, nullptr);
    Serial.println("Hall effect sensor initialized on D1");

    Serial.println("\n=== POV Display Ready ===");
    Serial.println("Configuration:");
    Serial.printf("  Arms: 3 (Inner:%d°, Middle:%d°, Outer:%d°)\n",
                  INNER_ARM_PHASE, MIDDLE_ARM_PHASE, OUTER_ARM_PHASE);
    Serial.printf("  LEDs per arm: %d\n", LEDS_PER_ARM);
    Serial.printf("  Arc width: %d degrees\n", DEGREES_PER_ARC);
    Serial.printf("  Arc start: %d degrees\n", ARC_START_ANGLE);
    Serial.printf("  Warm-up revolutions: %d\n", WARMUP_REVOLUTIONS);
    Serial.println("\nWaiting for rotation to start...");
    Serial.println("(LEDs will remain off during warm-up period)\n");
}

void loop()
{
    // Process new revolution detection from ISR
    if (newRevolutionDetected) {
        newRevolutionDetected = false;
        revTimer.addTimestamp(isrTimestamp);

        // Optional: Print status after warm-up complete
        if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
            Serial.println("Warm-up complete! Display active.");
        }
    }

    // Only display if warm-up complete and currently rotating
    if (revTimer.isWarmupComplete() && revTimer.isCurrentlyRotating()) {
        // Get current time and timing parameters
        timestamp_t now = esp_timer_get_time();
        timestamp_t lastHallTime = revTimer.getLastTimestamp();
        interval_t microsecondsPerRev = revTimer.getMicrosecondsPerRevolution();

        // Calculate elapsed time since hall trigger (middle arm at 0°)
        timestamp_t elapsed = now - lastHallTime;

        // Calculate microseconds per degree
        double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

        // Calculate middle arm's current angle (0-359) - it triggers the hall sensor
        double angleMiddle = fmod(static_cast<double>(elapsed) / microsecondsPerDegree, 360.0);

        // Calculate inner and outer arm angles (phase offset from middle)
        double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
        double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);

        // Check if each arm is within the display arc
        double arcEnd = ARC_START_ANGLE + currentArcDegrees;
        bool innerInArc = (angleInner >= ARC_START_ANGLE && angleInner < arcEnd);
        bool middleInArc = (angleMiddle >= ARC_START_ANGLE && angleMiddle < arcEnd);
        bool outerInArc = (angleOuter >= ARC_START_ANGLE && angleOuter < arcEnd);

        // Update arc size at configured interval
        if (now - lastArcUpdateTime >= ARC_UPDATE_INTERVAL) {
            if (arcGrowing) {
                currentArcDegrees++;
                if (currentArcDegrees >= MAX_ARC_DEGREES) {
                    arcGrowing = false;  // Start shrinking
                }
            } else {
                currentArcDegrees--;
                if (currentArcDegrees <= MIN_ARC_DEGREES) {
                    arcGrowing = true;  // Start growing
                }
            }
            lastArcUpdateTime = now;
        }

        // Set LEDs for each arm (physical order from center outward)
        // Inner arm: LEDs 10-19 (orange)
        for (uint16_t i = 0; i < LEDS_PER_ARM; i++) {
            strip.SetPixelColor(INNER_ARM_START + i, innerInArc ? INNER_ARM_COLOR : OFF_COLOR);
        }

        // Middle arm: LEDs 0-9 (red)
        for (uint16_t i = 0; i < LEDS_PER_ARM; i++) {
            strip.SetPixelColor(MIDDLE_ARM_START + i, middleInArc ? MIDDLE_ARM_COLOR : OFF_COLOR);
        }

        // Outer arm: LEDs 20-29 (green)
        for (uint16_t i = 0; i < LEDS_PER_ARM; i++) {
            strip.SetPixelColor(OUTER_ARM_START + i, outerInArc ? OUTER_ARM_COLOR : OFF_COLOR);
        }

        // Update the strip
        strip.Show();
    } else {
        // Not ready yet - keep all LEDs off
        strip.ClearTo(OFF_COLOR);
        strip.Show();

        // Small delay to avoid tight loop during warm-up
        delay(10);
    }

    // No delay in main loop when running - timing is critical!
    // The tight loop allows us to catch the precise moment to turn LEDs on/off
}
