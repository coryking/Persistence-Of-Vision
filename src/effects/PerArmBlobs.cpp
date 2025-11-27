#include "effects.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "blob_types.h"
#include "esp_timer.h"

// External references to globals from main.cpp
extern NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip;
extern Blob blobs[MAX_BLOBS];

// Hardware configuration (from main.cpp)
constexpr uint16_t LEDS_PER_ARM = 10;
constexpr uint16_t INNER_ARM_START = 10;
constexpr uint16_t MIDDLE_ARM_START = 0;
constexpr uint16_t OUTER_ARM_START = 20;

/**
 * Check if LED index is within blob's current radial extent (per-arm version)
 */
static bool isLedInBlob(uint16_t ledIndex, const Blob& blob) {
    if (!blob.active) return false;

    // Calculate radial range (LED indices covered by this blob)
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;

    // Check if LED is within range (clipped to 0-9)
    float ledFloat = static_cast<float>(ledIndex);
    return (ledFloat >= radialStart) && (ledFloat < radialEnd);
}

/**
 * Initialize 5 blobs with random distribution across arms (per-arm version)
 */
void setupPerArmBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        // Angular parameters
        float minAngularSize, maxAngularSize;
        float angularDriftSpeed;
        float angularSizeSpeed;
        float angularWanderRange;
        // Radial parameters
        float minRadialSize, maxRadialSize;
        float radialDriftSpeed;
        float radialSizeSpeed;
        float radialWanderRange;
    } templates[3] = {
        // Small, fast (angular 5-30°, radial 1-3 LEDs)
        {5, 30, 0.5, 0.3, 60,    1, 3, 0.4, 0.25, 2.0},
        // Medium (angular 10-60°, radial 2-5 LEDs)
        {10, 60, 0.3, 0.2, 90,   2, 5, 0.25, 0.15, 2.5},
        // Large, slow (angular 20-90°, radial 3-7 LEDs)
        {20, 90, 0.15, 0.1, 120, 3, 7, 0.15, 0.1, 3.0}
    };

    // Distribute 5 blobs: 2 inner, 2 middle, 1 outer
    uint8_t armAssignments[MAX_BLOBS] = {
        ARM_INNER, ARM_INNER,
        ARM_MIDDLE, ARM_MIDDLE,
        ARM_OUTER
    };

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        blobs[i].armIndex = armAssignments[i];
        blobs[i].color = RgbColor(citrusPalette[i]);  // Convert HSL to RGB

        // Use template based on blob index (varied sizes)
        BlobTemplate& tmpl = templates[i % 3];

        // Angular parameters
        blobs[i].wanderCenter = (i * 72.0f);  // Spread evenly: 0, 72, 144, 216, 288
        blobs[i].wanderRange = tmpl.angularWanderRange;
        blobs[i].driftVelocity = tmpl.angularDriftSpeed;
        blobs[i].minArcSize = tmpl.minAngularSize;
        blobs[i].maxArcSize = tmpl.maxAngularSize;
        blobs[i].sizeChangeRate = tmpl.angularSizeSpeed;

        // Radial parameters
        blobs[i].radialWanderCenter = 4.5f;  // Center of LED strip (0-9)
        blobs[i].radialWanderRange = tmpl.radialWanderRange;
        blobs[i].radialDriftVelocity = tmpl.radialDriftSpeed;
        blobs[i].minRadialSize = tmpl.minRadialSize;
        blobs[i].maxRadialSize = tmpl.maxRadialSize;
        blobs[i].radialSizeChangeRate = tmpl.radialSizeSpeed;

        blobs[i].birthTime = now;
        blobs[i].deathTime = 0;  // Immortal for now
    }
}

/**
 * Render per-arm blobs effect
 */
void renderPerArmBlobs(const RenderContext& ctx) {
    // Render each LED individually based on angular and radial position
    // Inner arm: LEDs 10-19
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        RgbColor ledColor(0, 0, 0);

        // Check all blobs assigned to inner arm
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active && blobs[i].armIndex == ARM_INNER) {
                // Check both angular and radial position
                if (isAngleInArc(ctx.innerArmDegrees, blobs[i]) && isLedInBlob(ledIdx, blobs[i])) {
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                }
            }
        }
        strip.SetPixelColor(INNER_ARM_START + ledIdx, ledColor);
    }

    // Middle arm: LEDs 0-9
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        RgbColor ledColor(0, 0, 0);

        // Check all blobs assigned to middle arm
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active && blobs[i].armIndex == ARM_MIDDLE) {
                // Check both angular and radial position
                if (isAngleInArc(ctx.middleArmDegrees, blobs[i]) && isLedInBlob(ledIdx, blobs[i])) {
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                }
            }
        }
        strip.SetPixelColor(MIDDLE_ARM_START + ledIdx, ledColor);
    }

    // Outer arm: LEDs 20-29
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        RgbColor ledColor(0, 0, 0);

        // Check all blobs assigned to outer arm
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active && blobs[i].armIndex == ARM_OUTER) {
                // Check both angular and radial position
                if (isAngleInArc(ctx.outerArmDegrees, blobs[i]) && isLedInBlob(ledIdx, blobs[i])) {
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
                }
            }
        }
        strip.SetPixelColor(OUTER_ARM_START + ledIdx, ledColor);
    }
}
