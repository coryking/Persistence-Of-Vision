#include <Arduino.h>
#include <WiFi.h>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "geometry.h"
#include "RevolutionTimer.h"
#include "HallEffectDriver.h"
#include "RenderContext.h"
#include "EffectManager.h"
#include "ESPNowComm.h"
#include "effects/NoiseField.h"
#include "effects/Radar.h"
#include "effects/SolidArms.h"
#include "effects/RpmArc.h"
#include "effects/PerArmBlobs.h"
#include "effects/VirtualBlobs.h"
#include "effects/ArmAlignment.h"
#include "effects/PulseChaser.h"
#include "effects/MomentumFlywheel.h"
#include "effects/CalibrationEffect.h"
#include "Imu.h"
#include "TelemetryTask.h"
#include "timing_utils.h"
#include "hardware_config.h"
#include "SlotTiming.h"
#include "HallSimulator.h"
#include "FrameProfiler.h"
#include "RotorDiagnosticStats.h"

// Phase offsets defined in types.h:
// OUTER_ARM_PHASE = 2400 units = 240° (arm[0])
// MIDDLE_ARM_PHASE = 0 units = 0° (arm[1], hall sensor reference)
// INSIDE_ARM_PHASE = 1200 units = 120° (arm[2])

// POV Display Configuration
#define WARMUP_REVOLUTIONS 20
#define ROLLING_AVERAGE_SIZE 20
#define ROTATION_TIMEOUT_US 10000000  // 10 seconds for hand-spin support (6 RPM min)

// ESP32-S3 hardware SPI for HD107s/SK9822/APA102 (DotStar)
// 41 physical LEDs: 1 level shifter + 40 display LEDs
// DotStarLbgrFeature: 'L' exposes 5-bit luminance byte for HD gamma decomposition
NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip(HardwareConfig::TOTAL_PHYSICAL_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// Hall effect sensor driver
HallEffectDriver hallDriver(HardwareConfig::HALL_PIN);

// Effect instances
NoiseField noiseFieldEffect;
Radar radarEffect;
SolidArms solidArmsEffect;
RpmArc rpmArcEffect;
PerArmBlobs perArmBlobsEffect;
VirtualBlobs virtualBlobsEffect;
ArmAlignment armAlignmentEffect;
PulseChaser pulseChaserEffect;
MomentumFlywheel momentumFlywheelEffect;
CalibrationEffect calibrationEffect;

// Effect manager
EffectManager effectManager;

// Double-buffered render contexts for dual-core pipeline
static RenderContext g_renderCtx[2];
static uint8_t g_writeBuffer = 0;

// Frame handoff from render (Core 1) to output (Core 0)
struct FrameCommand {
    uint8_t bufferIndex;
    timestamp_t targetTime;
    uint32_t frameCount;
};
static QueueHandle_t g_frameQueue = nullptr;
static SemaphoreHandle_t g_bufferFree[2] = {nullptr, nullptr};

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
    static timestamp_t lastHallTimestamp = 0;  // For period calculation

    while (1) {
        if (xQueueReceive(g_hallEventQueue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);

            // Record hall event for diagnostics (always, regardless of calibration state)
            RotorDiagnosticStats::instance().recordHallEvent();

            // Update smoothed hall period in diagnostics
            period_t avgUs = revTimer.getMicrosecondsPerRevolution();
            if (avgUs > 0) {
                RotorDiagnosticStats::instance().setHallAvgUs(avgUs);
            }

            // Calculate period since last hall trigger
            uint32_t period_us = (lastHallTimestamp > 0)
                ? static_cast<uint32_t>(event.triggerTimestamp - lastHallTimestamp)
                : 0;
            lastHallTimestamp = event.triggerTimestamp;

            // Detect motor start (stopped → rotating transition)
            bool isRotating = revTimer.isCurrentlyRotating();
            if (!wasRotating && isRotating) {
                revolutionCount = 1;
            }
            wasRotating = isRotating;

            // If calibration is active, send hall event for each trigger
            if (g_calibrationActive) {
                sendHallEvent(
                    event.triggerTimestamp,
                    period_us,
                    static_cast<rotation_t>(revolutionCount)
                );
            }

            // Notify current effect of revolution
            effectManager.onRevolution(revTimer.getMicrosecondsPerRevolution(), event.triggerTimestamp, revolutionCount++);

            if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
                Serial.println("Warm-up complete! Display active.");
            }
        }
    }
}

// Track which slot was last rendered (persists across loop iterations)
static int g_lastRenderedSlot = -1;

/**
 * Output task - runs on Core 0
 *
 * Handles the timing-critical output path:
 * 1. copyPixelsToStrip() - copy from RenderContext to LED strip
 * 2. waitForTargetTime() - busy-wait for precise angular position
 * 3. strip.Show() - fire DMA transfer
 *
 * This runs in parallel with rendering on Core 1, roughly halving
 * the effective frame time.
 */
