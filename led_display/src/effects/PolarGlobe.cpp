#include "effects/PolarGlobe.h"
#include "hardware_config.h"
#include "geometry.h"
#include "textures/polar_earth_day.h"
#include "textures/polar_earth_night.h"
#include "textures/polar_earth_clouds.h"
#include "textures/polar_mars.h"
#include "textures/polar_jupiter.h"
#include "textures/polar_saturn.h"
#include "textures/polar_neptune.h"
#include "textures/polar_sun.h"
#include "textures/polar_moon.h"
#include "textures/polar_mercury.h"
#include "textures/polar_makemake.h"
#include "esp_log.h"

static const char* TAG = "POLAR";

// All textures must have the same dimensions (720x44)
static constexpr int TEXTURE_WIDTH = POLAR_EARTH_DAY_WIDTH;
static constexpr int TEXTURE_HEIGHT = POLAR_EARTH_DAY_HEIGHT;

// Texture names for logging
static const char* TEXTURE_NAMES[] = {
    "Earth Day", "Earth Night", "Earth Clouds",
    "Mars", "Jupiter", "Saturn", "Neptune",
    "Sun", "Moon", "Mercury", "Makemake"
};

void PolarGlobe::paramUp() {
    textureIndex = (textureIndex + 1) % NUM_TEXTURES;
    ESP_LOGI(TAG, "Texture -> %s", TEXTURE_NAMES[textureIndex]);
}

void PolarGlobe::paramDown() {
    textureIndex = (textureIndex + NUM_TEXTURES - 1) % NUM_TEXTURES;
    ESP_LOGI(TAG, "Texture -> %s", TEXTURE_NAMES[textureIndex]);
}

void PolarGlobe::render(RenderContext& ctx) {
    // Rotate planet: ~36°/second (full rotation in 10 sec)
    // 720 columns = 360°, so 72 columns/second = 36°/second
    rotationOffset += (ctx.frameDeltaUs * 9) / 125000;
    rotationOffset %= TEXTURE_WIDTH;

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Map disc angle to texture column (longitude)
        int textureCol = (arm.angle / 5 + rotationOffset) % TEXTURE_WIDTH;

        int ledCount = HardwareConfig::ARM_LED_COUNT[a];
        for (int p = 0; p < ledCount; p++) {
            // Convert arm LED position to virtual ring
            int ring = (a == 0) ? p * 3 : p * 3 + a;

            // Map ring (0-39) to texture row (43-0)
            int textureRow = (TEXTURE_HEIGHT - 1) - (ring * (TEXTURE_HEIGHT - 1) / 39);
            if (textureRow < 0) textureRow = 0;
            if (textureRow >= TEXTURE_HEIGHT) textureRow = TEXTURE_HEIGHT - 1;

            // Read from selected texture
            CRGB color;
            switch (textureIndex) {
                case 0:  color = polar_earth_day[textureRow][textureCol]; break;
                case 1:  color = polar_earth_night[textureRow][textureCol]; break;
                case 2:  color = polar_earth_clouds[textureRow][textureCol]; break;
                case 3:  color = polar_mars[textureRow][textureCol]; break;
                case 4:  color = polar_jupiter[textureRow][textureCol]; break;
                case 5:  color = polar_saturn[textureRow][textureCol]; break;
                case 6:  color = polar_neptune[textureRow][textureCol]; break;
                case 7:  color = polar_sun[textureRow][textureCol]; break;
                case 8:  color = polar_moon[textureRow][textureCol]; break;
                case 9:  color = polar_mercury[textureRow][textureCol]; break;
                case 10: color = polar_makemake[textureRow][textureCol]; break;
                default: color = polar_earth_day[textureRow][textureCol]; break;
            }

            // Radial brightness compensation for POV physics
            uint8_t brightnessScale = 64 + (ring * 191 / 39);
            color.nscale8(brightnessScale);

            arm.pixels[p] = color;
        }
    }
}
