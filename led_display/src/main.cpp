#include <Arduino.h>
#include <WiFi.h>
#include "esp_timer.h"
#include "esp_log.h"
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
#include "BufferManager.h"
#include "RenderTask.h"
#include "OutputTask.h"

static const char* TAG = "MAIN";

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
                ESP_LOGI(TAG, "Warm-up complete! Display active.");
            }
        }
    }
}

// (Output task moved to OutputTask.cpp)

// ============================================================================
// Setup Helper Functions
// ============================================================================

void setupLedStrip() {
    // Initialize LED strip with custom SPI pins
    strip.Begin(HardwareConfig::SPI_CLK_PIN, -1, HardwareConfig::SPI_DATA_PIN, -1);
    strip.ClearTo(RgbwColor(0, 0, 0, 0));
    strip.Show();
    ESP_LOGI(TAG, "Strip initialized");

    // Startup blink pattern - diagnostic for reset detection
    // If you see periodic blinks during operation, ESP32 is resetting
    // Uses 5% brightness via 5-bit field to avoid power issues on wireless USB power
    ESP_LOGI(TAG, "Startup blink sequence...");
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
    ESP_LOGI(TAG, "Startup blink complete");
}

void setupHallSensor() {
    // Try simulator first (returns nullptr if TEST_MODE not defined)
    g_hallEventQueue = HallSimulator::begin();
    if (!g_hallEventQueue) {
        // Real hardware
        hallDriver.start();
        g_hallEventQueue = hallDriver.getEventQueue();
        ESP_LOGI(TAG, "Hall effect sensor initialized");
    }
}

void startHallProcessingTask() {
    // Pin to Core 1 (same core as RenderTask)
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        hallProcessingTask,
        "hallProcessor",
        4096,
        nullptr,
        3,
        nullptr,
        1  // Core 1
    );

    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hall processing task");
        while (1) { delay(1000); }
    }
    ESP_LOGI(TAG, "Hall processing task started on Core 1");
}

void setupImu() {
    if (imu.begin()) {
        ESP_LOGI(TAG, "IMU initialized");
        // Initialize telemetry task (waits for CalibrationEffect to start it)
        telemetryTaskInit();
    } else {
        ESP_LOGW(TAG, "IMU init failed - calibration unavailable");
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

    ESP_LOGI(TAG, "Registered %d effects", effectManager.getEffectCount());
}

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    // Disable Bluetooth to reduce jitter (WiFi needed for ESP-NOW)
    btStop();

    ESP_LOGI(TAG, "POV Display Initializing...");
#ifdef ENABLE_TIMING_INSTRUMENTATION
    ESP_LOGI(TAG, "Timing instrumentation enabled (FrameProfiler analytics active)");
    initProfilerAnalytics();
#else
    ESP_LOGW(TAG, "Timing instrumentation disabled (FrameProfiler analytics inactive)");
#endif

    setupLedStrip();
    setupHallSensor();
    setupImu();
    registerEffects();

    // Initialize ESP-NOW communication with motor controller
    setupESPNow();

    // Initialize effect manager (creates queue, starts first effect)
    effectManager.begin();

    // Initialize buffer manager and start dual-core render pipeline
    bufferManager.init();
    outputTask.start();
    renderTask.start();
    startHallProcessingTask();

    // Start diagnostic stats collection (sends to motor controller every 500ms)
    RotorDiagnosticStats::instance().setEffectNumber(1);  // Initial effect
    RotorDiagnosticStats::instance().setBrightness(effectManager.getBrightness());
    RotorDiagnosticStats::instance().start(500);

    ESP_LOGI(TAG, "Starting with effect 1");
    ESP_LOGI(TAG, "=== POV Display Ready ===");
}

void loop() {
    // Process any pending effect/brightness commands
    effectManager.processCommands();

    // Power state management - suspend/resume tasks
    static bool wasEnabled = true;
    bool isEnabled = effectManager.isDisplayEnabled();

    if (wasEnabled && !isEnabled) {
        // Power off
        renderTask.suspend();
        outputTask.suspend();
        strip.ClearTo(RgbwColor(0, 0, 0, 0));
        strip.Show();
        g_renderProfiler.reset();
        g_outputProfiler.reset();
        ESP_LOGI(TAG, "Display powered off");
    } else if (!wasEnabled && isEnabled) {
        // Power on
        outputTask.resume();
        renderTask.resume();
        ESP_LOGI(TAG, "Display powered on");
    }
    wasEnabled = isEnabled;

    vTaskDelay(pdMS_TO_TICKS(10));
}
