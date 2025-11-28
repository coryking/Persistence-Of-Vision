#ifndef PER_ARM_BLOBS_H
#define PER_ARM_BLOBS_H

#include "Effect.h"
#include "blob_types.h"

/**
 * Per-arm lava lamp blobs
 *
 * Each blob belongs to one arm and creates a colored wedge-shaped region.
 * Blobs are only visible when their assigned arm passes through their
 * angular arc and pixels are within their radial extent.
 *
 * Visual effect: Three independent lava lamps, one per arm.
 *
 * Features:
 * - 5 blobs distributed across 3 arms (2 inner, 2 middle, 1 outer)
 * - Time-based sine wave animation (position + size breathing)
 * - Radial range: 0-9 LEDs per arm
 * - Shape-first rendering (iterate blobs, not pixels)
 */
class PerArmBlobs : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(float rpm) override;

private:
    Blob blobs[MAX_BLOBS];  // MAX_BLOBS defined in blob_types.h

    void initializeBlobs();
};

#endif // PER_ARM_BLOBS_H
