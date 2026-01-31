#include "effects/Radar.h"
#include "effects/PhosphorPalettes.h"
#include <FastLED.h>
#include "polar_helpers.h"
#include <cmath>

// ============================================================
// Preset Configurations
// ============================================================

// Target speeds in units per second (wall-clock):
// - Display is 2 units wide (-1 to +1)
// - Aircraft: 2 units / 120 sec = 0.017 units/sec
// - Classic: 2 units / 300 sec = 0.007 units/sec
// - Marine: 2 units / 600 sec = 0.003 units/sec
// - Zombie: 1 unit / 45 sec = 0.022 units/sec (edge to center)

static const RadarPreset PRESETS[] = {
    // Aircraft: 6 sec sweep, ~2 min crossing, 3-6 targets, green
    { 6000000ULL, 0.017f, 3, 6, PhosphorType::P1_GREEN, "Aircraft" },
    // Classic: 10 sec sweep, ~5 min crossing, 5-8 targets, P7
    { 10000000ULL, 0.007f, 5, 8, PhosphorType::P7_BLUE_YELLOW, "Classic" },
    // Marine: 15 sec sweep, ~10 min crossing, 6-10 targets, orange
    { 15000000ULL, 0.003f, 6, 10, PhosphorType::P12_ORANGE, "Marine" },
    // Zombie: 5 sec sweep, ~45 sec to center, 15-25 targets, long orange
    { 5000000ULL, 0.022f, 15, 25, PhosphorType::P19_ORANGE_LONG, "Zombie" },
};

// ============================================================
// Initialization
// ============================================================

void Radar::begin() {
    lastRevolutionTime = 0;
    lastUpdateTime = 0;
    currentMicrosPerRev = 46000;  // ~1300 RPM default

    // Generate phosphor palettes using actual decay physics
    PhosphorPalettes::generateAll(blipPalettes, sweepPalettes);

    // Initialize all blips as inactive
    for (int i = 0; i < MAX_BLIPS; i++) {
        blips[i].active = false;
    }

    // Initialize world targets
    for (int i = 0; i < MAX_WORLD_TARGETS; i++) {
        worldTargets[i].active = false;
    }

    // Apply default preset (Classic)
    applyPreset();

    Serial.println("[Radar] Authentic PPI radar effect started");
}

// ============================================================
// Random Number Generation
// ============================================================

uint16_t Radar::nextRandom() {
    // XORshift16
    randomSeed ^= randomSeed << 7;
    randomSeed ^= randomSeed >> 9;
    randomSeed ^= randomSeed << 8;
    return randomSeed;
}

float Radar::randomFloat() {
    return static_cast<float>(nextRandom()) / 65535.0f;
}

// ============================================================
// World Target System
// ============================================================

void Radar::initWorldTarget(WorldTarget& target) {
    const RadarPreset& preset = PRESETS[static_cast<uint8_t>(currentMode)];
    float speed = preset.targetSpeed * (0.5f + randomFloat());  // 50%-150% of base speed
    float angle = randomFloat() * 2.0f * M_PI;

    if (currentMode == RadarMode::ZOMBIE) {
        // Zombie: spawn at edge, move toward center
        target.x = cosf(angle) * 0.95f;
        target.y = sinf(angle) * 0.95f;
        // Velocity toward center (inward)
        target.vx = -cosf(angle) * speed;
        target.vy = -sinf(angle) * speed;
    } else {
        // Normal: spawn anywhere, random direction
        float x, y;
        do {
            x = randomFloat() * 2.0f - 1.0f;  // -1 to 1
            y = randomFloat() * 2.0f - 1.0f;
        } while (x*x + y*y > 1.0f);  // Reject if outside unit circle

        target.x = x;
        target.y = y;
        target.vx = cosf(angle) * speed;
        target.vy = sinf(angle) * speed;
    }
    target.active = true;
}

