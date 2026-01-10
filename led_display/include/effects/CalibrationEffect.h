#ifndef CALIBRATION_EFFECT_H
#define CALIBRATION_EFFECT_H

#include "Effect.h"
#include <esp_timer.h>

/**
 * Calibration Effect for rotor balancing
 *
 * When active:
 * - Starts TelemetryTask to stream accelerometer samples over ESP-NOW
 * - Enables hall event streaming (via global flag)
 * - Minimal LED activity (visual feedback only)
 *
 * The TelemetryTask handles all accelerometer sampling and ESP-NOW sending,
 * decoupled from the LED render loop. This prevents sample drops when
 * render() skips or busy-waits for timing.
 *
 * rotation_num and micros_since_hall are computed in Python post-processing
 * from timestamp correlation with hall events.
 */
class CalibrationEffect : public Effect {
public:
    void begin() override;
    void end() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;

private:
    // Visual feedback - hue cycles 0-255 each revolution
    uint8_t m_hue = 0;
};

// Global flag: when true, hallProcessingTask sends HallEventMsg for each trigger
// Set by CalibrationEffect::begin(), cleared by CalibrationEffect::end()
extern volatile bool g_calibrationActive;

#endif // CALIBRATION_EFFECT_H
