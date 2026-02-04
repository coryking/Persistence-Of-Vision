#ifndef PROJECTION_TEST_H
#define PROJECTION_TEST_H

#include "Effect.h"

class ProjectionTest : public Effect {
public:
    void render(RenderContext& ctx) override;
private:
    // Rotation offset in angle units, updated each frame
    uint16_t rotationOffset = 0;
};

#endif
