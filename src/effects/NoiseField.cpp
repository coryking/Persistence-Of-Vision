#include "effects/NoiseField.h"
#include <FastLED.h>
#include "fl/noise.h"  // noiseCylinderCRGB (new in master, post-3.10.3)
#include "fl/map_range.h"
#include "polar_helpers.h"

/**
 * Render NoiseField effect - flowing lava texture
 *
 * Uses FastLED's noiseCylinderCRGB for seamless cylindrical noise.
 * Maps the POV disc as a cylinder where:
 *   - angle (θ) = position around the disc (radians)
 *   - height = radial position (0.0 = hub, 1.0 = tip)
 *
 * The cylinder mapping eliminates the 0°/360° seam problem.
 */
void NoiseField::render(RenderContext& ctx) {

    for (int armIdx = 0; armIdx < 3; armIdx++) {
        auto& arm = ctx.arms[armIdx];
        float angleRadians = angleUnitsToRadians(arm.angleUnits);

        for (int led = 0; led < 10; led++) {
            // Normalize radial position: 0.0 (hub) to 1.0 (tip)
            float height = led / 9.0f;

            // Sample cylindrical noise
            // radius controls zoom (larger = coarser pattern)
            CHSV color = fl::noiseCylinderHSV8(angleRadians, height, noiseTimeOffsetMs, 1.0f);
            arm.pixels[led] = color;
        }
    }
}

void NoiseField::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
  noiseTimeOffsetMs  = (uint32_t)(timestamp / 1000);

  // Oscillate radius between RADIUS_MIN and RADIUS_MAX over DRIFT_PERIOD_SECONDS
  //float phase = 2.0f * M_PI * timestamp / DRIFT_PERIOD_US;
  //float sinValue = sin(phase);  // -1.0 to 1.0
  //radius = fl::map_range(sinValue, -1.0f, 1.0f, RADIUS_MIN, RADIUS_MAX);
}
