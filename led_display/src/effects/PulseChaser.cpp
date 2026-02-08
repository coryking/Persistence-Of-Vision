#include "effects/PulseChaser.h"
#include "polar_helpers.h"
#include "hardware_config.h"
#include <FastLED.h>

void PulseChaser::begin() {
    // Clear all pulses
    for (int i = 0; i < MAX_PULSES; i++) {
        pulses[i].spawnTime = 0;
    }
    nextPulseIndex = 0;
}

uint8_t PulseChaser::speedToHue(interval_t microsPerRev) const {
    // Map speed to hue: fast (orange/yellow ~40) → slow (blue/purple ~160)
    uint8_t speed = speedFactor8HandSpin(microsPerRev);
    // speed 255 = fast → hue 40 (orange)
    // speed 0 = slow → hue 160 (blue)
    return 160 - scale8(120, speed);
}

void PulseChaser::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    (void)revolutionCount;

    // Spawn new pulse at the hall sensor position (0°)
    Pulse& p = pulses[nextPulseIndex];
    p.spawnTime = timestamp;
    p.spawnSpeed = usPerRev;
    p.hue = speedToHue(usPerRev);

    nextPulseIndex = (nextPulseIndex + 1) % MAX_PULSES;
}

void PulseChaser::render(RenderContext& ctx) {
    ctx.clear();

    timestamp_t now = ctx.timestampUs;

    for (int i = 0; i < MAX_PULSES; i++) {
        Pulse& p = pulses[i];

        // Skip empty slots
        if (p.spawnTime == 0 || p.spawnSpeed == 0) continue;

        // Calculate pulse age
        timestamp_t age = now - p.spawnTime;

        // Calculate fade based on age (fade over FADE_REVOLUTIONS worth of time)
        timestamp_t fadeTime = p.spawnSpeed * FADE_REVOLUTIONS;
        if (age >= fadeTime) {
            // Pulse has fully faded - clear it
            p.spawnTime = 0;
            continue;
        }
        uint8_t fade = 255 - static_cast<uint8_t>((age * 255) / fadeTime);

        // Calculate current angle of this pulse
        // angle = (age / spawnSpeed) * ANGLE_FULL_CIRCLE, but use integer math
        // age and spawnSpeed are uint64_t, careful with overflow
        angle_t pulseAngle = static_cast<angle_t>(
            (age % p.spawnSpeed) * ANGLE_FULL_CIRCLE / p.spawnSpeed
        );

        // Render this pulse on all arms
        for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
            auto& arm = ctx.arms[a];

            // Get intensity for this arm (0-255: 0 = outside pulse, 255 = at center)
            uint8_t intensity = arcIntensityUnits(arm.angle, pulseAngle, PULSE_WIDTH_UNITS);

            if (intensity == 0) continue;

            // Apply fade to intensity
            intensity = scale8(intensity, fade);

            // Create color from hue
            CRGB16 color = CHSV(p.hue, 255, intensity);

            // Additive blend to all LEDs in arm (uniform radial fill)
            for (int led = 0; led < HardwareConfig::LEDS_PER_ARM; led++) {
                arm.pixels[led] += color;
            }
        }
    }
}
