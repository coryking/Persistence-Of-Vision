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
    static constexpr float RPM_MIN = 800.0f;
    static constexpr float RPM_MAX = 2500.0f;
    static constexpr float BASE_ARC_WIDTH = 20.0f;
    static constexpr float ARC_CENTER = 0.0f;

    // State
    CRGB gradient[30];
    float arcWidth = BASE_ARC_WIDTH;

    // Helpers
    void initializeGradient();
    uint8_t rpmToPixelCount(float rpm) const;
};

#endif // RPM_ARC_H
