#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <cstdint>

namespace HardwareConfig {
    constexpr uint16_t LEDS_PER_ARM = 10;
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LEDS = 30;

    constexpr uint16_t INNER_ARM_START = 10;
    constexpr uint16_t MIDDLE_ARM_START = 0;
    constexpr uint16_t OUTER_ARM_START = 20;
}

#endif
