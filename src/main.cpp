#include <Arduino.h>
#include <cmath>
#include "esp_timer.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "types.h"
#include "RevolutionTimer.h"
#include "HallEffectDriver.h"
#include "RenderContext.h"
#include "EffectRegistry.h"
#include "effects/NoiseField.h"
#include "effects/SolidArms.h"
#include "effects/RpmArc.h"
#include "timing_utils.h"

// Hardware Configuration
#define NUM_LEDS 30
#define LEDS_PER_ARM 10
#define HALL_PIN D1

// ===== TEST MODE CONFIGURATION =====
#define TEST_RPM 2800.0
#define TEST_VARY_RPM false

// Physical arm layout - maps arm index to physical LED start position
// Arms: [0]=inner, [1]=middle (hall trigger), [2]=outer
static const uint16_t ARM_START[3] = {10, 0, 20};  // Physical LED indices

// Phase offsets (degrees) - middle arm triggers hall sensor at 0°
#define MIDDLE_ARM_PHASE 0
#define INNER_ARM_PHASE 120
#define OUTER_ARM_PHASE 240

// POV Display Configuration
#define WARMUP_REVOLUTIONS 20
#define ROLLING_AVERAGE_SIZE 20
#define ROTATION_TIMEOUT_US 2000000

// ESP32-S3 hardware SPI for SK9822/APA102 (DotStar)
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Revolution timing
RevolutionTimer revTimer(WARMUP_REVOLUTIONS, ROLLING_AVERAGE_SIZE, ROTATION_TIMEOUT_US);

// Hall effect sensor driver
HallEffectDriver hallDriver(HALL_PIN);

// Effect instances
NoiseField noiseFieldEffect;
SolidArms solidArmsEffect;
RpmArc rpmArcEffect;

// Effect registry
EffectRegistry effectRegistry;

// Render context (reused each frame)
RenderContext renderCtx;

// Timing instrumentation
#if ENABLE_TIMING_INSTRUMENTATION
uint32_t instrumentationFrameCount = 0;
bool csvHeaderPrinted = false;
#endif

// Test mode: simulated rotation
#ifdef TEST_MODE
double simulatedAngle = 0.0;
timestamp_t lastSimUpdate = 0;
interval_t simulatedMicrosecondsPerRev = 0;

double simulateRotation() {
    timestamp_t now = esp_timer_get_time();

    if (lastSimUpdate == 0) {
        lastSimUpdate = now;
        simulatedAngle = 0.0;
    }

    timestamp_t elapsed = now - lastSimUpdate;
    lastSimUpdate = now;

    double rpm = TEST_RPM;
    if (TEST_VARY_RPM) {
        double timeSec = now / 1000000.0;
        rpm = 700.0 + 1050.0 * (1.0 + sin(timeSec * 0.5));
    }

    simulatedMicrosecondsPerRev = static_cast<interval_t>(60000000.0 / rpm);

    double degreesPerMicrosecond = (rpm * 360.0) / 60000000.0;
    simulatedAngle += degreesPerMicrosecond * elapsed;

    while (simulatedAngle >= 360.0) {
        simulatedAngle -= 360.0;
    }

    return simulatedAngle;
}
#endif

/**
 * FreeRTOS task for processing hall sensor events
 */
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();

    while (1) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);

            // Notify current effect of revolution
            float rpm = 60000000.0f / static_cast<float>(revTimer.getMicrosecondsPerRevolution());
            effectRegistry.onRevolution(rpm);

            if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
                Serial.println("Warm-up complete! Display active.");
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("POV Display Initializing...");

    // Initialize LED strip
    strip.Begin();
    strip.ClearTo(RgbColor(0, 0, 0));
    strip.Show();
    Serial.println("Strip initialized");

    // Setup hall effect sensor
    hallDriver.start();
    Serial.println("Hall effect sensor initialized");

    // Start hall processing task
    BaseType_t taskCreated = xTaskCreate(
        hallProcessingTask,
        "hallProcessor",
        2048,
        nullptr,
        3,
        nullptr
    );

    if (taskCreated != pdPASS) {
        Serial.println("ERROR: Failed to create hall processing task");
        while (1) { delay(1000); }
    }
    Serial.println("Hall processing task started");

    // Register effects
    effectRegistry.registerEffect(&noiseFieldEffect);
    effectRegistry.registerEffect(&solidArmsEffect);
    effectRegistry.registerEffect(&rpmArcEffect);
    effectRegistry.begin();

    Serial.printf("Registered %d effects, starting with effect 0\n", effectRegistry.getEffectCount());

    Serial.println("\n=== POV Display Ready ===");
    Serial.println("Effects: NoiseField, SolidArms, RpmArc");
    Serial.println("Waiting for rotation...\n");
}

void loop() {
    static bool wasRotating = false;

#ifdef TEST_MODE
    double angleMiddle = simulateRotation();
    double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
    double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);
    interval_t microsecondsPerRev = simulatedMicrosecondsPerRev;
    timestamp_t now = esp_timer_get_time();
    bool isRotating = true;
    bool isWarmupComplete = true;
#else
    bool isRotating = revTimer.isCurrentlyRotating();
    if (!wasRotating && isRotating) {
        Serial.println("Motor started - switching to next effect");
        effectRegistry.next();
    }
    wasRotating = isRotating;

    bool isWarmupComplete = revTimer.isWarmupComplete();

    timestamp_t now = esp_timer_get_time();
    timestamp_t lastHallTime = revTimer.getLastTimestamp();
    interval_t microsecondsPerRev = revTimer.getMicrosecondsPerRevolution();

    timestamp_t elapsed = now - lastHallTime;
    double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

    double angleMiddle = fmod(static_cast<double>(elapsed) / microsecondsPerDegree, 360.0);
    double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
    double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);
#endif

    if (isWarmupComplete && isRotating) {
#if ENABLE_TIMING_INSTRUMENTATION
        if (!csvHeaderPrinted) {
            Serial.println("frame,effect,total_us,angle_deg,rpm");
            csvHeaderPrinted = true;
        }
        int64_t frameStart = timingStart();
#endif

        // Populate render context
        renderCtx.timeUs = static_cast<uint32_t>(now);
        renderCtx.microsPerRev = microsecondsPerRev;

        // Set arm angles - arms[0]=inner, arms[1]=middle, arms[2]=outer
        renderCtx.arms[0].angle = static_cast<float>(angleInner);
        renderCtx.arms[1].angle = static_cast<float>(angleMiddle);
        renderCtx.arms[2].angle = static_cast<float>(angleOuter);

        // Render current effect
        Effect* current = effectRegistry.current();
        if (current) {
            current->render(renderCtx);
        }

        // Copy arm buffers to LED strip
        for (int a = 0; a < 3; a++) {
            uint16_t start = ARM_START[a];
            for (int p = 0; p < 10; p++) {
                CRGB color = renderCtx.arms[a].pixels[p];
                color.nscale8(128);  // Power budget
                strip.SetPixelColor(start + p, RgbColor(color.r, color.g, color.b));
            }
        }

        strip.Show();

#if ENABLE_TIMING_INSTRUMENTATION
        int64_t totalTime = timingEnd(frameStart);
        double rpm = (microsecondsPerRev > 0) ? (60000000.0 / microsecondsPerRev) : 0.0;
        Serial.printf("%u,%u,%lld,%.2f,%.1f\n",
                      instrumentationFrameCount++,
                      effectRegistry.getCurrentIndex(),
                      totalTime,
                      angleMiddle,
                      rpm);
#endif

    } else {
        strip.ClearTo(RgbColor(0, 0, 0));
        strip.Show();
        delay(10);
    }
}
