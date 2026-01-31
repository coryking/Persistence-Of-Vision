# ESP32 SPI Driver Analysis for HD107S LEDs

*Investigation Date: January 31, 2026*
*FastLED Version: Current master (8cd217f137)*
*Compared to: Pinned commit ac595965af (Nov 27, 2025)*

## Issue #1609 Status: PARTIALLY FIXED

The ESP32 SPI per-byte DMA transaction problem (causing ~1.7μs gaps) has been addressed but the fix is **disabled by default**.

### The Fix for HD107S (APA102-Compatible True SPI)

**Bulk Transfer Mode Available** (Commit 5870d54c50, Nov 13, 2024)

Enable with:
```cpp
#define FASTLED_ESP32_SPI_BULK_TRANSFER 1
#include "FastLED.h"
```

Performance improvement:
- Before: ~900 transactions for 300 LEDs, 1.53ms overhead
- After: ~5 transactions, 8.5μs overhead

Configuration options:
```cpp
#define FASTLED_ESP32_SPI_BULK_TRANSFER 1           // Enable bulk mode
#define FASTLED_ESP32_SPI_BULK_TRANSFER_SIZE 64     // Pixels per batch (default)
```

### Code Paths

Three distinct SPI implementations exist in FastLED:

#### 1. Legacy ESP32SPIOutput (FOR YOUR HD107S)
- Files: `fastspi_esp32_arduino.h` (~272 lines), `fastspi_esp32_idf.h` (~164 lines)
- Now includes bulk transfer support
- Works in both Arduino and ESP-IDF backends

#### 2. Modern ChannelEngineSpi (NOT for HD107S - WS2812 only)
- Files: `channel_engine_spi.h`, `channel_engine_spi.cpp.hpp`
- ISR-based double-buffered staging (4KB buffers A/B)
- Wave8 encoding with multi-lane support
- **Not applicable to clocked SPI LEDs like HD107S**

#### 3. Alternative Drivers
- **PARLIO**: 8.0MHz, 1-16 parallel strips - ESP32-P4/C6/H2/C5
- **LCD RGB**: 3.2MHz - ESP32-P4 only
- **LCD I80**: ESP32-S3/P4 - up to 16 parallel strips

### Key Files

```
src/platforms/esp/32/fastspi_esp32.h          # Dispatch router
src/platforms/esp/32/fastspi_esp32_arduino.h  # Arduino backend (bulk transfer here)
src/platforms/esp/32/fastspi_esp32_idf.h      # ESP-IDF backend
src/platforms/esp/32/channel_engine_spi.*     # WS2812 only, not for HD107S
```

### Changes Since Pinned Commit

1. Dispatch header refactor (`fastspi_esp32.h` is now a router)
2. New ChannelEngine architecture (not relevant for HD107S)
3. Peripheral abstraction layer added (`ispi_peripheral.h`)
4. **Bulk transfer integration** - the key fix for you

### Recommendation

To test if bulk transfer fixes your SPI issues:

```cpp
#define FASTLED_ESP32_SPI_BULK_TRANSFER 1
#include "FastLED.h"

// Then use HD107 as normal
FastLED.addLeds<HD107, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```

If this works well, you could potentially drop NeoPixelBus entirely.
