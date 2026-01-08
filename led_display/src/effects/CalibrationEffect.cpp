#include "effects/CalibrationEffect.h"
#include "ESPNowComm.h"
#include "hardware_config.h"
#include <Arduino.h>

// Global calibration flag
volatile bool g_calibrationActive = false;

void CalibrationEffect::begin() {
    Serial.println("[CAL] Calibration mode ACTIVE - streaming accel data");

    // Initialize batch
    m_msg.sample_count = 0;
    m_batchStartTime = 0;
    m_lastSampleTime = 0;

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
    // Keep all LEDs off - we're not rendering, just sampling
    for (int arm = 0; arm < HardwareConfig::NUM_ARMS; arm++) {
        for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
            ctx.arms[arm].pixels[p] = CRGB::Black;
        }
    }

    // Poll accelerometer at ~400Hz
    uint32_t now = esp_timer_get_time();
    if (now - m_lastSampleTime >= SAMPLE_INTERVAL_US) {
        m_lastSampleTime = now;

        Accelerometer::Reading reading;
        if (accel.read(reading)) {
            addSample(reading, now);
        }
    }
}

void CalibrationEffect::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Hall events are sent by hallProcessingTask when g_calibrationActive is true
    // Nothing extra needed here
    (void)usPerRev;
    (void)timestamp;
    (void)revolutionCount;
}

void CalibrationEffect::addSample(const Accelerometer::Reading& reading, uint32_t timestamp) {
    // Start new batch if needed
    if (m_msg.sample_count == 0) {
        m_batchStartTime = timestamp;
        m_msg.base_timestamp_us = timestamp;
    }

    // Add sample to batch
    AccelSample& sample = m_msg.samples[m_msg.sample_count];
    sample.delta_us = static_cast<uint16_t>(timestamp - m_batchStartTime);
    sample.x = reading.x;
    sample.y = reading.y;
    sample.z = reading.z;
    m_msg.sample_count++;

    // Flush batch if full
    if (m_msg.sample_count >= BATCH_SIZE) {
        flushBatch();
    }
}

void CalibrationEffect::flushBatch() {
    if (m_msg.sample_count == 0) return;

    // Send via ESP-NOW
    sendAccelSamples(m_msg);

    // Reset batch
    m_msg.sample_count = 0;
    m_batchStartTime = 0;
}
