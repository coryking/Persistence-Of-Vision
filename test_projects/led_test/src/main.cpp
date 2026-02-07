// SPI/DMA Timing Characterization for HD107S/SK9822 LEDs
//
// Measures NeoPixelBus SPI timing using back-to-back vs spaced Show() calls.
// For DMA methods: show2 (burst) includes wait for previous DMA, show3 (spaced) does not.
// Wire time = show2_us - show3_us
//
// Tests sync (Arduino SPI) vs async DMA (ESP-IDF) methods.
// Tests maintainBuffer=true (copy) vs maintainBuffer=false (swap) modes.
//
// Output: CSV to serial at 921600 baud

#include <Arduino.h>
#include <NeoPixelBus.h>
#include <esp_timer.h>
#include <SPI.h>

// Custom DMA method typedefs at different speeds
typedef DotStarEsp32DmaSpiMethodBase<SpiSpeed40Mhz, Esp32Spi2Bus> DotStarEsp32Dma40MhzMethod;
typedef DotStarEsp32DmaSpiMethodBase<SpiSpeed20Mhz, Esp32Spi2Bus> DotStarEsp32Dma20MhzMethod;
typedef DotStarEsp32DmaSpiMethodBase<SpiSpeed10Mhz, Esp32Spi2Bus> DotStarEsp32Dma10MhzMethod;

// Hardware pins (matches led_display)
constexpr uint8_t DATA_PIN = D10;
constexpr uint8_t CLK_PIN = D8;

// Test configuration
constexpr int ITERATIONS = 25;
constexpr int SETTLE_DELAY_MS = 15;  // Delay to ensure DMA completion
const int LED_COUNTS[] = {1, 10, 20, 40, 60, 80, 100, 150, 200, 300, 400};
constexpr int NUM_LED_COUNTS = sizeof(LED_COUNTS) / sizeof(LED_COUNTS[0]);

// Timed Show() wrapper - returns duration in microseconds
template<typename T_STRIP>
int64_t timedShow(T_STRIP& strip, bool maintainBuffer) {
    int64_t t0 = esp_timer_get_time();
    strip.Show(maintainBuffer);
    return esp_timer_get_time() - t0;
}

// Timed delay wrapper - returns actual delay duration in microseconds
int64_t timedDelay(int ms) {
    int64_t t0 = esp_timer_get_time();
    delay(ms);
    return esp_timer_get_time() - t0;
}

// Generic test function
template<typename T_STRIP>
void runTest(T_STRIP& strip, const char* speed, const char* method,
             const char* feature, int ledCount, bool maintainBuffer) {
    const char* bufferMode = maintainBuffer ? "copy" : "swap";

    strip.Begin(CLK_PIN, -1, DATA_PIN, -1);

    // Initial Show(true) to initialize both buffers per NeoPixelBus docs
    strip.SetPixelColor(0, RgbColor(0, 0, 0));
    strip.Show(true);
    delay(SETTLE_DELAY_MS);

    for (int i = 0; i < ITERATIONS; i++) {
        // === Show 1: Normal (no pending DMA from previous iteration's delay) ===
        strip.SetPixelColor(0, RgbColor(i & 1 ? 255 : 0, 0, 0));
        int64_t show1_us = timedShow(strip, maintainBuffer);

        // === Show 2: Burst (immediately after Show 1, may wait for DMA) ===
        strip.SetPixelColor(0, RgbColor(i & 1 ? 0 : 255, 0, 0));
        int64_t show2_us = timedShow(strip, maintainBuffer);

        // === Delay 1: Let any pending DMA complete ===
        int64_t delay1_us = timedDelay(SETTLE_DELAY_MS);

        // === Show 3: Spaced (after delay, no pending DMA) ===
        strip.SetPixelColor(0, RgbColor(i & 1 ? 128 : 64, 0, 0));
        int64_t show3_us = timedShow(strip, maintainBuffer);

        // === Delay 2: Settle before next iteration ===
        int64_t delay2_us = timedDelay(SETTLE_DELAY_MS);

        Serial.printf("%s,%s,%s,%s,%d,%d,%lld,%lld,%lld,%lld,%lld\n", speed,
                      method, feature, bufferMode, ledCount, i, show1_us,
                      show2_us, delay1_us, show3_us, delay2_us);

        delay(10); // lets chill to let serial finish before continuing
    }
}

