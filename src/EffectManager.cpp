#include "EffectManager.h"

void EffectManager::begin() {
    prefs.begin("pov", false);  // Open NVS namespace "pov" in read-write mode

    // Load current effect from NVS, default to 0 if not found
    currentEffect = prefs.getUChar("effect", 0);

    // Validate effect index, default to 0 if invalid
    if (currentEffect >= NUM_EFFECTS) {
        currentEffect = 0;
    }
}

uint8_t EffectManager::getCurrentEffect() const {
    return currentEffect;
}

void EffectManager::saveNextEffect() {
    // Increment effect counter, wrap around at NUM_EFFECTS
    uint8_t nextEffect = (currentEffect + 1) % NUM_EFFECTS;

    // Persist to NVS
    prefs.putUChar("effect", nextEffect);
}
