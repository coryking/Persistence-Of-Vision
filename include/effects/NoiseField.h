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
    void render(RenderContext& ctx) override;

private:
    uint16_t timeOffset = 0;
    static constexpr uint16_t ANIMATION_SPEED = 10;
};

#endif // NOISE_FIELD_H