void outputTask(void* pvParameters) {
    (void)pvParameters;
    FrameCommand cmd;

    while (true) {
        // Wait for a frame to be ready (with timeout to avoid wedging)
        if (xQueueReceive(g_frameQueue, &cmd, pdMS_TO_TICKS(100)) == pdPASS) {
            RenderContext& ctx = g_renderCtx[cmd.bufferIndex];

            g_outputProfiler.markStart(cmd.frameCount);

            // Copy rendered pixels to LED strip buffer
            copyPixelsToStrip(ctx, strip);
            g_outputProfiler.markCopyEnd();

            // Busy-wait until the disc reaches target angular position
            waitForTargetTime(cmd.targetTime);
            g_outputProfiler.markWaitEnd();

            // Fire DMA transfer at precise moment
            strip.Show();
            g_outputProfiler.markShowEnd();

            // Record successful render in diagnostics
            RotorDiagnosticStats::instance().recordRenderEvent(true, false);

            // Release buffer back to render task
            xSemaphoreGive(g_bufferFree[cmd.bufferIndex]);

            // Emit profiler output (respects sample interval)
            g_outputProfiler.emit();
        }
    }
}

// ============================================================================
// Setup Helper Functions
// ============================================================================

void setupLedStrip() {
    // Initialize LED strip with custom SPI pins
    strip.Begin(HardwareConfig::SPI_CLK_PIN, -1, HardwareConfig::SPI_DATA_PIN, -1);
    strip.ClearTo(RgbwColor(0, 0, 0, 0));
    strip.Show();
    Serial.println("Strip initialized");

    // Startup blink pattern - diagnostic for reset detection
    // If you see periodic blinks during operation, ESP32 is resetting
    // Uses 5% brightness via 5-bit field to avoid power issues on wireless USB power
    Serial.println("Startup blink sequence...");
    constexpr uint8_t BOOT_BRIGHTNESS_5BIT = 2;  // Low 5-bit brightness (~6% of max)
    const RgbwColor bootColors[3] = {
        RgbwColor(255, 0, 0, BOOT_BRIGHTNESS_5BIT),    // Red
        RgbwColor(0, 255, 0, BOOT_BRIGHTNESS_5BIT),    // Green
        RgbwColor(0, 0, 255, BOOT_BRIGHTNESS_5BIT)     // Blue
    };

    for (int flash = 0; flash < 3; flash++) {
        // Each arm gets a different color, rotating each flash
        for (uint16_t arm = 0; arm < HardwareConfig::NUM_ARMS; arm++) {
            uint8_t colorIdx = (arm + flash) % 3;  // Rotate colors each flash
            uint16_t startLed = HardwareConfig::ARM_START[arm];
            for (uint16_t led = 0; led < HardwareConfig::LEDS_PER_ARM; led++) {
                strip.SetPixelColor(startLed + led, bootColors[colorIdx]);
            }
        }
        strip.Show();
        delay(500);
        strip.ClearTo(RgbwColor(0, 0, 0, 0));
        strip.Show();
        delay(500);
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
        4096,  // Increased from 2048 - was causing stack overflow
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

void startOutputTask() {
    // Create frame handoff queue (depth 1 - render blocks if output is behind)
    g_frameQueue = xQueueCreate(1, sizeof(FrameCommand));
    if (!g_frameQueue) {
        Serial.println("ERROR: Failed to create frame queue");
        while (1) { delay(1000); }
    }

    // Create buffer semaphores (binary - one per buffer)
    g_bufferFree[0] = xSemaphoreCreateBinary();
    g_bufferFree[1] = xSemaphoreCreateBinary();
    if (!g_bufferFree[0] || !g_bufferFree[1]) {
        Serial.println("ERROR: Failed to create buffer semaphores");
        while (1) { delay(1000); }
    }

    // Pre-give both semaphores so first renders don't block
    xSemaphoreGive(g_bufferFree[0]);
    xSemaphoreGive(g_bufferFree[1]);

    // Start output task on Core 0, priority 2 (below hall sensor at 3)
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        outputTask,
        "output",
        4096,
        nullptr,
        2,
        nullptr,
        0  // Core 0
    );

    if (taskCreated != pdPASS) {
        Serial.println("ERROR: Failed to create output task");
        while (1) { delay(1000); }
    }
    Serial.println("Output task started on Core 0");
}

void setupImu() {
    if (imu.begin()) {
        Serial.println("IMU initialized");
        // Initialize telemetry task (waits for CalibrationEffect to start it)
        telemetryTaskInit();
    } else {
        Serial.println("WARNING: IMU init failed - calibration unavailable");
    }
}

void registerEffects() {
    effectManager.registerEffect(&radarEffect);
    effectManager.registerEffect(&noiseFieldEffect);
    effectManager.registerEffect(&solidArmsEffect);
    effectManager.registerEffect(&rpmArcEffect);
    effectManager.registerEffect(&perArmBlobsEffect);
    effectManager.registerEffect(&virtualBlobsEffect);
    effectManager.registerEffect(&armAlignmentEffect);
    effectManager.registerEffect(&pulseChaserEffect);
    effectManager.registerEffect(&momentumFlywheelEffect);
    effectManager.registerEffect(&calibrationEffect);  // Effect 10 for rotor balancing

    Serial.printf("Registered %d effects\n", effectManager.getEffectCount());
}

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    // Disable Bluetooth to reduce jitter (WiFi needed for ESP-NOW)
    btStop();

    Serial.println("POV Display Initializing...");

    setupLedStrip();
    setupHallSensor();
    startHallProcessingTask();
    startOutputTask();  // Dual-core render pipeline
    setupImu();
    registerEffects();

    // Initialize ESP-NOW communication with motor controller
    setupESPNow();

    // Initialize effect manager (creates queue, starts first effect)
    effectManager.begin();

    // Start diagnostic stats collection (sends to motor controller every 500ms)
    RotorDiagnosticStats::instance().setEffectNumber(1);  // Initial effect
    RotorDiagnosticStats::instance().setBrightness(effectManager.getBrightness());
    RotorDiagnosticStats::instance().start(500);

    Serial.printf("Starting with effect 1\n");
    Serial.println("\n=== POV Display Ready ===");
}

