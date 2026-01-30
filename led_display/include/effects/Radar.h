#ifndef RADAR_H
#define RADAR_H

#include "Effect.h"
#include "hardware_config.h"

/**
 * Vintage radar display effect
 *
 * Features:
 * - Rotating sweep beam (independent of physical disc rotation)
 * - Phosphor decay trail behind sweep
 * - Range rings at fixed radial positions
 */
class Radar : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    void nextMode() override;  // Unused
    void prevMode() override;  // Unused
    void paramUp() override;   // Unused
    void paramDown() override; // Unused

private:
    // === Color Palette (high contrast vintage) ===
    static constexpr CRGB SWEEP_COLOR = CRGB(255, 255, 255);       // Pure white sweep
    static constexpr CRGB PHOSPHOR_BRIGHT = CRGB(0, 180, 60);      // Darker phosphor green
    static constexpr CRGB PHOSPHOR_DIM = CRGB(0, 40, 15);          // Much dimmer trail end
    static constexpr CRGB RANGE_RING_COLOR = CRGB(10, 20, 35);     // Blue-grey rings

    // === Sweep Configuration ===
    // 28 angle units per revolution = ~130 revolutions per sweep = ~6 seconds at 1300 RPM
    static constexpr angle_t SWEEP_ANGLE_PER_REV = 28;

    // Sweep state (revolution-count based to avoid wall-clock desync)
    angle_t sweepAngleUnits = 0;

    // === Phosphor Trail ===
    static constexpr angle_t TRAIL_WIDTH = ANGLE_UNITS(90);  // 90 degree trail

    // === Helper Methods ===

    /**
     * Check if virtual pixel is on a range ring
     */
    bool isRangeRing(uint8_t vPixel) const;
};

#endif // RADAR_H
