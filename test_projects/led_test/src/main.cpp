#include <Arduino.h>
#include <FastLED.h>
#include <NeoPixelBus.h>
#include <esp_timer.h>

// Benchmark configuration: Set to 1 for FastLED, 0 for NeoPixelBus
#define BENCHMARK_FASTLED 1

// Hardware Configuration
#define MAX_LEDS 42
#define DATA_PIN 7      // Orange wire - SPI MOSI (GPIO 7)
#define CLOCK_PIN 9     // Green wire - SPI SCK (GPIO 9)

// Test pattern
#define TEST_R 128
#define TEST_G 128
#define TEST_B 128

// Test configurations
const int LED_COUNTS[] = {30, 33, 42};
const int NUM_CONFIGS = 3;
const int NUM_ITERATIONS = 10;
const int NUM_WARMUP = 3;

// FastLED buffer (allocated at max size)
CRGB leds[MAX_LEDS];

// Simple stats structure
struct PerfResult {
    uint32_t min_us;
    uint32_t avg_us;
    uint32_t max_us;
};

// Dual timing result (for DMA-based methods)
struct DmaPerfResult {
    PerfResult show_call;    // Time for Show() to return
    PerfResult total_time;   // Time including DMA completion
};

// Calculate min/avg/max from timing samples
PerfResult calculateStats(uint32_t* times, int count) {
    PerfResult result = {0xFFFFFFFF, 0, 0};
    uint32_t sum = 0;

    for (int i = 0; i < count; i++) {
        if (times[i] < result.min_us) result.min_us = times[i];
        if (times[i] > result.max_us) result.max_us = times[i];
        sum += times[i];
    }

    result.avg_us = sum / count;
    return result;
}

// Benchmark FastLED.show()
PerfResult benchmarkFastLED(int numLeds) {
    Serial.println("  Setting test pattern...");
    Serial.flush();

    // Set test pattern
    for (int i = 0; i < numLeds; i++) {
        leds[i] = CRGB(TEST_R, TEST_G, TEST_B);
    }

    Serial.println("  Starting warmup iterations...");
    Serial.flush();

    // Warmup iterations (not timed)
    for (int i = 0; i < NUM_WARMUP; i++) {
        Serial.printf("    Warmup %d...", i + 1);
        Serial.flush();
        FastLED.show();
        Serial.println(" OK");
        delay(10);
    }

    Serial.println("  Starting timed iterations...");
    Serial.flush();

    // Timed iterations
    uint32_t times[NUM_ITERATIONS];
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        int64_t start = esp_timer_get_time();
        FastLED.show();
        int64_t elapsed = esp_timer_get_time() - start;
        times[i] = (uint32_t)elapsed;
        delay(10);
    }

    return calculateStats(times, NUM_ITERATIONS);
}

// Benchmark NeoPixelBus.Show() with DMA timing
//
// NeoPixelBus DMA behavior:
// - Show() waits for PREVIOUS async DMA transfer to complete
// - Show() queues NEW async DMA transfer
// - Show() returns (new transfer happens in background)
// - Show() only transfers if buffer is dirty (optimization)
//
// This means Show() is both synchronous (waits for previous) and asynchronous (queues new).
// For POV displays this is perfect - you can't start frame N+1 until frame N finishes anyway.
DmaPerfResult benchmarkNeoPixelBus(NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod>* strip, int numLeds) {
    // Set test pattern
    RgbColor color(TEST_R, TEST_G, TEST_B);
    for (int i = 0; i < numLeds; i++) {
        strip->SetPixelColor(i, color);
    }

    // Warmup iterations (not timed)
    for (int i = 0; i < NUM_WARMUP; i++) {
        strip->Show();
        delay(10);
    }


    // Timed iterations
    // IMPORTANT: NeoPixelBus has dirty tracking - Show() returns immediately if
    // buffer hasn't changed. Must modify buffer between iterations to force actual transfer!
    uint32_t show_times[NUM_ITERATIONS];
    uint32_t total_times[NUM_ITERATIONS];

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        // Toggle pixel to force dirty flag (NeoPixelBus optimization)
        RgbColor toggle = (i % 2 == 0) ? RgbColor(TEST_R, TEST_G, TEST_B) : RgbColor(TEST_R, TEST_G, TEST_B + 1);
        strip->SetPixelColor(0, toggle);

        // Measure Show() call time
        // Note: Show() internally waits for PREVIOUS DMA transfer, then queues new one
        int64_t start = esp_timer_get_time();
        strip->Show();
        int64_t after_show = esp_timer_get_time();
        show_times[i] = (uint32_t)(after_show - start);

        // Wait for THIS transfer to complete
        while (!strip->CanShow()) {
            delayMicroseconds(1);
        }
        int64_t after_dma = esp_timer_get_time();
        total_times[i] = (uint32_t)(after_dma - start);

        delayMicroseconds(100);
    }

    DmaPerfResult result;
    result.show_call = calculateStats(show_times, NUM_ITERATIONS);
    result.total_time = calculateStats(total_times, NUM_ITERATIONS);
    return result;
}