void loop() {
    // Process any pending effect/brightness commands
    effectManager.processCommands();

    // Check if display is powered off (motor controller sent power off)
    if (!effectManager.isDisplayEnabled()) {
        handleNotRotating(strip);
        g_lastRenderedSlot = -1;  // Reset so we start fresh when powered back on
        g_renderProfiler.reset();
        g_outputProfiler.reset();
        return;
    }

    // ========== DUAL-CORE RENDER PIPELINE ==========
    // Core 1 (this loop): Calculate slot, render effect to buffer
    // Core 0 (output task): Copy to strip, wait for angle, Show()
    //
    // Double-buffering lets both cores work in parallel:
    // While Core 0 outputs frame N, Core 1 renders frame N+1

    // 1. Get atomic snapshot of timing values
    TimingSnapshot timing = revTimer.getTimingSnapshot();

    // 2. Handle not-rotating state
    if (!timing.isRotating || !timing.warmupComplete) {
        RotorDiagnosticStats::instance().recordRenderEvent(false, true);  // notRotating=true
        handleNotRotating(strip);
        g_lastRenderedSlot = -1;  // Reset on stop so we start fresh
        g_renderProfiler.reset();
        g_outputProfiler.reset();
        return;
    }

    // 3. Calculate next slot to render (always advance from last rendered)
    SlotTarget target = calculateNextSlot(g_lastRenderedSlot, timing);

    // 4. Check if we're behind schedule (past target time)
    timestamp_t now = esp_timer_get_time();
    if (now > target.targetTime) {
        // We're behind - skip this slot and try the next one
        RotorDiagnosticStats::instance().recordRenderEvent(false, false);  // skip
        g_lastRenderedSlot = target.slotNumber;
        return;
    }

    // 5. Wait for our buffer to be free (usually instant due to double-buffering)
    RenderContext& ctx = g_renderCtx[g_writeBuffer];
    xSemaphoreTake(g_bufferFree[g_writeBuffer], portMAX_DELAY);

    // 6. Render for TARGET angle (future position)
    revTimer.startRender();

    uint32_t thisFrame = globalFrameCount++;
    g_renderProfiler.markStart(thisFrame);

    // Populate render context with target angle (not current angle!)
    interval_t microsecondsPerRev = timing.lastActualInterval;
    if (microsecondsPerRev == 0) microsecondsPerRev = timing.microsecondsPerRev;

    ctx.frameCount = thisFrame;
    ctx.timeUs = static_cast<uint32_t>(now);
    ctx.microsPerRev = microsecondsPerRev;
    ctx.slotSizeUnits = target.slotSize;

    // Set arm angles from target - arms[0]=outer, arms[1]=middle(hall), arms[2]=inside
    ctx.arms[0].angleUnits = (target.angleUnits + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
    ctx.arms[1].angleUnits = target.angleUnits;
    ctx.arms[2].angleUnits = (target.angleUnits + INSIDE_ARM_PHASE) % ANGLE_FULL_CIRCLE;

    // Render current effect
    Effect* current = effectManager.current();
    if (current) {
        current->render(ctx);
    }
    g_renderProfiler.markRenderEnd();

    revTimer.endRender();

    // 7. Hand off to output task
    FrameCommand cmd = {g_writeBuffer, target.targetTime, thisFrame};
    xQueueSend(g_frameQueue, &cmd, 0);  // Non-blocking (queue depth 1)
    g_renderProfiler.markQueueEnd();

    // 8. Emit profiler output and flip buffer
    g_renderProfiler.emit();

    g_writeBuffer = 1 - g_writeBuffer;
    g_lastRenderedSlot = target.slotNumber;
}
