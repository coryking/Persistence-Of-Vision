#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "types.h"
#include "RevolutionTimer.h"
#include "HallEffectDriver.h"
#include "blob_types.h"
#include "EffectManager.h"
#include "RenderContext.h"
#include "effects.h"
#include "timing_utils.h"
#include "pixel_utils.h"
#include "blob_cache.h"

// Hardware Configuration
#define NUM_LEDS 30
#define LEDS_PER_ARM 10
#define HALL_PIN D1

// ===== TEST MODE CONFIGURATION =====
// These are NOT defined by default (production mode)
// Define via platformio.ini build flags for profiling/testing
// Example: -DTEST_MODE -DENABLE_TIMING_INSTRUMENTATION
#define TEST_RPM 2800.0                      // Simulated RPM when TEST_MODE defined
#define TEST_VARY_RPM false                  // Oscillate between 700-2800 RPM
// ===============================================

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

// RPM Arc Effect Configuration
#define RPM_MIN 800.0f           // Minimum RPM (1 virtual pixel)
#define RPM_MAX 2500.0f          // Maximum RPM (30 virtual pixels)
#define ARC_WIDTH_DEGREES 20.0f  // Arc width in degrees
#define ARC_CENTER_DEGREES 0.0f  // Arc center position (hall sensor)

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
// Uses GPIO 7 (data) and GPIO 9 (clock) via hardware SPI
// Using 40MHz SPI (fastest for this LED count)
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// Hall effect sensor driver
HallEffectDriver hallDriver(HALL_PIN);

// Arm colors (solid RGB for visibility)
const RgbColor INNER_ARM_COLOR(255, 0, 0);    // Red (inner)
const RgbColor MIDDLE_ARM_COLOR(0, 255, 0);   // Green (middle)
const RgbColor OUTER_ARM_COLOR(0, 0, 255);    // Blue (outer)
const RgbColor OFF_COLOR(0, 0, 0);

// Virtual display mapping (from POV_DISPLAY.md)
// Note: Could use PROGMEM to save RAM, but keeping in RAM for simpler access
uint8_t VIRTUAL_TO_PHYSICAL[30] = {
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

uint8_t PHYSICAL_TO_VIRTUAL[30] = {
     0,  3,  6,  9, 12, 15, 18, 21, 24, 27,  // physical 0-9 (Arm A/Middle)
     1,  4,  7, 10, 13, 16, 19, 22, 25, 28,  // physical 10-19 (Arm B/Inner)
     2,  5,  8, 11, 14, 17, 20, 23, 26, 29   // physical 20-29 (Arm C/Outer)
};

// Blob pool
Blob blobs[MAX_BLOBS];

// Effect manager
EffectManager effectManager;

// Timing instrumentation
#if ENABLE_TIMING_INSTRUMENTATION
uint32_t instrumentationFrameCount = 0;
bool csvHeaderPrinted = false;
#endif

// Test mode: simulated rotation
#if TEST_MODE
double simulatedAngle = 0.0;
timestamp_t lastSimUpdate = 0;
interval_t simulatedMicrosecondsPerRev = 0;

/**
 * Simulate rotation based on time and configured RPM
 * Returns current angle (0-360) and updates simulatedMicrosecondsPerRev
 */
double simulateRotation() {
    timestamp_t now = esp_timer_get_time();

    if (lastSimUpdate == 0) {
        lastSimUpdate = now;
        simulatedAngle = 0.0;
    }

    // Calculate time delta
    timestamp_t elapsed = now - lastSimUpdate;
    lastSimUpdate = now;

    // Calculate current RPM (possibly varying)
    double rpm = TEST_RPM;
    if (TEST_VARY_RPM) {
        // Oscillate: 700 + 1050 * (1 + sin(t))
        double timeSec = now / 1000000.0;
        rpm = 700.0 + 1050.0 * (1.0 + sin(timeSec * 0.5));
    }

    // Calculate microseconds per revolution
    simulatedMicrosecondsPerRev = static_cast<interval_t>(60000000.0 / rpm);

    // Calculate angle advancement
    double degreesPerMicrosecond = (rpm * 360.0) / 60000000.0;
    simulatedAngle += degreesPerMicrosecond * elapsed;

    // Wrap at 360
    while (simulatedAngle >= 360.0) {
        simulatedAngle -= 360.0;
    }

    return simulatedAngle;
}
#endif

/**
 * FreeRTOS task for processing hall sensor events
 * Runs at high priority to process timestamps immediately
 */
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();

    while (1) {
        // Block waiting for hall sensor event
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            // Process timestamp immediately
            revTimer.addTimestamp(event.triggerTimestamp);

            // Print warm-up complete message once
            if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
                Serial.println("Warm-up complete! Display active.");
            }
        }
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
    strip.ClearTo(OFF_COLOR);           // Clear strip to black
    strip.Show();
    Serial.println("Strip initialized");

    // Setup hall effect sensor driver
    Serial.println("Setting up hall effect sensor...");
    hallDriver.start();
    Serial.println("Hall effect sensor initialized on D1");

    // Start hall processing task (priority 3 = higher than loop which runs at priority 1)
    BaseType_t taskCreated = xTaskCreate(
        hallProcessingTask,     // Task function
        "hallProcessor",        // Task name
        2048,                   // Stack size (bytes)
        nullptr,                // Task parameters
        3,                      // Priority (higher than loop's default priority 1)
        nullptr                 // Task handle (not needed)
    );

    if (taskCreated != pdPASS) {
        Serial.println("ERROR: Failed to create hall processing task");
        while (1) { delay(1000); } // Halt
    }
    Serial.println("Hall processing task started (priority 3)");

    // Initialize effect manager and load current effect
    Serial.println("Initializing effect manager...");
    effectManager.begin();

