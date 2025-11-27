#ifndef BLOB_TYPES_H
#define BLOB_TYPES_H

#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <NeoPixelBus.h>
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
    RgbColor color;               // Blob's color

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
// HSL: Hue (0-1), Saturation (0-1), Lightness (0-1)
inline HslColor citrusPalette[MAX_BLOBS] = {
    HslColor(0.08f, 1.0f, 0.5f),    // Orange (30°)
    HslColor(0.15f, 0.9f, 0.5f),    // Yellow-orange (55°)
    HslColor(0.25f, 0.85f, 0.5f),   // Yellow-green (90°)
    HslColor(0.33f, 0.9f, 0.45f),   // Green (120°)
    HslColor(0.40f, 0.85f, 0.4f)    // Blue-green (145°)
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
