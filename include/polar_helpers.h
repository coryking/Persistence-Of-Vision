#ifndef POLAR_HELPERS_H
#define POLAR_HELPERS_H

#include <cmath>
#include <algorithm>
#include "types.h"
#include "RenderContext.h"

/**
 * Polar coordinate helpers for POV display effects
 *
 * These helpers handle the math for angular and radial operations,
 * including proper 360° wraparound handling.
 */

// ============================================================
// Integer Angle Helpers (3600 units = 360 degrees)
// ============================================================

/**
 * Normalize angle units to 0-3599 range
 */
inline angle_t normalizeAngleUnits(int32_t units) {
    int32_t normalized = units % ANGLE_FULL_CIRCLE;
    return normalized < 0 ? static_cast<angle_t>(normalized + ANGLE_FULL_CIRCLE) : static_cast<angle_t>(normalized);
}

/**
 * Signed angular distance in units (-1800 to +1800)
 *
 * @return Distance in range -1800 to +1800
 *         Positive = clockwise from 'from' to 'to'
 *         Negative = counter-clockwise
 */
inline int16_t angularDistanceUnits(angle_t from, angle_t to) {
    int32_t diff = static_cast<int32_t>(to) - static_cast<int32_t>(from);
    diff = ((diff % ANGLE_FULL_CIRCLE) + ANGLE_FULL_CIRCLE) % ANGLE_FULL_CIRCLE;
    return diff > ANGLE_HALF_CIRCLE ? static_cast<int16_t>(diff - ANGLE_FULL_CIRCLE) : static_cast<int16_t>(diff);
}

/**
 * Absolute angular distance in units (0 to 1800)
 *
 * @return Distance in range 0 to 1800
 */
inline angle_t angularDistanceAbsUnits(angle_t a, angle_t b) {
    int16_t dist = angularDistanceUnits(a, b);
    return static_cast<angle_t>(dist < 0 ? -dist : dist);
}

/**
 * Check if angle is within arc centered at 'center' with given 'width'
 *
 * Handles 360° wraparound correctly (e.g., arc from 3500 to 100 units)
 *
 * @param angle Angle to test (units)
 * @param center Center of arc (units)
 * @param width Total width of arc (units)
 * @return true if angle is within arc
 */
inline bool isAngleInArcUnits(angle_t angle, angle_t center, angle_t width) {
    angle_t halfWidth = width / 2;
    angle_t dist = angularDistanceAbsUnits(center, angle);
    return dist <= halfWidth;
}

/**
 * Arc intensity using integer math (returns 0-255 for FastLED scale8)
 *
 * @param angle Angle to test (units)
 * @param center Center of arc (units)
 * @param width Total width of arc (units)
 * @return 0 = outside arc, 255 = at center, linear fade between
 */
inline uint8_t arcIntensityUnits(angle_t angle, angle_t center, angle_t width) {
    angle_t halfWidth = width / 2;
    angle_t dist = angularDistanceAbsUnits(center, angle);
    if (dist > halfWidth) return 0;
    // Linear interpolation: 255 at center, 0 at edge
    return static_cast<uint8_t>(255 - (static_cast<uint32_t>(dist) * 255 / halfWidth));
}

// ============================================================
// Speed Helpers
// ============================================================

/**
 * Map rotation speed to 0-255 (faster = higher value)
 * Uses microsPerRev which is the raw timing measurement.
 * Lower microsPerRev = faster rotation = higher return value.
 */
inline uint8_t speedFactor8(interval_t microsPerRev) {
    if (microsPerRev >= MICROS_PER_REV_MAX) return 0;
    if (microsPerRev <= MICROS_PER_REV_MIN) return 255;
    return static_cast<uint8_t>(
        (MICROS_PER_REV_MAX - microsPerRev) * 255 /
        (MICROS_PER_REV_MAX - MICROS_PER_REV_MIN)
    );
}

// ============================================================
// Angular Helpers (Float - Legacy)
// ============================================================

/**
 * Normalize angle to 0-360 range
 */
inline float normalizeAngle(float angle) {
    angle = fmod(angle, 360.0f);
    return angle < 0.0f ? angle + 360.0f : angle;
}

/**
 * Signed angular distance from 'from' to 'to'
 *
 * @return Distance in range -180 to +180
 *         Positive = clockwise from 'from' to 'to'
 *         Negative = counter-clockwise
 */
