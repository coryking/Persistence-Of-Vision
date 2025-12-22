#ifndef NOISE_FIELD_RGB_H
#define NOISE_FIELD_RGB_H

#include "Effect.h"

/**
 * Psychedelic RGB noise using cylindrical Perlin noise
 *
 * Uses 3 separate noise samples for R, G, B channels directly,
 * creating flowing rainbow chaos. No palette mapping.
 */
class NoiseFieldRGB : public Effect {
public:
    void render(RenderContext &ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    SpeedRange getSpeedRange() const override { return {10, 200}; }  // Hand-spin only

    uint32_t noiseTimeOffsetMs = 0;
    static constexpr float RADIUS = 1.25f;  // Fixed zoom level
};

#endif // NOISE_FIELD_RGB_H
