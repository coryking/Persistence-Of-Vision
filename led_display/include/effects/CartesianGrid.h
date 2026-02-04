#ifndef CARTESIAN_GRID_H
#define CARTESIAN_GRID_H

#include "Effect.h"

/**
 * Cartesian grid effect - renders straight grid lines in polar display
 *
 * Validates coordinate system mapping by drawing vertical and horizontal
 * grid lines at regular intervals. Arrow keys shift the grid pattern to
 * test alignment behavior.
 *
 * Key design:
 * - Origin (0,0) at true center of display (in the hole)
 * - Grid spans approximately -44 to +44 in both x and y
 * - ~44 pixels from center to edge: 40 real LEDs + ~4 "virtual" hole pixels
 * - Arrow keys shift grid lines (left/right for x, up/down for y)
 */
class CartesianGrid : public Effect {
public:
    void render(RenderContext& ctx) override;
    void paramUp() override;    // Shift grid up (y+)
    void paramDown() override;  // Shift grid down (y-)
    void nextMode() override;   // Shift grid right (x+)
    void prevMode() override;   // Shift grid left (x-)

private:
    int xOffset = 0;  // Grid horizontal shift
    int yOffset = 0;  // Grid vertical shift
};

#endif // CARTESIAN_GRID_H