bool Radar::worldToPolar(const WorldTarget& target, angle_t& bearing, uint8_t& range) const {
    // Compute range (distance from center)
    float r = sqrtf(target.x * target.x + target.y * target.y);
    if (r > 1.0f) {
        return false;  // Outside display bounds
    }

    // Compute bearing (angle from center)
    // atan2 returns -PI to PI, convert to 0-3600 angle units
    float angleRad = atan2f(target.y, target.x);
    if (angleRad < 0) {
        angleRad += 2.0f * M_PI;
    }
    bearing = static_cast<angle_t>(angleRad * ANGLE_FULL_CIRCLE / (2.0f * M_PI));

    // Convert range to virtual pixel (0 = center, TOTAL_LOGICAL_LEDS-1 = edge)
    range = static_cast<uint8_t>(r * (HardwareConfig::TOTAL_LOGICAL_LEDS - 1));

    return true;
}

bool Radar::sweepCrossedBearing(angle_t oldSweep, angle_t newSweep, angle_t targetBearing) const {
    // Handle wraparound case
    if (newSweep < oldSweep) {
        // Sweep crossed 0 degrees
        // Target is crossed if it's between oldSweep and 3600, OR between 0 and newSweep
        return (targetBearing >= oldSweep) || (targetBearing < newSweep);
    } else {
        // Normal case: target is crossed if between old and new
        return (targetBearing >= oldSweep) && (targetBearing < newSweep);
    }
}

Blip* Radar::findFreeBlip() {
    for (int i = 0; i < MAX_BLIPS; i++) {
        if (!blips[i].active) {
            return &blips[i];
        }
    }
    return nullptr;  // All slots full
}

// ============================================================
// Phosphor Color Lookup
// ============================================================

CRGB Radar::getPhosphorColor(timestamp_t ageUs, timestamp_t maxAgeUs, bool forSweep) const {
    if (ageUs >= maxAgeUs) {
        return CRGB::Black;
    }

    // Map age to 0-255 palette index
    // 0 = fresh (bright), 255 = fully decayed (black)
    uint8_t paletteIndex = static_cast<uint8_t>((ageUs * 255ULL) / maxAgeUs);

    // Select palette: sweep uses dimmer palette, blips use full brightness
    const CRGBPalette256& palette = forSweep
        ? sweepPalettes[static_cast<uint8_t>(currentPhosphorType)]
        : blipPalettes[static_cast<uint8_t>(currentPhosphorType)];

    // LINEARBLEND_NOWRAP prevents wrapping from index 255 back to 0
    // (default LINEARBLEND would blend black→white at end of palette)
    return ColorFromPalette(palette, paletteIndex, 255, LINEARBLEND_NOWRAP);
}

// ============================================================
// Revolution Handler
// ============================================================

void Radar::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    (void)revolutionCount;

    const RadarPreset& preset = PRESETS[static_cast<uint8_t>(currentMode)];

    // Update timing
    currentMicrosPerRev = usPerRev;

    // Calculate elapsed time since last revolution
    timestamp_t elapsed = timestamp - lastRevolutionTime;
    lastRevolutionTime = timestamp;

    // Compute sweep angles from wall-clock time for crossing detection
    angle_t oldSweepAngle = static_cast<angle_t>(
        ((timestamp - elapsed) % preset.sweepPeriodUs) * ANGLE_FULL_CIRCLE / preset.sweepPeriodUs
    );
    angle_t newSweepAngle = static_cast<angle_t>(
        (timestamp % preset.sweepPeriodUs) * ANGLE_FULL_CIRCLE / preset.sweepPeriodUs
    );

    // === Check for sweep crossing targets ===
    // (Target movement is now in render() using wall-clock time)
    for (int i = 0; i < targetCount; i++) {
        WorldTarget& target = worldTargets[i];
        if (!target.active) continue;

        angle_t bearing;
        uint8_t range;
        if (!worldToPolar(target, bearing, range)) continue;

        // Check if sweep crossed this target's bearing
        if (sweepCrossedBearing(oldSweepAngle, newSweepAngle, bearing)) {
            // Spawn a blip
            Blip* blip = findFreeBlip();
            if (blip) {
                blip->bearing = bearing;
                blip->virtualPixel = range;
                blip->createdAt = timestamp;
                blip->active = true;
            }
        }
    }

    // === Reap old blips ===
    for (int i = 0; i < MAX_BLIPS; i++) {
        if (blips[i].active) {
            timestamp_t age = timestamp - blips[i].createdAt;
            if (age > MAX_BLIP_LIFETIME_US) {
                blips[i].active = false;
            }
        }
    }
}

