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
 * - Streams accelerometer samples over ESP-NOW (interrupt-driven at 800Hz)
 * - Enables hall event streaming (via global flag)
 * - Minimal LED activity to maximize CPU for sampling
 *
 * Data is sent to motor controller for serial output.
 * Each sample includes rotation_num and micros_since_hall for phase analysis.
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

    // Rotation tracking (updated by onRevolution callback)
    rotation_t m_currentRotation = 0;
    timestamp_t m_lastHallTimestamp = 0;

    // Sequence counter for drop detection (monotonically incrementing)
    sequence_t m_sequenceNum = 0;

    // Visual feedback - hue cycles 0-255 each revolution
    uint8_t m_hue = 0;

    void flushBatch();
    void addSample(const xyzFloat& reading, timestamp_t timestamp);
};

// Global flag: when true, hallProcessingTask sends HallEventMsg for each trigger
// Set by CalibrationEffect::begin(), cleared by CalibrationEffect::end()
extern volatile bool g_calibrationActive;

#endif // CALIBRATION_EFFECT_H