// Run both buffer modes for a given strip type
template<typename T_STRIP>
void runBothBufferModes(const char* speed, const char* method, const char* feature, int ledCount) {
    {
        T_STRIP strip(ledCount);
        runTest(strip, speed, method, feature, ledCount, true);   // maintainBuffer=true (copy)
    }
    // turns out this affects nothing!
    //{
    //    T_STRIP strip(ledCount);
    //    runTest(strip, speed, method, feature, ledCount, false);  // maintainBuffer=false (swap)
    //}
}

void setup() {
    Serial.begin(921600);
    delay(1000);

    while (Serial.available()) Serial.read();
    while (!Serial.available()) {
        Serial.println("Press any key to start timing test...");
        delay(2000);
    }
    while (Serial.available()) Serial.read();

    Serial.println("\nStarting SPI/DMA timing characterization...\n");
    Serial.println("spi_mhz,method,feature,buffer_mode,led_count,iteration,show1_us,show2_us,delay1_us,show3_us,delay2_us");

    for (int c = 0; c < NUM_LED_COUNTS; c++) {
        int ledCount = LED_COUNTS[c];

        // ========== SYNCHRONOUS METHODS (Arduino SPI) ==========

        // 40 MHz sync
        runBothBufferModes<NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod>>(
            "40", "sync", "BGR", ledCount);
        runBothBufferModes<NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod>>(
            "40", "sync", "LBGR", ledCount);

        // 20 MHz sync
        runBothBufferModes<NeoPixelBus<DotStarBgrFeature, DotStarSpi20MhzMethod>>(
            "20", "sync", "BGR", ledCount);
        runBothBufferModes<NeoPixelBus<DotStarLbgrFeature, DotStarSpi20MhzMethod>>(
            "20", "sync", "LBGR", ledCount);

        // 10 MHz sync
        runBothBufferModes<NeoPixelBus<DotStarBgrFeature, DotStarSpi10MhzMethod>>(
            "10", "sync", "BGR", ledCount);
        runBothBufferModes<NeoPixelBus<DotStarLbgrFeature, DotStarSpi10MhzMethod>>(
            "10", "sync", "LBGR", ledCount);

        // Release Arduino SPI before DMA takes over
        SPI.end();
        delay(100);

        // ========== ASYNC DMA METHODS (ESP-IDF SPI) ==========

        // 40 MHz DMA
        runBothBufferModes<NeoPixelBus<DotStarBgrFeature, DotStarEsp32Dma40MhzMethod>>(
            "40", "dma", "BGR", ledCount);
        runBothBufferModes<NeoPixelBus<DotStarLbgrFeature, DotStarEsp32Dma40MhzMethod>>(
            "40", "dma", "LBGR", ledCount);

        // 20 MHz DMA
        runBothBufferModes<NeoPixelBus<DotStarBgrFeature, DotStarEsp32Dma20MhzMethod>>(
            "20", "dma", "BGR", ledCount);
        runBothBufferModes<NeoPixelBus<DotStarLbgrFeature, DotStarEsp32Dma20MhzMethod>>(
            "20", "dma", "LBGR", ledCount);

        // 10 MHz DMA
        runBothBufferModes<NeoPixelBus<DotStarBgrFeature, DotStarEsp32DmaSpiMethod>>(
            "10", "dma", "BGR", ledCount);
        runBothBufferModes<NeoPixelBus<DotStarLbgrFeature, DotStarEsp32DmaSpiMethod>>(
            "10", "dma", "LBGR", ledCount);
        SPI.end();
        delay(50);
    }

    Serial.println("DONE");
}

void loop() {
    delay(1000);
}