#ifdef ENABLE_DETAILED_TIMING
    // Override to force specific effect for profiling
    effectManager.setCurrentEffect(2);  // 2 = SolidArms (test pattern, always visible)
    Serial.println("PROFILING MODE: Forcing effect 2 (SolidArms)");
#endif

    uint8_t currentEffect = effectManager.getCurrentEffect();
    Serial.printf("Current effect: %d\n", currentEffect);

    // Initialize all effects (all need to be ready for runtime switching)
    Serial.println("Setting up all effects...");
    setupPerArmBlobs();
    Serial.println("  - Per-arm blobs configured");
    setupVirtualBlobs();
    Serial.println("  - Virtual blobs configured");
    Serial.println("  - Solid arms diagnostic ready");
    Serial.println("  - RPM arc effect ready");

    // Save next effect for next boot/motor restart
    effectManager.saveNextEffect();
    Serial.println("Next effect queued for next power cycle");

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
    static bool wasRotating = false;

#if TEST_MODE
    // === TEST MODE: Simulated Rotation ===
    // Simulate rotation and bypass hall sensor
    double angleMiddle = simulateRotation();
    double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
    double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);
    interval_t microsecondsPerRev = simulatedMicrosecondsPerRev;
    timestamp_t now = esp_timer_get_time();

    // Force warm-up complete
    bool isRotating = true;
    bool isWarmupComplete = true;

#else
    // === REAL HARDWARE MODE ===
    // Detect motor stop/start transitions
    bool isRotating = revTimer.isCurrentlyRotating();
    if (!wasRotating && isRotating) {
        // Motor just started - switch to next effect
        Serial.println("Motor started - switching to next effect");
        effectManager.saveNextEffect();
    }
    wasRotating = isRotating;

    bool isWarmupComplete = revTimer.isWarmupComplete();

    // Get timing from hall sensor
    timestamp_t now = esp_timer_get_time();
    timestamp_t lastHallTime = revTimer.getLastTimestamp();
    interval_t microsecondsPerRev = revTimer.getMicrosecondsPerRevolution();

    // Calculate elapsed time since hall trigger
    timestamp_t elapsed = now - lastHallTime;
    double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

    // Calculate arm angles
    double angleMiddle = fmod(static_cast<double>(elapsed) / microsecondsPerDegree, 360.0);
    double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
    double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);
