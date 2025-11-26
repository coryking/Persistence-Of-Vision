#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "types.h"
#include "RevolutionTimer.h"
#include "blob_types.h"

// Effect selection - comment/uncomment to switch effects
//#define EFFECT_PER_ARM_BLOBS  // Original per-arm blobs
#define EFFECT_VIRTUAL_BLOBS    // Virtual display blobs (active)

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

// Virtual display mapping (from POV_DISPLAY.md)
const uint8_t VIRTUAL_TO_PHYSICAL[30] = {
     0, 10, 20,  // virtual 0-2
     1, 11, 21,  // virtual 3-5
     2, 12, 22,  // virtual 6-8
     3, 13, 23,  // virtual 9-11
     4, 14, 24,  // virtual 12-14
     5, 15, 25,  // virtual 15-17
     6, 16, 26,  // virtual 18-20
     7, 17, 27,  // virtual 21-23
     8, 18, 28,  // virtual 24-26
     9, 19, 29   // virtual 27-29
};

const uint8_t PHYSICAL_TO_VIRTUAL[30] = {
     0,  3,  6,  9, 12, 15, 18, 21, 24, 27,  // physical 0-9 (Arm A/Middle)
     1,  4,  7, 10, 13, 16, 19, 22, 25, 28,  // physical 10-19 (Arm B/Inner)
     2,  5,  8, 11, 14, 17, 20, 23, 26, 29   // physical 20-29 (Arm C/Outer)
};

// Blob pool
Blob blobs[MAX_BLOBS];

/**
 * ISR handler for hall effect sensor
 * Captures timestamp immediately and sets flag for processing in main loop
 */
void IRAM_ATTR hallSensorISR(void* arg) {
    isrTimestamp = esp_timer_get_time();
    newRevolutionDetected = true;
}

// ============================================================================
// PER-ARM BLOBS EFFECT
// ============================================================================
#ifdef EFFECT_PER_ARM_BLOBS

/**
 * Check if LED index is within blob's current radial extent (per-arm version)
 */
bool isLedInBlob(uint16_t ledIndex, const Blob& blob) {
    if (!blob.active) return false;

    // Calculate radial range (LED indices covered by this blob)
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;

    // Check if LED is within range (clipped to 0-9)
    float ledFloat = static_cast<float>(ledIndex);
    return (ledFloat >= radialStart) && (ledFloat < radialEnd);
}

/**
 * Initialize 5 blobs with random distribution across arms (per-arm version)
 */
void setupPerArmBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        // Angular parameters
        float minAngularSize, maxAngularSize;
        float angularDriftSpeed;
        float angularSizeSpeed;
        float angularWanderRange;
        // Radial parameters
        float minRadialSize, maxRadialSize;
        float radialDriftSpeed;
        float radialSizeSpeed;
        float radialWanderRange;
    } templates[3] = {
        // Small, fast (angular 5-30°, radial 1-3 LEDs)
        {5, 30, 0.5, 0.3, 60,    1, 3, 0.4, 0.25, 2.0},
        // Medium (angular 10-60°, radial 2-5 LEDs)
        {10, 60, 0.3, 0.2, 90,   2, 5, 0.25, 0.15, 2.5},
        // Large, slow (angular 20-90°, radial 3-7 LEDs)
        {20, 90, 0.15, 0.1, 120, 3, 7, 0.15, 0.1, 3.0}
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

        // Angular parameters
        blobs[i].wanderCenter = (i * 72.0f);  // Spread evenly: 0, 72, 144, 216, 288
        blobs[i].wanderRange = tmpl.angularWanderRange;
        blobs[i].driftVelocity = tmpl.angularDriftSpeed;
        blobs[i].minArcSize = tmpl.minAngularSize;
        blobs[i].maxArcSize = tmpl.maxAngularSize;
        blobs[i].sizeChangeRate = tmpl.angularSizeSpeed;

        // Radial parameters
        blobs[i].radialWanderCenter = 4.5f;  // Center of LED strip (0-9)
        blobs[i].radialWanderRange = tmpl.radialWanderRange;
        blobs[i].radialDriftVelocity = tmpl.radialDriftSpeed;
        blobs[i].minRadialSize = tmpl.minRadialSize;
        blobs[i].maxRadialSize = tmpl.maxRadialSize;
        blobs[i].radialSizeChangeRate = tmpl.radialSizeSpeed;

        blobs[i].birthTime = now;
        blobs[i].deathTime = 0;  // Immortal for now
    }
}

