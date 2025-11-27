#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "types.h"
#include "RevolutionTimer.h"
#include "blob_types.h"
#include "EffectManager.h"
#include "RenderContext.h"
#include "effects.h"

// Hardware Configuration
#define NUM_LEDS 30
#define LEDS_PER_ARM 10
#define HALL_PIN D1

// Physical arm layout (from center outward): Inner, Middle, Outer
// LED index mapping (based on wiring observation)
#define INNER_ARM_START 10    // LEDs 10-19 (orange)
#define MIDDLE_ARM_START 0    // LEDs 0-9 (red) - triggers hall sensor
#define OUTER_ARM_START 20    // LEDs 20-29 (green)

// Phase offsets (degrees) - middle arm triggers hall sensor at 0°
#define MIDDLE_ARM_PHASE 0
#define INNER_ARM_PHASE 120
#define OUTER_ARM_PHASE 240

// POV Display Configuration
#define DEGREES_PER_ARC 4           // 4-degree line width
#define ARC_START_ANGLE 0           // Line starts at 0 degrees (hall trigger position)
#define ARC_GROWTH_INTERVAL_MS 50  // Milliseconds between each degree change (tune: 100-500ms)
#define WARMUP_REVOLUTIONS 20       // Number of revolutions before display starts
#define ROLLING_AVERAGE_SIZE 20     // Smoothing window size
#define ROTATION_TIMEOUT_US 2000000 // 2 seconds timeout to detect stopped rotation

// LED Configuration
#define LED_LUMINANCE 127 // 0-255

// RPM Arc Effect Configuration
#define RPM_MIN 800.0f           // Minimum RPM (1 virtual pixel)
#define RPM_MAX 2500.0f          // Maximum RPM (30 virtual pixels)
#define ARC_WIDTH_DEGREES 20.0f  // Arc width in degrees
#define ARC_CENTER_DEGREES 0.0f  // Arc center position (hall sensor)

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
// Uses GPIO 7 (data) and GPIO 9 (clock) via hardware SPI
// Using 40MHz SPI (fastest for this LED count)
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// ISR state
volatile bool newRevolutionDetected = false;
volatile timestamp_t isrTimestamp = 0;

// Arm colors (solid RGB for visibility)
const RgbColor INNER_ARM_COLOR(255, 0, 0);    // Red (inner)
const RgbColor MIDDLE_ARM_COLOR(0, 255, 0);   // Green (middle)
const RgbColor OUTER_ARM_COLOR(0, 0, 255);    // Blue (outer)
const RgbColor OFF_COLOR(0, 0, 0);

// Virtual display mapping (from POV_DISPLAY.md)
// Note: Could use PROGMEM to save RAM, but keeping in RAM for simpler access
uint8_t VIRTUAL_TO_PHYSICAL[30] = {
     0, 10, 20,  // virtual 0-2
     1, 11, 21,  // virtual 3-5
     2, 12, 22,  // virtual 6-8
     3, 13, 23,  // virtual 9-11
     4, 14, 24,  // virtual 12-14
     5, 15, 25,  // virtual 15-17
     6, 16, 26,  // virtual 18-20
     7, 17, 27,  // virtual 21-23
     8, 18, 28,  // virtual 24-26
     9, 19, 29   // virtual 27-29
};

uint8_t PHYSICAL_TO_VIRTUAL[30] = {
     0,  3,  6,  9, 12, 15, 18, 21, 24, 27,  // physical 0-9 (Arm A/Middle)
     1,  4,  7, 10, 13, 16, 19, 22, 25, 28,  // physical 10-19 (Arm B/Inner)
     2,  5,  8, 11, 14, 17, 20, 23, 26, 29   // physical 20-29 (Arm C/Outer)
};

// Blob pool
Blob blobs[MAX_BLOBS];

