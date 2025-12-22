#ifndef VIRTUAL_BLOBS_H
#define VIRTUAL_BLOBS_H

#include "Effect.h"
#include "blob_types.h"

/**
 * Virtual display blobs (unified 30-row space)
 *
 * Blobs exist in unified 30-row radial space. A blob can appear
 * on multiple arms simultaneously when they pass through its angular
 * arc. Uses virtual pixel mapping (0-29 radial range).
 *
 * Visual effect: Interlaced patterns across all three arms.
 *
 * Features:
 * - 5 blobs in unified virtual space
 * - Time-based sine wave animation (position + size breathing)
 * - Radial range: 0-29 (maps to all three arms)
 * - Shape-first rendering (iterate blobs, check all arms)
 */
class VirtualBlobs : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;

private:
    Blob blobs[MAX_BLOBS];  // MAX_BLOBS defined in blob_types.h

    void initializeBlobs();
};

#endif // VIRTUAL_BLOBS_H
