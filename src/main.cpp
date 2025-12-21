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
#include "effects/ArmAlignment.h"
#include "timing_utils.h"
#include "hardware_config.h"
#include "SlotTiming.h"

// Hardware Configuration

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
ArmAlignment armAlignmentEffect;

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

// Track which slot was last rendered (persists across loop iterations)
static int g_lastRenderedSlot = -1;

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
    //effectRegistry.registerEffect(&noiseFieldEffect);
    effectRegistry.registerEffect(&solidArmsEffect);
    //effectRegistry.registerEffect(&rpmArcEffect);
    //effectRegistry.registerEffect(&perArmBlobsEffect);
    //effectRegistry.registerEffect(&virtualBlobsEffect);
    //effectRegistry.registerEffect(&armAlignmentEffect);

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
    // ========== PRECISION TIMING MODEL ==========
    // We render for a FUTURE angular position, then wait until the disc
    // reaches that position before firing Show(). This ensures the angle
    // told to the renderer matches where LEDs actually illuminate.

    // 1. Get atomic snapshot of timing values
    TimingSnapshot timing = revTimer.getTimingSnapshot();

    // 2. Handle not-rotating state
    if (!timing.isRotating || !timing.warmupComplete) {
        handleNotRotating(strip);
        g_lastRenderedSlot = -1;  // Reset on stop so we start fresh
        return;
    }

    // 3. Calculate next slot to render (always advance from last rendered)
    SlotTarget target = calculateNextSlot(g_lastRenderedSlot, timing);

    // 4. Check if we're behind schedule (past target time)
    timestamp_t now = esp_timer_get_time();
    if (now > target.targetTime) {
        // We're behind - skip this slot and try the next one
        g_lastRenderedSlot = target.slotNumber;
        return;
    }

    // 5. Render for TARGET angle (future position)
    revTimer.startRender();

#if ENABLE_TIMING_INSTRUMENTATION
    if (!csvHeaderPrinted) {
        Serial.println("frame,effect,total_us,slot,angle_units,usec_per_rev,resolution_units,rev_count,angular_res,last_interval_us");
        csvHeaderPrinted = true;
    }
    int64_t frameStart = timingStart();
#endif

    // Populate render context with target angle (not current angle!)
    interval_t microsecondsPerRev = timing.lastActualInterval;
    if (microsecondsPerRev == 0) microsecondsPerRev = timing.microsecondsPerRev;

    renderCtx.frameCount = globalFrameCount++;
    renderCtx.timeUs = static_cast<uint32_t>(now);
    renderCtx.microsPerRev = microsecondsPerRev;
    renderCtx.slotSizeUnits = target.slotSize;

    // Set arm angles from target - arms[0]=outer, arms[1]=middle(hall), arms[2]=inside
    renderCtx.arms[0].angleUnits = (target.angleUnits + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
    renderCtx.arms[1].angleUnits = target.angleUnits;
    renderCtx.arms[2].angleUnits = (target.angleUnits + INSIDE_ARM_PHASE) % ANGLE_FULL_CIRCLE;

    // Render current effect
    Effect* current = effectRegistry.current();
    if (current) {
        current->render(renderCtx);
    }

    // Copy arm buffers to LED strip (NeoPixelBus double-buffers, so this is safe)
    copyPixelsToStrip(renderCtx, strip);

    revTimer.endRender();

    // 6. Wait for precise moment (busy-wait)
    waitForTargetTime(target.targetTime);

    // 7. Fire at exact angular position
    strip.Show();

    // 8. Update state for next iteration
    g_lastRenderedSlot = target.slotNumber;

#if ENABLE_TIMING_INSTRUMENTATION
    int64_t totalTime = timingEnd(frameStart);
    Serial.printf("%u,%u,%lld,%d,%u,%llu,%u,%u,%f,%llu\n",
                  renderCtx.frameCount,
                  effectRegistry.getCurrentIndex(),
                  totalTime,
                  target.slotNumber,
                  target.angleUnits,
                  microsecondsPerRev,
                  target.slotSize,
                  revTimer.getRevolutionCount(),
                  timing.angularResolution,
                  timing.lastActualInterval);
#endif
}
