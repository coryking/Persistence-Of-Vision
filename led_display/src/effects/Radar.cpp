#include "effects/Radar.h"
#include "effects/PhosphorPalettes.h"
#include <FastLED.h>
#include "polar_helpers.h"
#include <cmath>

// ============================================================
// Initialization
// ============================================================

void Radar::begin() {
    lastRevolutionTime = 0;
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

    // Spawn initial targets
    for (int i = 0; i < targetCount; i++) {
        initWorldTarget(worldTargets[i]);
        worldTargets[i].active = true;
    }

    Serial.println("[Radar] Authentic PPI radar effect started");
    Serial.printf("[Radar] Phosphor: P7, Targets: %d\n", targetCount);
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
    // Spawn at random position within unit circle
    // Use rejection sampling for uniform distribution
    float x, y;
    do {
        x = randomFloat() * 2.0f - 1.0f;  // -1 to 1
        y = randomFloat() * 2.0f - 1.0f;
    } while (x*x + y*y > 1.0f);  // Reject if outside unit circle

    target.x = x;
    target.y = y;

    // Random velocity (slow drift)
    // Velocity per sweep = position change per ~6 seconds
    float speed = 0.02f + randomFloat() * 0.03f;  // 0.02 to 0.05 units per sweep
    float angle = randomFloat() * 2.0f * M_PI;
    target.vx = cosf(angle) * speed;
    target.vy = sinf(angle) * speed;
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

    // Update timing
    currentMicrosPerRev = usPerRev;

    // Calculate elapsed time since last revolution
    timestamp_t elapsed = timestamp - lastRevolutionTime;
    lastRevolutionTime = timestamp;

    // Compute sweep angles from wall-clock time for crossing detection
    angle_t oldSweepAngle = static_cast<angle_t>(
        ((timestamp - elapsed) % SWEEP_PERIOD_US) * ANGLE_FULL_CIRCLE / SWEEP_PERIOD_US
    );
    angle_t newSweepAngle = static_cast<angle_t>(
        (timestamp % SWEEP_PERIOD_US) * ANGLE_FULL_CIRCLE / SWEEP_PERIOD_US
    );

    // === Update world targets ===
    for (int i = 0; i < targetCount; i++) {
        WorldTarget& target = worldTargets[i];
        if (!target.active) continue;

        // Move target
        target.x += target.vx;
        target.y += target.vy;

        // Check if target exited unit circle
        float r2 = target.x * target.x + target.y * target.y;
        if (r2 > 1.0f) {
            // Respawn at random edge position
            initWorldTarget(target);
        }
    }

    // === Check for sweep crossing targets ===
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

    // Compute sweep angle from wall-clock time (perfectly smooth)
    angle_t sweepAngleUnits = static_cast<angle_t>(
        (now % SWEEP_PERIOD_US) * ANGLE_FULL_CIRCLE / SWEEP_PERIOD_US
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
                // timeSinceSweep = (distBehind / ANGLE_FULL_CIRCLE) * SWEEP_PERIOD_US
                uint64_t timeSinceSweepUs = (static_cast<uint64_t>(distBehind) * SWEEP_PERIOD_US) / ANGLE_FULL_CIRCLE;

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
    // Increase target count
    if (targetCount < MAX_WORLD_TARGETS) {
        targetCount++;
        // Activate the new target
        initWorldTarget(worldTargets[targetCount - 1]);
        worldTargets[targetCount - 1].active = true;
        Serial.printf("[Radar] Targets: %d\n", targetCount);
    }
}

void Radar::paramDown() {
    // Decrease target count
    if (targetCount > 0) {
        // Deactivate the last target
        worldTargets[targetCount - 1].active = false;
        targetCount--;
        Serial.printf("[Radar] Targets: %d\n", targetCount);
    }
}
