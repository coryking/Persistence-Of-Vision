#ifndef POLAR_GLOBE_H
#define POLAR_GLOBE_H

#include "Effect.h"

/**
 * PolarGlobe - Planetary bodies viewed from above a pole
 *
 * Displays pre-computed polar-native textures of celestial bodies.
 * The math is trivial since disc angle maps directly to longitude.
 *
 * Texture layout:
 *   - Row 0 = outer edge (equator)
 *   - Row 43 = center (pole)
 *   - Column = longitude (0° to 360°)
 *
 * Controls:
 *   - Up/Down arrows: Cycle through planets
 *
 * Order: Earth Day, Earth Night, Earth Clouds, Mars, Jupiter, Saturn,
 *        Neptune, Sun, Moon, Mercury, Makemake
 */
class PolarGlobe : public Effect {
public:
    void render(RenderContext& ctx) override;
    void up() override;
    void down() override;

private:
    static constexpr uint8_t NUM_TEXTURES = 11;

    // Rotation offset in texture columns, updated each frame
    uint16_t rotationOffset = 0;

    // Current texture index
    uint8_t textureIndex = 0;
};

#endif // POLAR_GLOBE_H
