#include <Arduino.h>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "types.h"
#include "RevolutionTimer.h"

// Hardware Configuration
#define NUM_LEDS 30
#define NUM_LEDS_ACTIVE 10  // First 10 LEDs for single arm
#define HALL_PIN D1

// POV Display Configuration
#define DEGREES_PER_ARC 4           // 4-degree line width
#define ARC_START_ANGLE 0           // Line starts at 0 degrees (hall trigger position)
#define ARC_GROWTH_INTERVAL_MS 50  // Milliseconds between each degree change (tune: 100-500ms)
#define WARMUP_REVOLUTIONS 20       // Number of revolutions before display starts
#define ROLLING_AVERAGE_SIZE 20     // Smoothing window size
#define ROTATION_TIMEOUT_US 2000000 // 2 seconds timeout to detect stopped rotation

// LED Configuration
#define LED_LUMINANCE 64            // 25% brightness (0-255 scale)

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
// Uses GPIO 7 (data) and GPIO 9 (clock) via hardware SPI
// Using 40MHz SPI (fastest for this LED count)
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// ISR state
volatile bool newRevolutionDetected = false;
volatile timestamp_t isrTimestamp = 0;

// Line color (red)
const RgbColor LINE_COLOR(255, 0, 0);
const RgbColor OFF_COLOR(0, 0, 0);

// Arc animation state
int currentArcDegrees = DEGREES_PER_ARC;  // Start at 4 degrees
bool arcGrowing = true;                    // true = growing, false = shrinking
timestamp_t lastArcUpdateTime = 0;         // Track last animation update
const interval_t ARC_UPDATE_INTERVAL = ARC_GROWTH_INTERVAL_MS * 1000;  // Convert ms to microseconds
const int MIN_ARC_DEGREES = 4;
const int MAX_ARC_DEGREES = 180;

/**
 * ISR handler for hall effect sensor
 * Captures timestamp immediately and sets flag for processing in main loop
 */
void IRAM_ATTR hallSensorISR(void* arg) {
    isrTimestamp = esp_timer_get_time();
    newRevolutionDetected = true;
}

void setup()
{
    Serial.begin(115200);
    delay(2000); // Give serial time to initialize
    Serial.println("POV Display Initializing...");

    // Initialize LED strip
    Serial.println("Initializing LED strip...");
    strip.Begin();
    strip.SetLuminance(LED_LUMINANCE);  // 25% brightness
    strip.ClearTo(OFF_COLOR);           // Clear strip to black
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

    Serial.println("\n=== POV Display Ready ===");
    Serial.println("Configuration:");
    Serial.printf("  Line width: %d degrees\n", DEGREES_PER_ARC);
    Serial.printf("  Line start: %d degrees\n", ARC_START_ANGLE);
    Serial.printf("  Active LEDs: %d of %d\n", NUM_LEDS_ACTIVE, NUM_LEDS);
    Serial.printf("  Warm-up revolutions: %d\n", WARMUP_REVOLUTIONS);
    Serial.println("\nWaiting for rotation to start...");
    Serial.println("(LEDs will remain off during warm-up period)\n");
}

void loop()
{
    // Process new revolution detection from ISR
    if (newRevolutionDetected) {
        newRevolutionDetected = false;
        revTimer.addTimestamp(isrTimestamp);

        // Optional: Print status after warm-up complete
        if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
            Serial.println("Warm-up complete! Display active.");
        }
    }

    // Only display if warm-up complete and currently rotating
    if (revTimer.isWarmupComplete() && revTimer.isCurrentlyRotating()) {
        // Get current time and timing parameters
        timestamp_t now = esp_timer_get_time();
        timestamp_t lastHallTime = revTimer.getLastTimestamp();
        interval_t microsecondsPerRev = revTimer.getMicrosecondsPerRevolution();

        // Calculate microseconds per degree
        double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

        // Calculate arc timing
        timestamp_t arcStartTime = lastHallTime + static_cast<interval_t>(ARC_START_ANGLE * microsecondsPerDegree);
        timestamp_t arcEndTime = lastHallTime + static_cast<interval_t>((ARC_START_ANGLE + currentArcDegrees) * microsecondsPerDegree);

        // Check if we're currently within the arc
        bool inArc = (now >= arcStartTime && now < arcEndTime);

        // Update arc size at configured interval
        if (now - lastArcUpdateTime >= ARC_UPDATE_INTERVAL) {
            if (arcGrowing) {
                currentArcDegrees++;
                if (currentArcDegrees >= MAX_ARC_DEGREES) {
                    arcGrowing = false;  // Start shrinking
                }
            } else {
                currentArcDegrees--;
                if (currentArcDegrees <= MIN_ARC_DEGREES) {
                    arcGrowing = true;  // Start growing
                }
            }
            lastArcUpdateTime = now;
        }

        // Control all 30 LEDs explicitly to prevent artifacts
        for (uint16_t i = 0; i < NUM_LEDS; i++) {
            if (i < NUM_LEDS_ACTIVE) {
                // First 10 LEDs - active display
                if (inArc) {
                    strip.SetPixelColor(i, LINE_COLOR);
                } else {
                    strip.SetPixelColor(i, OFF_COLOR);
                }
            } else {
                // LEDs 10-29 - explicitly turn off to prevent artifacts
                strip.SetPixelColor(i, OFF_COLOR);
            }
        }

        // Update the strip
        strip.Show();
    } else {
        // Not ready yet - keep all LEDs off
        strip.ClearTo(OFF_COLOR);
        strip.Show();

        // Small delay to avoid tight loop during warm-up
        delay(10);
    }

    // No delay in main loop when running - timing is critical!
    // The tight loop allows us to catch the precise moment to turn LEDs on/off
}
