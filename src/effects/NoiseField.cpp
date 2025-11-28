#include "effects.h"
#include "arm_renderer.h"
#include <FastLED.h>

extern const uint8_t PHYSICAL_TO_VIRTUAL[30];

/**
 * Render NoiseField effect - organic texture using FastLED's inoise16()
 *
 * Creates flowing lava-like patterns by mapping 2D Perlin noise to the polar display.
 * Noise coordinates are based on:
 * - X axis: arm angle (0-360°)
 * - Y axis: radial LED position (0-29)
 * - Time: animated offset for flowing motion
 *
 * Uses LavaColors_p palette for volcanic appearance.
 */
void renderNoiseField(RenderContext& ctx) {
    static uint16_t timeOffset = 0;
    timeOffset += 10;  // Animation speed (higher = faster flow)

    renderAllArms(ctx, [&](uint16_t physicalLed, uint16_t ledIdx, const ArmInfo& arm) {
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];

        // Map angle and radial position to 2D noise space
        // inoise16() expects 0-65535 for full range
        uint16_t noiseX = arm.angle * 182;      // 0-360° → 0-65535 (angle)
        uint16_t noiseY = virtualPos * 2184;    // 0-29 → 0-65535 (radial position)

        // Get noise value and convert to brightness (0-255)
        uint16_t noiseValue = inoise16(noiseX, noiseY, timeOffset);
        uint8_t brightness = noiseValue >> 8;

        // Map to lava color palette
        ctx.leds[physicalLed] = ColorFromPalette(LavaColors_p, brightness);
    });
}
