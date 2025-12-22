#ifndef EFFECT_SCHEDULER_H
#define EFFECT_SCHEDULER_H

#include <Preferences.h>
#include "EffectRegistry.h"

/**
 * EffectScheduler - Manages effect NVS persistence and motor-triggered switching
 *
 * Restores original EffectManager behavior:
 * - Boot: Load from NVS, advance effect, save, display
 * - Motor start: Advance effect, save to NVS
 *
 * NOT responsible for: Managing effects (that's EffectRegistry), periodic timing
 */
class EffectScheduler {
public:
    /**
     * Initialize scheduler at boot
     * Loads saved effect from NVS, advances to next, saves, starts registry
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

        // Advance to next effect (boot behavior)
        registry->next();

        // Save new effect to NVS
        prefs.putUChar("effect", registry->getCurrentIndex());

        // Start registry (calls begin() on current effect)
        registry->begin();
    }

    /**
     * Handle motor start event
     * Call from hallProcessingTask when rotation starts
     *
     * Advances to next effect and saves to NVS
     */
    void onMotorStart() {
        if (registry && registry->getEffectCount() > 1) {
            registry->next();
            prefs.putUChar("effect", registry->getCurrentIndex());
        }
    }

private:
    EffectRegistry* registry = nullptr;
    Preferences prefs;
};

#endif // EFFECT_SCHEDULER_H
