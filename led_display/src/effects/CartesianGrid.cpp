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
static constexpr int GRID_SPACING = 10;         // Pixels between grid lines
static constexpr float LINE_THICKNESS = 1.0f;   // Grid line thickness in pixels

// Grid line colors
static const CRGB GRID_COLOR = CRGB(255, 255, 255);  // White lines
static const CRGB BACKGROUND_COLOR = CRGB(0, 0, 0);  // Black background

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Check if a coordinate is near a grid line
 * @param coord The coordinate to check (can be offset)
 * @param spacing Distance between grid lines
 * @return true if within LINE_THICKNESS of a grid line
 */
static bool isNearGridLine(float coord, int spacing) {
    float distToLine = fmodf(fabsf(coord), spacing);
    if (distToLine > spacing / 2.0f) {
        distToLine = spacing - distToLine;
    }
    return distToLine < LINE_THICKNESS;
}

// =============================================================================
// Effect Implementation
// =============================================================================

void CartesianGrid::render(RenderContext& ctx) {
    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Get arm angle in radians for coordinate conversion
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

            // Check if on a grid line (with offset from arrow keys)
            bool onVerticalLine = isNearGridLine(x + xOffset, GRID_SPACING);
            bool onHorizontalLine = isNearGridLine(y + yOffset, GRID_SPACING);

            // Set pixel color
            if (onVerticalLine || onHorizontalLine) {
                arm.pixels[p] = GRID_COLOR;
            } else {
                arm.pixels[p] = BACKGROUND_COLOR;
            }
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
