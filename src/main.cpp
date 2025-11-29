#include <Arduino.h>
#include <WiFi.h>
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
#include "RenderContext.h"
#include "EffectRegistry.h"
#include "EffectScheduler.h"
#include "effects/NoiseField.h"
#include "effects/SolidArms.h"
#include "effects/RpmArc.h"
#include "effects/PerArmBlobs.h"
#include "effects/VirtualBlobs.h"
#include "timing_utils.h"

// Hardware Configuration
#define NUM_LEDS 30
#define LEDS_PER_ARM 10
#define HALL_PIN D1

// ===== TEST MODE CONFIGURATION =====
#define TEST_RPM 2800.0
#define TEST_VARY_RPM false

// Physical arm layout - maps arm index to physical LED start position
// Arms: [0]=inner, [1]=middle (hall trigger), [2]=outer
static const uint16_t ARM_START[3] = {10, 0, 20};  // Physical LED indices

// Phase offsets removed - use constants from types.h instead
// (INNER_ARM_PHASE = 1200 units = 120°, OUTER_ARM_PHASE = 2400 units = 240°)

// POV Display Configuration
#define WARMUP_REVOLUTIONS 20
#define ROLLING_AVERAGE_SIZE 20
#define ROTATION_TIMEOUT_US 2000000

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// Hall effect sensor driver
HallEffectDriver hallDriver(HALL_PIN);

// Effect instances
NoiseField noiseFieldEffect;
SolidArms solidArmsEffect;
RpmArc rpmArcEffect;
PerArmBlobs perArmBlobsEffect;
VirtualBlobs virtualBlobsEffect;

// Effect registry
EffectRegistry effectRegistry;

// Effect scheduler (manages NVS persistence)
EffectScheduler effectScheduler;

// Render context (reused each frame)
RenderContext renderCtx;

// Timing instrumentation
#if ENABLE_TIMING_INSTRUMENTATION
bool csvHeaderPrinted = false;
#endif

// Global frame counter (shared via RenderContext)
uint32_t globalFrameCount = 0;

// Test mode: simulated rotation
// Feed synthetic hall timestamps into revTimer to exercise real timing logic
#ifdef TEST_MODE
timestamp_t lastHallTimestamp = 0;
interval_t simulatedMicrosecondsPerRev = 0;

void simulateHallTrigger() {
    timestamp_t now = esp_timer_get_time();

    // Generate synthetic RPM
    double rpm = TEST_RPM;
    if (TEST_VARY_RPM) {
        double timeSec = now / 1000000.0;
        rpm = 700.0 + 1050.0 * (1.0 + sin(timeSec * 0.5));
    }

    interval_t microsecondsPerRev = static_cast<interval_t>(60000000.0 / rpm);

    // Inject synthetic hall trigger into revTimer at appropriate intervals
    if (lastHallTimestamp == 0) {
        // First trigger
        lastHallTimestamp = now;
        revTimer.addTimestamp(now);
    } else if (now - lastHallTimestamp >= microsecondsPerRev) {
        // Time for next trigger
        lastHallTimestamp += microsecondsPerRev;
        revTimer.addTimestamp(lastHallTimestamp);
        simulatedMicrosecondsPerRev = microsecondsPerRev;
    }
}
#endif

