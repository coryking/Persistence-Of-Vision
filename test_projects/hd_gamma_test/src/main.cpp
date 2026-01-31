/**
 * HD Gamma Test - Compare FastLED's HD gamma with per-LED 5-bit brightness
 *
 * Tests FastLED's five_bit_hd_gamma_bitshift() on actual POV hardware,
 * comparing HD gamma mode vs standard mode side-by-side.
 *
 * Hardware: Same as led_display project (Seeed XIAO ESP32S3)
 * Uses only ARM3/Outer (14 LEDs, physical indices 27-40)
 *
 * LED Layout: Split screen comparison
 * - LEDs 0-6 (hub half):  HD gamma mode (5-bit brightness decomposition)
 * - LEDs 7-13 (tip half): Standard mode (brightness=31, RGB only)
 * - Both halves show same saturation gradient: 0% at boundary, 100% at ends
 *
 * Effect: Angular gradient from hall sensor position
 * - 0° = full brightness (V=255)
 * - 355° = black (V=0)
 * - 355-360° = black buffer
 */

#include <Arduino.h>
#include <NeoPixelBus.h>
#include <FastLED.h>
#include "fl/five_bit_hd_gamma.h"

// =============================================================================
// Hardware Configuration (from led_display/include/hardware_config.h)
// =============================================================================

constexpr uint8_t HALL_PIN = D5;       // Hall effect sensor
constexpr uint8_t SPI_DATA_PIN = D10;  // SK9822 Data (MOSI)
constexpr uint8_t SPI_CLK_PIN = D8;    // SK9822 Clock (SCK)

// Full strip is 41 LEDs, but we only use ARM3 (physical 27-40)
constexpr uint16_t TOTAL_PHYSICAL_LEDS = 41;
constexpr uint16_t ARM3_START = 27;    // First LED of ARM3/Outer
constexpr uint16_t ARM3_LEDS = 14;     // LEDs in ARM3

// ARM3 wiring is REVERSED: Physical 27 = tip, Physical 40 = hub
// Our logical LED 0 should be at hub (physical 40), LED 13 at tip (physical 27)
// So logical index i maps to physical index: ARM3_START + (ARM3_LEDS - 1 - i)

// =============================================================================
// LED Strip (NeoPixelBus with DotStar/SK9822 support)
// =============================================================================

// DotStarLbgrFeature: SK9822 uses LBGR order (Luminance, Blue, Green, Red)
// The 'L' (luminance) is the 5-bit brightness byte (0-31)
NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip(TOTAL_PHYSICAL_LEDS);

// =============================================================================
// Hall Sensor ISR
// =============================================================================

volatile uint32_t hall_timestamp_us = 0;
volatile uint32_t hall_period_us = 0;
volatile bool new_revolution = false;

void IRAM_ATTR hallISR() {
    uint32_t now = micros();
    hall_period_us = now - hall_timestamp_us;
    hall_timestamp_us = now;
    new_revolution = true;
}

// =============================================================================
// Animation State
// =============================================================================

uint8_t current_hue = 0;  // Rotates slowly for visual variety

// Saturation levels for each LED (14 total)
// Mirror pattern: 100% at ends (LED 0, LED 13), 0% at boundary (LED 6, LED 7)
// This lets you compare same saturation across the HD/Standard split
constexpr uint8_t LED_SATURATION[14] = {
    255,  // LED 0 (hub): 100% - HD mode
    213,  // LED 1: 83%
    170,  // LED 2: 67%
    128,  // LED 3: 50%
    85,   // LED 4: 33%
    42,   // LED 5: 17%
    0,    // LED 6 (boundary): 0% grayscale - HD mode
    0,    // LED 7 (boundary): 0% grayscale - Standard mode
    42,   // LED 8: 17%
    85,   // LED 9: 33%
    128,  // LED 10: 50%
    170,  // LED 11: 67%
    213,  // LED 12: 83%
    255   // LED 13 (tip): 100% - Standard mode
};