#endif

    // Only display if warm-up complete and currently rotating
    if (isWarmupComplete && isRotating) {
#if ENABLE_TIMING_INSTRUMENTATION
        // Print CSV header once
        if (!csvHeaderPrinted) {
            Serial.println("frame,effect,gen_us,xfer_us,total_us,angle_deg,rpm");
            csvHeaderPrinted = true;
        }

        int64_t frameStart = timingStart();
#endif

        // Create LED buffer (external to context)
        CRGB ledBuffer[30] = {};  // Zero-initialized

        // Create rendering context
        RenderContext ctx = {
            .currentMicros = static_cast<unsigned long>(now),
            .innerArmDegrees = static_cast<float>(angleInner),
            .middleArmDegrees = static_cast<float>(angleMiddle),
            .outerArmDegrees = static_cast<float>(angleOuter),
            .microsecondsPerRev = microsecondsPerRev,
            .leds = ledBuffer  // Point to external buffer
        };

        // Update all blob animations (used by blob effects)
        for (int i = 0; i < MAX_BLOBS; i++) {
            updateBlob(blobs[i], now);
        }

#if TEST_MODE
        // Debug: Check blob status and colors every 100 frames
        if (instrumentationFrameCount % 100 == 0) {
            Serial.print("Blobs: ");
            for (int i = 0; i < MAX_BLOBS; i++) {
                Serial.printf("B%d:%s(R%d,G%d,B%d,angle=%.1f,radial=%.1f) ",
                    i,
                    blobs[i].active ? "A" : "I",
                    blobs[i].color.r, blobs[i].color.g, blobs[i].color.b,
                    blobs[i].currentStartAngle,
                    blobs[i].currentRadialCenter);
            }
            Serial.println();
        }
#endif

        // Update blob cache after all animations (eliminates ~150 fmod calls per frame)
        updateBlobCache(blobs, MAX_BLOBS, true);  // true = virtual range (0-29)

#if TEST_MODE
        // Debug: Check blob cache values every 100 frames
        if (instrumentationFrameCount % 100 == 0) {
            Serial.printf("Cache B0: angle=%.1f-%.1f wrap=%d radial=%.1f-%.1f wrap=%d\n",
                blobCache[0].angleStart, blobCache[0].angleEnd, blobCache[0].angleWraps,
                blobCache[0].radialStart, blobCache[0].radialEnd, blobCache[0].radialWraps);
        }
#endif

#if ENABLE_TIMING_INSTRUMENTATION
        int64_t genStart = timingStart();
#endif

        // Dispatch to current effect
        switch(effectManager.getCurrentEffect()) {
            case 0:
                renderPerArmBlobs(ctx);
                break;
            case 1:
                renderVirtualBlobs(ctx);
                break;
            case 2:
                renderSolidArms(ctx);
                break;
            case 3:
                renderRpmArc(ctx);
                break;
            case 4:
                renderNoiseField(ctx);
                break;
        }

#if TEST_MODE
        // Debug: Check LED buffer after rendering every 100 frames
        if (instrumentationFrameCount % 100 == 0) {
            Serial.print("LEDs: ");
            for (int i = 0; i < 10; i++) {  // First 10 LEDs
                Serial.printf("[%d](%d,%d,%d) ", i, ctx.leds[i].r, ctx.leds[i].g, ctx.leds[i].b);
            }
            Serial.println();
        }
#endif

        // Convert CRGB buffer to NeoPixelBus format with power budget
        for (int i = 0; i < 30; i++) {
            CRGB color = ctx.leds[i];
            color.nscale8(128);  // 50% brightness for power budget
            strip.SetPixelColor(i, RgbColor(color.r, color.g, color.b));
        }

#if ENABLE_TIMING_INSTRUMENTATION
        int64_t genTime = timingEnd(genStart);
        int64_t xferStart = timingStart();
#endif

        // Update the strip
        strip.Show();

#if ENABLE_TIMING_INSTRUMENTATION
        int64_t xferTime = timingEnd(xferStart);
        int64_t totalTime = timingEnd(frameStart);

        // Calculate RPM
        double rpm = (microsecondsPerRev > 0) ? (60000000.0 / microsecondsPerRev) : 0.0;

        // Output raw CSV: frame,effect,gen_us,xfer_us,total_us,angle_deg,rpm
        Serial.printf("%u,%u,%lld,%lld,%lld,%.2f,%.1f\n",
                      instrumentationFrameCount,
                      effectManager.getCurrentEffect(),
                      genTime,
                      xferTime,
                      totalTime,
                      angleMiddle,
                      rpm);

        instrumentationFrameCount++;
#endif

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
