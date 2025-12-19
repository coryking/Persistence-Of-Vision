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
#include "hardware_config.h"

// Hardware Configuration

#define GLOBAL_BRIGHTNESS 100

// ===== TEST MODE CONFIGURATION =====
#define TEST_RPM 360.0
#define TEST_VARY_RPM false

// Phase offsets defined in types.h:
// OUTER_ARM_PHASE = 2400 units = 240° (arm[0])
// MIDDLE_ARM_PHASE = 0 units = 0° (arm[1], hall sensor reference)
// INSIDE_ARM_PHASE = 1200 units = 120° (arm[2])

// POV Display Configuration
#define WARMUP_REVOLUTIONS 20
#define ROLLING_AVERAGE_SIZE 20
#define ROTATION_TIMEOUT_US 2000000

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(HardwareConfig::TOTAL_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// Hall effect sensor driver
HallEffectDriver hallDriver(HardwareConfig::HALL_PIN);

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

// Test mode: ESP-IDF timer-based hall sensor simulation
// Posts HallEffectEvent to queue, exercising full hallProcessingTask path
#ifdef TEST_MODE
esp_timer_handle_t testHallTimer = nullptr;
QueueHandle_t testHallQueue = nullptr;

// Timer callback - posts hall events to queue (same as real ISR)
static void IRAM_ATTR testHallTimerCallback(void* arg) {
    HallEffectEvent event;
    event.triggerTimestamp = esp_timer_get_time();

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xQueueOverwriteFromISR((QueueHandle_t)arg, &event, &higherPriorityTaskWoken);

    if (higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Optional: Variable RPM updater timer (if TEST_VARY_RPM enabled)
#if TEST_VARY_RPM
esp_timer_handle_t testRpmUpdaterTimer = nullptr;

static void IRAM_ATTR testRpmUpdaterCallback(void* arg) {
    // Calculate new RPM using sinusoidal function
    timestamp_t now = esp_timer_get_time();
    double timeSec = now / 1000000.0;
    double rpm = 700.0 + 1050.0 * (1.0 + sin(timeSec * 0.5));

    // Calculate new interval
    uint64_t newIntervalUs = static_cast<uint64_t>(60000000.0 / rpm);

    // Reconfigure main hall timer
    esp_timer_stop(testHallTimer);
    esp_timer_start_periodic(testHallTimer, newIntervalUs);
}
#endif // TEST_VARY_RPM
#endif // TEST_MODE

/**
 * FreeRTOS task for processing hall sensor events
 */
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
#ifdef TEST_MODE
    QueueHandle_t queue = testHallQueue;  // Use simulated queue in TEST_MODE
#else
    QueueHandle_t queue = hallDriver.getEventQueue();  // Use real hall sensor queue
#endif
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

    // Initialize LED strip with custom SPI pins
    // Begin(sck, miso, mosi, ss)
    strip.Begin(HardwareConfig::SPI_CLK_PIN, -1, HardwareConfig::SPI_DATA_PIN, -1);
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

#ifdef TEST_MODE
    // TEST_MODE: Setup timer-based hall sensor simulation
    Serial.println("TEST_MODE: Initializing timer-based hall simulation");

    // Create event queue (size 1, same as HallEffectDriver)
    testHallQueue = xQueueCreate(1, sizeof(HallEffectEvent));
    if (testHallQueue == nullptr) {
        Serial.println("ERROR: Failed to create test hall queue");
        while (1) { delay(1000); }
    }

    // Create main hall timer
    esp_timer_create_args_t hallTimerArgs = {
        .callback = testHallTimerCallback,
        .arg = (void*)testHallQueue,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "test_hall_timer"
    };

    esp_err_t err = esp_timer_create(&hallTimerArgs, &testHallTimer);
    if (err != ESP_OK) {
        Serial.printf("ERROR: Failed to create hall timer: %d\n", err);
        while (1) { delay(1000); }
    }

    // Calculate interval from TEST_RPM
    uint64_t intervalUs = static_cast<uint64_t>(60000000.0 / TEST_RPM);
    Serial.printf("TEST_MODE: Starting hall timer at %.1f RPM (interval: %llu us)\n",
                  TEST_RPM, intervalUs);

    // Start periodic timer
    err = esp_timer_start_periodic(testHallTimer, intervalUs);
    if (err != ESP_OK) {
        Serial.printf("ERROR: Failed to start hall timer: %d\n", err);
        while (1) { delay(1000); }
    }

#if TEST_VARY_RPM
    // Create RPM updater timer (fires every 100ms to adjust RPM)
    esp_timer_create_args_t rpmUpdaterArgs = {
        .callback = testRpmUpdaterCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "test_rpm_updater"
    };

    err = esp_timer_create(&rpmUpdaterArgs, &testRpmUpdaterTimer);
    if (err != ESP_OK) {
        Serial.printf("ERROR: Failed to create RPM updater timer: %d\n", err);
        while (1) { delay(1000); }
    }

    // Start updater timer (100ms period)
    err = esp_timer_start_periodic(testRpmUpdaterTimer, 100000);
    if (err != ESP_OK) {
        Serial.printf("ERROR: Failed to start RPM updater timer: %d\n", err);
        while (1) { delay(1000); }
    }
    Serial.println("TEST_MODE: Variable RPM enabled");
#endif // TEST_VARY_RPM

    Serial.println("TEST_MODE: Hall simulation initialized");
#else
    // Setup hall effect sensor (real hardware)
    hallDriver.start();
    Serial.println("Hall effect sensor initialized");
#endif // TEST_MODE

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
    // Get atomic snapshot of timing values
    // In test mode: testHallTimerCallback() posts events to queue → hallProcessingTask
    // In real mode: sensorTriggered_ISR() posts events to queue → hallProcessingTask
    // Both paths exercise identical queue/task integration
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
    angle_t angleMiddleUnits, angleOuterUnits, angleInsideUnits;
    if (microsecondsPerRev > 0) {
        angleMiddleUnits = static_cast<angle_t>(
            (static_cast<uint32_t>(elapsed) * 3600UL / microsecondsPerRev) % 3600
        );
        angleOuterUnits = (angleMiddleUnits + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
        angleInsideUnits = (angleMiddleUnits + INSIDE_ARM_PHASE) % ANGLE_FULL_CIRCLE;
    } else {
        // Not rotating - use safe default angles (all at 0°)
        angleMiddleUnits = angleOuterUnits = angleInsideUnits = 0;
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
        angleOuterUnits = (slotBoundaryAngle + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
        angleInsideUnits = (slotBoundaryAngle + INSIDE_ARM_PHASE) % ANGLE_FULL_CIRCLE;

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

        // Set arm angles - arms[0]=outer, arms[1]=middle(hall), arms[2]=inside
        renderCtx.arms[0].angleUnits = angleOuterUnits;
        renderCtx.arms[1].angleUnits = angleMiddleUnits;
        renderCtx.arms[2].angleUnits = angleInsideUnits;

        // Render current effect
        Effect* current = effectRegistry.current();
        if (current) {
            current->render(renderCtx);
        }

        // Copy arm buffers to LED strip
        for (int a = 0; a < 3; a++) {
            uint16_t start = HardwareConfig::ARM_START[a];
            for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
                CRGB color = renderCtx.arms[a].pixels[p];
                color.nscale8(GLOBAL_BRIGHTNESS);  // Power budget (slip ring provides full power)
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
