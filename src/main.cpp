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

// Blob structure - independent lava lamp blobs with lifecycle
struct Blob {
    bool active;                  // Is this blob alive?
    uint8_t armIndex;             // Which arm (0-2) this blob belongs to
    RgbColor color;               // Blob's color

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

    // Lifecycle timing
    timestamp_t birthTime;        // When blob spawned
    timestamp_t deathTime;        // When blob will die (0 = immortal for now)
};

// Arm indices
#define ARM_INNER  0
#define ARM_MIDDLE 1
#define ARM_OUTER  2

// Blob pool - up to 5 total blobs across all arms
#define MAX_BLOBS 5
Blob blobs[MAX_BLOBS];

// Color palette: Citrus (oranges to greens)
// HSL: Hue (0-1), Saturation (0-1), Lightness (0-1)
HslColor citrusPalette[MAX_BLOBS] = {
    HslColor(0.08f, 1.0f, 0.5f),    // Orange (30°)
    HslColor(0.15f, 0.9f, 0.5f),    // Yellow-orange (55°)
    HslColor(0.25f, 0.85f, 0.5f),   // Yellow-green (90°)
    HslColor(0.33f, 0.9f, 0.45f),   // Green (120°)
    HslColor(0.40f, 0.85f, 0.4f)    // Blue-green (145°)
};

/**
 * ISR handler for hall effect sensor
 * Captures timestamp immediately and sets flag for processing in main loop
 */
void IRAM_ATTR hallSensorISR(void* arg) {
    isrTimestamp = esp_timer_get_time();
    newRevolutionDetected = true;
}

/**
 * Update blob animation state using time-based sine waves
 */
void updateBlob(Blob& blob, timestamp_t now) {
    if (!blob.active) return;

    float timeInSeconds = now / 1000000.0f;

    // Position drift: sine wave wandering around center point
    blob.currentStartAngle = blob.wanderCenter +
                             sin(timeInSeconds * blob.driftVelocity) * blob.wanderRange;
    blob.currentStartAngle = fmod(blob.currentStartAngle + 360.0f, 360.0f);

    // Size breathing: sine wave oscillation between min and max
    float sizePhase = timeInSeconds * blob.sizeChangeRate;
    blob.currentArcSize = blob.minArcSize +
                          (blob.maxArcSize - blob.minArcSize) *
                          (sin(sizePhase) * 0.5f + 0.5f);
}

/**
 * Check if angle is within blob's current arc (handles 360° wraparound)
 */
bool isAngleInArc(double angle, const Blob& blob) {
    if (!blob.active) return false;

    double arcEnd = blob.currentStartAngle + blob.currentArcSize;

    // Handle wraparound (e.g., arc from 350° to 10°)
    if (arcEnd > 360.0f) {
        return (angle >= blob.currentStartAngle) || (angle < fmod(arcEnd, 360.0f));
    }

    return (angle >= blob.currentStartAngle) && (angle < arcEnd);
}

/**
 * Initialize 5 blobs with random distribution across arms
 */
void setupBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        float minSize, maxSize;
        float driftSpeed;
        float sizeSpeed;
        float wanderRange;
    } templates[3] = {
        {5, 30, 0.5, 0.3, 60},      // Small, fast
        {10, 60, 0.3, 0.2, 90},     // Medium
        {20, 90, 0.15, 0.1, 120}    // Large, slow
    };

    // Distribute 5 blobs: 2 inner, 2 middle, 1 outer
    uint8_t armAssignments[MAX_BLOBS] = {
        ARM_INNER, ARM_INNER,
        ARM_MIDDLE, ARM_MIDDLE,
        ARM_OUTER
    };

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        blobs[i].armIndex = armAssignments[i];
        blobs[i].color = RgbColor(citrusPalette[i]);  // Convert HSL to RGB

        // Use template based on blob index (varied sizes)
        BlobTemplate& tmpl = templates[i % 3];

        // Random wander center within 0-360°
        blobs[i].wanderCenter = (i * 72.0f);  // Spread evenly: 0, 72, 144, 216, 288
        blobs[i].wanderRange = tmpl.wanderRange;
        blobs[i].driftVelocity = tmpl.driftSpeed;

        blobs[i].minArcSize = tmpl.minSize;
        blobs[i].maxArcSize = tmpl.maxSize;
        blobs[i].sizeChangeRate = tmpl.sizeSpeed;

        blobs[i].birthTime = now;
        blobs[i].deathTime = 0;  // Immortal for now
    }
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

    // Initialize blobs
    Serial.println("Setting up blob animations...");
    setupBlobs();
    Serial.println("Blobs configured");

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

        // Update all blob animations
        for (int i = 0; i < MAX_BLOBS; i++) {
            updateBlob(blobs[i], now);
        }

        // Calculate accumulated color for each arm (additive blending)
        RgbColor innerColor(0, 0, 0);
        RgbColor middleColor(0, 0, 0);
        RgbColor outerColor(0, 0, 0);

        // Check all blobs and accumulate colors for their respective arms
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (!blobs[i].active) continue;

            bool blobActive = false;
            double armAngle = 0;

            // Determine which arm angle to check based on blob's assigned arm
            if (blobs[i].armIndex == ARM_INNER) {
                armAngle = angleInner;
                blobActive = isAngleInArc(armAngle, blobs[i]);
                if (blobActive) {
                    innerColor.R = min(255, innerColor.R + blobs[i].color.R);
                    innerColor.G = min(255, innerColor.G + blobs[i].color.G);
                    innerColor.B = min(255, innerColor.B + blobs[i].color.B);
                }
            } else if (blobs[i].armIndex == ARM_MIDDLE) {
                armAngle = angleMiddle;
                blobActive = isAngleInArc(armAngle, blobs[i]);
                if (blobActive) {
                    middleColor.R = min(255, middleColor.R + blobs[i].color.R);
                    middleColor.G = min(255, middleColor.G + blobs[i].color.G);
                    middleColor.B = min(255, middleColor.B + blobs[i].color.B);
                }
            } else if (blobs[i].armIndex == ARM_OUTER) {
                armAngle = angleOuter;
                blobActive = isAngleInArc(armAngle, blobs[i]);
                if (blobActive) {
                    outerColor.R = min(255, outerColor.R + blobs[i].color.R);
                    outerColor.G = min(255, outerColor.G + blobs[i].color.G);
                    outerColor.B = min(255, outerColor.B + blobs[i].color.B);
                }
            }
        }

        // Set LEDs for each arm with accumulated colors
        // Inner arm: LEDs 10-19
        for (uint16_t i = 0; i < LEDS_PER_ARM; i++) {
            strip.SetPixelColor(INNER_ARM_START + i, innerColor);
        }

        // Middle arm: LEDs 0-9
        for (uint16_t i = 0; i < LEDS_PER_ARM; i++) {
            strip.SetPixelColor(MIDDLE_ARM_START + i, middleColor);
        }

        // Outer arm: LEDs 20-29
        for (uint16_t i = 0; i < LEDS_PER_ARM; i++) {
            strip.SetPixelColor(OUTER_ARM_START + i, outerColor);
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