// Effect manager
EffectManager effectManager;

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

    // Initialize effect manager and load current effect
    Serial.println("Initializing effect manager...");
    effectManager.begin();
    uint8_t currentEffect = effectManager.getCurrentEffect();
    Serial.printf("Current effect: %d\n", currentEffect);

    // Initialize all effects (all need to be ready for runtime switching)
    Serial.println("Setting up all effects...");
    setupPerArmBlobs();
    Serial.println("  - Per-arm blobs configured");
    setupVirtualBlobs();
    Serial.println("  - Virtual blobs configured");
    Serial.println("  - Solid arms diagnostic ready");
    Serial.println("  - RPM arc effect ready");

    // Save next effect for next boot/motor restart
    effectManager.saveNextEffect();
    Serial.println("Next effect queued for next power cycle");

    Serial.println("\n=== POV Display Ready ===");
    Serial.println("Configuration:");
    Serial.printf("  Arms: 3 (Inner:%d°, Middle:%d°, Outer:%d°)\n",
                  INNER_ARM_PHASE, MIDDLE_ARM_PHASE, OUTER_ARM_PHASE);
    Serial.printf("  LEDs per arm: %d\n", LEDS_PER_ARM);
    Serial.printf("  Arc width: %d degrees\n", DEGREES_PER_ARC);
    Serial.printf("  Arc start: %d degrees\n", ARC_START_ANGLE);
    Serial.printf("  Warm-up revolutions: %d\n", WARMUP_REVOLUTIONS);
    Serial.println("\nWaiting for rotation to start...");
    Serial.println("(LEDs will remain off during warm-up period)\n");
}

void loop()
{
    static bool wasRotating = false;

    // Process new revolution detection from ISR
    if (newRevolutionDetected) {
        newRevolutionDetected = false;
        revTimer.addTimestamp(isrTimestamp);

        // Optional: Print status after warm-up complete
        if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
            Serial.println("Warm-up complete! Display active.");
        }
    }

    // Detect motor stop/start transitions
    bool isRotating = revTimer.isCurrentlyRotating();
    if (!wasRotating && isRotating) {
        // Motor just started - switch to next effect
        Serial.println("Motor started - switching to next effect");
        effectManager.saveNextEffect();
    }
    wasRotating = isRotating;

    // Only display if warm-up complete and currently rotating
    if (revTimer.isWarmupComplete() && isRotating) {
        // Get current time and timing parameters
        timestamp_t now = esp_timer_get_time();
        timestamp_t lastHallTime = revTimer.getLastTimestamp();
        interval_t microsecondsPerRev = revTimer.getMicrosecondsPerRevolution();

        // Calculate elapsed time since hall trigger (middle arm at 0°)
        timestamp_t elapsed = now - lastHallTime;

        // Calculate microseconds per degree
        double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

        // Calculate middle arm's current angle (0-359) - it triggers the hall sensor
        double angleMiddle = fmod(static_cast<double>(elapsed) / microsecondsPerDegree, 360.0);

        // Calculate inner and outer arm angles (phase offset from middle)
        double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
        double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);

        // Create rendering context
        RenderContext ctx = {
            .currentMicros = static_cast<unsigned long>(now),
            .innerArmDegrees = static_cast<float>(angleInner),
            .middleArmDegrees = static_cast<float>(angleMiddle),
            .outerArmDegrees = static_cast<float>(angleOuter),
            .microsecondsPerRev = microsecondsPerRev
        };

        // Update all blob animations (used by blob effects)
        for (int i = 0; i < MAX_BLOBS; i++) {
            updateBlob(blobs[i], now);
        }

        // Dispatch to current effect
        switch(effectManager.getCurrentEffect()) {
            case 0:
                renderPerArmBlobs(ctx);
                break;
            case 1:
                renderVirtualBlobs(ctx);
                break;
            case 2:
                renderSolidArms(ctx);
                break;
            case 3:
                renderRpmArc(ctx);
                break;
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
