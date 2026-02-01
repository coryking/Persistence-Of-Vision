#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Hall sensor simulation for desktop/bench testing.
 *
 * When TEST_MODE is defined, provides timer-based hall event simulation.
 * When disabled, all functions return nullptr/false and compile to nothing.
 */
namespace HallSimulator {

// Configuration defaults
constexpr float DEFAULT_TEST_RPM = 1600.0f;  // Match typical operating speed
constexpr bool DEFAULT_VARY_RPM = false;

/**
 * Compile-time check if simulation is active
 */
constexpr bool isActive() {
#ifdef TEST_MODE
    return true;
#else
    return false;
#endif
}

/**
 * Initialize hall sensor simulation.
 *
 * @param targetRpm Target RPM for simulation (default 360)
 * @param enableVariableRpm If true, RPM varies sinusoidally (default false)
 * @return Event queue handle if TEST_MODE, nullptr otherwise
 */
QueueHandle_t begin(float targetRpm = DEFAULT_TEST_RPM,
                    bool enableVariableRpm = DEFAULT_VARY_RPM);

/**
 * Get the simulation event queue.
 * @return Queue handle if TEST_MODE and initialized, nullptr otherwise
 */
QueueHandle_t getEventQueue();

} // namespace HallSimulator
