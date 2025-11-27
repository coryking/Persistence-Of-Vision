#include "effects.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "blob_types.h"
#include "esp_timer.h"

// External references to globals from main.cpp
extern NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip;
extern Blob blobs[MAX_BLOBS];
extern const uint8_t PHYSICAL_TO_VIRTUAL[30];

// Hardware configuration (from main.cpp)
constexpr uint16_t LEDS_PER_ARM = 10;
constexpr uint16_t INNER_ARM_START = 10;
constexpr uint16_t MIDDLE_ARM_START = 0;
constexpr uint16_t OUTER_ARM_START = 20;

/**
 * Check if virtual LED position is within blob's current radial extent
 * (with wraparound support for 0-29 range)
 */
static bool isVirtualLedInBlob(uint8_t virtualPos, const Blob& blob) {
    if (!blob.active) return false;

    // Calculate radial range (virtual positions covered by this blob)
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;

    // Check with wraparound handling
    float pos = static_cast<float>(virtualPos);

    // If range doesn't wrap around, simple check
    if (radialStart >= 0 && radialEnd < 30) {
        return (pos >= radialStart) && (pos < radialEnd);
    }

    // Handle wraparound (blob spans 29 → 0 boundary)
    if (radialStart < 0) {
        // Blob extends below 0, wraps to high end
        return (pos >= (radialStart + 30)) || (pos < radialEnd);
    }

    if (radialEnd >= 30) {
        // Blob extends above 29, wraps to low end
        return (pos >= radialStart) || (pos < (radialEnd - 30));
    }

    return false;
}

/**
 * Initialize 5 blobs for virtual display (0-29 radial range)
 */
void setupVirtualBlobs() {
    timestamp_t now = esp_timer_get_time();

    // Blob configuration templates for variety
    struct BlobTemplate {
        // Angular parameters
        float minAngularSize, maxAngularSize;
        float angularDriftSpeed;
        float angularSizeSpeed;
        float angularWanderRange;
        // Radial parameters
        float minRadialSize, maxRadialSize;
        float radialDriftSpeed;
        float radialSizeSpeed;
        float radialWanderRange;
    } templates[3] = {
        // Small, fast (angular 5-30°, radial 2-6 LEDs)
        {5, 30, 0.5, 0.3, 60,    2, 6, 0.4, 0.25, 4.0},
        // Medium (angular 10-60°, radial 4-10 LEDs)
        {10, 60, 0.3, 0.2, 90,   4, 10, 0.25, 0.15, 6.0},
        // Large, slow (angular 20-90°, radial 6-14 LEDs)
        {20, 90, 0.15, 0.1, 120, 6, 14, 0.15, 0.1, 8.0}
    };

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        blobs[i].armIndex = 0;  // Unused for virtual blobs
        blobs[i].color = RgbColor(citrusPalette[i]);  // Convert HSL to RGB

        // Use template based on blob index (varied sizes)
        BlobTemplate& tmpl = templates[i % 3];

        // Angular parameters
        blobs[i].wanderCenter = (i * 72.0f);  // Spread evenly: 0, 72, 144, 216, 288
        blobs[i].wanderRange = tmpl.angularWanderRange;
        blobs[i].driftVelocity = tmpl.angularDriftSpeed;
        blobs[i].minArcSize = tmpl.minAngularSize;
        blobs[i].maxArcSize = tmpl.maxAngularSize;
        blobs[i].sizeChangeRate = tmpl.angularSizeSpeed;

        // Radial parameters - now use full 0-29 range
        blobs[i].radialWanderCenter = 14.5f;  // Center of virtual display (0-29)
        blobs[i].radialWanderRange = tmpl.radialWanderRange;
        blobs[i].radialDriftVelocity = tmpl.radialDriftSpeed;
        blobs[i].minRadialSize = tmpl.minRadialSize;
        blobs[i].maxRadialSize = tmpl.maxRadialSize;
        blobs[i].radialSizeChangeRate = tmpl.radialSizeSpeed;

        blobs[i].birthTime = now;
        blobs[i].deathTime = 0;  // Immortal for now
    }
}