// =============================================================================
// Rendering
// =============================================================================

/**
 * Convert logical LED index (0-13) to physical strip index (27-40)
 * ARM3 wiring is reversed: logical 0 = hub = physical 40
 */
inline uint16_t logicalToPhysical(uint8_t logical_idx) {
    return ARM3_START + (ARM3_LEDS - 1 - logical_idx);
}

/**
 * Render one frame with the given value (brightness) for all LEDs
 * Split screen: LEDs 0-6 = HD gamma, LEDs 7-13 = Standard
 */
void renderFrame(uint8_t value) {
    // Clear entire strip first
    strip.ClearTo(RgbwColor(0, 0, 0, 0));

    for (uint8_t led = 0; led < 14; led++) {
        uint8_t saturation = LED_SATURATION[led];

        // Create HSV color for this LED
        CHSV hsv_color(current_hue, saturation, value);

        // Convert HSV to RGB using FastLED
        CRGB rgb_color;
        hsv2rgb_rainbow(hsv_color, rgb_color);

        uint16_t physical_idx = logicalToPhysical(led);

        if (led <= 6) {
            // --- HD Gamma Mode (LEDs 0-6, hub half) ---
            CRGB output_rgb;
            uint8_t brightness_5bit;

            fl::five_bit_hd_gamma_bitshift(
                rgb_color,
                CRGB(255, 255, 255),  // no color temp correction
                255,                   // full brightness (no pre-dimming)
                &output_rgb,
                &brightness_5bit
            );

            strip.SetPixelColor(physical_idx,
                RgbwColor(output_rgb.r, output_rgb.g, output_rgb.b, brightness_5bit));
        } else {
            // --- Standard Mode (LEDs 7-13, tip half) ---
            strip.SetPixelColor(physical_idx,
                RgbwColor(rgb_color.r, rgb_color.g, rgb_color.b, 31));
        }
    }
}


// =============================================================================
// Setup and Loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);  // Wait for serial

    Serial.println("HD Gamma Test - Starting");
    Serial.println("Hardware: Seeed XIAO ESP32S3");
    Serial.println("Using ARM3/Outer only (14 LEDs, physical 27-40)");
    Serial.println("");
    Serial.println("LED Layout (split screen):");
    Serial.println("  LEDs 0-6 (hub half):  HD gamma mode");
    Serial.println("  LEDs 7-13 (tip half): Standard mode (brightness=31)");
    Serial.println("");
    Serial.println("Saturation gradient (mirrored):");
    Serial.println("  LED 0 (hub): S=100%  |  LED 13 (tip): S=100%");
    Serial.println("  LED 6:       S=0%    |  LED 7:        S=0%");
    Serial.println("Compare across the split to see HD vs Standard at same saturation");

    // Initialize NeoPixelBus
    strip.Begin();
    strip.ClearTo(RgbwColor(0, 0, 0, 0));
    strip.Show();

    // Initialize hall sensor
    pinMode(HALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallISR, FALLING);

    Serial.println("");
    Serial.println("Initialized. Waiting for rotation...");
}

void loop() {
    // Snapshot volatile state
    noInterrupts();
    uint32_t period = hall_period_us;
    uint32_t hall_time = hall_timestamp_us;
    bool new_rev = new_revolution;
    if (new_rev) new_revolution = false;
    interrupts();

    // Need valid period to render (10ms to 1s = 60-6000 RPM)
    if (period < 10000 || period > 1000000) {
        return;
    }

    // Calculate current angle (0-359)
    uint32_t elapsed = micros() - hall_time;
    uint16_t angle = (elapsed * 360UL) / period;
    if (angle >= 360) angle = 359;

    // Compute value from angle: 0° = full brightness, 355° = black
    uint8_t value = (angle < 355) ? 255 - (angle * 255 / 355) : 0;

    // Render and show
    renderFrame(value);
    strip.Show();

    // Update hue on new revolution
    if (new_rev) {
        current_hue++;
    }
}