// ============================================================
// Render
// ============================================================

void IRAM_ATTR Radar::render(RenderContext& ctx) {
    timestamp_t now = ctx.timeUs;
    const RadarPreset& preset = PRESETS[static_cast<uint8_t>(currentMode)];

    // === Wall-clock target movement ===
    if (lastUpdateTime > 0) {
        timestamp_t deltaUs = now - lastUpdateTime;
        float deltaSec = deltaUs / 1000000.0f;

        // Move targets by wall-clock time (independent of disc RPM)
        for (int i = 0; i < targetCount; i++) {
            WorldTarget& target = worldTargets[i];
            if (!target.active) continue;

            target.x += target.vx * deltaSec;
            target.y += target.vy * deltaSec;

            // Check for respawn condition
            float r2 = target.x * target.x + target.y * target.y;
            if (currentMode == RadarMode::ZOMBIE) {
                // Zombie: respawn when reaching center
                if (r2 < 0.05f) {  // Within ~22% of center radius
                    initWorldTarget(target);
                }
            } else {
                // Normal: respawn when exiting edge
                if (r2 > 1.0f) {
                    initWorldTarget(target);
                }
            }
        }
    }
    lastUpdateTime = now;

    // Compute sweep angle from wall-clock time (perfectly smooth)
    angle_t sweepAngleUnits = static_cast<angle_t>(
        (now % preset.sweepPeriodUs) * ANGLE_FULL_CIRCLE / preset.sweepPeriodUs
    );

    // Render each arm
    for (int armIdx = 0; armIdx < HardwareConfig::NUM_ARMS; armIdx++) {
        auto& arm = ctx.arms[armIdx];
        angle_t armAngle = arm.angleUnits;

        for (int led = 0; led < HardwareConfig::ARM_LED_COUNT[armIdx]; led++) {
            uint8_t vPixel = armLedToVirtual(armIdx, led);

            // Start with black
            CRGB color = CRGB::Black;

            // === Layer 1: Phosphor sweep trail ===
            // Compute angular distance from sweep to this arm position
            // Trail is BEHIND the sweep (in the direction opposite to sweep rotation)
            int16_t angularDist = angularDistanceUnits(sweepAngleUnits, armAngle);

            // Convert to "distance behind sweep" (positive = behind)
            // Sweep rotates clockwise (increasing angle), so "behind" = lower angle
            int32_t distBehind = -angularDist;
            if (distBehind < 0) {
                distBehind += ANGLE_FULL_CIRCLE;
            }

            // Only render trail behind sweep (not ahead)
            if (distBehind > 0 && distBehind < ANGLE_FULL_CIRCLE) {
                // Convert angular distance to time since sweep passed
                // timeSinceSweep = (distBehind / ANGLE_FULL_CIRCLE) * sweepPeriodUs
                uint64_t timeSinceSweepUs = (static_cast<uint64_t>(distBehind) * preset.sweepPeriodUs) / ANGLE_FULL_CIRCLE;

                // Get phosphor color based on time since sweep (dim sweep palette)
                if (timeSinceSweepUs < SWEEP_DECAY_TIME_US) {
                    color = getPhosphorColor(timeSinceSweepUs, SWEEP_DECAY_TIME_US, true);
                }
            }

            // === Layer 2: Target blips (additive) ===
            for (int i = 0; i < MAX_BLIPS; i++) {
                const Blip& blip = blips[i];
                if (!blip.active) continue;

                // Check if this LED is at the blip's position
                // Blip occupies one virtual pixel at a specific bearing
                if (blip.virtualPixel != vPixel) continue;

                // Check angular proximity (within one slot width)
                angle_t dist = angularDistanceAbsUnits(armAngle, blip.bearing);
                if (dist > ctx.slotSizeUnits) continue;

                // Blip matches - compute its color based on age (bright blip palette)
                timestamp_t blipAge = now - blip.createdAt;
                if (blipAge < MAX_BLIP_LIFETIME_US) {
                    CRGB blipColor = getPhosphorColor(blipAge, MAX_BLIP_LIFETIME_US, false);
                    color += blipColor;  // Additive blend
                }
            }

            // No separate sweep line - the phosphor trail at age=0 (palette index 0) IS the sweep.
            // The leading edge of the phosphor trail is the brightest part, which is physically correct.

            arm.pixels[led] = color;
        }
    }
}