/**
 * Render virtual display blobs effect
 */
void renderVirtualBlobs(const RenderContext& ctx) {
    // Render using virtual addressing - each LED checks against all blobs

#ifdef ENABLE_DETAILED_TIMING
    // Track cumulative time for different operations
    int64_t totalAngleCheckTime = 0;
    int64_t totalRadialCheckTime = 0;
    int64_t totalColorBlendTime = 0;
    int64_t totalArrayLookupTime = 0;
    int64_t totalRgbConstructTime = 0;
    int64_t totalSetPixelTime = 0;
    int64_t totalTimingOverhead = 0;
    int64_t innerArmStart = esp_timer_get_time();

    // Measure the cost of measurement itself
    int64_t timingTest1 = esp_timer_get_time();
    int64_t timingTest2 = esp_timer_get_time();
    int64_t singleTimingCallCost = timingTest2 - timingTest1;
#endif

    // Inner arm: LEDs 10-19
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
#ifdef ENABLE_DETAILED_TIMING
        int64_t lookupStart = esp_timer_get_time();
#endif
        uint16_t physicalLed = INNER_ARM_START + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
#ifdef ENABLE_DETAILED_TIMING
        totalArrayLookupTime += esp_timer_get_time() - lookupStart;

        int64_t rgbStart = esp_timer_get_time();
#endif
        RgbColor ledColor(0, 0, 0);
#ifdef ENABLE_DETAILED_TIMING
        totalRgbConstructTime += esp_timer_get_time() - rgbStart;
#endif

        // Check ALL blobs (no arm filtering)
        for (int i = 0; i < MAX_BLOBS; i++) {
#ifdef ENABLE_DETAILED_TIMING
            int64_t angleStart = esp_timer_get_time();
#endif
            bool angleInArc = blobs[i].active && isAngleInArc(ctx.innerArmDegrees, blobs[i]);
#ifdef ENABLE_DETAILED_TIMING
            int64_t angleEnd = esp_timer_get_time();
            totalAngleCheckTime += angleEnd - angleStart;
            totalTimingOverhead += singleTimingCallCost * 2; // Two get_time calls
#endif

            if (angleInArc) {
#ifdef ENABLE_DETAILED_TIMING
                int64_t radialStart = esp_timer_get_time();
#endif
                bool radialMatch = isVirtualLedInBlob(virtualPos, blobs[i]);
#ifdef ENABLE_DETAILED_TIMING
                int64_t radialEnd = esp_timer_get_time();
                totalRadialCheckTime += radialEnd - radialStart;
                totalTimingOverhead += singleTimingCallCost * 2;
#endif

                if (radialMatch) {
#ifdef ENABLE_DETAILED_TIMING
                    int64_t blendStart = esp_timer_get_time();
#endif
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
#ifdef ENABLE_DETAILED_TIMING
                    int64_t blendEnd = esp_timer_get_time();
                    totalColorBlendTime += blendEnd - blendStart;
                    totalTimingOverhead += singleTimingCallCost * 2;
#endif
                }
            }
        }
#ifdef ENABLE_DETAILED_TIMING
        int64_t setPixelStart = esp_timer_get_time();
#endif
        strip.SetPixelColor(physicalLed, ledColor);
#ifdef ENABLE_DETAILED_TIMING
        totalSetPixelTime += esp_timer_get_time() - setPixelStart;
#endif
    }

#ifdef ENABLE_DETAILED_TIMING
    int64_t innerArmTime = esp_timer_get_time() - innerArmStart;
    int64_t middleArmStart = esp_timer_get_time();
#endif

    // Middle arm: LEDs 0-9
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
#ifdef ENABLE_DETAILED_TIMING
        int64_t lookupStart = esp_timer_get_time();
#endif
        uint16_t physicalLed = MIDDLE_ARM_START + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
#ifdef ENABLE_DETAILED_TIMING
        totalArrayLookupTime += esp_timer_get_time() - lookupStart;
        int64_t rgbStart = esp_timer_get_time();
#endif
        RgbColor ledColor(0, 0, 0);
