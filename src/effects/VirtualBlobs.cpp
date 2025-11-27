#include "effects.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "blob_types.h"
#include "esp_timer.h"

// External references to globals from main.cpp
extern NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip;
extern Blob blobs[MAX_BLOBS];
extern const uint8_t PHYSICAL_TO_VIRTUAL[30];

// Hardware configuration (from main.cpp)
constexpr uint16_t LEDS_PER_ARM = 10;
constexpr uint16_t INNER_ARM_START = 10;
constexpr uint16_t MIDDLE_ARM_START = 0;
constexpr uint16_t OUTER_ARM_START = 20;

/**
 * Check if virtual LED position is within blob's current radial extent
 * (with wraparound support for 0-29 range)
 */
static bool isVirtualLedInBlob(uint8_t virtualPos, const Blob& blob) {
    if (!blob.active) return false;

    // Calculate radial range (virtual positions covered by this blob)
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;

    // Check with wraparound handling
    float pos = static_cast<float>(virtualPos);

    // If range doesn't wrap around, simple check
    if (radialStart >= 0 && radialEnd < 30) {
        return (pos >= radialStart) && (pos < radialEnd);
    }

    // Handle wraparound (blob spans 29 → 0 boundary)
    if (radialStart < 0) {
        // Blob extends below 0, wraps to high end
        return (pos >= (radialStart + 30)) || (pos < radialEnd);
    }

    if (radialEnd >= 30) {
        // Blob extends above 29, wraps to low end
        return (pos >= radialStart) || (pos < (radialEnd - 30));
    }

    return false;
}

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
void renderVirtualBlobs(const RenderContext& ctx) {
    // Render using virtual addressing - each LED checks against all blobs

    // Inner arm: LEDs 10-19
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = INNER_ARM_START + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
        RgbColor ledColor(0, 0, 0);

        // Check ALL blobs (no arm filtering)
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active &&
                isAngleInArc(ctx.innerArmDegrees, blobs[i]) &&
                isVirtualLedInBlob(virtualPos, blobs[i])) {
                ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                ledColor.B = min(255, ledColor.B + blobs[i].color.B);
            }
        }
        strip.SetPixelColor(physicalLed, ledColor);
    }

    // Middle arm: LEDs 0-9
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = MIDDLE_ARM_START + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
        RgbColor ledColor(0, 0, 0);

        // Check ALL blobs (no arm filtering)
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active &&
                isAngleInArc(ctx.middleArmDegrees, blobs[i]) &&
                isVirtualLedInBlob(virtualPos, blobs[i])) {
                ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                ledColor.B = min(255, ledColor.B + blobs[i].color.B);
            }
        }
        strip.SetPixelColor(physicalLed, ledColor);
    }

    // Outer arm: LEDs 20-29
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = OUTER_ARM_START + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
        RgbColor ledColor(0, 0, 0);

        // Check ALL blobs (no arm filtering)
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active &&
                isAngleInArc(ctx.outerArmDegrees, blobs[i]) &&
                isVirtualLedInBlob(virtualPos, blobs[i])) {
                ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                ledColor.B = min(255, ledColor.B + blobs[i].color.B);
            }
        }
        strip.SetPixelColor(physicalLed, ledColor);
    }
}
