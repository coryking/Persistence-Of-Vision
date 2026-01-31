# ESP32 SPI Driver Analysis for HD107S LEDs

*Investigation Date: January 31, 2026*
*FastLED Version: Current master (8cd217f137)*

## Issue #1609 Status: FIX EXISTS, NEEDS VALIDATION

The ESP32 SPI per-byte DMA transaction problem has a fix, but reports suggest it may not work correctly on ESP32-S3.

## The Bulk Transfer Fix

**Introduced:** Commit 5870d54c50, November 13, 2024
**First Release:** v3.9.4

Enable with:
```cpp
#define FASTLED_ESP32_SPI_BULK_TRANSFER 1
#include "FastLED.h"
```

### What It Does

**Default (disabled):** Each byte sent via individual `SPI.transfer()` calls
- ~900 transactions for 300 LEDs
- Each transaction grabs/releases SPI bus lock
- Creates ~1.7us gaps between bytes

**Bulk mode (enabled):** Batches pixels before sending
- Accumulates into `CRGB[64]` buffer (stack allocated)
- Sends via `SPI.writePixels()` when buffer fills
- ~5 transactions for 300 LEDs

### Key Detail: 64 PIXELS, Not 64 Bytes

```cpp
CRGB data_block[FASTLED_ESP32_SPI_BULK_TRANSFER_SIZE] = {0};  // Default 64
```

64 CRGBs = 64 pixels = 192 bytes per batch.

Configurable:
```cpp
#define FASTLED_ESP32_SPI_BULK_TRANSFER_SIZE 128  // Larger batches
```

## ESP32-S3 Compatibility Concern

From GitHub issue #1609 (January 2025), a user reported:
- Enabled the flag, confirmed `FASTLED_ESP32_SPI_BULK_TRANSFER_SIZE` was set
- Still got same 7 FPS for 128 APA102s
- The `writePixels` template appeared to never be called
- Traced to FSPI/VSPI constant handling issues in preprocessor

**This needs validation on ESP32-S3** with logic analyzer to confirm the fix is actually being used.

## Why Disabled by Default

1. **Stack RAM usage:** 192 bytes for default buffer
2. **Buffering latency:** Small LED counts don't benefit
3. **Conservative compatibility:** Avoid breaking edge cases
4. **ESP32 variant differences:** FSPI/VSPI/SPI2 constant handling varies

## Comparison: FastLED vs NeoPixelBus SPI

| Aspect | NeoPixelBus DMA | FastLED Bulk (64px) | FastLED Default |
|--------|-----------------|---------------------|-----------------|
| Transaction Size | Entire frame | 64 pixels (192 bytes) | 1 pixel (3 bytes) |
| Buffer Location | Heap (DMA-aligned) | Stack | None |
| Transfer Method | `spi_device_queue_trans()` | `writePixels()` | `SPI.transfer()` |
| Transactions (300 LEDs) | 1 | ~5 | ~900 |

**NeoPixelBus sends the entire frame in one DMA transaction** - fundamentally more efficient than FastLED's chunked approach.

## Key Files

```
src/platforms/esp/32/core/fastspi_esp32.h          # Dispatch + defines
src/platforms/esp/32/core/fastspi_esp32_arduino.h  # Arduino backend, bulk transfer impl
src/platforms/esp/32/core/fastspi_esp32_idf.h      # ESP-IDF backend
```

## Test Project Validation Goals

1. Enable `FASTLED_ESP32_SPI_BULK_TRANSFER` on ESP32-S3
2. Verify with logic analyzer that batched transfers actually occur
3. Compare timing to NeoPixelBus baseline
4. Determine if NeoPixelBus can be eliminated or remains necessary
