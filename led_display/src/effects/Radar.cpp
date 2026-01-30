#include "effects/Radar.h"
#include <FastLED.h>
#include "polar_helpers.h"

void Radar::begin() {
    // Initialize all targets as inactive
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        targets[i].active = false;
        targets[i].strength = 0;
        targets[i].angularVelocity = 0;
        targets[i].radialVelocity = 0;
    }
    nextTargetSlot = 0;
    revolutionsSinceSpawn = 0;
    sweepStartTime = 0;

    Serial.println("[Radar] Effect started - density=2");
}

angle_t Radar::getSweepAngle(timestamp_t now) const {
    // Calculate elapsed time since sweep started
    timestamp_t elapsed = now - sweepStartTime;

    // Convert to position within current sweep period
    uint32_t positionInPeriod = elapsed % SWEEP_PERIOD_US;

    // Map to angle units (0-3599)
    // Note: sweep rotates in same direction as disc (clockwise when viewed from above)
    return static_cast<angle_t>((static_cast<uint64_t>(positionInPeriod) * ANGLE_FULL_CIRCLE) / SWEEP_PERIOD_US);
}

uint32_t Radar::nextRandom() {
    // Simple LCG: next = (a * seed + c) mod m
    // Using parameters from Numerical Recipes
    randomSeed = randomSeed * 1664525 + 1013904223;
    return randomSeed;
}

void Radar::spawnTarget(timestamp_t now) {
    // Find oldest target slot to replace
    RadarTarget& target = targets[nextTargetSlot];

    // Generate random position
    target.angle = static_cast<angle_t>(nextRandom() % ANGLE_FULL_CIRCLE);
    target.vPixel = static_cast<uint8_t>(nextRandom() % HardwareConfig::TOTAL_LOGICAL_LEDS);

    // Skip if on range ring (looks weird)
    if (isRangeRing(target.vPixel)) {
        target.vPixel = (target.vPixel + 3) % HardwareConfig::TOTAL_LOGICAL_LEDS;
    }

    // Generate random velocity (drift direction and speed)
    int32_t angVelRange = MAX_ANGULAR_VELOCITY - MIN_ANGULAR_VELOCITY;
    target.angularVelocity = static_cast<int16_t>(MIN_ANGULAR_VELOCITY +
        (nextRandom() % angVelRange));

    int32_t radVelRange = MAX_RADIAL_VELOCITY - MIN_RADIAL_VELOCITY;
    target.radialVelocity = static_cast<int8_t>(MIN_RADIAL_VELOCITY +
        (nextRandom() % radVelRange));

    target.lastHitTime = now;
    target.lastMoveTime = now;
    target.strength = 0;  // Will bloom when sweep first passes
    target.active = true;

    nextTargetSlot = (nextTargetSlot + 1) % MAX_TARGETS;
}

void Radar::updateTargets(angle_t sweepAngle, timestamp_t now) {
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        RadarTarget& target = targets[i];
        if (!target.active) continue;

        // Check if sweep is passing over this target
        angle_t dist = angularDistanceAbsUnits(sweepAngle, target.angle);
        if (dist <= BLOOM_WINDOW) {
            // Bloom! Target was just hit by sweep
            target.strength = 255;
            target.lastHitTime = now;
        } else {
            // Decay based on time since last hit
            timestamp_t elapsed = now - target.lastHitTime;
            if (elapsed >= TARGET_DECAY_US) {
                target.strength = 0;
            } else {
                // Exponential-ish decay: strength = 255 * (1 - elapsed/decay)^2
                uint32_t ratio = (TARGET_DECAY_US - elapsed) * 256 / TARGET_DECAY_US;
                target.strength = static_cast<uint8_t>((ratio * ratio) >> 8);
            }
        }
    }
}

void Radar::moveTargets(timestamp_t now) {
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        RadarTarget& target = targets[i];
        if (!target.active) continue;

        // Calculate elapsed time since last move (in microseconds)
        timestamp_t elapsed = now - target.lastMoveTime;
        if (elapsed < 100000) continue;  // Update every 100ms for smoother movement

        target.lastMoveTime = now;

        // Update angular position
        // angularVelocity is in angle units per second
        int32_t angularDelta = (static_cast<int32_t>(target.angularVelocity) * elapsed) / 1000000;
        int32_t newAngle = static_cast<int32_t>(target.angle) + angularDelta;

        // Wrap angle to 0-3599
        while (newAngle < 0) newAngle += ANGLE_FULL_CIRCLE;
        while (newAngle >= ANGLE_FULL_CIRCLE) newAngle -= ANGLE_FULL_CIRCLE;
        target.angle = static_cast<angle_t>(newAngle);

        // Update radial position
        // radialVelocity is in virtual pixels per 10 seconds
        int32_t radialDelta = (static_cast<int32_t>(target.radialVelocity) * elapsed) / 10000000;
        int32_t newPixel = static_cast<int32_t>(target.vPixel) + radialDelta;

        // Bounce off edges
        if (newPixel < 2) {
            newPixel = 2;
            target.radialVelocity = -target.radialVelocity;  // Reverse direction
        } else if (newPixel >= static_cast<int32_t>(HardwareConfig::TOTAL_LOGICAL_LEDS - 1)) {
            newPixel = HardwareConfig::TOTAL_LOGICAL_LEDS - 2;
            target.radialVelocity = -target.radialVelocity;  // Reverse direction
        }
        target.vPixel = static_cast<uint8_t>(newPixel);
    }
}

