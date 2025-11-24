#include <Arduino.h>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"

#define NUM_LEDS 30
#define HALL_PIN D1

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
// Uses GPIO 7 (data) and GPIO 9 (clock) via hardware SPI
// Using 40MHz SPI (fastest for this LED count)
// NeoPixelBusLg provides brightness and gamma correction support
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Hall effect sensor state
volatile bool colorChangeTriggered = false;
volatile uint8_t currentColorIndex = 0;

// Color palette
const RgbColor colors[] = {
    RgbColor(255, 0, 0),     // Red
    RgbColor(0, 255, 0),     // Green
    RgbColor(0, 0, 255),     // Blue
    RgbColor(255, 255, 0),   // Yellow
    RgbColor(255, 0, 255),   // Magenta
    RgbColor(0, 255, 255),   // Cyan
    RgbColor(255, 255, 255)  // White
};
const uint8_t numColors = sizeof(colors) / sizeof(colors[0]);

// ISR handler for hall effect sensor
void IRAM_ATTR hallSensorISR(void* arg) {
    colorChangeTriggered = true;
}

void setup()
{
    Serial.begin(115200);
    delay(2000); // Give serial time to initialize
    Serial.println("Serial initialized");

    Serial.println("Initializing LED strip...");
    strip.Begin();
    strip.SetLuminance(64);  // Full brightness (0-255)
    strip.ClearTo(RgbColor(0, 0, 0));  // Clear strip to black
    strip.Show();
    Serial.println("Strip initialized");

    // Setup hall effect sensor GPIO and ISR
    Serial.println("Setting up hall effect sensor...");
    gpio_num_t hallPin = static_cast<gpio_num_t>(HALL_PIN);

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;          // Triggers on FALLING EDGE
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << HALL_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;        // Pull-up enabled
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(hallPin, hallSensorISR, nullptr);
    Serial.println("Hall effect sensor initialized on D1");

    // Wait for serial monitor to be ready and user to press any key
    Serial.println("\n=== ESP32-S3 SPI Performance Test ===");
    Serial.println("Press any key to start the test...");

    while(!Serial.available()) {
        delay(100);
    }

    // Clear the input buffer
    while(Serial.available()) {
        Serial.read();
    }

    Serial.println("Starting test...\n");

    // Test: how fast can we update?
    uint64_t start = esp_timer_get_time();
    for(int i = 0; i < 1000; i++) {
        strip.SetPixelColor(0, RgbColor(255, 0, 0));
        strip.Show();
    }
    uint64_t elapsed = esp_timer_get_time() - start;

    Serial.printf("1000 updates took %llu us\n", elapsed);
    Serial.printf("Per update: %llu us\n", elapsed / 1000);
    Serial.printf("Update rate: %.2f Hz\n", 1000000.0 / (elapsed / 1000.0));

    Serial.println("\nTest complete.");
    Serial.println("Now running continuous color display. Hall sensor will change colors.\n");
}

void changeToRandomColor() {
    currentColorIndex = random(0, numColors);
    RgbColor currentColor = colors[currentColorIndex];
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.SetPixelColor(i, currentColor);
    }

    // Measure and display update timing
    uint64_t start = esp_timer_get_time();
    strip.Show();
    uint64_t elapsed = esp_timer_get_time() - start;

    Serial.printf("Sample: %llu us (Color: %d)\n", elapsed,
                    currentColorIndex);
}

void loop()
{
    // Check if hall sensor triggered a color change
    if (colorChangeTriggered) {
        colorChangeTriggered = false;
        currentColorIndex = (currentColorIndex + 1) % numColors;
        Serial.printf("Hall sensor triggered! Switching to color %d\n", currentColorIndex);
        changeToRandomColor();

    }

    delay(2);  // Small delay to prevent tight looping
}
