#include "effects/CalibrationEffect.h"
#include "ESPNowComm.h"
#include "hardware_config.h"
#include <Arduino.h>
#include <FastLED.h>

// Global calibration flag
volatile bool g_calibrationActive = false;

void CalibrationEffect::begin() {
    Serial.println("[CAL] Calibration mode ACTIVE - streaming accel data");

    // Initialize batch
    m_msg.sample_count = 0;
    m_lastSampleTime = 0;
    m_hue = 0;

    // Enable hall event streaming
    g_calibrationActive = true;

    // Print start marker (motor controller will echo this)
    Serial.println("# CAL_START");
}

void CalibrationEffect::end() {
    // Flush any remaining samples
    if (m_msg.sample_count > 0) {
        flushBatch();
    }

    // Disable hall event streaming
    g_calibrationActive = false;

    Serial.println("# CAL_STOP");
    Serial.println("[CAL] Calibration mode ended");
}

void CalibrationEffect::render(RenderContext& ctx) {
    // Visual feedback: one LED per arm cycles through hues each revolution
    // This lets you see the effect is active without using serial
    for (int arm = 0; arm < HardwareConfig::NUM_ARMS; arm++) {
        // First LED shows current hue, rest are off
        ctx.arms[arm].pixels[0] = CHSV(m_hue, 255, 128);  // Half brightness to save power
        for (int p = 1; p < HardwareConfig::LEDS_PER_ARM; p++) {
            ctx.arms[arm].pixels[p] = CRGB::Black;
        }
    }

    // Poll accelerometer at ~400Hz
    timestamp_t now = esp_timer_get_time();
    if (now - m_lastSampleTime >= SAMPLE_INTERVAL_US) {
        m_lastSampleTime = now;

        xyzFloat reading;
        if (accel.read(reading)) {
            addSample(reading, now);
        }
    }
}

void CalibrationEffect::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Hall events are sent by hallProcessingTask when g_calibrationActive is true
    (void)usPerRev;
    (void)timestamp;
    (void)revolutionCount;

    // Cycle hue each revolution for visual feedback (0-255 for FastLED)
    m_hue = (m_hue + 1) % 256;
}

void CalibrationEffect::addSample(const xyzFloat& reading, timestamp_t timestamp) {
    // Add sample to batch with absolute timestamp
    AccelSample& sample = m_msg.samples[m_msg.sample_count];
    sample.timestamp_us = timestamp;
    sample.x = reading.x;
    sample.y = reading.y;
    sample.z = reading.z;
    m_msg.sample_count++;

    // Flush batch if full
    if (m_msg.sample_count >= ACCEL_SAMPLES_MAX_BATCH) {
        flushBatch();
    }
}

void CalibrationEffect::flushBatch() {
    if (m_msg.sample_count == 0) return;

    // Send via ESP-NOW
    sendAccelSamples(m_msg);

    // Reset batch
    m_msg.sample_count = 0;
}
