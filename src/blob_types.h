#ifndef BLOB_TYPES_H
#define BLOB_TYPES_H

#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <FastLED.h>
#include "types.h"

// Arm indices
#define ARM_INNER  0
#define ARM_MIDDLE 1
#define ARM_OUTER  2

// Blob pool - up to 5 total blobs across all arms
#define MAX_BLOBS 5

// Blob structure - independent lava lamp blobs with lifecycle
struct Blob {
    bool active;                  // Is this blob alive?
    uint8_t armIndex;             // Which arm (0-2) this blob belongs to (unused for virtual blobs)
    CRGB color;                   // Blob's color

    // Angular position (where the arc is)
    float currentStartAngle;      // 0-360°, current position
    float driftVelocity;          // rad/sec frequency for sine wave drift
    float wanderCenter;           // center point for wandering
    float wanderRange;            // +/- range from center

    // Angular size (how big the arc is)
    float currentArcSize;         // current size in degrees
    float minArcSize;             // minimum wedge size
    float maxArcSize;             // maximum wedge size
    float sizeChangeRate;         // frequency for size oscillation

    // Radial position (LED index along arm, 0-9 for per-arm, 0-29 for virtual)
    float currentRadialCenter;    // current LED position (can go out of bounds)
    float radialDriftVelocity;    // rad/sec frequency for radial drift
    float radialWanderCenter;     // radial center point for wandering
    float radialWanderRange;      // +/- radial drift range

    // Radial size (height in LED count)
    float currentRadialSize;      // current height in LEDs
    float minRadialSize;          // minimum LED count
    float maxRadialSize;          // maximum LED count
    float radialSizeChangeRate;   // frequency for radial breathing

    // Lifecycle timing
    timestamp_t birthTime;        // When blob spawned
    timestamp_t deathTime;        // When blob will die (0 = immortal for now)
};

// Color palette: Citrus (oranges to greens)
// CHSV: Hue (0-255), Saturation (0-255), Value (0-255)
inline CHSV citrusPalette[MAX_BLOBS] = {
    CHSV(20, 255, 255),    // Orange (30° × 255/360 ≈ 20)
    CHSV(39, 230, 255),    // Yellow-orange (55° × 255/360 ≈ 39)
    CHSV(64, 217, 255),    // Yellow-green (90° × 255/360 ≈ 64)
    CHSV(85, 230, 230),    // Green (120° × 255/360 ≈ 85)
    CHSV(102, 217, 204)    // Blue-green (145° × 255/360 ≈ 102)
};

/**
 * Update blob animation state using time-based sine waves
 */
inline void updateBlob(Blob& blob, timestamp_t now) {
    if (!blob.active) return;

    float timeInSeconds = now / 1000000.0f;

    // Angular position drift: sine wave wandering around center point
    blob.currentStartAngle = blob.wanderCenter +
                             sin(timeInSeconds * blob.driftVelocity) * blob.wanderRange;
    blob.currentStartAngle = fmod(blob.currentStartAngle + 360.0f, 360.0f);

    // Angular size breathing: sine wave oscillation between min and max
    float angularPhase = timeInSeconds * blob.sizeChangeRate;
    blob.currentArcSize = blob.minArcSize +
                          (blob.maxArcSize - blob.minArcSize) *
                          (sin(angularPhase) * 0.5f + 0.5f);

    // Radial position drift: sine wave wandering around center point
    blob.currentRadialCenter = blob.radialWanderCenter +
                               sin(timeInSeconds * blob.radialDriftVelocity) * blob.radialWanderRange;
    // Note: No wraparound needed - clipping happens at render time

    // Radial size breathing: sine wave oscillation between min and max
    float radialPhase = timeInSeconds * blob.radialSizeChangeRate;
    blob.currentRadialSize = blob.minRadialSize +
                            (blob.maxRadialSize - blob.minRadialSize) *
                            (sin(radialPhase) * 0.5f + 0.5f);
}

/**
 * Check if angle is within blob's current arc (handles 360° wraparound)
 */
inline bool isAngleInArc(double angle, const Blob& blob) {
    if (!blob.active) return false;

    double arcEnd = blob.currentStartAngle + blob.currentArcSize;

    // Handle wraparound (e.g., arc from 350° to 10°)
    if (arcEnd > 360.0f) {
        return (angle >= blob.currentStartAngle) || (angle < fmod(arcEnd, 360.0f));
    }

    return (angle >= blob.currentStartAngle) && (angle < arcEnd);
}

#endif // BLOB_TYPES_H
