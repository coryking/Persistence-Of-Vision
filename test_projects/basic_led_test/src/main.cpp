#include <Arduino.h>
#include <NeoPixelBus.h>

// Platform-specific pin definitions and NeoPixelBus method
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32-S3 (Seeed XIAO) - uses generic SPI method
    #define LED_DATA D10    // SPI Data (MOSI)
    #define LED_CLOCK D8    // SPI Clock (SCK)
    #define USE_GENERIC_SPI
#else
    // ESP32-WROOM - uses VSPI hardware SPI
    #define LED_DATA 23     // GPIO23 = VSPI MOSI
    #define LED_CLOCK 18    // GPIO18 = VSPI SCK
#endif

constexpr uint16_t NUM_LEDS = 3;

// NeoPixelBus strip definition varies by platform
#if defined(USE_GENERIC_SPI)
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
#else
NeoPixelBus<DotStarBgrFeature, DotStarEsp32Vspi2MhzMethod> strip(NUM_LEDS);
#endif

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial on USB CDC

    Serial.println("HD107S LED Test - RGB Cycle");
    Serial.printf("Pins: Data=%d, Clock=%d\n", LED_DATA, LED_CLOCK);

    // Initialize with custom SPI pins (matches led_display project)
    strip.Begin(LED_CLOCK, -1, LED_DATA, -1);
    strip.Show();
}

void loop() {
    // Red
    Serial.println("RED");
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, RgbColor(255, 0, 0));
    }
    strip.Show();
    delay(1000);

    // Green
    Serial.println("GREEN");
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, RgbColor(0, 255, 0));
    }
    strip.Show();
    delay(1000);

    // Blue
    Serial.println("BLUE");
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, RgbColor(0, 0, 255));
    }
    strip.Show();
    delay(1000);
}
