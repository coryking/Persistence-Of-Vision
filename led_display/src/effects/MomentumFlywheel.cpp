#include "effects/MomentumFlywheel.h"
#include "polar_helpers.h"
#include "hardware_config.h"
#include <FastLED.h>

void MomentumFlywheel::begin() {
    storedEnergy = 0;
    lastDecayTime = 0;
}

uint16_t MomentumFlywheel::speedToEnergy(interval_t microsPerRev) const {
    // Map speed to energy: faster = more energy
    // Use hand-spin speed range
    uint8_t speed = speedFactor8HandSpin(microsPerRev);
    // Scale up to 16-bit for smoother decay
    return static_cast<uint16_t>(speed) << 8;
}

CHSV MomentumFlywheel::energyToColor(uint16_t energy) const {
    // Map energy to color: high = warm orange, low = cool blue
    // energy is 0-65535, scale down to 0-255 for color calc
    uint8_t energy8 = energy >> 8;

    // High energy (255) → hue 40 (orange/yellow)
    // Low energy (0) → hue 160 (blue/purple)
    uint8_t hue = 160 - scale8(120, energy8);

    // Brightness follows energy with a minimum threshold
    uint8_t brightness = energy8 > 20 ? energy8 : 0;

    // Full saturation
    return CHSV(hue, 255, brightness);
}

void MomentumFlywheel::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    (void)revolutionCount;

    // Calculate energy from current speed
    uint16_t newEnergy = speedToEnergy(usPerRev);

    // Pump up energy if new value exceeds current (captures acceleration)
    if (newEnergy > storedEnergy) {
        storedEnergy = newEnergy;
    }

    // Also update decay time reference
    lastDecayTime = timestamp;
}

void MomentumFlywheel::render(RenderContext& ctx) {
    ctx.clear();

    timestamp_t now = ctx.timestampUs;

    // Apply continuous exponential decay
    if (lastDecayTime > 0 && storedEnergy > 0) {
        timestamp_t elapsed = now - lastDecayTime;

        // Exponential decay: energy *= 0.5^(elapsed/halfLife)
        // Using integer approximation: shift right for each half-life elapsed
        // For smoother decay, we'll use a linear approximation for small intervals
        //
        // decay_factor = 2^(-elapsed/halfLife)
        // For elapsed << halfLife: decay_factor ≈ 1 - (elapsed * ln(2) / halfLife)
        // ln(2) ≈ 0.693, use 179/256 ≈ 0.699

        if (elapsed >= DECAY_HALF_LIFE_US * 8) {
            // More than 8 half-lives: essentially zero
            storedEnergy = 0;
        } else {
            // Calculate decay: subtract proportional amount each frame
            // decay_amount = energy * elapsed * 179 / (halfLife * 256)
            uint32_t decayAmount = (static_cast<uint32_t>(storedEnergy) * elapsed * 179) /
                                   (static_cast<uint32_t>(DECAY_HALF_LIFE_US) * 256);

            if (decayAmount >= storedEnergy) {
                storedEnergy = 0;
            } else {
                storedEnergy -= static_cast<uint16_t>(decayAmount);
            }
        }

        lastDecayTime = now;
    }

    // If no energy, nothing to render
    if (storedEnergy == 0) return;

    // Get color from energy level
    CRGB16 color = energyToColor(storedEnergy);

    // Fill all pixels with the glow color
    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];
        for (int led = 0; led < HardwareConfig::LEDS_PER_ARM; led++) {
            arm.pixels[led] = color;
        }
    }
}