#endif // EFFECT_PER_ARM_BLOBS

// ============================================================================
// VIRTUAL DISPLAY BLOBS EFFECT
// ============================================================================
#ifdef EFFECT_VIRTUAL_BLOBS

/**
 * Check if virtual LED position is within blob's current radial extent
 * (with wraparound support for 0-29 range)
 */
bool isVirtualLedInBlob(uint8_t virtualPos, const Blob& blob) {
    if (!blob.active) return false;

    // Calculate radial range (virtual positions covered by this blob)
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;

    // Check with wraparound handling
    float pos = static_cast<float>(virtualPos);

    // If range doesn't wrap around, simple check
    if (radialStart >= 0 && radialEnd < 30) {
        return (pos >= radialStart) && (pos < radialEnd);
    }

    // Handle wraparound (blob spans 29 → 0 boundary)
    if (radialStart < 0) {
        // Blob extends below 0, wraps to high end
        return (pos >= (radialStart + 30)) || (pos < radialEnd);
    }

    if (radialEnd >= 30) {
        // Blob extends above 29, wraps to low end
        return (pos >= radialStart) || (pos < (radialEnd - 30));
    }

    return false;
}

/**
 * Initialize 5 blobs for virtual display (0-29 radial range)
 */
void setupVirtualBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        // Angular parameters
        float minAngularSize, maxAngularSize;
        float angularDriftSpeed;
        float angularSizeSpeed;
        float angularWanderRange;
        // Radial parameters
        float minRadialSize, maxRadialSize;
        float radialDriftSpeed;
        float radialSizeSpeed;
        float radialWanderRange;
    } templates[3] = {
        // Small, fast (angular 5-30°, radial 2-6 LEDs)
        {5, 30, 0.5, 0.3, 60,    2, 6, 0.4, 0.25, 4.0},
        // Medium (angular 10-60°, radial 4-10 LEDs)
        {10, 60, 0.3, 0.2, 90,   4, 10, 0.25, 0.15, 6.0},
        // Large, slow (angular 20-90°, radial 6-14 LEDs)
        {20, 90, 0.15, 0.1, 120, 6, 14, 0.15, 0.1, 8.0}
    };

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        blobs[i].armIndex = 0;  // Unused for virtual blobs
        blobs[i].color = RgbColor(citrusPalette[i]);  // Convert HSL to RGB

        // Use template based on blob index (varied sizes)
        BlobTemplate& tmpl = templates[i % 3];

        // Angular parameters
        blobs[i].wanderCenter = (i * 72.0f);  // Spread evenly: 0, 72, 144, 216, 288
        blobs[i].wanderRange = tmpl.angularWanderRange;
        blobs[i].driftVelocity = tmpl.angularDriftSpeed;
        blobs[i].minArcSize = tmpl.minAngularSize;
        blobs[i].maxArcSize = tmpl.maxAngularSize;
        blobs[i].sizeChangeRate = tmpl.angularSizeSpeed;

        // Radial parameters - now use full 0-29 range
        blobs[i].radialWanderCenter = 14.5f;  // Center of virtual display (0-29)
        blobs[i].radialWanderRange = tmpl.radialWanderRange;
        blobs[i].radialDriftVelocity = tmpl.radialDriftSpeed;
        blobs[i].minRadialSize = tmpl.minRadialSize;
        blobs[i].maxRadialSize = tmpl.maxRadialSize;
        blobs[i].radialSizeChangeRate = tmpl.radialSizeSpeed;

        blobs[i].birthTime = now;
        blobs[i].deathTime = 0;  // Immortal for now
    }
}

#endif // EFFECT_VIRTUAL_BLOBS

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
#ifdef EFFECT_PER_ARM_BLOBS
    setupPerArmBlobs();
    Serial.println("Per-arm blobs configured");