// Run benchmark for specific LED count
void runBenchmarkForLEDCount(int numLeds) {
    Serial.printf("\n======== Testing with %d LEDs ========\n", numLeds);

#if BENCHMARK_FASTLED
    // Initialize FastLED
    Serial.println("Initializing FastLED...");
    Serial.printf("  DATA_PIN: %d, CLOCK_PIN: %d\n", DATA_PIN, CLOCK_PIN);
    Serial.flush();

    FastLED.clear();
    Serial.println("  FastLED.clear() OK");
    Serial.flush();

    FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, numLeds);
    Serial.println("  FastLED.addLeds() OK");
    Serial.flush();

    FastLED.setBrightness(255);
    Serial.println("  FastLED.setBrightness() OK");
    Serial.flush();

    // Run FastLED benchmark
    Serial.println("Running FastLED benchmark...");
    PerfResult fastled = benchmarkFastLED(numLeds);

    // Display results
    Serial.println("\n--- FastLED Results ---");
    Serial.printf("  Min: %4lu μs  |  Avg: %4lu μs  |  Max: %4lu μs\n",
                  fastled.min_us, fastled.avg_us, fastled.max_us);
#else
    // Initialize NeoPixelBus (matching production setup)
    NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod>* strip =
        new NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod>(numLeds);

    // IMPORTANT: Explicitly specify SPI pins (matches ../src/main.cpp)
    // Begin(clockPin, -1, dataPin, -1)
    Serial.println("Initializing NeoPixelBus...");
    Serial.printf("  CLOCK_PIN: %d, DATA_PIN: %d\n", CLOCK_PIN, DATA_PIN);
    strip->Begin(CLOCK_PIN, -1, DATA_PIN, -1);
    Serial.println("  NeoPixelBus.Begin() OK");

    // Run NeoPixelBus benchmark
    Serial.println("Running NeoPixelBus benchmark...");
    DmaPerfResult neopixel = benchmarkNeoPixelBus(strip, numLeds);

    // Display results
    Serial.println("\n--- NeoPixelBus Results ---");
    Serial.println("Show() call time (DMA kickoff):");
    Serial.printf("  Min: %4lu μs  |  Avg: %4lu μs  |  Max: %4lu μs\n",
                  neopixel.show_call.min_us, neopixel.show_call.avg_us, neopixel.show_call.max_us);
    Serial.println("Total time (including DMA completion):");
    Serial.printf("  Min: %4lu μs  |  Avg: %4lu μs  |  Max: %4lu μs\n",
                  neopixel.total_time.min_us, neopixel.total_time.avg_us, neopixel.total_time.max_us);

    // Cleanup
    delete strip;
#endif
}

void setup() {
    Serial.begin(115200);

    // Wait up to 5 seconds for USB CDC
    unsigned long start = millis();
    while (!Serial && (millis() - start < 5000)) {
        delay(10);
    }

    // Print header
    Serial.println("\n=== LED Performance Benchmark ===");
    Serial.println("Hardware: ESP32-S3-Zero");
    Serial.println("LEDs: SK9822/APA102 @ 40MHz SPI");
#if BENCHMARK_FASTLED
    Serial.println("Library: FastLED");
#else
    Serial.println("Library: NeoPixelBus");
#endif
    Serial.printf("Pattern: Solid white (%d,%d,%d)\n", TEST_R, TEST_G, TEST_B);
    Serial.printf("Iterations: %d\n", NUM_ITERATIONS);
    Serial.printf("Warmup: %d iterations (not timed)\n", NUM_WARMUP);
    Serial.println("\nPress any key to start benchmark...");

    // Flush existing input
    while (Serial.available()) Serial.read();

    // Wait for keypress
    while (!Serial.available()) {
        delay(10);
    }
    Serial.read(); // Consume the keypress

    Serial.println("\nStarting benchmark...\n");
}

void loop() {
    // Run benchmarks for each LED count
    for (int i = 0; i < NUM_CONFIGS; i++) {
        runBenchmarkForLEDCount(LED_COUNTS[i]);
        delay(500);  // Small delay between configurations
    }

    // Done
    Serial.println("\n=== Benchmark Complete ===");
    Serial.println("All tests finished. Reset to run again.");

    // Enter infinite loop
    while (true) {
        delay(1000);
    }
}
