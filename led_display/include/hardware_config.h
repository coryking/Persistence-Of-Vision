#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>
#include <cstdint>

namespace HardwareConfig {
    // LED configuration
    constexpr uint16_t LEDS_PER_ARM = 11;
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LEDS = 33;
    constexpr uint8_t GLOBAL_BRIGHTNESS = 255;

    // IMPORTANT: NEVER hardcode loop limits in effects!
    // ❌ BAD:  for (int p = 0; p < 10; p++)
    // ✅ GOOD: for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++)

    // Hardware pin assignments (Seeed XIAO ESP32S3)
    // See docs/led_display/HARDWARE.md for physical hardware details
    constexpr uint8_t HALL_PIN = D5;       // Brown wire - Hall effect sensor
    constexpr uint8_t SPI_DATA_PIN = D10;  // Blue wire - SK9822 Data (MOSI)
    constexpr uint8_t SPI_CLK_PIN = D8;    // Purple wire - SK9822 Clock (SCK)

    // ADXL345 Accelerometer (I2C mode - CS and SDO set HIGH in software)
    // Wire colors: SCL=Brown, SDA=Orange, SDO=Purple, INT1=Green, CS=Blue
    constexpr uint8_t ACCEL_SCL_PIN = D9;   // Brown wire - I2C Clock
    constexpr uint8_t ACCEL_SDA_PIN = D2;   // Orange wire - I2C Data
    constexpr uint8_t ACCEL_SDO_PIN = D3;   // Purple wire - Set HIGH for addr 0x1D
    constexpr uint8_t ACCEL_CS_PIN = D1;    // Blue wire - Set HIGH to enable I2C mode
    constexpr uint8_t ACCEL_INT1_PIN = D0;  // Green wire - Interrupt 1 (unused)

    // Physical arm layout (indexed by logical position)
    // Logical arm[0] = Outer (furthest from center, +240° from hall sensor)
    // Logical arm[1] = Middle (between outer and inside, 0° hall sensor reference)
    // Logical arm[2] = Inside (closest to center, +120° from hall sensor)
    //
    // LED wiring:
    // - arm[0] (outer): REVERSED (LED0 at tip, LED10 at hub)
    // - arm[1] (middle): Normal (LED0 at hub, LED10 at tip)
    // - arm[2] (inside): Normal (LED0 at hub, LED10 at tip)
    //
    // Physical LED addressing (verified with led_display_test):
    // - Physical LEDs 0-10   = Middle arm (arm[1]) - Normal ordering
    // - Physical LEDs 11-21  = Inside arm (arm[2]) - Normal ordering
    // - Physical LEDs 22-32  = Outer arm (arm[0])  - REVERSED ordering

    constexpr uint16_t MIDDLE_ARM_START = 0;                              // arm[1]: First segment (hall sensor reference)
    constexpr uint16_t INSIDE_ARM_START = MIDDLE_ARM_START + LEDS_PER_ARM; // arm[2]: Second segment
    constexpr uint16_t OUTER_ARM_START = INSIDE_ARM_START + LEDS_PER_ARM;  // arm[0]: Third segment (REVERSED)

    // Physical LED start positions indexed by arm (lookup table)
    constexpr uint16_t ARM_START[3] = {
        OUTER_ARM_START,   // arm[0]
        MIDDLE_ARM_START,  // arm[1]
        INSIDE_ARM_START   // arm[2]
    };

    // LED ordering (normal vs reversed wiring)
    // false = LED0 at hub, LED10 at tip (normal)
    // true = LED0 at tip, LED10 at hub (reversed)
    constexpr bool ARM_LED_REVERSED[3] = {
        true,   // arm[0] = Outer (REVERSED wiring)
        false,  // arm[1] = Middle (normal wiring)
        false   // arm[2] = Inside (normal wiring)
    };
}

#endif
