#ifndef EFFECT_MANAGER_H
#define EFFECT_MANAGER_H

#include <Preferences.h>

/**
 * @brief Manages effect selection and persistence across power cycles
 *
 * Stores the current effect index in NVS (non-volatile storage) and handles
 * cycling through effects. Each call to saveNextEffect() increments the effect
 * counter and persists it for the next boot or motor restart.
 */
class EffectManager {
public:
    /**
     * @brief Initialize the effect manager and load the current effect from NVS
     *
     * Should be called once during setup(). Loads the current effect from NVS,
     * defaulting to 0 if not found or invalid.
     */
    void begin();

    /**
     * @brief Get the currently active effect index
     *
     * @return Effect index (0-3):
     *   0 = Per-Arm Blobs
     *   1 = Virtual Display Blobs
     *   2 = Solid Arms Diagnostic
     *   3 = RPM Arc
     */
    uint8_t getCurrentEffect() const;

    /**
     * @brief Set the currently active effect (for testing/profiling)
     *
     * @param effect Effect index to set (0-3)
     */
    void setCurrentEffect(uint8_t effect);

    /**
     * @brief Increment to next effect and save to NVS for next power cycle
     *
     * Increments the effect counter (wrapping from 3 back to 0) and persists
     * the new value to NVS. This does NOT change the currently running effect;
     * it only sets which effect will run on the next boot or motor restart.
     */
    void saveNextEffect();

    static constexpr uint8_t NUM_EFFECTS = 4;

private:
    Preferences prefs;
    uint8_t currentEffect;
};

#endif // EFFECT_MANAGER_H
