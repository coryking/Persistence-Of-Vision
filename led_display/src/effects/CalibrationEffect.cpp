#include "effects/CalibrationEffect.h"
#include "TelemetryTask.h"
#include "hardware_config.h"
#include <esp_log.h>
#include <FastLED.h>

static const char* TAG = "CAL";

// Global calibration flag
volatile bool g_calibrationActive = false;

void CalibrationEffect::begin() {
    ESP_LOGI(TAG, "Calibration mode ACTIVE - telemetry task streaming accel data");

    m_hue = 0;

    // Enable hall event streaming
    g_calibrationActive = true;

    // Start the telemetry task (handles all accel sampling)
    telemetryTaskStart();

    // Print start marker (motor controller will echo this)
    ESP_LOGI(TAG, "# CAL_START");
}

void CalibrationEffect::end() {
    // Stop the telemetry task (flushes remaining samples)
    telemetryTaskStop();

    // Disable hall event streaming
    g_calibrationActive = false;

    ESP_LOGI(TAG, "# CAL_STOP");
    ESP_LOGI(TAG, "Calibration mode ended");
}

void CalibrationEffect::render(RenderContext& ctx) {
    // Visual feedback: one LED per arm cycles through hues each revolution
    for (int arm = 0; arm < HardwareConfig::NUM_ARMS; arm++) {
        ctx.arms[arm].pixels[0] = CHSV(m_hue, 255, 128);  // Half brightness to save power
        for (int p = 1; p < HardwareConfig::LEDS_PER_ARM; p++) {
            ctx.arms[arm].pixels[p] = CRGB::Black;
        }
    }

    // Accelerometer sampling is handled by TelemetryTask - nothing to do here
}

void CalibrationEffect::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Cycle hue each revolution for visual feedback (0-255 for FastLED)
    m_hue = static_cast<uint8_t>((m_hue + 1) % 256);

    (void)usPerRev;       // Unused
    (void)timestamp;      // Unused
    (void)revolutionCount; // Unused
}
