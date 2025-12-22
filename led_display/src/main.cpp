#include <Arduino.h>
#include <WiFi.h>
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
#include "effects/NoiseFieldRGB.h"
#include "effects/SolidArms.h"
#include "effects/RpmArc.h"
#include "effects/PerArmBlobs.h"
#include "effects/VirtualBlobs.h"
#include "effects/ArmAlignment.h"
#include "effects/PulseChaser.h"
#include "effects/MomentumFlywheel.h"
#include "timing_utils.h"
#include "hardware_config.h"
#include "SlotTiming.h"
#include "HallSimulator.h"
#include "FrameProfiler.h"

// Phase offsets defined in types.h:
// OUTER_ARM_PHASE = 2400 units = 240° (arm[0])
// MIDDLE_ARM_PHASE = 0 units = 0° (arm[1], hall sensor reference)
// INSIDE_ARM_PHASE = 1200 units = 120° (arm[2])

// POV Display Configuration
#define WARMUP_REVOLUTIONS 20
#define ROLLING_AVERAGE_SIZE 20
#define ROTATION_TIMEOUT_US 10000000  // 10 seconds for hand-spin support (6 RPM min)

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(HardwareConfig::TOTAL_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// Hall effect sensor driver
HallEffectDriver hallDriver(HardwareConfig::HALL_PIN);

// Effect instances
NoiseField noiseFieldEffect;
NoiseFieldRGB noiseFieldRGBEffect;
SolidArms solidArmsEffect;
RpmArc rpmArcEffect;
PerArmBlobs perArmBlobsEffect;
VirtualBlobs virtualBlobsEffect;
ArmAlignment armAlignmentEffect;
PulseChaser pulseChaserEffect;
MomentumFlywheel momentumFlywheelEffect;

// Effect registry
EffectRegistry effectRegistry;

// Effect scheduler (manages NVS persistence)
EffectScheduler effectScheduler;

// Render context (reused each frame)
RenderContext renderCtx;

// Global frame counter (shared via RenderContext)
uint32_t globalFrameCount = 0;

// Hall sensor event queue (set during setup - either simulator or real hardware)
static QueueHandle_t g_hallEventQueue = nullptr;

/**
 * FreeRTOS task for processing hall sensor events
 */
void hallProcessingTask(void* pvParameters) {
    (void)pvParameters;
    HallEffectEvent event;
    bool wasRotating = false;  // Track motor state
    static uint16_t revolutionCount = 1;

    while (1) {
        if (xQueueReceive(g_hallEventQueue, &event, portMAX_DELAY) == pdPASS) {
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

// ============================================================================
// Setup Helper Functions
// ============================================================================

void setupLedStrip() {
    // Initialize LED strip with custom SPI pins
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
}

void setupHallSensor() {
    // Try simulator first (returns nullptr if TEST_MODE not defined)
    g_hallEventQueue = HallSimulator::begin();
    if (!g_hallEventQueue) {
        // Real hardware
        hallDriver.start();
        g_hallEventQueue = hallDriver.getEventQueue();
        Serial.println("Hall effect sensor initialized");
    }
}

void startHallProcessingTask() {
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
}

void registerEffects() {
    //effectRegistry.registerEffect(&noiseFieldEffect);
    //effectRegistry.registerEffect(&noiseFieldRGBEffect);
    effectRegistry.registerEffect(&solidArmsEffect);
    //effectRegistry.registerEffect(&rpmArcEffect);
    //effectRegistry.registerEffect(&perArmBlobsEffect);
    //effectRegistry.registerEffect(&virtualBlobsEffect);
    //effectRegistry.registerEffect(&armAlignmentEffect);
    effectRegistry.registerEffect(&pulseChaserEffect);
    effectRegistry.registerEffect(&momentumFlywheelEffect);

    Serial.printf("Registered %d effects\n", effectRegistry.getEffectCount());
}

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    // Disable WiFi/BT to reduce jitter
    WiFi.mode(WIFI_OFF);
    btStop();

    Serial.println("POV Display Initializing...");

    setupLedStrip();
    setupHallSensor();
    startHallProcessingTask();
    registerEffects();

    // Initialize scheduler (loads NVS, advances effect, saves, starts registry)
    effectScheduler.begin(&effectRegistry);

    Serial.printf("Starting with effect %u\n", effectRegistry.getCurrentIndex());
    Serial.println("\n=== POV Display Ready ===");
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
    auto frameHandle = FrameProfiler::frameStart();

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

    FrameProfiler::frameEnd(frameHandle,
                            renderCtx.frameCount,
                            effectRegistry.getCurrentIndex(),
                            target.slotNumber,
                            target.angleUnits,
                            microsecondsPerRev,
                            target.slotSize,
                            revTimer.getRevolutionCount(),
                            timing.angularResolution,
                            timing.lastActualInterval);
}
