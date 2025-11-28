#ifndef POLAR_HELPERS_H
#define POLAR_HELPERS_H

#include <cmath>
#include <algorithm>
#include "RenderContext.h"

/**
 * Polar coordinate helpers for POV display effects
 *
 * These helpers handle the math for angular and radial operations,
 * including proper 360° wraparound handling.
 */

// ============================================================
// Angular Helpers
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

/**
 * Check if ALL arms are within the target arc
 *
 * Use when you want the virtual column to appear as a unit.
 * Since arms are 120° apart, this requires a wide arc (~240°+)
 * for all three to be visible simultaneously.
 */
inline bool isVirtualColumnInArc(const RenderContext& ctx,
                                  float arcCenter,
                                  float arcWidth) {
    for (int a = 0; a < 3; a++) {
        if (!isAngleInArc(ctx.arms[a].angle, arcCenter, arcWidth)) {
            return false;
        }
    }
    return true;
}

/**
 * Check if ANY arm is within the target arc
 *
 * Use when partial visibility is acceptable.
 */
inline bool isAnyArmInArc(const RenderContext& ctx,
                          float arcCenter,
                          float arcWidth) {
    for (int a = 0; a < 3; a++) {
        if (isAngleInArc(ctx.arms[a].angle, arcCenter, arcWidth)) {
            return true;
        }
    }
    return false;
}

/**
 * Get intensity for each arm based on arc position
 *
 * @param ctx Render context with arm angles
 * @param arcCenter Center of arc (degrees)
 * @param arcWidth Width of arc (degrees)
 * @param intensities Output: array of 3 intensities (0.0-1.0)
 */
inline void getArmIntensities(const RenderContext& ctx,
                               float arcCenter,
                               float arcWidth,
                               float intensities[3]) {
    for (int a = 0; a < 3; a++) {
        intensities[a] = arcIntensity(ctx.arms[a].angle, arcCenter, arcWidth);
    }
}

/**
 * Count how many arms are currently in the arc
 *
 * @return 0, 1, 2, or 3
 */
inline uint8_t countArmsInArc(const RenderContext& ctx,
                               float arcCenter,
                               float arcWidth) {
    uint8_t count = 0;
    for (int a = 0; a < 3; a++) {
        if (isAngleInArc(ctx.arms[a].angle, arcCenter, arcWidth)) {
            count++;
        }
    }
    return count;
}

#endif // POLAR_HELPERS_H
