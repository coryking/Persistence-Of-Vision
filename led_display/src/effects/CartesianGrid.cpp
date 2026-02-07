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

// Animation configuration
static constexpr uint8_t DRIFT_BPM = 12;    // Slow drift speed
static constexpr uint8_t ZOOM_BPM = 6;      // Very slow zoom in/out
static constexpr float DRIFT_RANGE = 15.0f;  // Max drift in pixels
static constexpr float ZOOM_MIN = 0.7f;     // Zoom out to 70%
static constexpr float ZOOM_MAX = 1.3f;     // Zoom in to 130%

// Feather width range for UI mapping
static constexpr float FEATHER_MIN = 0.5f;
static constexpr float FEATHER_MAX = 10.0f;
static constexpr float INDICATOR_ARC_MIN = 0.20f;  // 20% arc at min feather
static constexpr float INDICATOR_ARC_MAX = 1.0f;   // 100% arc at max feather

// Precomputed reciprocals (avoid division in render loop)
static constexpr float RECIP_GRID_SPACING = 1.0f / GRID_SPACING;

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
    // Automatic animation - slow drift and zoom
    // Using different phases (0, 85, 170) to avoid synchronized movement
    float xDrift = (beatsin16(DRIFT_BPM, 0, 1000, 0, 0) - 500) * DRIFT_RANGE / 500.0f;
    float yDrift = (beatsin16(DRIFT_BPM, 0, 1000, 0, 85) - 500) * DRIFT_RANGE / 500.0f;
    float zoomFactor = ZOOM_MIN + (beatsin16(ZOOM_BPM, 0, 1000, 0, 170) / 1000.0f) * (ZOOM_MAX - ZOOM_MIN);

    // Compute reciprocal for current feather width
    float invW = 1.0f / aaFeatherWidth;

    // Indicator ring configuration - color based on AA mode
    CRGB indicatorColor;
    switch (aaMode) {
        case AAMode::SDF_LINEAR:
            indicatorColor = CRGB(0, 255, 0);  // Green
            break;
        case AAMode::SDF_SMOOTHSTEP:
            indicatorColor = CRGB(0, 0, 255);  // Blue
            break;
        case AAMode::BINARY_NO_AA:
            indicatorColor = CRGB(255, 0, 0);  // Red
            break;
        default:
            indicatorColor = CRGB(255, 255, 255);
            break;
    }

    // Map feather width to arc percentage (20% to 100%)
    // For binary mode, always use 100%
    float arcPercent;
    if (aaMode == AAMode::BINARY_NO_AA) {
        arcPercent = 1.0f;
    } else {
        arcPercent = INDICATOR_ARC_MIN +
                     (aaFeatherWidth - FEATHER_MIN) / (FEATHER_MAX - FEATHER_MIN) *
                     (INDICATOR_ARC_MAX - INDICATOR_ARC_MIN);
        arcPercent = fmaxf(INDICATOR_ARC_MIN, fminf(INDICATOR_ARC_MAX, arcPercent));
    }

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Precompute trig once per arm (angle is constant across all LEDs in this arm)
        float angleRad = angleUnitsToRadians(arm.angle);
        float cosAngle = cosf(angleRad);
        float sinAngle = sinf(angleRad);

        // Check if this arm is within the indicator arc (angle normalized to 0-1)
        float normalizedAngle = angleRad / (2.0f * M_PI);  // 0.0 to 1.0
        bool inIndicatorArc = (normalizedAngle <= arcPercent);

        for (int p = 0; p < HardwareConfig::ARM_LED_COUNT[a]; p++) {
            // Get physical radius from geometry
            int ring = a + (p * HardwareConfig::NUM_ARMS);
            float radius_mm = RadialGeometry::ringRadiusMM(ring);

            // Indicator ring - innermost ring (ring 0) shows AA mode and feather width
            if (ring == 0 && inIndicatorArc) {
                arm.pixels[p] = indicatorColor;
                continue;
            }

            // Convert to pixel units
            float radius_pixels = radiusToPixels(radius_mm);

            // Compute Cartesian coordinates
            float x0 = radius_pixels * cosAngle;
            float y0 = radius_pixels * sinAngle;

            // Apply zoom (scale around origin)
            float x = x0 * zoomFactor;
            float y = y0 * zoomFactor;

            // Apply drift
            x += xDrift;
            y += yDrift;

            // Compute distance to nearest grid line in each direction
            float dx = distToGridLine(x, RECIP_GRID_SPACING);
            float dy = distToGridLine(y, RECIP_GRID_SPACING);

            // SDF: minimum distance to any grid line
            float dist = fminf(dx, dy);

            // Compute opacity based on AA mode
            uint8_t opacity;

            switch (aaMode) {
                case AAMode::SDF_LINEAR: {
                    // Linear ramp: opacity = 0.5 - sdf * invW
                    float sdf = dist - LINE_HALF_WIDTH;
                    float alpha = 0.5f - sdf * invW;

                    if (alpha <= 0.0f) {
                        opacity = 0;
                    } else if (alpha >= 1.0f) {
                        opacity = 255;
                    } else {
                        opacity = (uint8_t)(alpha * 255.0f);
                    }
                    break;
                }

                case AAMode::SDF_SMOOTHSTEP: {
                    // Smoothstep (Hermite curve) for smoother falloff
                    float sdf = dist - LINE_HALF_WIDTH;
                    float t = 0.5f - sdf * invW;  // normalized to [0,1]

                    if (t <= 0.0f) {
                        opacity = 0;
                    } else if (t >= 1.0f) {
                        opacity = 255;
                    } else {
                        // Hermite interpolation: 3t^2 - 2t^3
                        float smoothed = t * t * (3.0f - 2.0f * t);
                        opacity = (uint8_t)(smoothed * 255.0f);
                    }
                    break;
                }

                case AAMode::BINARY_NO_AA: {
                    // Original binary mode (no anti-aliasing)
                    opacity = (dist < LINE_HALF_WIDTH) ? 255 : 0;
                    break;
                }

                default:
                    opacity = 0;
                    break;
            }

            // Blend background and grid color based on opacity
            arm.pixels[p] = blend(BACKGROUND_COLOR, GRID_COLOR, opacity);
        }
    }
}