// ============================================================
// Button Handlers
// ============================================================

void Radar::nextMode() {
    // Cycle phosphor type forward
    uint8_t next = (static_cast<uint8_t>(currentPhosphorType) + 1) % static_cast<uint8_t>(PhosphorType::COUNT);
    currentPhosphorType = static_cast<PhosphorType>(next);

    const char* names[] = {"P7 (Blue-Yellow)", "P12 (Orange)", "P19 (Orange Long)", "P1 (Green)"};
    Serial.printf("[Radar] Phosphor: %s\n", names[next]);
}

void Radar::prevMode() {
    // Cycle phosphor type backward
    uint8_t prev = static_cast<uint8_t>(currentPhosphorType);
    prev = (prev == 0) ? (static_cast<uint8_t>(PhosphorType::COUNT) - 1) : (prev - 1);
    currentPhosphorType = static_cast<PhosphorType>(prev);

    const char* names[] = {"P7 (Blue-Yellow)", "P12 (Orange)", "P19 (Orange Long)", "P1 (Green)"};
    Serial.printf("[Radar] Phosphor: %s\n", names[prev]);
}

void Radar::paramUp() {
    // Cycle to next preset mode
    uint8_t next = (static_cast<uint8_t>(currentMode) + 1) % static_cast<uint8_t>(RadarMode::COUNT);
    currentMode = static_cast<RadarMode>(next);
    applyPreset();
}

void Radar::paramDown() {
    // Cycle to previous preset mode
    uint8_t prev = static_cast<uint8_t>(currentMode);
    prev = (prev == 0) ? (static_cast<uint8_t>(RadarMode::COUNT) - 1) : (prev - 1);
    currentMode = static_cast<RadarMode>(prev);
    applyPreset();
}

// ============================================================
// Preset Application
// ============================================================

void Radar::applyPreset() {
    const RadarPreset& preset = PRESETS[static_cast<uint8_t>(currentMode)];

    // Set phosphor type from preset
    currentPhosphorType = preset.phosphor;

    // Random target count within preset range
    targetCount = preset.minTargets + (nextRandom() % (preset.maxTargets - preset.minTargets + 1));

    // Reinitialize all targets with new velocities
    for (int i = 0; i < MAX_WORLD_TARGETS; i++) {
        if (i < targetCount) {
            initWorldTarget(worldTargets[i]);
            worldTargets[i].active = true;
        } else {
            worldTargets[i].active = false;
        }
    }

    // Clear all existing blips for clean transition
    for (int i = 0; i < MAX_BLIPS; i++) {
        blips[i].active = false;
    }

    Serial.printf("[Radar] Mode: %s (sweep %.1fs, %d targets)\n",
        preset.name, preset.sweepPeriodUs / 1000000.0f, targetCount);
}
