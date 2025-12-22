#ifndef EFFECT_SCHEDULER_H
#define EFFECT_SCHEDULER_H

#include <Preferences.h>
#include "EffectRegistry.h"
#include "types.h"
#include "esp_timer.h"

// Shuffle interval: 20 seconds in microseconds
static constexpr interval_t SHUFFLE_INTERVAL_US = 20000000;

/**
 * EffectScheduler - Manages effect persistence and speed-aware shuffling
 *
 * Behavior:
 * - Boot: Load from NVS, pick random valid effect for current speed
 * - Speed mode change: Switch to valid effect if current isn't valid, reset timer
 * - Every 20 seconds: Shuffle to random effect valid for current speed
 *
 * NOT responsible for: Managing effects (that's EffectRegistry), periodic timing
 */
class EffectScheduler {
public:
    /**
     * Initialize scheduler at boot
     * Loads saved effect from NVS, starts registry
     *
     * @param registry Effect registry to control
     */
    void begin(EffectRegistry* registry) {
        this->registry = registry;

        // Open NVS
        prefs.begin("pov", false);  // R/W mode

        // Load last saved effect
        uint8_t savedIndex = prefs.getUChar("effect", 0);

        // Validate and set to saved effect
        if (savedIndex < registry->getEffectCount()) {
            registry->setEffect(savedIndex);
        }

        // Start registry (calls begin() on current effect)
        registry->begin();

        // Initialize shuffle timer
        lastShuffleTimeUs = esp_timer_get_time();
    }

    /**
     * Called when speed mode changes (crosses 200 RPM threshold)
     * Switches to a valid effect for the new speed if current isn't valid
     *
     * @param microsPerRev Current speed in microseconds per revolution
     * @param isNowSlow True if entering slow mode, false if entering motor mode
     */
    void onSpeedModeChange(interval_t microsPerRev, bool isNowSlow) {
        if (!registry) return;

        inSlowMode = isNowSlow;
        currentSpeed = microsPerRev;
        lastShuffleTimeUs = esp_timer_get_time();

        // If current effect isn't valid for new speed, switch to a valid one
        if (!registry->isCurrentValidForSpeed(microsPerRev)) {
            registry->setRandomValidEffect(microsPerRev);
            prefs.putUChar("effect", registry->getCurrentIndex());
        }
    }

    /**
     * Update shuffle timer - call on each revolution
     * Shuffles to random valid effect every 20 seconds
     *
     * @param currentTimeUs Current timestamp from esp_timer_get_time()
     * @param microsPerRev Current speed in microseconds per revolution
     */
    void updateShuffle(timestamp_t currentTimeUs, interval_t microsPerRev) {
        if (!registry) return;

        currentSpeed = microsPerRev;

        // Check if 20 seconds have elapsed
        if (currentTimeUs - lastShuffleTimeUs >= SHUFFLE_INTERVAL_US) {
            registry->setRandomValidEffect(microsPerRev);
            prefs.putUChar("effect", registry->getCurrentIndex());
            lastShuffleTimeUs = currentTimeUs;
        }
    }

    /**
     * Check if scheduler is in slow mode
     */
    bool isInSlowMode() const { return inSlowMode; }

    /**
     * Get current speed being tracked
     */
    interval_t getCurrentSpeed() const { return currentSpeed; }

private:
    EffectRegistry* registry = nullptr;
    Preferences prefs;

    // Speed mode state
    bool inSlowMode = false;
    timestamp_t lastShuffleTimeUs = 0;
    interval_t currentSpeed = 0;
};

#endif // EFFECT_SCHEDULER_H