/**
 * FreeRTOS task for processing hall sensor events
 */
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();
    bool wasRotating = false;  // Track motor state
    static uint16_t revolutionCount = 1;

    while (1) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);

            // Detect motor start (stopped → rotating transition)
            bool isRotating = revTimer.isCurrentlyRotating();
            if (!wasRotating && isRotating) {
              revolutionCount = 1;
                effectScheduler.onMotorStart();  // Advance effect, save to NVS
            }
            wasRotating = isRotating;

            // Notify current effect of revolution
            effectRegistry.onRevolution(revTimer.getMicrosecondsPerRevolution(), event.triggerTimestamp, revolutionCount++);

            if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
                Serial.println("Warm-up complete! Display active.");
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    // Disable WiFi to reduce jitter - WiFi interrupts steal cycles
    WiFi.mode(WIFI_OFF);
    btStop();  // Also disable Bluetooth

    Serial.println("POV Display Initializing...");

    // Initialize LED strip
    strip.Begin();
    strip.ClearTo(RgbColor(0, 0, 0));
    strip.Show();
    Serial.println("Strip initialized");

    // Startup blink pattern - diagnostic for reset detection
    // If you see periodic blinks during operation, ESP32 is resetting
    Serial.println("Startup blink sequence...");
    for (int i = 0; i < 3; i++) {
        strip.ClearTo(RgbColor(64, 0, 0));  // Dim red
        strip.Show();
        delay(100);
        strip.ClearTo(RgbColor(0, 0, 0));
        strip.Show();
        delay(100);
    }
    Serial.println("Startup blink complete");

    // Setup hall effect sensor
    hallDriver.start();
    Serial.println("Hall effect sensor initialized");

    // Start hall processing task
    BaseType_t taskCreated = xTaskCreate(
        hallProcessingTask,
        "hallProcessor",
        2048,
        nullptr,
        3,
        nullptr
    );

    if (taskCreated != pdPASS) {
        Serial.println("ERROR: Failed to create hall processing task");
        while (1) { delay(1000); }
    }
    Serial.println("Hall processing task started");

    // Register effects
    effectRegistry.registerEffect(&noiseFieldEffect);
    //effectRegistry.registerEffect(&solidArmsEffect);
    //effectRegistry.registerEffect(&rpmArcEffect);
    //effectRegistry.registerEffect(&perArmBlobsEffect);
    //effectRegistry.registerEffect(&virtualBlobsEffect);

    // Initialize scheduler (loads NVS, advances effect, saves, starts registry)
    effectScheduler.begin(&effectRegistry);

    Serial.printf("Registered %d effects, starting with effect %u\n",
                  effectRegistry.getEffectCount(),
                  effectRegistry.getCurrentIndex());

    Serial.println("\n=== POV Display Ready ===");
    Serial.println("Effects: NoiseField, SolidArms, RpmArc, PerArmBlobs, VirtualBlobs");
    Serial.println("Waiting for rotation...\n");
}