#endif
#ifdef EFFECT_VIRTUAL_BLOBS
    setupVirtualBlobs();
    Serial.println("Virtual display blobs configured");
#endif

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

#ifdef EFFECT_PER_ARM_BLOBS
        // ====================================================================
        // PER-ARM RENDERING
        // ====================================================================
        // Render each LED individually based on angular and radial position
        // Inner arm: LEDs 10-19
        for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
            RgbColor ledColor(0, 0, 0);

            // Check all blobs assigned to inner arm
            for (int i = 0; i < MAX_BLOBS; i++) {
                if (blobs[i].active && blobs[i].armIndex == ARM_INNER) {
                    // Check both angular and radial position
                    if (isAngleInArc(angleInner, blobs[i]) && isLedInBlob(ledIdx, blobs[i])) {
                        ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                        ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                        ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                    }
                }
            }
            strip.SetPixelColor(INNER_ARM_START + ledIdx, ledColor);
        }

        // Middle arm: LEDs 0-9
        for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
            RgbColor ledColor(0, 0, 0);

            // Check all blobs assigned to middle arm
            for (int i = 0; i < MAX_BLOBS; i++) {
                if (blobs[i].active && blobs[i].armIndex == ARM_MIDDLE) {
                    // Check both angular and radial position
                    if (isAngleInArc(angleMiddle, blobs[i]) && isLedInBlob(ledIdx, blobs[i])) {
                        ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                        ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                        ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                    }
                }
            }
            strip.SetPixelColor(MIDDLE_ARM_START + ledIdx, ledColor);
        }

        // Outer arm: LEDs 20-29
        for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
            RgbColor ledColor(0, 0, 0);

            // Check all blobs assigned to outer arm
            for (int i = 0; i < MAX_BLOBS; i++) {
                if (blobs[i].active && blobs[i].armIndex == ARM_OUTER) {
                    // Check both angular and radial position
                    if (isAngleInArc(angleOuter, blobs[i]) && isLedInBlob(ledIdx, blobs[i])) {
                        ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                        ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                        ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                    }
                }
            }
            strip.SetPixelColor(OUTER_ARM_START + ledIdx, ledColor);
        }

        // Update the strip
        strip.Show();

#endif // EFFECT_PER_ARM_BLOBS

#ifdef EFFECT_VIRTUAL_BLOBS
        // ====================================================================
        // VIRTUAL DISPLAY RENDERING
        // ====================================================================
        // Render using virtual addressing - each LED checks against all blobs

        // Inner arm: LEDs 10-19
        for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
            uint16_t physicalLed = INNER_ARM_START + ledIdx;
            uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
            RgbColor ledColor(0, 0, 0);

            // Check ALL blobs (no arm filtering)
            for (int i = 0; i < MAX_BLOBS; i++) {
                if (blobs[i].active &&
                    isAngleInArc(angleInner, blobs[i]) &&
                    isVirtualLedInBlob(virtualPos, blobs[i])) {
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                }
            }
            strip.SetPixelColor(physicalLed, ledColor);
        }

        // Middle arm: LEDs 0-9
        for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
            uint16_t physicalLed = MIDDLE_ARM_START + ledIdx;
            uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
            RgbColor ledColor(0, 0, 0);

            // Check ALL blobs (no arm filtering)
            for (int i = 0; i < MAX_BLOBS; i++) {
                if (blobs[i].active &&
                    isAngleInArc(angleMiddle, blobs[i]) &&
                    isVirtualLedInBlob(virtualPos, blobs[i])) {
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                }
            }
            strip.SetPixelColor(physicalLed, ledColor);
        }

        // Outer arm: LEDs 20-29
        for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
            uint16_t physicalLed = OUTER_ARM_START + ledIdx;
            uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
            RgbColor ledColor(0, 0, 0);

            // Check ALL blobs (no arm filtering)
            for (int i = 0; i < MAX_BLOBS; i++) {
                if (blobs[i].active &&
                    isAngleInArc(angleOuter, blobs[i]) &&
                    isVirtualLedInBlob(virtualPos, blobs[i])) {
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                }
            }
            strip.SetPixelColor(physicalLed, ledColor);
        }

        // Update the strip
        strip.Show();

#endif // EFFECT_VIRTUAL_BLOBS

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
