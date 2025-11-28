#ifndef BLOB_TYPES_H
#define BLOB_TYPES_H

#include <Arduino.h>
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
    angle_t currentStartAngleUnits;  // 0-3600 (0-360°), current position
    uint16_t driftPhaseAccum;        // FastLED beat accumulator for drift phase
    angle_t wanderCenterUnits;       // center point for wandering (in angle units)
    angle_t wanderRangeUnits;        // +/- range from center (in angle units)

    // Angular size (how big the arc is)
    angle_t currentArcSizeUnits;     // current size in angle units
    angle_t minArcSizeUnits;         // minimum wedge size in angle units
    angle_t maxArcSizeUnits;         // maximum wedge size in angle units
    uint16_t sizePhaseAccum;         // FastLED beat accumulator for size oscillation

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

// Color palette: Lava (black → deep red → orange → yellow → white hot)
// CHSV: Hue (0-255), Saturation (0-255), Value (0-255)
inline CHSV citrusPalette[MAX_BLOBS] = {
    CHSV(0, 255, 80),      // Deep red (barely glowing)
    CHSV(0, 255, 180),     // Bright red (hot)
    CHSV(10, 255, 255),    // Red-orange (molten)
    CHSV(20, 200, 255),    // Orange (very hot)
    CHSV(32, 180, 255)     // Yellow-orange (white hot)
};

/**
 * Update blob animation state using pure integer math
 * Uses FastLED's sin16() for performance (no floating point!)
 *
 * Integer angle system: angle_t where 3600 = 360 degrees (0.1° precision)
 * Phase accumulators increment each frame for smooth animation
 */
inline void updateBlob(Blob& blob, timestamp_t now) {
    if (!blob.active) return;

    // Angular position drift: sine wave wandering around center point
    // sin16() takes 0-65535 (full circle), returns -32768 to 32767
    int16_t angleSin = sin16(blob.driftPhaseAccum);

    // Scale sin16 output (-32768 to 32767) to wanderRange
    // sin16 gives full scale, we need to map to +/- wanderRangeUnits
    // scale16by8() is faster but we need signed math here
    int32_t offset = ((int32_t)angleSin * (int32_t)blob.wanderRangeUnits) / 32768;

    // Calculate current position: center + offset
    int32_t newAngle = (int32_t)blob.wanderCenterUnits + offset;

    // Wrap to 0-3600 range
    while (newAngle < 0) newAngle += ANGLE_FULL_CIRCLE;
    while (newAngle >= ANGLE_FULL_CIRCLE) newAngle -= ANGLE_FULL_CIRCLE;

    blob.currentStartAngleUnits = (angle_t)newAngle;

    // Angular size breathing: sine wave oscillation between min and max
    // sin16 returns -32768 to 32767, we want 0 to range for breathing
    int16_t sizeSin = sin16(blob.sizePhaseAccum);

    // Convert -32768..32767 to 0..65535 for unipolar oscillation
    uint16_t sizeOscillation = (uint16_t)((int32_t)sizeSin + 32768);

    // Scale oscillation to size range: min + scale16(oscillation, max - min)
    uint16_t sizeRange = blob.maxArcSizeUnits - blob.minArcSizeUnits;
    blob.currentArcSizeUnits = blob.minArcSizeUnits + scale16(sizeOscillation, sizeRange);

    // Radial position drift: sine wave wandering around center point
    int16_t radialAngleSin = sin16(blob.radialDriftVelocity);  // TODO: needs phase accumulator too
    blob.currentRadialCenter = blob.radialWanderCenter + (radialAngleSin / 32768.0f) * blob.radialWanderRange;
    // Note: No wraparound needed - clipping happens at render time

    // Radial size breathing: sine wave oscillation between min and max
    int16_t radialSizeSin = sin16(blob.radialSizeChangeRate);  // TODO: needs phase accumulator too
    float radialSizeOscillation = (radialSizeSin / 32768.0f) * 0.5f + 0.5f;  // 0..1 range
    blob.currentRadialSize = blob.minRadialSize +
                            (blob.maxRadialSize - blob.minRadialSize) * radialSizeOscillation;
}

#endif // BLOB_TYPES_H
