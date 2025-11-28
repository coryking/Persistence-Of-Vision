#include "effects.h"
#include <FastLED.h>
#include "blob_types.h"
#include "esp_timer.h"
#include "hardware_config.h"
#include "arm_renderer.h"
#include "blob_cache.h"

// External references to globals from main.cpp
extern Blob blobs[MAX_BLOBS];
extern const uint8_t PHYSICAL_TO_VIRTUAL[30];

/**
 * Initialize 5 blobs for virtual display (0-29 radial range)
 */
void setupVirtualBlobs() {
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
        // Small, fast (angular 5-30°, radial 2-6 LEDs)
        {5, 30, 0.5, 0.3, 60,    2, 6, 0.4, 0.25, 4.0},
        // Medium (angular 10-60°, radial 4-10 LEDs)
        {10, 60, 0.3, 0.2, 90,   4, 10, 0.25, 0.15, 6.0},
        // Large, slow (angular 20-90°, radial 6-14 LEDs)
        {20, 90, 0.15, 0.1, 120, 6, 14, 0.15, 0.1, 8.0}
    };

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        blobs[i].armIndex = 0;  // Unused for virtual blobs
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

        // Radial parameters - now use full 0-29 range
        blobs[i].radialWanderCenter = 14.5f;  // Center of virtual display (0-29)
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
 * Render virtual display blobs effect
 */
void renderVirtualBlobs(RenderContext& ctx) {
    renderAllArms(ctx, [&](uint16_t physicalLed, uint16_t ledIdx, const ArmInfo& arm) {
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
        CRGB color = CRGB::Black;

        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active &&
                isAngleInArcCached(arm.angle, i) &&
                isLedInBlobCached(virtualPos, i)) {
                color += blobs[i].color;
            }
        }

        ctx.leds[physicalLed] = color;
    });
}
