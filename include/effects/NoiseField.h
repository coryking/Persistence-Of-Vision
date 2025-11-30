#ifndef NOISE_FIELD_H
#define NOISE_FIELD_H

#include "Effect.h"

/**
 * Organic flowing texture using Perlin noise
 *
 * Creates lava-like patterns by mapping 2D noise to polar coordinates.
 * Each arm samples noise at its actual angle, creating natural flow.
 */
class NoiseField : public Effect {
public:
    void render(RenderContext &ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;

    timestamp_t noiseTimeOffsetMs = 0;
    float radius = 1.5f;
    static constexpr float ANIMATION_SPEED = 10.0f;
    static constexpr float DRIFT_PERIOD_SECONDS = 10.0f;
    static constexpr float DRIFT_PERIOD_US = SECONDS_TO_MICROS(DRIFT_PERIOD_SECONDS);
    static constexpr float RADIUS_MIN = 0.5f;
    static constexpr float RADIUS_MAX = 2.0f;
};
#endif // NOISE_FIELD_H
