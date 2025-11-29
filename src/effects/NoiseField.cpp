#include "effects/NoiseField.h"
#include <FastLED.h>

/**
 * Render NoiseField effect - flowing lava texture
 *
 * Uses sparse sampling (3 points instead of 10) + interpolation for speed,
 * while keeping the original's smooth flowing character.
 *
 * Note: Has a seam at 0°/360° boundary (accepted tradeoff for simplicity)
 */
void NoiseField::render(RenderContext& ctx) {
    timeOffset += ANIMATION_SPEED;

    // SCALE TUNING: Adjust these to change pattern size
    // Larger divisor = bigger patterns (zoomed out)
    // Smaller divisor = smaller patterns (zoomed in)
    const uint8_t ANGLE_SCALE_DIVISOR = 4;   // Try: 2, 4, 8, 16
    const uint8_t RADIAL_SCALE_DIVISOR = 4;  // Try: 2, 4, 8, 16

    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Map angle to noise X - divide to zoom out
        uint16_t noiseX = static_cast<uint16_t>((static_cast<uint32_t>(arm.angleUnits) * 65536UL) / 3600 / ANGLE_SCALE_DIVISOR);

        // Sample noise at 3 key radial positions: hub, middle, tip
        uint8_t samples[3];
        const uint8_t sampleRadii[3] = {0, 15, 27};  // Virtual positions 0, 15, 27 (out of 0-29)

        for (int i = 0; i < 3; i++) {
            // Map radial position to noise Y - divide to zoom out
            uint16_t noiseY = (sampleRadii[i] * 2184) / RADIAL_SCALE_DIVISOR;

            // Sample 3D noise (X=angle, Y=radius, Z=time)
            uint16_t noiseValue = inoise16(noiseX, noiseY, timeOffset);
            samples[i] = noiseValue >> 8;
        }

        // Interpolate all 10 pixels from the 3 samples
        // Pixels 0-4: lerp between samples[0] and samples[1]
        for (int p = 0; p < 5; p++) {
            uint8_t frac = (p * 255) / 5;  // 0 → 255 over 5 pixels
            uint8_t brightness = lerp8by8(samples[0], samples[1], frac);
            arm.pixels[p] = ColorFromPalette(LavaColors_p, brightness);
        }

        // Pixels 5-9: lerp between samples[1] and samples[2]
        for (int p = 5; p < 10; p++) {
            uint8_t frac = ((p - 5) * 255) / 4;  // 0 → 255 over 4 pixels
            uint8_t brightness = lerp8by8(samples[1], samples[2], frac);
            arm.pixels[p] = ColorFromPalette(LavaColors_p, brightness);
        }
    }
}
