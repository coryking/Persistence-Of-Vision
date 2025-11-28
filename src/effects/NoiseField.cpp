#include "effects/NoiseField.h"
#include <FastLED.h>

/**
 * Render NoiseField effect - organic texture using FastLED's inoise16()
 *
 * Creates flowing lava-like patterns by mapping 2D Perlin noise to the polar display.
 * Each arm uses its actual current angle for noise sampling, creating natural flow
 * that respects the physical reality of the spinning display.
 */
void NoiseField::render(RenderContext& ctx) {
    timeOffset += ANIMATION_SPEED;

    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Use THIS arm's actual angle for noise X
        // inoise16() expects 0-65535 range
        uint16_t noiseX = static_cast<uint16_t>(arm.angle * 182.0f);

        for (int p = 0; p < 10; p++) {
            // Virtual position: interleaved across arms
            // virt 0 = arm0:led0, virt 1 = arm1:led0, virt 2 = arm2:led0
            // virt 3 = arm0:led1, etc.
            uint8_t virtualPos = a + p * 3;

            // Map radial position to noise Y (0-29 → 0-65535)
            uint16_t noiseY = virtualPos * 2184;

            // Sample 3D noise (X=angle, Y=radius, Z=time)
            uint16_t noiseValue = inoise16(noiseX, noiseY, timeOffset);
            uint8_t brightness = noiseValue >> 8;

            // Map through lava palette
            arm.pixels[p] = ColorFromPalette(LavaColors_p, brightness);
        }
    }
}
