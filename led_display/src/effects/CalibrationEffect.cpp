#include "effects/CalibrationEffect.h"
#include "ESPNowComm.h"
#include "hardware_config.h"
#include <Arduino.h>
#include <FastLED.h>

// Global calibration flag
volatile bool g_calibrationActive = false;

void CalibrationEffect::begin() {
    Serial.println("[CAL] Calibration mode ACTIVE - streaming accel data (800Hz, interrupt-driven)");

    // Initialize batch and counters
    m_msg.sample_count = 0;
    m_sequenceNum = 0;
    m_currentRotation = 0;
    m_lastHallTimestamp = 0;
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
    Serial.printf("[CAL] Calibration mode ended (%u samples sent)\n", m_sequenceNum);
}

void CalibrationEffect::render(RenderContext& ctx) {
    // Visual feedback: one LED per arm cycles through hues each revolution
    for (int arm = 0; arm < HardwareConfig::NUM_ARMS; arm++) {
        ctx.arms[arm].pixels[0] = CHSV(m_hue, 255, 128);  // Half brightness to save power
        for (int p = 1; p < HardwareConfig::LEDS_PER_ARM; p++) {
            ctx.arms[arm].pixels[p] = CRGB::Black;
        }
    }

    // Process all pending accelerometer samples from interrupt queue
    timestamp_t sampleTimestamp;
    while (accel.getNextTimestamp(sampleTimestamp)) {
        xyzFloat reading;
        if (accel.read(reading)) {
            addSample(reading, sampleTimestamp);
        }
    }
}

void CalibrationEffect::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Cache rotation info for use in addSample()
    m_currentRotation = static_cast<rotation_t>(revolutionCount);
    m_lastHallTimestamp = timestamp;

    // Cycle hue each revolution for visual feedback (0-255 for FastLED)
    m_hue = static_cast<uint8_t>((m_hue + 1) % 256);

    (void)usPerRev;  // Unused
}

void CalibrationEffect::addSample(const xyzFloat& reading, timestamp_t timestamp) {
    // Build sample with all fields
    AccelSample& sample = m_msg.samples[m_msg.sample_count];
    sample.timestamp_us = timestamp;
    sample.sequence_num = m_sequenceNum++;
    sample.rotation_num = m_currentRotation;

    // Compute microseconds since last hall trigger for phase calculation
    // Guard against timestamp being before hall (shouldn't happen, but be safe)
    if (timestamp >= m_lastHallTimestamp) {
        sample.micros_since_hall = static_cast<period_t>(timestamp - m_lastHallTimestamp);
    } else {
        sample.micros_since_hall = 0;
    }

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