bool Radar::isRangeRing(uint8_t vPixel) const {
    // Range rings at 1/4, 1/2, 3/4 of total virtual pixels
    uint8_t ring1 = HardwareConfig::TOTAL_LOGICAL_LEDS / 4;
    uint8_t ring2 = HardwareConfig::TOTAL_LOGICAL_LEDS / 2;
    uint8_t ring3 = (HardwareConfig::TOTAL_LOGICAL_LEDS * 3) / 4;

    return (vPixel == ring1 || vPixel == ring2 || vPixel == ring3);
}

void IRAM_ATTR Radar::render(RenderContext& ctx) {
    timestamp_t now = ctx.timeUs;

    // Initialize sweep start time on first render
    if (sweepStartTime == 0) {
        sweepStartTime = now;
    }

    angle_t sweepAngle = getSweepAngle(now);

    // Update target positions and states
    moveTargets(now);
    updateTargets(sweepAngle, now);

    // Render each arm
    for (int armIdx = 0; armIdx < HardwareConfig::NUM_ARMS; armIdx++) {
        auto& arm = ctx.arms[armIdx];
        angle_t armAngle = arm.angleUnits;

        for (int led = 0; led < HardwareConfig::ARM_LED_COUNT[armIdx]; led++) {
            uint8_t vPixel = armLedToVirtual(armIdx, led);

            // Start with black
            CRGB color = CRGB::Black;

            // === Layer 1: Range rings (dimmest, rendered first) ===
            if (isRangeRing(vPixel)) {
                color = RANGE_RING_COLOR;
            }

            // === Layer 2: Targets (additive blend) ===
            for (uint8_t t = 0; t < MAX_TARGETS; t++) {
                const RadarTarget& target = targets[t];
                if (!target.active || target.strength == 0) continue;

                // Check if this pixel matches target's radial position
                if (target.vPixel == vPixel) {
                    // Check if arm is at target's angular position (with some tolerance)
                    angle_t angDist = angularDistanceAbsUnits(armAngle, target.angle);
                    if (angDist <= ANGLE_UNITS(3)) {  // 3 degree tolerance for arm position
                        // Add target color scaled by strength (red/orange accent)
                        CRGB targetColor = TARGET_COLOR;
                        targetColor.nscale8(target.strength);
                        color += targetColor;
                    }
                }
            }

            // === Layer 3: Phosphor trail (additive blend) ===
            // Trail is BEHIND the sweep (in the direction opposite to sweep rotation)
            int16_t trailDist = angularDistanceUnits(sweepAngle, armAngle);

            // Trail is behind sweep, so we want negative distances (arm behind sweep)
            // Convert to positive "distance behind sweep"
            int16_t distBehind = -trailDist;
            if (distBehind < 0) {
                distBehind += ANGLE_FULL_CIRCLE;
            }

            if (distBehind > 0 && distBehind <= static_cast<int16_t>(TRAIL_WIDTH)) {
                // Exponential decay: intensity = (1 - dist/width)^2
                uint32_t ratio = (TRAIL_WIDTH - distBehind) * 256 / TRAIL_WIDTH;
                uint8_t intensity = static_cast<uint8_t>((ratio * ratio) >> 8);

                // Blend between dim and bright phosphor
                CRGB phosphor = blend(PHOSPHOR_DIM, PHOSPHOR_BRIGHT, intensity);
                phosphor.nscale8(intensity);
                color += phosphor;
            }

            // === Layer 4: Sweep beam (brightest, on top) ===
            angle_t sweepDist = angularDistanceAbsUnits(armAngle, sweepAngle);
            if (sweepDist <= ctx.slotSizeUnits) {
                // Sweep beam is exactly 1 slot wide (thinnest possible)
                color = SWEEP_COLOR;
            }

            arm.pixels[led] = color;
        }
    }
}

void Radar::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    (void)usPerRev;

    // Spawn targets based on density setting
    if (densityMode > 0) {
        revolutionsSinceSpawn++;
        if (revolutionsSinceSpawn >= SPAWN_INTERVAL_REVS[densityMode]) {
            spawnTarget(timestamp);
            revolutionsSinceSpawn = 0;
        }
    }
}

void Radar::nextMode() {
    // Sweep speed locked to slow - left/right controls disabled
}

void Radar::prevMode() {
    // Sweep speed locked to slow - left/right controls disabled
}

void Radar::paramUp() {
    if (densityMode < DENSITY_MODE_COUNT - 1) {
        densityMode++;
    }
    const char* densityNames[] = {"None", "Sparse", "Medium", "Busy"};
    Serial.printf("[Radar] Target density -> %s\n", densityNames[densityMode]);
}

void Radar::paramDown() {
    if (densityMode > 0) {
        densityMode--;
    }
    const char* densityNames[] = {"None", "Sparse", "Medium", "Busy"};
    Serial.printf("[Radar] Target density -> %s\n", densityNames[densityMode]);
}
