#ifndef SLOT_TIMING_H
#define SLOT_TIMING_H

#include "geometry.h"
#include "RevolutionTimer.h"
#include "RenderContext.h"
#include "hardware_config.h"
#include "esp_timer.h"
#include <NeoPixelBus.h>

// Forward declare (defined in main.cpp)
class EffectManager;
extern EffectManager effectManager;

// Perceptual brightness mapping (gamma 2.2)
// Input: 0-10, Output: 0-255 for nscale8()
// Human vision perceives brightness logarithmically, so linear 50% feels
// much brighter than halfway. Gamma correction compensates for this.
inline uint8_t brightnessToScale(uint8_t brightness) {
    if (brightness == 0) return 0;
    if (brightness >= 10) return 255;
    // gamma 2.2: output = 255 * (input/10)^2.2
    float normalized = brightness / 10.0f;
    return static_cast<uint8_t>(255.0f * powf(normalized, 2.2f));
}

/**
 * SlotTiming - Precision timing helpers for POV display
 *
 * These functions implement the "render-ahead + busy-wait" timing model.
 * We render for a FUTURE angular position, then wait until the disc
 * reaches that position before firing Show(). This ensures the angle
 * told to the renderer matches where LEDs actually illuminate.
 */

/**
 * Calculate the next slot to render for precision timing
 *
 * @param lastRenderedSlot Previous slot that was rendered (-1 on first call)
 * @param timing Current timing snapshot from RevolutionTimer
 * @return SlotTarget with target slot, angle, and time
 */
inline SlotTarget calculateNextSlot(int lastRenderedSlot, const TimingSnapshot& timing) {
    SlotTarget target;

    // Calculate slot geometry from adaptive angular resolution
    target.slotSize = static_cast<angle_t>(timing.angularResolution * 10.0f);
    if (target.slotSize == 0) target.slotSize = 30;  // 3° default
    target.totalSlots = 3600 / target.slotSize;

    // Calculate next slot (always advance from last rendered)
    target.slotNumber = (lastRenderedSlot + 1) % target.totalSlots;
    target.angleUnits = static_cast<angle_t>(target.slotNumber * target.slotSize);

    // Calculate target time: when will disc be at targetAngle?
    // Formula: lastHallTime + (targetAngle / 360°) * microsecondsPerRev
    interval_t microsecondsPerRev = timing.lastActualInterval;
    if (microsecondsPerRev == 0) microsecondsPerRev = timing.microsecondsPerRev;

    target.targetTime = timing.lastTimestamp +
        (static_cast<uint64_t>(target.angleUnits) * microsecondsPerRev / 3600);

    // Handle revolution wrap: if target time is in the past by more than
    // half a revolution, it must be referring to the NEXT revolution
    timestamp_t now = esp_timer_get_time();
    if (target.targetTime < now && (now - target.targetTime) > microsecondsPerRev / 2) {
        target.targetTime += microsecondsPerRev;
    }

    return target;
}

/**
 * Busy-wait until the precise target time
 *
 * @param targetTime Timestamp to wait for (microseconds)
 */
inline void waitForTargetTime(timestamp_t targetTime) {
    while (esp_timer_get_time() < targetTime) {
        // Busy wait - CPU has nothing else to do
        // The ESP32-S3 has no other work during this wait
    }
}

/**
 * Copy rendered pixels from RenderContext to the LED strip
 *
 * Applies runtime brightness from DisplayState (0-10 scale, gamma-corrected to 0-255)
 * Handles per-arm LED counts (arm[0] has 14, others have 13) and level shifter at index 0.
 *
 * @param ctx RenderContext containing rendered arm pixel data
 * @param ledStrip NeoPixelBus strip to copy pixels to
 */
template<typename T_STRIP>
void copyPixelsToStrip(const RenderContext& ctx, T_STRIP& ledStrip) {
    // Level shifter at physical index 0 - always black
    ledStrip.SetPixelColor(0, RgbColor(0, 0, 0));

    // Get runtime brightness from EffectManager
    uint8_t brightness = effectManager.getBrightness();
    uint8_t scale = brightnessToScale(brightness);  // Gamma-corrected 0-255

    for (int a = 0; a < 3; a++) {
        uint16_t start = HardwareConfig::ARM_START[a];
        uint16_t count = HardwareConfig::ARM_LED_COUNT[a];  // Use per-arm count
        bool reversed = HardwareConfig::ARM_LED_REVERSED[a];

        for (int p = 0; p < count; p++) {  // Only copy valid LEDs
            int physicalPos = reversed ? (count - 1 - p) : p;

            CRGB color = ctx.arms[a].pixels[p];
            if (scale < 255) {
                color.nscale8(scale);
            }
            ledStrip.SetPixelColor(start + physicalPos, RgbColor(color.r, color.g, color.b));
        }
    }
}

/**
 * Handle the not-rotating state (clear LEDs, delay)
 */
template<typename T_STRIP>
void handleNotRotating(T_STRIP& ledStrip) {
    ledStrip.ClearTo(RgbColor(0, 0, 0));
    ledStrip.Show();
    delay(10);
}

#endif // SLOT_TIMING_H
