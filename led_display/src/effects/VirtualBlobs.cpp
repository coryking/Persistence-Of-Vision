#include "effects/VirtualBlobs.h"
#include "blob_types.h"
#include "polar_helpers.h"
#include "esp_timer.h"

void VirtualBlobs::begin() {
    initializeBlobs();
}

void VirtualBlobs::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Update blob animations once per revolution (47 Hz at 2800 RPM)
    for (auto& blob : blobs) {
        updateBlob(blob, timestamp);
    }
}

void VirtualBlobs::render(RenderContext& ctx) {
    ctx.clear();

    // Shape-first: iterate blobs, not pixels
    for (const auto& blob : blobs) {
        if (!blob.active) continue;

        // Check EACH arm (blob can appear on multiple arms simultaneously)
        for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
            auto& arm = ctx.arms[a];

            if (!isAngleInArcUnits(arm.angleUnits, blob.currentStartAngleUnits, blob.currentArcSizeUnits)) {
                continue;  // This arm not in blob
            }

            // Radial extent in virtual space (0-29)
            float radialHalfSize = blob.currentRadialSize / 2.0f;
            float radialStart = blob.currentRadialCenter - radialHalfSize;
            float radialEnd = blob.currentRadialCenter + radialHalfSize;

            // Map virtual pixels to this arm's LEDs
            for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
                uint8_t virtualPos = a + p * 3;  // Virtual mapping
                float vPos = static_cast<float>(virtualPos);

                if (vPos >= radialStart && vPos <= radialEnd) {
                    arm.pixels[p] += blob.color;  // Additive blending
                }
            }
        }
    }
}

void VirtualBlobs::initializeBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        float minAngularSize, maxAngularSize;
        float angularDriftSpeed, angularSizeSpeed, angularWanderRange;
        float minRadialSize, maxRadialSize;
        float radialDriftSpeed, radialSizeSpeed, radialWanderRange;
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

        // Angular parameters (convert degrees to units: degrees * 10)
        blobs[i].wanderCenterUnits = static_cast<angle_t>(i * 720);  // Spread evenly: 0, 72, 144, 216, 288 degrees
        blobs[i].wanderRangeUnits = static_cast<angle_t>(tmpl.angularWanderRange * 10);
        blobs[i].driftPhaseAccum = random16();  // Random starting phase
        blobs[i].minArcSizeUnits = static_cast<angle_t>(tmpl.minAngularSize * 10);
        blobs[i].maxArcSizeUnits = static_cast<angle_t>(tmpl.maxAngularSize * 10);
        blobs[i].sizePhaseAccum = random16();  // Random starting phase

        // Radial parameters (0-29 range for virtual)
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
