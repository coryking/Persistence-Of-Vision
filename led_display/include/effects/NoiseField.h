#ifndef NOISE_FIELD_H
#define NOISE_FIELD_H

#include "Effect.h"
#include "effects/SharedPalettes.h"

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
    void right() override;
    void left() override;
    void up() override;    // Next palette
    void down() override;  // Previous palette

    timestamp_t noiseTimeOffsetMs = 0;
    float radius = 1.5f;

    // Palette (manual control via up/down)
    uint8_t paletteIndex = 0;
    CRGBPalette16 palette = SharedPalettes::PALETTES[0];

    // Contrast modes: 0=Normal, 1=S-curve, 2=Turbulence, 3=Quantize, 4=Expanded, 5=Compressed
    uint8_t contrastMode = 0;
    static constexpr uint8_t CONTRAST_MODE_COUNT = 6;

    static constexpr float ANIMATION_SPEED = 10.0f;
    static constexpr float RADIUS_PERIOD_SECONDS = 15.0f;
    static constexpr float RADIUS_PERIOD_US = SECONDS_TO_MICROS(RADIUS_PERIOD_SECONDS);
    static constexpr float RADIUS_MIN = 0.75f;
    static constexpr float RADIUS_MAX = 1.75f;
};
#endif // NOISE_FIELD_H
