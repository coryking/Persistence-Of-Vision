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

// Arm animation structure - each arm has independent animation
struct ArmAnimation {
    // Position (where the arc is)
    float currentStartAngle;      // 0-360°, current position
    float driftVelocity;          // rad/sec frequency for sine wave drift
    float wanderCenter;           // center point for wandering
    float wanderRange;            // +/- range from center

    // Size (how big the arc is)
    float currentArcSize;         // current size in degrees
    float minArcSize;             // minimum wedge size
    float maxArcSize;             // maximum wedge size
    float sizeChangeRate;         // frequency for size oscillation

    // Timing
    timestamp_t lastUpdate;       // for delta-time calculations
};

// Arm indices
#define ARM_INNER  0
#define ARM_MIDDLE 1
#define ARM_OUTER  2

// One animation state per arm
ArmAnimation arms[3];

/**
 * ISR handler for hall effect sensor
 * Captures timestamp immediately and sets flag for processing in main loop
 */
void IRAM_ATTR hallSensorISR(void* arg) {
    isrTimestamp = esp_timer_get_time();
    newRevolutionDetected = true;
}

/**
 * Update arm animation state using time-based sine waves
 */
void updateArmAnimation(ArmAnimation& arm, timestamp_t now) {
    float timeInSeconds = now / 1000000.0f;

    // Position drift: sine wave wandering around center point
    arm.currentStartAngle = arm.wanderCenter +
                            sin(timeInSeconds * arm.driftVelocity) * arm.wanderRange;
    arm.currentStartAngle = fmod(arm.currentStartAngle + 360.0f, 360.0f);

    // Size breathing: sine wave oscillation between min and max
    float sizePhase = timeInSeconds * arm.sizeChangeRate;
    arm.currentArcSize = arm.minArcSize +
                         (arm.maxArcSize - arm.minArcSize) *
                         (sin(sizePhase) * 0.5f + 0.5f);
}

/**
 * Check if angle is within arm's current arc (handles 360° wraparound)
 */
bool isAngleInArc(double angle, const ArmAnimation& arm) {
    double arcEnd = arm.currentStartAngle + arm.currentArcSize;

    // Handle wraparound (e.g., arc from 350° to 10°)
    if (arcEnd > 360.0f) {
        return (angle >= arm.currentStartAngle) || (angle < fmod(arcEnd, 360.0f));
    }

    return (angle >= arm.currentStartAngle) && (angle < arcEnd);
}

/**
 * Initialize arm animation configurations with different personalities
 */
void setupArmAnimations() {
    timestamp_t now = esp_timer_get_time();

    // Inner arm: small, fast bubble
    arms[ARM_INNER].wanderCenter = 0;
    arms[ARM_INNER].wanderRange = 60;        // wanders ±60°
    arms[ARM_INNER].driftVelocity = 0.5;     // rad/sec frequency
    arms[ARM_INNER].minArcSize = 5;
    arms[ARM_INNER].maxArcSize = 30;
    arms[ARM_INNER].sizeChangeRate = 0.3;
    arms[ARM_INNER].lastUpdate = now;

    // Middle arm: medium, medium speed
    arms[ARM_MIDDLE].wanderCenter = 120;
    arms[ARM_MIDDLE].wanderRange = 90;
    arms[ARM_MIDDLE].driftVelocity = 0.3;
    arms[ARM_MIDDLE].minArcSize = 10;
    arms[ARM_MIDDLE].maxArcSize = 60;
    arms[ARM_MIDDLE].sizeChangeRate = 0.2;
    arms[ARM_MIDDLE].lastUpdate = now;

    // Outer arm: large, slow blob
    arms[ARM_OUTER].wanderCenter = 240;
    arms[ARM_OUTER].wanderRange = 120;
    arms[ARM_OUTER].driftVelocity = 0.15;
    arms[ARM_OUTER].minArcSize = 20;
    arms[ARM_OUTER].maxArcSize = 90;
    arms[ARM_OUTER].sizeChangeRate = 0.1;
    arms[ARM_OUTER].lastUpdate = now;
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

    // Initialize arm animations
    Serial.println("Setting up arm animations...");
    setupArmAnimations();
    Serial.println("Arm animations configured");

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

        // Update each arm's independent animation state
        updateArmAnimation(arms[ARM_INNER], now);
        updateArmAnimation(arms[ARM_MIDDLE], now);
        updateArmAnimation(arms[ARM_OUTER], now);

        // Check each arm against its own arc
        bool innerInArc = isAngleInArc(angleInner, arms[ARM_INNER]);
        bool middleInArc = isAngleInArc(angleMiddle, arms[ARM_MIDDLE]);
        bool outerInArc = isAngleInArc(angleOuter, arms[ARM_OUTER]);

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