void CartesianGrid::up() {
    // Increase feather width (softer AA)
    aaFeatherWidth += 0.5f;
    if (aaFeatherWidth > 10.0f) aaFeatherWidth = 10.0f;
    ESP_LOGI(TAG, "AA feather width: %.1f", aaFeatherWidth);
}

void CartesianGrid::down() {
    // Decrease feather width (sharper AA)
    aaFeatherWidth -= 0.5f;
    if (aaFeatherWidth < 0.5f) aaFeatherWidth = 0.5f;
    ESP_LOGI(TAG, "AA feather width: %.1f", aaFeatherWidth);
}

void CartesianGrid::right() {
    // Next AA technique
    int mode = (int)aaMode;
    mode = (mode + 1) % (int)AAMode::MODE_COUNT;
    aaMode = (AAMode)mode;

    const char* modeNames[] = {"SDF Linear", "SDF Smoothstep", "Binary (No AA)"};
    ESP_LOGI(TAG, "AA mode: %s", modeNames[mode]);
}

void CartesianGrid::left() {
    // Previous AA technique
    int mode = (int)aaMode;
    mode = (mode - 1 + (int)AAMode::MODE_COUNT) % (int)AAMode::MODE_COUNT;
    aaMode = (AAMode)mode;

    const char* modeNames[] = {"SDF Linear", "SDF Smoothstep", "Binary (No AA)"};
    ESP_LOGI(TAG, "AA mode: %s", modeNames[mode]);
}
