#ifndef BLOB_CACHE_H
#define BLOB_CACHE_H

#include "blob_types.h"
#include <cmath>

/**
 * Pre-computed blob geometry for fast rendering
 * Eliminates ~150 fmod() calls and ~100 divisions per frame
 */
struct BlobGeometry {
    float radialStart;
    float radialEnd;
    bool radialWraps;     // For VirtualBlobs (0-29 range)

    float angleStart;     // Normalized 0-360
    float angleEnd;       // Normalized 0-360
    bool angleWraps;      // Crosses 0°/360° boundary
};

// Global blob cache (pre-computed once per frame)
inline BlobGeometry blobCache[MAX_BLOBS];

/**
 * Update blob cache with pre-computed geometry
 * Call this in main.cpp after updateBlob() loop, before rendering
 *
 * @param blobs Array of blobs to cache
 * @param count Number of blobs (typically MAX_BLOBS)
 * @param virtual_range If true, use 0-29 range (VirtualBlobs); if false, use 0-9 (PerArmBlobs)
 */
inline void updateBlobCache(const Blob blobs[], int count, bool virtual_range) {
    const float rangeMax = virtual_range ? 30.0f : 10.0f;

    for (int i = 0; i < count; i++) {
        if (!blobs[i].active) continue;

        const Blob& b = blobs[i];

        // Pre-compute radial extent
        float halfSize = b.currentRadialSize / 2.0f;
        blobCache[i].radialStart = b.currentRadialCenter - halfSize;
        blobCache[i].radialEnd = b.currentRadialCenter + halfSize;
        blobCache[i].radialWraps = virtual_range &&
                                   (blobCache[i].radialStart < 0 || blobCache[i].radialEnd >= rangeMax);

        // Pre-compute angle extent (eliminate fmod in isAngleInArc)
        blobCache[i].angleStart = fmod(b.currentStartAngle + 360.0f, 360.0f);
        float arcEnd = b.currentStartAngle + b.currentArcSize;
        blobCache[i].angleEnd = fmod(arcEnd + 360.0f, 360.0f);
        blobCache[i].angleWraps = arcEnd > 360.0f;
    }
}

/**
 * Fast radial check using pre-computed cache
 *
 * @param pos LED position (0-9 for PerArmBlobs, 0-29 for VirtualBlobs)
 * @param blobIdx Blob index in cache
 * @return true if LED is within blob's radial range
 */
inline bool isLedInBlobCached(uint8_t pos, int blobIdx) {
    const BlobGeometry& g = blobCache[blobIdx];
    float fpos = static_cast<float>(pos);

    if (g.radialWraps) {
        // Wraparound case (VirtualBlobs)
        if (g.radialStart < 0) {
            return (fpos >= (g.radialStart + 30)) || (fpos < g.radialEnd);
        } else {
            return (fpos >= g.radialStart) || (fpos < (g.radialEnd - 30));
        }
    } else {
        // Simple range (PerArmBlobs or no wrap)
        return (fpos >= g.radialStart) && (fpos < g.radialEnd);
    }
}

/**
 * Fast angle check using pre-computed cache
 *
 * @param angle Arm angle in degrees (0-360)
 * @param blobIdx Blob index in cache
 * @return true if angle is within blob's arc
 */
inline bool isAngleInArcCached(float angle, int blobIdx) {
    const BlobGeometry& g = blobCache[blobIdx];

    if (g.angleWraps) {
        return (angle >= g.angleStart) || (angle < g.angleEnd);
    } else {
        return (angle >= g.angleStart) && (angle < g.angleEnd);
    }
}

#endif