void loop() {
#ifdef TEST_MODE
    // Generate synthetic hall triggers at TEST_RPM to exercise revTimer logic
    simulateHallTrigger();
#endif

    // Get atomic snapshot of timing values
    // In test mode: simulateHallTrigger() feeds synthetic timestamps
    // In real mode: hallProcessingTask feeds real ISR timestamps
    // Rest of code path is identical
    timestamp_t now = esp_timer_get_time();
    TimingSnapshot timing = revTimer.getTimingSnapshot();

    bool isRotating = timing.isRotating;
    bool isWarmupComplete = timing.warmupComplete;
    timestamp_t lastHallTime = timing.lastTimestamp;

    // Use ACTUAL last interval for angle calculation (not smoothed average)
    // This prevents drift within a revolution when motor speed varies slightly
    // The smoothed value is still available for display/resolution calculations
    interval_t microsecondsPerRev = timing.lastActualInterval;

    // Fall back to smoothed if no actual interval yet (first revolution)
    if (microsecondsPerRev == 0) {
        microsecondsPerRev = timing.microsecondsPerRev;
    }

    timestamp_t elapsed = now - lastHallTime;

#ifdef ENABLE_DETAILED_TIMING
    // Detect race condition: elapsed should never be > ~1.5 revolutions
    // or suspiciously large (wraparound from negative)
    static uint32_t raceDetectCount = 0;
    if (microsecondsPerRev > 0) {
        // Check for wraparound (elapsed would be huge if now < lastHallTime)
        if (elapsed > 1000000000ULL) {  // > 1000 seconds = definitely wraparound
            Serial.printf("RACE_WRAPAROUND: elapsed=%llu lastHall=%llu now=%llu\n",
                          elapsed, lastHallTime, now);
            raceDetectCount++;
        }
        // Check for elapsed > 1.5 revolutions (might indicate missed hall or race)
        else if (elapsed > (microsecondsPerRev * 3 / 2)) {
            Serial.printf("RACE_LARGE_ELAPSED: elapsed=%llu expected<%llu\n",
                          elapsed, microsecondsPerRev);
            raceDetectCount++;
        }
    }
#endif

    // Integer angle calculation - 3600 units = 360 degrees (0.1° resolution)
    // SAFETY: Only calculate if microsecondsPerRev > 0 to avoid division by zero
    angle_t angleMiddleUnits, angleInnerUnits, angleOuterUnits;
    if (microsecondsPerRev > 0) {
        angleMiddleUnits = static_cast<angle_t>(
            (static_cast<uint32_t>(elapsed) * 3600UL / microsecondsPerRev) % 3600
        );
        angleInnerUnits = (angleMiddleUnits + INNER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
        angleOuterUnits = (angleMiddleUnits + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
    } else {
        // Not rotating - use safe default angles (all at 0°)
        angleMiddleUnits = angleInnerUnits = angleOuterUnits = 0;
    }

#ifdef ENABLE_DETAILED_TIMING
    // Detect large angle jumps (potential race condition symptom)
    static angle_t prevAngleMiddleUnits = 0;
    if (prevAngleMiddleUnits > 0) {
        int32_t angleDiff = static_cast<int32_t>(angleMiddleUnits) - static_cast<int32_t>(prevAngleMiddleUnits);
        // Normalize to -1800 to +1800 (±180°)
        if (angleDiff > 1800) angleDiff -= 3600;
        if (angleDiff < -1800) angleDiff += 3600;
        // At 2800 RPM, expect ~8.4 units per frame. Flag jumps > 900 units (90°)
        if (abs(angleDiff) > 900) {
            Serial.printf("ANGLE_JUMP: prev=%u curr=%u diff=%d\n",
                          prevAngleMiddleUnits, angleMiddleUnits, angleDiff);
        }
    }
    prevAngleMiddleUnits = angleMiddleUnits;
#endif

    if (isWarmupComplete && isRotating) {
        // Angle-slot gating: only render when we've moved to a new angular position
        // Resolution adapts to RPM and render performance, updated once per revolution
        // All valid resolutions evenly divide 360° for clean 0° alignment
        angle_t slotSizeUnits = static_cast<angle_t>(timing.angularResolution * 10.0f);
        if (slotSizeUnits == 0) slotSizeUnits = 30;  // 3 degrees default
        static int lastSlot = -1;

        int currentSlot = angleMiddleUnits / slotSizeUnits;

        // Check if we've moved to a new slot (handles wraparound at 360°)
        bool shouldRender = (currentSlot != lastSlot);

        if (!shouldRender) {
            return;  // Skip this iteration - we're still in the same angular slot
        }
        lastSlot = currentSlot;

        // Snap angles to slot boundary for deterministic rendering
        // This ensures pattern boundaries are crossed at exact, consistent angles
        // regardless of when loop() happens to sample esp_timer_get_time()
        angle_t slotBoundaryAngle = static_cast<angle_t>(currentSlot * slotSizeUnits);
        angleMiddleUnits = slotBoundaryAngle;
        angleInnerUnits = (slotBoundaryAngle + INNER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
        angleOuterUnits = (slotBoundaryAngle + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;

        // Start render timing measurement
        revTimer.startRender();

#if ENABLE_TIMING_INSTRUMENTATION
        if (!csvHeaderPrinted) {
            Serial.println("frame,effect,total_us,slot,angle_units,usec_per_rev,resolution_units,rev_count,elapsed_us,angular_res,last_interval_us");
            csvHeaderPrinted = true;
        }
        int64_t frameStart = timingStart();
#endif

        // Populate render context
        renderCtx.frameCount = globalFrameCount++;
        renderCtx.timeUs = static_cast<uint32_t>(now);
        renderCtx.microsPerRev = microsecondsPerRev;
        renderCtx.slotSizeUnits = slotSizeUnits;

        // Set arm angles - arms[0]=inner, arms[1]=middle, arms[2]=outer
        renderCtx.arms[0].angleUnits = angleInnerUnits;
        renderCtx.arms[1].angleUnits = angleMiddleUnits;
        renderCtx.arms[2].angleUnits = angleOuterUnits;

        // Render current effect
        Effect* current = effectRegistry.current();
        if (current) {
            current->render(renderCtx);
        }

        // Copy arm buffers to LED strip
        for (int a = 0; a < 3; a++) {
            uint16_t start = ARM_START[a];
            for (int p = 0; p < 10; p++) {
                CRGB color = renderCtx.arms[a].pixels[p];
                color.nscale8(32);  // Power budget
                strip.SetPixelColor(start + p, RgbColor(color.r, color.g, color.b));
            }
        }

        strip.Show();

        // End render timing measurement
        revTimer.endRender();

#if ENABLE_TIMING_INSTRUMENTATION
        int64_t totalTime = timingEnd(frameStart);
        Serial.printf("%u,%u,%lld,%d,%u,%llu,%u,%u,%llu,%f,%llu\n",
                      renderCtx.frameCount,
                      effectRegistry.getCurrentIndex(),
                      totalTime,
                      currentSlot,
                      angleMiddleUnits,
                      microsecondsPerRev,
                      slotSizeUnits,
                      revTimer.getRevolutionCount(),
                      elapsed,
                      timing.angularResolution,
                      timing.lastActualInterval);
#endif

    } else {
        strip.ClearTo(RgbColor(0, 0, 0));
        strip.Show();
        delay(10);
    }
}
