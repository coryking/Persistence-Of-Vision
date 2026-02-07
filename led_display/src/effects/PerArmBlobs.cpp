#include "effects/PerArmBlobs.h"
#include "blob_types.h"
#include "polar_helpers.h"
#include "esp_timer.h"

void PerArmBlobs::begin() {
    initializeBlobs();
}

void PerArmBlobs::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Update blob animations once per revolution (47 Hz at 2800 RPM)
    for (auto& blob : blobs) {
        updateBlob(blob, timestamp);
    }
}

void PerArmBlobs::render(RenderContext& ctx) {
    ctx.clear();

    // Shape-first: iterate blobs, not pixels
    for (const auto& blob : blobs) {
        if (!blob.active) continue;

        // This blob belongs to ONE specific arm
        uint8_t targetArm = blob.armIndex;
        auto& arm = ctx.arms[targetArm];

        // Check if arm is currently in blob's angular arc
        if (!isAngleInArcUnits(arm.angle, blob.currentStartAngleUnits, blob.currentArcSizeUnits)) {
            continue;  // Arm not in blob's wedge - skip this blob
        }

        // Fill affected radial pixels on this arm (0 to LEDS_PER_ARM-1)
        float radialHalfSize = blob.currentRadialSize / 2.0f;
        float radialStart = blob.currentRadialCenter - radialHalfSize;
        float radialEnd = blob.currentRadialCenter + radialHalfSize;

        for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
            float pixelPos = static_cast<float>(p);
            if (pixelPos >= radialStart && pixelPos <= radialEnd) {
                arm.pixels[p] += blob.color;  // Additive blending
            }
        }
    }
}

void PerArmBlobs::initializeBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        float minAngularSize, maxAngularSize;
        float angularDriftSpeed, angularSizeSpeed, angularWanderRange;
        float minRadialSize, maxRadialSize;
        float radialDriftSpeed, radialSizeSpeed, radialWanderRange;
    } templates[3] = {
        // Small, fast (angular 5-30°, radial 1-3 LEDs)
        {5, 30, 0.5, 0.3, 60,    1, 3, 0.4, 0.25, 2.0},
        // Medium (angular 10-60°, radial 2-5 LEDs)
        {10, 60, 0.3, 0.2, 90,   2, 5, 0.25, 0.15, 2.5},
        // Large, slow (angular 20-90°, radial 3-7 LEDs)
        {20, 90, 0.15, 0.1, 120, 3, 7, 0.15, 0.1, 3.0}
    };

    // Distribute blobs: 2 inside, 2 middle, 1 outer
    uint8_t armAssignments[MAX_BLOBS] = {
        ARM_INSIDE, ARM_INSIDE,
        ARM_MIDDLE, ARM_MIDDLE,
        ARM_OUTER
    };

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        blobs[i].armIndex = armAssignments[i];
        blobs[i].color = citrusPalette[i];  // CHSV to CRGB conversion automatic

        // Use template based on blob index (varied sizes)
        BlobTemplate& tmpl = templates[i % 3];

        // Angular parameters (convert degrees to units: degrees * 10)
        blobs[i].wanderCenterUnits = static_cast<angle_t>(i * 720);  // Spread evenly: 0, 720, 1440, 2160, 2880
        blobs[i].wanderRangeUnits = static_cast<angle_t>(tmpl.angularWanderRange * 10);
        blobs[i].driftPhaseAccum = random16();  // Random start for variety
        blobs[i].minArcSizeUnits = static_cast<angle_t>(tmpl.minAngularSize * 10);
        blobs[i].maxArcSizeUnits = static_cast<angle_t>(tmpl.maxAngularSize * 10);
        blobs[i].sizePhaseAccum = random16();  // Random start for variety

        // Radial parameters (0-9 range for per-arm)
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
