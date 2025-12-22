#include "effects/NoiseFieldRGB.h"
#include <FastLED.h>
#include "fl/noise.h"
#include "fl/map_range.h"
#include "polar_helpers.h"

/**
 * Render NoiseFieldRGB effect - psychedelic flowing RGB
 *
 * Uses FastLED's noiseCylinderCRGB for seamless cylindrical noise.
 * Three separate noise samples drive R, G, B channels directly.
 */
void IRAM_ATTR NoiseFieldRGB::render(RenderContext& ctx) {
    for (int armIdx = 0; armIdx < 3; armIdx++) {
        auto& arm = ctx.arms[armIdx];
        float angleRadians = angleUnitsToRadians(arm.angleUnits);

        for (int led = 0; led < HardwareConfig::LEDS_PER_ARM; led++) {
            uint8_t virtualPos = armLedToVirtual(armIdx, led);
            float height = fl::map_range<float, float>(virtualPos, 0, 29, 0.0f, 1.0f);

            CRGB color = fl::noiseCylinderCRGB(angleRadians, height, noiseTimeOffsetMs, RADIUS);
            arm.pixels[led] = color;
        }
    }
}

void NoiseFieldRGB::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    noiseTimeOffsetMs = (uint32_t)(timestamp / 50);
}
