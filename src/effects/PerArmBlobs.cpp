#include "effects.h"
#include <FastLED.h>
#include "blob_types.h"
#include "esp_timer.h"
#include "pixel_utils.h"
#include "hardware_config.h"
#include "arm_renderer.h"
#include "blob_cache.h"

// External references to globals from main.cpp
extern Blob blobs[MAX_BLOBS];

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
        blobs[i].color = citrusPalette[i];  // CHSV to CRGB conversion automatic

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
void renderPerArmBlobs(RenderContext& ctx) {
    renderAllArms(ctx, [&](uint16_t physicalLed, uint16_t ledIdx, const ArmInfo& arm) {
        CRGB color = CRGB::Black;

        // Check all blobs assigned to this arm
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active &&
                blobs[i].armIndex == arm.armIndex &&
                isAngleInArcCached(arm.angle, i) &&
                isLedInBlobCached(ledIdx, i)) {
                color += blobs[i].color;
            }
        }

        ctx.leds[physicalLed] = color;
    });
}
