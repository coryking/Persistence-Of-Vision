#ifndef EFFECT_REGISTRY_H
#define EFFECT_REGISTRY_H

#include "Effect.h"
#include <FastLED.h>  // For random8()

/**
 * Manages effect lifecycle and switching
 *
 * Effects are registered at compile time. The registry handles
 * begin()/end() calls when switching between effects.
 *
 * Usage:
 *   EffectRegistry registry;
 *   registry.registerEffect(&myEffect1);
 *   registry.registerEffect(&myEffect2);
 *   registry.begin();  // Calls myEffect1.begin()
 *
 *   // In loop:
 *   registry.current()->render(ctx);
 *
 *   // To switch:
 *   registry.next();  // Calls myEffect1.end(), myEffect2.begin()
 */
class EffectRegistry {
public:
    static constexpr uint8_t MAX_EFFECTS = 8;

private:
    Effect* effects[MAX_EFFECTS] = {};
    uint8_t effectCount = 0;
    uint8_t currentIndex = 0;

public:
    /**
     * Register an effect (call during setup)
     *
     * @param effect Pointer to effect instance (must outlive registry)
     * @return Index of registered effect, or 255 if registry is full
     */
    uint8_t registerEffect(Effect* effect) {
        if (effectCount >= MAX_EFFECTS) return 255;
        effects[effectCount] = effect;
        return effectCount++;
    }

    /**
     * Initialize the registry and start first effect
     * Call after all effects are registered
     */
    void begin() {
        if (effectCount > 0 && effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Get currently active effect
     * @return Pointer to current effect, or nullptr if none registered
     */
    Effect* current() {
        return (currentIndex < effectCount) ? effects[currentIndex] : nullptr;
    }

    /**
     * Switch to next effect (wraps around)
     * Calls end() on current, begin() on next
     */
    void next() {
        if (effectCount == 0) return;

        if (effects[currentIndex]) {
            effects[currentIndex]->end();
        }

        currentIndex = (currentIndex + 1) % effectCount;

        if (effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Switch to previous effect (wraps around)
     */
    void previous() {
        if (effectCount == 0) return;

        if (effects[currentIndex]) {
            effects[currentIndex]->end();
        }

        currentIndex = (currentIndex == 0) ? effectCount - 1 : currentIndex - 1;

        if (effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Switch to specific effect by index
     * No-op if index invalid or already current
     */
    void setEffect(uint8_t index) {
        if (index >= effectCount) return;
        if (index == currentIndex) return;

        if (effects[currentIndex]) {
            effects[currentIndex]->end();
        }

        currentIndex = index;

        if (effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Get current effect index
     */
    uint8_t getCurrentIndex() const { return currentIndex; }

    /**
     * Get total number of registered effects
     */
    uint8_t getEffectCount() const { return effectCount; }

    // ========== Speed-Aware Effect Selection ==========

    /**
     * Get count of effects valid for given speed
     */
    uint8_t getValidEffectCount(interval_t microsPerRev) const {
        uint8_t count = 0;
        for (uint8_t i = 0; i < effectCount; i++) {
            if (effects[i] && effects[i]->getSpeedRange().contains(microsPerRev)) {
                count++;
            }
        }
        return count;
    }

    /**
     * Check if current effect is valid for given speed
     */
    bool isCurrentValidForSpeed(interval_t microsPerRev) const {
        if (currentIndex >= effectCount || !effects[currentIndex]) return false;
        return effects[currentIndex]->getSpeedRange().contains(microsPerRev);
    }

    /**
     * Set a random effect valid for the given speed
     * Falls back to any effect if none match the speed range
     */
    void setRandomValidEffect(interval_t microsPerRev) {
        // Build list of valid indices
        uint8_t validIndices[MAX_EFFECTS];
        uint8_t validCount = 0;

        for (uint8_t i = 0; i < effectCount; i++) {
            if (effects[i] && effects[i]->getSpeedRange().contains(microsPerRev)) {
                validIndices[validCount++] = i;
            }
        }

        if (validCount == 0) {
            // Fallback: pick any effect
            if (effectCount > 0) {
                setEffect(random8(effectCount));
            }
        } else if (validCount == 1) {
            // Only one valid - use it
            setEffect(validIndices[0]);
        } else {
            // Pick random from valid set
            setEffect(validIndices[random8(validCount)]);
        }
    }

    /**
     * Notify current effect of revolution boundary
     * Call from hall sensor handler
     *
     * @param rpm Current revolutions per minute
     */
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
        if (currentIndex < effectCount && effects[currentIndex]) {
            effects[currentIndex]->onRevolution(usPerRev, timestamp, revolutionCount);
        }
    }
};

#endif // EFFECT_REGISTRY_H
