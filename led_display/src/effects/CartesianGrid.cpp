#include "effects/CartesianGrid.h"
#include "hardware_config.h"
#include "geometry.h"
#include "polar_helpers.h"
#include "cartesian_helpers.h"
#include "esp_log.h"
#include <FastLED.h>
#include <math.h>

static const char* TAG = "GRID";


// Cartesian coordinate range: -44 to +44
static constexpr int COORD_MIN = -PIXELS_TO_EDGE;
static constexpr int COORD_MAX = PIXELS_TO_EDGE;


// Grid configuration
static constexpr float GRID_SPACING = 10.0f;        // Pixels between grid lines
static constexpr float LINE_HALF_WIDTH = 0.5f;     // Half-width of grid lines in pixels

// Grid line colors
static const CRGB GRID_COLOR = CRGB(255, 255, 255);  // White lines
static const CRGB BACKGROUND_COLOR = CRGB(0, 0, 0);  // Black background

// SDF anti-aliasing configuration
// Transition width: ~1 ring spacing in pixel units for smooth feathering
static constexpr float AA_TRANSITION_WIDTH = 2.3f;  // ~1 ring spacing in pixel units

// Precomputed reciprocals (avoid division in render loop)
static constexpr float RECIP_GRID_SPACING = 1.0f / GRID_SPACING;
static constexpr float INV_W = 1.0f / AA_TRANSITION_WIDTH;

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Compute distance to nearest grid line using SDF approach
 * @param coord The coordinate to check (can be offset)
 * @param recipSpacing Precomputed 1/spacing for fast modulo
 * @return Distance to nearest grid line (unsigned)
 */
static float distToGridLine(float coord, float recipSpacing) {
    float a = fabsf(coord);
    float d = a - GRID_SPACING * truncf(a * recipSpacing);  // fast fmod via reciprocal
    if (d > GRID_SPACING * 0.5f) {
        d = GRID_SPACING - d;
    }
    return d;
}

// =============================================================================
// Effect Implementation
// =============================================================================

void CartesianGrid::render(RenderContext& ctx) {
    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Precompute trig once per arm (angle is constant across all LEDs in this arm)
        float angleRad = angleUnitsToRadians(arm.angle);
        float cosAngle = cosf(angleRad);
        float sinAngle = sinf(angleRad);

        for (int p = 0; p < HardwareConfig::ARM_LED_COUNT[a]; p++) {
            // Get physical radius from geometry
            int ring = a + (p * HardwareConfig::NUM_ARMS);
            float radius_mm = RadialGeometry::ringRadiusMM(ring);

            // Convert to pixel units
            float radius_pixels = radiusToPixels(radius_mm);

            // Compute Cartesian coordinates
            float x = radius_pixels * cosAngle;
            float y = radius_pixels * sinAngle;

            // Compute distance to nearest grid line in each direction (with offset)
            float dx = distToGridLine(x + xOffset, RECIP_GRID_SPACING);
            float dy = distToGridLine(y + yOffset, RECIP_GRID_SPACING);

            // SDF: minimum distance to any grid line
            float dist = fminf(dx, dy);

            // SDF linear ramp: convert distance to opacity
            // sdf = distance - line_half_width (negative inside line, positive outside)
            float sdf = dist - LINE_HALF_WIDTH;

            // Linear ramp: opacity = 0.5 - sdf * invW
            // At sdf = -LINE_HALF_WIDTH (line center): opacity = 0.5 + 0.5 = 1.0
            // At sdf = +AA_TRANSITION_WIDTH: opacity = 0.5 - 1.0 = -0.5 → clamps to 0
            float alpha = 0.5f - sdf * INV_W;

            // Clamp to [0, 1] and convert to 0-255 range
            uint8_t opacity;
            if (alpha <= 0.0f) {
                opacity = 0;
            } else if (alpha >= 1.0f) {
                opacity = 255;
            } else {
                opacity = (uint8_t)(alpha * 255.0f);
            }

            // Blend background and grid color based on opacity
            arm.pixels[p] = blend(BACKGROUND_COLOR, GRID_COLOR, opacity);
        }
    }
}

void CartesianGrid::up() {
    yOffset++;
    ESP_LOGI(TAG, "Grid offset -> (%d, %d)", xOffset, yOffset);
}

void CartesianGrid::down() {
    yOffset--;
    ESP_LOGI(TAG, "Grid offset -> (%d, %d)", xOffset, yOffset);
}

void CartesianGrid::right() {
    xOffset++;
    ESP_LOGI(TAG, "Grid offset -> (%d, %d)", xOffset, yOffset);
}

void CartesianGrid::left() {
    xOffset--;
    ESP_LOGI(TAG, "Grid offset -> (%d, %d)", xOffset, yOffset);
}