#ifdef ENABLE_DETAILED_TIMING
        totalRgbConstructTime += esp_timer_get_time() - rgbStart;
#endif

        // Check ALL blobs (no arm filtering)
        for (int i = 0; i < MAX_BLOBS; i++) {
#ifdef ENABLE_DETAILED_TIMING
            int64_t angleStart = esp_timer_get_time();
#endif
            bool angleInArc = blobs[i].active && isAngleInArc(ctx.middleArmDegrees, blobs[i]);
#ifdef ENABLE_DETAILED_TIMING
            int64_t angleEnd = esp_timer_get_time();
            totalAngleCheckTime += angleEnd - angleStart;
            totalTimingOverhead += singleTimingCallCost * 2;
#endif

            if (angleInArc) {
#ifdef ENABLE_DETAILED_TIMING
                int64_t radialStart = esp_timer_get_time();
#endif
                bool radialMatch = isVirtualLedInBlob(virtualPos, blobs[i]);
#ifdef ENABLE_DETAILED_TIMING
                int64_t radialEnd = esp_timer_get_time();
                totalRadialCheckTime += radialEnd - radialStart;
                totalTimingOverhead += singleTimingCallCost * 2;
#endif

                if (radialMatch) {
#ifdef ENABLE_DETAILED_TIMING
                    int64_t blendStart = esp_timer_get_time();
#endif
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
#ifdef ENABLE_DETAILED_TIMING
                    int64_t blendEnd = esp_timer_get_time();
                    totalColorBlendTime += blendEnd - blendStart;
                    totalTimingOverhead += singleTimingCallCost * 2;
#endif
                }
            }
        }
#ifdef ENABLE_DETAILED_TIMING
        int64_t setPixelStart = esp_timer_get_time();
#endif
        strip.SetPixelColor(physicalLed, ledColor);
#ifdef ENABLE_DETAILED_TIMING
        totalSetPixelTime += esp_timer_get_time() - setPixelStart;
#endif
    }

#ifdef ENABLE_DETAILED_TIMING
    int64_t middleArmTime = esp_timer_get_time() - middleArmStart;
    int64_t outerArmStart = esp_timer_get_time();
#endif

    // Outer arm: LEDs 20-29
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
#ifdef ENABLE_DETAILED_TIMING
        int64_t lookupStart = esp_timer_get_time();
#endif
        uint16_t physicalLed = OUTER_ARM_START + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
#ifdef ENABLE_DETAILED_TIMING
        totalArrayLookupTime += esp_timer_get_time() - lookupStart;
        int64_t rgbStart = esp_timer_get_time();
#endif
        RgbColor ledColor(0, 0, 0);
#ifdef ENABLE_DETAILED_TIMING
        totalRgbConstructTime += esp_timer_get_time() - rgbStart;
#endif

        // Check ALL blobs (no arm filtering)
        for (int i = 0; i < MAX_BLOBS; i++) {
#ifdef ENABLE_DETAILED_TIMING
            int64_t angleStart = esp_timer_get_time();
#endif
            bool angleInArc = blobs[i].active && isAngleInArc(ctx.outerArmDegrees, blobs[i]);
#ifdef ENABLE_DETAILED_TIMING
            int64_t angleEnd = esp_timer_get_time();
            totalAngleCheckTime += angleEnd - angleStart;
            totalTimingOverhead += singleTimingCallCost * 2;
#endif

            if (angleInArc) {
#ifdef ENABLE_DETAILED_TIMING
                int64_t radialStart = esp_timer_get_time();
#endif
                bool radialMatch = isVirtualLedInBlob(virtualPos, blobs[i]);
#ifdef ENABLE_DETAILED_TIMING
                int64_t radialEnd = esp_timer_get_time();
                totalRadialCheckTime += radialEnd - radialStart;
                totalTimingOverhead += singleTimingCallCost * 2;
#endif

                if (radialMatch) {
#ifdef ENABLE_DETAILED_TIMING
                    int64_t blendStart = esp_timer_get_time();
#endif
                    ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                    ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                    ledColor.B = min(255, ledColor.B + blobs[i].color.B);
#ifdef ENABLE_DETAILED_TIMING
                    int64_t blendEnd = esp_timer_get_time();
                    totalColorBlendTime += blendEnd - blendStart;
                    totalTimingOverhead += singleTimingCallCost * 2;
#endif
                }
            }
        }
