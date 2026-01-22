#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>
#include <cstdint>

namespace HardwareConfig {
    // LED configuration
    // - LEDS_PER_ARM: Maximum LEDs per arm (for buffer sizing)
    // - ARM_LED_COUNT: Actual LED count per arm (arm[0]=14, others=13)
    // - TOTAL_LOGICAL_LEDS: LEDs effects see (40)
    // - TOTAL_PHYSICAL_LEDS: Including level shifter at index 0 (41)
    constexpr uint16_t LEDS_PER_ARM = 14;        // Max (for buffer sizing)
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LOGICAL_LEDS = 40;  // What effects see
    constexpr uint16_t TOTAL_PHYSICAL_LEDS = 41; // Including level shifter

    // Per-arm LED counts (arm[0]=ARM3/outer, arm[1]=ARM2/middle, arm[2]=ARM1/inner)
    constexpr uint16_t ARM_LED_COUNT[3] = {14, 13, 13};

    constexpr uint8_t GLOBAL_BRIGHTNESS = 255;

    // IMPORTANT: NEVER hardcode loop limits in effects!
    // ❌ BAD:  for (int p = 0; p < 10; p++)
    // ✅ GOOD: for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++)

    // Hardware pin assignments (Seeed XIAO ESP32S3)
    // See docs/led_display/HARDWARE.md for physical hardware details
    constexpr uint8_t HALL_PIN = D5;       // Hall effect sensor
    constexpr uint8_t SPI_DATA_PIN = D10;  // SK9822 Data (MOSI)
    constexpr uint8_t SPI_CLK_PIN = D8;    // SK9822 Clock (SCK)

    // MPU-9250 IMU (9-axis: gyro + accel + magnetometer)
    // Supports I2C (400kHz) or SPI (1MHz all registers, 20MHz sensor/interrupt only)
    // Wire colors: SCL=Orange, SDA=Yellow, ADO=Green, INT=Blue, NCS=Black
    // Datasheet: docs/datasheets/PS-MPU-9250A-01-v1.1.pdf
    constexpr uint8_t IMU_SCL_PIN = D0;    // Orange wire - I2C/SPI Clock
    constexpr uint8_t IMU_SDA_PIN = D1;    // Yellow wire - I2C/SPI Data In
    constexpr uint8_t IMU_ADO_PIN = D2;    // Green wire - I2C addr LSB (0=0x68, 1=0x69) / SPI Data Out
    constexpr uint8_t IMU_INT_PIN = D3;    // Blue wire - Interrupt output
    constexpr uint8_t IMU_NCS_PIN = D4;    // Black wire - SPI chip select (HIGH for I2C mode)

    // Physical arm layout (indexed by logical position)
    // Logical arm[0] = Outer/ARM3 (furthest from center, +240° from hall sensor) - 14 LEDs
    // Logical arm[1] = Middle/ARM2 (between outer and inside, 0° hall sensor reference) - 13 LEDs
    // Logical arm[2] = Inside/ARM1 (closest to center, +120° from hall sensor) - 13 LEDs
    //
    // Physical strip layout (41 LEDs total):
    // - Physical LED 0     = Level shifter (always dark, 3.3V→5V conversion)
    // - Physical LEDs 1-13 = ARM1/Inside (arm[2]) - Normal ordering, 13 LEDs
    // - Physical LEDs 14-26 = ARM2/Middle (arm[1]) - Normal ordering, 13 LEDs
    // - Physical LEDs 27-40 = ARM3/Outer (arm[0]) - REVERSED ordering, 14 LEDs
    //
    // ARM3's extra LED is at the hub end (closest to center, 1/3 pitch further
    // inward than ARM1/ARM2), creating an asymmetric virtual display.

    // Physical LED start positions indexed by arm (after level shifter at index 0)
    constexpr uint16_t ARM_START[3] = {
        27,  // arm[0] = ARM3/Outer (14 LEDs, reversed)
        14,  // arm[1] = ARM2/Middle (13 LEDs, normal)
        1    // arm[2] = ARM1/Inside (13 LEDs, normal)
    };

    // LED ordering (normal vs reversed wiring)
    // false = LED0 at hub, LEDs toward tip (normal)
    // true = LED0 at tip, LEDs toward hub (reversed)
    constexpr bool ARM_LED_REVERSED[3] = {
        true,   // arm[0] = ARM3/Outer (REVERSED wiring, 14 LEDs)
        false,  // arm[1] = ARM2/Middle (normal wiring, 13 LEDs)
        false   // arm[2] = ARM1/Inside (normal wiring, 13 LEDs)
    };
}

#endif
