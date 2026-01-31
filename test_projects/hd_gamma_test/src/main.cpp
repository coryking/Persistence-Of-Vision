/**
 * HD Gamma Test - Compare FastLED's HD gamma with per-LED 5-bit brightness
 *
 * Tests FastLED's five_bit_hd_gamma_bitshift() on actual POV hardware,
 * comparing HD gamma mode vs standard mode side-by-side across different
 * saturation levels.
 *
 * Hardware: Same as led_display project (Seeed XIAO ESP32S3)
 * Uses only ARM3/Outer (14 LEDs, physical indices 27-40)
 *
 * LED Layout: 7 pairs, each comparing HD vs Standard at different saturation
 * - Pair 0 (hub):  S=0%   (grayscale)
 * - Pair 6 (tip):  S=100% (fully saturated)
 * - Within each pair: Even=HD gamma, Odd=Standard
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

// Saturation levels for each pair (0=hub, 6=tip)
// High saturation at tip (larger arc = more visual detail)
constexpr uint8_t PAIR_SATURATION[7] = {
    0,    // Pair 0 (hub): grayscale
    42,   // Pair 1: 17%
    85,   // Pair 2: 33%
    128,  // Pair 3: 50%
    170,  // Pair 4: 67%
    213,  // Pair 5: 83%
    255   // Pair 6 (tip): fully saturated
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
 * Each LED pair tests different saturation, HD vs Standard side-by-side
 */
void renderFrame(uint8_t value) {
    // Clear entire strip first
    strip.ClearTo(RgbwColor(0, 0, 0, 0));

    for (uint8_t pair = 0; pair < 7; pair++) {
        uint8_t saturation = PAIR_SATURATION[pair];

        // Create HSV color for this pair
        CHSV hsv_color(current_hue, saturation, value);

        // Convert HSV to RGB using FastLED
        CRGB rgb_color;
        hsv2rgb_rainbow(hsv_color, rgb_color);

        // --- HD Gamma Mode (even LED in pair) ---
        {
            CRGB output_rgb;
            uint8_t brightness_5bit;

            // Apply HD gamma with 5-bit brightness decomposition
            // No color correction (255,255,255), full brightness (255)
            fl::five_bit_hd_gamma_bitshift(
                rgb_color,
                CRGB(255, 255, 255),  // no color temp correction
                255,                   // full brightness (no pre-dimming)
                &output_rgb,
                &brightness_5bit
            );

            uint8_t logical_idx = pair * 2;  // Even LED: 0, 2, 4, 6, 8, 10, 12
            uint16_t physical_idx = logicalToPhysical(logical_idx);

            // RgbwColor: R, G, B, W (where W is used as 5-bit brightness for SK9822)
            strip.SetPixelColor(physical_idx,
                RgbwColor(output_rgb.r, output_rgb.g, output_rgb.b, brightness_5bit));
        }

        // --- Standard Mode (odd LED in pair) ---
        {
            uint8_t logical_idx = pair * 2 + 1;  // Odd LED: 1, 3, 5, 7, 9, 11, 13
            uint16_t physical_idx = logicalToPhysical(logical_idx);

            // Standard mode: use RGB directly, brightness fixed at 31 (max)
            strip.SetPixelColor(physical_idx,
                RgbwColor(rgb_color.r, rgb_color.g, rgb_color.b, 31));
        }
    }
}

/**
 * Static test pattern for bench mode (not spinning)
 * Shows all saturation levels at medium brightness
 */
void renderBenchPattern() {
    // Clear entire strip
    strip.ClearTo(RgbwColor(0, 0, 0, 0));

    // Medium brightness for bench testing
    constexpr uint8_t BENCH_VALUE = 128;

    for (uint8_t pair = 0; pair < 7; pair++) {
        uint8_t saturation = PAIR_SATURATION[pair];
        CHSV hsv_color(current_hue, saturation, BENCH_VALUE);
        CRGB rgb_color;
        hsv2rgb_rainbow(hsv_color, rgb_color);

        // HD Gamma Mode (even LED)
        {
            CRGB output_rgb;
            uint8_t brightness_5bit;
            fl::five_bit_hd_gamma_bitshift(
                rgb_color,
                CRGB(255, 255, 255),
                255,
                &output_rgb,
                &brightness_5bit
            );

            uint8_t logical_idx = pair * 2;
            uint16_t physical_idx = logicalToPhysical(logical_idx);
            strip.SetPixelColor(physical_idx,
                RgbwColor(output_rgb.r, output_rgb.g, output_rgb.b, brightness_5bit));
        }

        // Standard Mode (odd LED)
        {
            uint8_t logical_idx = pair * 2 + 1;
            uint16_t physical_idx = logicalToPhysical(logical_idx);
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
    Serial.println("LED Layout (7 pairs, HD vs Standard side-by-side):");
    Serial.println("  Pair 0 (hub):  S=0%   - grayscale");
    Serial.println("  Pair 1:        S=17%");
    Serial.println("  Pair 2:        S=33%");
    Serial.println("  Pair 3:        S=50%");
    Serial.println("  Pair 4:        S=67%");
    Serial.println("  Pair 5:        S=83%");
    Serial.println("  Pair 6 (tip):  S=100% - fully saturated");
    Serial.println("");
    Serial.println("Within each pair:");
    Serial.println("  Even LED = HD gamma (5-bit brightness decomposition)");
    Serial.println("  Odd LED  = Standard (brightness=31, RGB only)");

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
    // Snapshot volatile state (minimize time with interrupts effectively disabled)
    noInterrupts();
    uint32_t period = hall_period_us;
    uint32_t hall_time = hall_timestamp_us;
    bool new_rev = new_revolution;
    if (new_rev) new_revolution = false;
    interrupts();

    // Check if we're spinning (period between 10ms and 1s is valid)
    // 10ms = 6000 RPM (way too fast), 1s = 60 RPM (way too slow)
    bool spinning = (period > 10000 && period < 1000000);

    if (!spinning) {
        // Bench mode: show static test pattern, rotate hue slowly
        static uint32_t last_bench_update = 0;
        uint32_t now = millis();
        if (now - last_bench_update > 50) {  // 20 FPS bench mode
            last_bench_update = now;
            current_hue++;
            renderBenchPattern();
            strip.Show();
        }
        return;
    }

    // Calculate current angle (0-359)
    uint32_t elapsed = micros() - hall_time;
    uint16_t angle = (elapsed * 360UL) / period;
    if (angle >= 360) angle = 359;  // Clamp

    // Compute value from angle: 0° = full brightness, 355° = black
    // 355-360° is a black buffer zone
    uint8_t value;
    if (angle >= 355) {
        value = 0;
    } else {
        // Linear fade: 0° → V=255, 355° → V=0
        value = 255 - (angle * 255 / 355);
    }

    // Render frame
    renderFrame(value);

    // Show (async DMA transfer, ~35µs for 41 LEDs at 40MHz)
    strip.Show();

    // Update hue on new revolution
    if (new_rev) {
        current_hue += 1;  // Slow rotation through color wheel
    }
}
