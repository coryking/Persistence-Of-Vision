#ifndef RPM_ARC_H
#define RPM_ARC_H

#include "Effect.h"

/**
 * RPM-based growing arc effect
 *
 * Displays an arc at the hall sensor position (0°) with radial extent
 * proportional to current RPM. Higher RPM = more pixels lit.
 *
 * Features:
 * - Radial gradient from green (inner) to red (outer)
 * - Arc width can be animated based on RPM
 * - Soft edges using arc intensity
 * - Per-arm angle checking (arms can be partially in/out of arc)
 *
 * Future expansion points:
 * - Animate arc width
 * - Cycle through color palettes
 * - Multiple arcs
 */
class RpmArc : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;

private:
    // Configuration
    static constexpr angle_t BASE_ARC_WIDTH_UNITS = 200;      // 20 degrees base
    static constexpr angle_t MAX_EXTRA_WIDTH_UNITS = 100;     // up to 10 degrees extra at max speed
    static constexpr angle_t ARC_CENTER_UNITS = 0;             // 0 degrees = 0 units

    // State
    CRGB gradient[30];
    angle_t arcWidthUnits;

    // Helpers
    void initializeGradient();
    uint8_t speedToPixelCount(uint8_t speedFactor) const;
};

#endif // RPM_ARC_H
