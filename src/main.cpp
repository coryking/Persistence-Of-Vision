#include <Arduino.h>
#include "esp_timer.h"

#define FASTLED_ALL_PINS_HARDWARE_SPI
#define FASTLED_ESP32_SPI_BUS FSPI

#define LED_CLOCK 9
#define LED_DATA 7
#define NUM_LEDS 30
#define LED_DATA_RATE_MHZ 20

#include <FastLED.h>
CRGB leds[NUM_LEDS];

#ifdef DO_FANCY_SHIT

void setup()
{
    Serial.begin(115200);
    // Wait for USB CDC connection on ESP32-S3
    delay(100);

    Serial.println("=== ESP32-S3 Starting Up ===");
    Serial.println("Stage 1: Serial initialized");
    Serial.flush();

    delay(1000);

    Serial.println("Stage 2: About to initialize FastLED");
    Serial.flush();

    FastLED.addLeds<SK9822, LED_DATA, LED_CLOCK, BGR, DATA_RATE_MHZ(LED_DATA_RATE_MHZ)>(&leds[0], NUM_LEDS);

    Serial.println("Stage 3: FastLED initialized successfully");
    Serial.println("=== Setup Complete ===");
    Serial.flush();
}

void loop()
{
    fill_solid(leds, NUM_LEDS, CRGB::White);
    uint64_t start = esp_timer_get_time();
    FastLED.show();
    uint64_t end = esp_timer_get_time();
    Serial.printf("Sample: %llu\n", end - start);
    delay(1000);
}
#else
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("Hello there");
}

void loop()
{
    delay(1000);
    Serial.println("Hello there from the loop");
}
#endif