inline float angularDistance(float from, float to) {
    float diff = normalizeAngle(to - from);
    return diff > 180.0f ? diff - 360.0f : diff;
}

/**
 * Absolute angular distance between two angles
 *
 * @return Distance in range 0 to 180
 */
inline float angularDistanceAbs(float a, float b) {
    return fabsf(angularDistance(a, b));
}

/**
 * Check if angle is within arc centered at 'center' with given 'width'
 *
 * Handles 360° wraparound correctly (e.g., arc from 350° to 10°)
 *
 * @param angle Angle to test (degrees)
 * @param center Center of arc (degrees)
 * @param width Total width of arc (degrees)
 * @return true if angle is within arc
 */
inline bool isAngleInArc(float angle, float center, float width) {
    float halfWidth = width / 2.0f;
    float dist = angularDistanceAbs(center, angle);
    return dist <= halfWidth;
}

/**
 * Calculate intensity based on position within arc
 *
 * @param angle Angle to test (degrees)
 * @param center Center of arc (degrees)
 * @param width Total width of arc (degrees)
 * @return 0.0 = outside arc, 1.0 = at center, linear fade between
 */
inline float arcIntensity(float angle, float center, float width) {
    float halfWidth = width / 2.0f;
    float dist = angularDistanceAbs(center, angle);
    if (dist > halfWidth) return 0.0f;
    return 1.0f - (dist / halfWidth);
}

/**
 * Calculate soft-edge intensity with configurable fade zone
 *
 * @param angle Angle to test (degrees)
 * @param center Center of arc (degrees)
 * @param width Total width of arc (degrees)
 * @param fadeWidth Width of fade zone at edges (degrees)
 * @return 0.0 = outside, 1.0 = inside solid region, 0-1 in fade zone
 */
inline float arcIntensitySoftEdge(float angle, float center, float width, float fadeWidth) {
    float halfWidth = width / 2.0f;
    float dist = angularDistanceAbs(center, angle);

    if (dist > halfWidth) return 0.0f;
    if (dist < halfWidth - fadeWidth) return 1.0f;

    // In fade zone
    return (halfWidth - dist) / fadeWidth;
}

// ============================================================
// Radial Helpers
// ============================================================

/**
 * Check if virtual pixel position is within radial range
 *
 * @param virtualPos Virtual pixel position (0-29)
 * @param start Start of range (inclusive)
 * @param end End of range (exclusive)
 */
inline bool isRadiusInRange(uint8_t virtualPos, uint8_t start, uint8_t end) {
    return virtualPos >= start && virtualPos < end;
}

/**
 * Normalized radius from virtual position
 *
 * @param virtualPos Virtual pixel position (0-29)
 * @return 0.0 = hub (center), 1.0 = tip (edge)
 */
inline float normalizedRadius(uint8_t virtualPos) {
    return static_cast<float>(virtualPos) / 29.0f;
}

/**
 * Virtual position from normalized radius
 *
 * @param normalized Normalized radius (0.0-1.0)
 * @return Virtual pixel position (0-29)
 */
inline uint8_t virtualFromNormalized(float normalized) {
    float clamped = std::max(0.0f, std::min(1.0f, normalized));
    return static_cast<uint8_t>(clamped * 29.0f);
}

/**
 * Get arm index and LED position from virtual pixel
 *
 * @param virtualPos Virtual pixel position (0-29)
 * @param armIndex Output: arm index (0-2)
 * @param ledPos Output: LED position within arm (0-9)
 */
inline void virtualToArmLed(uint8_t virtualPos, uint8_t& armIndex, uint8_t& ledPos) {
    armIndex = virtualPos % 3;
    ledPos = virtualPos / 3;
}

/**
 * Get virtual pixel from arm index and LED position
 *
 * @param armIndex Arm index (0-2)
 * @param ledPos LED position within arm (0-9)
 * @return Virtual pixel position (0-29)
 */
inline uint8_t armLedToVirtual(uint8_t armIndex, uint8_t ledPos) {
    return armIndex + ledPos * 3;
}

// ============================================================
// Virtual Column Helpers
// ============================================================
// NOTE: Legacy float-based virtual column helpers removed.
// These referenced ctx.arms[a].angle which no longer exists
// (now using angleUnits for integer math).
// If you need similar functionality, use the integer-based
// angle helpers (isAngleInArcUnits, arcIntensityUnits, etc.)
// and work with angleUnits directly.

#endif // POLAR_HELPERS_H
