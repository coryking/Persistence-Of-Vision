#ifndef EFFECT_REGISTRY_H
#define EFFECT_REGISTRY_H

#include "Effect.h"

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