#ifdef ENABLE_DETAILED_TIMING
        int64_t setPixelStart = esp_timer_get_time();
#endif
        strip.SetPixelColor(physicalLed, ledColor);
#ifdef ENABLE_DETAILED_TIMING
        totalSetPixelTime += esp_timer_get_time() - setPixelStart;
#endif
    }

#ifdef ENABLE_DETAILED_TIMING
    int64_t outerArmTime = esp_timer_get_time() - outerArmStart;

    // Print detailed breakdown (only every ~100 frames to avoid serial spam)
    static uint32_t detailFrameCount = 0;
    if (++detailFrameCount % 100 == 0) {
        int64_t totalTime = innerArmTime + middleArmTime + outerArmTime;
        int64_t measuredTime = totalAngleCheckTime + totalRadialCheckTime + totalColorBlendTime +
                               totalArrayLookupTime + totalRgbConstructTime + totalSetPixelTime +
                               totalTimingOverhead;
        int64_t unmeasuredOverhead = totalTime - measuredTime;

        Serial.println("\n=== VirtualBlobs Profiling Report (Single Frame) ===");
        Serial.println("UNITS: All times in microseconds (μs), percentages of total render time");
        Serial.println();

        Serial.println("--- Per-Arm Timing ---");
        Serial.printf("Inner Arm:  %4lld μs  (%5.1f%%)\n", innerArmTime, (innerArmTime * 100.0) / totalTime);
        Serial.printf("Middle Arm: %4lld μs  (%5.1f%%)\n", middleArmTime, (middleArmTime * 100.0) / totalTime);
        Serial.printf("Outer Arm:  %4lld μs  (%5.1f%%)\n", outerArmTime, (outerArmTime * 100.0) / totalTime);
        Serial.printf("TOTAL:      %4lld μs\n", totalTime);
        Serial.println();

        Serial.println("--- Detailed Operation Breakdown ---");
        Serial.printf("Angle checks (isAngleInArc):        %4lld μs  (%5.1f%%)  [~150 calls]\n",
                      totalAngleCheckTime, (totalAngleCheckTime * 100.0) / totalTime);
        Serial.printf("Radial checks (isVirtualLedInBlob): %4lld μs  (%5.1f%%)\n",
                      totalRadialCheckTime, (totalRadialCheckTime * 100.0) / totalTime);
        Serial.printf("Color blends (RGB addition):        %4lld μs  (%5.1f%%)\n",
                      totalColorBlendTime, (totalColorBlendTime * 100.0) / totalTime);
        Serial.printf("Array lookups (PHYSICAL_TO_VIRTUAL): %4lld μs  (%5.1f%%)  [30 calls]\n",
                      totalArrayLookupTime, (totalArrayLookupTime * 100.0) / totalTime);
        Serial.printf("RgbColor construction:              %4lld μs  (%5.1f%%)  [30 calls]\n",
                      totalRgbConstructTime, (totalRgbConstructTime * 100.0) / totalTime);
        Serial.printf("SetPixelColor calls:                %4lld μs  (%5.1f%%)  [30 calls]\n",
                      totalSetPixelTime, (totalSetPixelTime * 100.0) / totalTime);
        Serial.printf("Timing instrumentation overhead:    %4lld μs  (%5.1f%%)\n",
                      totalTimingOverhead, (totalTimingOverhead * 100.0) / totalTime);
        Serial.printf("Unmeasured (loop/other overhead):   %4lld μs  (%5.1f%%)\n",
                      unmeasuredOverhead, (unmeasuredOverhead * 100.0) / totalTime);
        Serial.println();

        Serial.printf("Measurement overhead per esp_timer_get_time() call: %lld μs\n", singleTimingCallCost);
        Serial.println("====================================================\n");
    }
#endif
}
