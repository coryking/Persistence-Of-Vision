#ifndef CARTESIAN_GRID_H
#define CARTESIAN_GRID_H

#include "Effect.h"

/**
 * Cartesian grid effect - renders straight grid lines in polar display
 * with anti-aliasing experiments
 *
 * Features:
 * - Multiple AA techniques (SDF linear, smoothstep, binary/no-AA)
 * - Tunable feather width
 * - Automatic slow animation (drift and rotation)
 *
 * Controls:
 * - up/down: Adjust AA feather width
 * - left/right: Cycle through AA techniques
 */
class CartesianGrid : public Effect {
public:
    void render(RenderContext& ctx) override;
    void up() override;    // Increase feather width (softer AA)
    void down() override;  // Decrease feather width (sharper AA)
    void right() override; // Next AA technique
    void left() override;  // Previous AA technique

private:
    enum class AAMode {
        SDF_LINEAR = 0,    // SDF with linear ramp
        SDF_SMOOTHSTEP,    // SDF with smoothstep (Hermite curve)
        BINARY_NO_AA,      // Original binary (no AA)
        MODE_COUNT
    };

    AAMode aaMode = AAMode::SDF_LINEAR;
    float aaFeatherWidth = 2.3f;  // Tunable AA transition width
};

#endif // CARTESIAN_GRID_H
