#include <Arduino.h>
#include "esp_timer.h"
#include <NeoPixelBus.h>

#define NUM_LEDS 30

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
// Uses GPIO 7 (data) and GPIO 9 (clock) via hardware SPI
NeoPixelBus<DotStarBgrFeature, DotStarSpiMethod> strip(NUM_LEDS);

void setup()
{
    Serial.begin(115200);
    delay(5000);
    strip.Begin();
    Serial.println("Hello there");
}

void loop()
{
    // Fill all LEDs with white
    for(int i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, RgbColor(255, 255, 255));
    }

    uint64_t start = esp_timer_get_time();
    strip.Show();
    uint64_t end = esp_timer_get_time();
    Serial.printf("Sample: %llu\n", end - start);
    delay(1000);
}
