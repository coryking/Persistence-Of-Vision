#ifndef CALIBRATION_EFFECT_H
#define CALIBRATION_EFFECT_H

#include "Effect.h"
#include "Accelerometer.h"
#include "messages.h"
#include <esp_timer.h>

/**
 * Calibration Effect for rotor balancing
 *
 * When active:
 * - Streams accelerometer samples over ESP-NOW
 * - Enables hall event streaming (via global flag)
 * - Keeps LEDs off to maximize CPU for sampling
 *
 * Data is sent to motor controller for serial output.
 * Analyze in spreadsheet to find imbalance phase angle.
 */
class CalibrationEffect : public Effect {
public:
    void begin() override;
    void end() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;

private:
    // Sample buffer for batching - uses max from messages.h
    AccelSampleMsg m_msg;

    // Polling timing
    timestamp_t m_lastSampleTime = 0;
    static constexpr uint32_t SAMPLE_INTERVAL_US = 2500;  // 400 Hz

    // Visual feedback - hue cycles 0-359 each revolution
    uint16_t m_hue = 0;

    void flushBatch();
    void addSample(const xyzFloat& reading, timestamp_t timestamp);
};

// Global flag: when true, hallProcessingTask sends HallEventMsg for each trigger
// Set by CalibrationEffect::begin(), cleared by CalibrationEffect::end()
extern volatile bool g_calibrationActive;

#endif // CALIBRATION_EFFECT_H
