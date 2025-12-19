#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <cstdint>

namespace HardwareConfig {
    // LED configuration
    constexpr uint16_t LEDS_PER_ARM = 11;
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LEDS = 33;

    // Hardware pin assignments (Seeed XIAO ESP32S3)
    constexpr uint8_t HALL_PIN = D7;       // Brown wire - Hall effect sensor
    constexpr uint8_t SPI_DATA_PIN = D10;  // Blue wire - SK9822 Data (MOSI)
    constexpr uint8_t SPI_CLK_PIN = D8;    // Purple wire - SK9822 Clock (SCK)

    // Physical arm layout (indexed by physical position, not LED address)
    // arm[0] = Outer (furthest from center, +240° from hall sensor)
    // arm[1] = Middle (between outer and inside, 0° hall sensor reference)
    // arm[2] = Inside (closest to center, +120° from hall sensor)

    constexpr uint16_t OUTER_ARM_START = 0;    // arm[0]: LEDs 0-10
    constexpr uint16_t MIDDLE_ARM_START = 22;  // arm[1]: LEDs 22-32 (hall sensor)
    constexpr uint16_t INSIDE_ARM_START = 11;  // arm[2]: LEDs 11-21

    // Physical LED start positions indexed by arm (lookup table)
    constexpr uint16_t ARM_START[3] = {
        OUTER_ARM_START,   // arm[0]
        MIDDLE_ARM_START,  // arm[1]
        INSIDE_ARM_START   // arm[2]
    };
}

#endif
