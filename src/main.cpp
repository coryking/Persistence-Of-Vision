#include <Arduino.h>
#include "esp_timer.h"

// Original pins - physically wired
#define LED_CLOCK 9
#define LED_DATA 7
#define NUM_LEDS 30

#ifdef DO_FASTLED_INIT
#include <FastLED.h>
CRGB leds[NUM_LEDS];
#endif

#ifdef DO_FASTLED_INIT

void setup()
{
    Serial.begin(115200);
    delay(100);

    Serial.println("=== ESP32-S3 Starting Up ===");
    Serial.println("Stage 1: Serial initialized");
    Serial.flush();

    delay(1000);

    Serial.println("Stage 2: About to initialize FastLED");
    Serial.flush();

    // APA102HD mode - software SPI with default speed
    FastLED.addLeds<APA102HD, LED_DATA, LED_CLOCK, BGR>(&leds[0], NUM_LEDS);

    Serial.println("Stage 3: FastLED initialized successfully");
    Serial.println("=== Setup Complete ===");
    Serial.flush();
}

void loop()
{
    static uint8_t hue = 0;

#ifdef DO_FASTLED_SHOW
    // Test with rainbow pattern
    fill_rainbow(leds, NUM_LEDS, hue, 7);

    uint64_t start = esp_timer_get_time();
    FastLED.show();
    uint64_t end = esp_timer_get_time();

    Serial.printf("Frame %03d: %llu us | ", hue, end - start);
    Serial.printf("Speed: %.1f fps\n", 1000000.0 / (end - start));

    hue += 4;
#else
    Serial.println("Loop running (no FastLED.show())");
#endif

    delay(50);  // ~20 fps
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
