# NeoPixelBus Bulk Update Investigation

**Date:** 2025-11-27
**Context:** Investigating 104-147 μs overhead for 30 SetPixelColor() calls (3.5-4.9 μs per call)
**Goal:** Find faster alternatives to per-pixel SetPixelColor for bulk updates

---

## Executive Summary

**BEST OPTION: Direct Buffer Access via `Pixels()`**

NeoPixelBus provides `Pixels()` method for direct buffer access. For 30 LEDs with DotStarBgrFeature:
- Buffer is 120 bytes (4 bytes per LED: luminance/brightness byte + B + G + R)
- Can bypass per-pixel overhead entirely
- **Must call `ApplyPostAdjustments()` after direct manipulation** (for NeoPixelBusLg gamma/luminance)
- Alternative: Use base NeoPixelBus without luminance (skip gamma correction entirely)

**Expected improvement:** 104-147 μs → ~5-10 μs (10-20x faster)

---

## 1. SetPixelColor Analysis

### What SetPixelColor Does Internally

**Call chain for `NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod>::SetPixelColor()`:**

```cpp
// NeoPixelBusLg.h line 162-166
void SetPixelColor(uint16_t indexPixel, typename T_COLOR_FEATURE::ColorObject color)
{
    color = Shader.Apply(indexPixel, color);  // Step 1: Apply luminance + gamma
    NeoPixelBus<T_COLOR_FEATURE, T_METHOD>::SetPixelColor(indexPixel, color);  // Step 2
}

// NeoPixelBus.h line 183-190
void SetPixelColor(uint16_t indexPixel, typename T_COLOR_FEATURE::ColorObject color)
{
    if (indexPixel < _countPixels)
    {
        T_COLOR_FEATURE::applyPixelColor(_pixels(), indexPixel, color);  // Step 3
        Dirty();  // Step 4: Mark buffer dirty
    }
}
```

**Step-by-step overhead breakdown:**

1. **Luminance Shader Apply (NeoPixelBusLg only):**
   ```cpp
   // NeoPixelBusLg.h line 67-72
   typename T_COLOR_FEATURE::ColorObject Apply(uint16_t, const typename T_COLOR_FEATURE::ColorObject& original)
   {
       typename T_COLOR_FEATURE::ColorObject color = original.Dim(_luminance);  // Dim operation
       return NeoGamma<T_GAMMA>::Correct(color);  // Gamma correction
   }
   ```
   - `Dim()`: Per-channel multiplication (R, G, B each multiplied by luminance/255)
   - `NeoGamma<>::Correct()`: Gamma curve lookup or calculation per channel
   - **Cost:** ~1-2 μs per call

2. **Bounds check:**
   ```cpp
   if (indexPixel < _countPixels)
   ```
   - Trivial cost (~0.01 μs)

3. **applyPixelColor (DotStarL4ByteFeature):**
   ```cpp
   // DotStarL4ByteFeature.h line 34-42
   static void applyPixelColor(uint8_t* pPixels, uint16_t indexPixel, ColorObject color)
   {
       uint8_t* p = getPixelAddress(pPixels, indexPixel);  // Calculate address
       *p++ = 0xE0 | (color.W < 31 ? color.W : 31);        // Brightness byte
       *p++ = color[V_IC_1];  // B (for BGR feature)
       *p++ = color[V_IC_2];  // G
       *p = color[V_IC_3];    // R
   }
   ```
   - `getPixelAddress()`: `pPixels + indexPixel * 4` (trivial)
   - 4 memory writes
   - **Cost:** ~0.1-0.2 μs

4. **Dirty flag:**
   ```cpp
   void Dirty() { _state |= NEO_DIRTY; }
   ```
   - Single bitwise OR
   - **Cost:** ~0.01 μs

### Why It's Slow (3.5-4.9 μs per call)

**Primary culprit: Gamma correction in NeoPixelBusLg**

- Each `SetPixelColor()` applies `Dim()` + `NeoGamma::Correct()` per pixel
- Gamma correction does floating point math or table lookups for R, G, B
- **This is the 3-4 μs overhead you're seeing**

**Additional overhead:**
- Function call overhead (virtual dispatch through template parameters)
- Color object construction/destruction (RgbColor → RgbwColor conversions)
- Cache misses if iterating through non-contiguous memory

**Why it's worse than expected:**
- You're using `NeoPixelBusLg` which **always** applies gamma/luminance
- Even if you set luminance to 255, it still runs the shader pipeline
- No way to disable gamma correction in NeoPixelBusLg (by design)

---

## 2. Alternative APIs

### Option A: Direct Buffer Access via `Pixels()`

**API:**
```cpp
uint8_t* Pixels()  // NeoPixelBus.h line 163-166
```

**Buffer format for DotStarBgrFeature (4 bytes per LED):**
```
LED 0: [brightness] [B] [G] [R]
LED 1: [brightness] [B] [G] [R]
...
LED 29: [brightness] [B] [G] [R]
```

**Brightness byte format:**
- Bits 7-5: Always `111` (0xE0)
- Bits 4-0: Brightness (0-31)
- Example: `0xE0 | 31` = full brightness (0xFF)

**Example code:**
```cpp
// Get raw buffer pointer
uint8_t* buffer = strip.Pixels();

// Directly write 30 LEDs
for (int i = 0; i < 30; i++) {
    uint8_t* pixel = buffer + (i * 4);
    pixel[0] = 0xFF;  // Full brightness (0xE0 | 31)
    pixel[1] = blue;  // B
    pixel[2] = green; // G
    pixel[3] = red;   // R
}

// CRITICAL: Must call ApplyPostAdjustments() if using NeoPixelBusLg
strip.ApplyPostAdjustments();  // Apply luminance + gamma to entire buffer
strip.Dirty();  // Mark buffer dirty
strip.Show();   // Send to LEDs
```

**Performance:**
- Buffer write: ~30 iterations × 4 writes = ~120 memory ops
- **Estimated time:** ~5-10 μs (depends on memory speed)
- `ApplyPostAdjustments()`: Loops through all 30 pixels applying gamma
  - **Cost:** ~30-60 μs (1-2 μs per pixel)
- **Total:** ~35-70 μs (vs 104-147 μs currently)

**Pros:**
- Keeps gamma correction (maintains visual consistency)
- Simple to use
- Still 2-4x faster than per-pixel SetPixelColor

**Cons:**
- Still pays gamma correction cost
- Must remember to call `ApplyPostAdjustments()`
- Buffer format is undocumented (must read source)

### Option B: Switch to Base NeoPixelBus (Skip Gamma Entirely)

**Change declaration:**
```cpp
// Current (with luminance/gamma):
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Proposed (no luminance/gamma):
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
```

**Then use direct buffer access:**
```cpp
uint8_t* buffer = strip.Pixels();

for (int i = 0; i < 30; i++) {
    uint8_t* pixel = buffer + (i * 4);
    pixel[0] = 0xFF;  // Full brightness
    pixel[1] = blue;
    pixel[2] = green;
    pixel[3] = red;
}

strip.Dirty();  // Mark dirty
strip.Show();   // Send to LEDs
```

**Performance:**
- Buffer write: ~5-10 μs
- No gamma correction overhead
- **Total:** ~5-10 μs (10-20x faster than current)

**Pros:**
- Fastest option (no gamma overhead)
- Still use NeoPixelBus infrastructure
- Direct buffer control

**Cons:**
- Lose gamma correction (colors may look different)
- Must manually apply luminance if needed (scale RGB values)
- Breaking change (affects all existing effects)

### Option C: Pre-build Frame Buffer + memcpy

**Approach:**
```cpp
// Pre-allocate frame buffer (static or global)
static uint8_t frameBuffer[120];  // 30 LEDs × 4 bytes

// Build frame in buffer
for (int i = 0; i < 30; i++) {
    uint8_t* pixel = frameBuffer + (i * 4);
    pixel[0] = 0xFF;  // Brightness
    pixel[1] = blue;
    pixel[2] = green;
    pixel[3] = red;
}

// Copy to NeoPixelBus buffer
memcpy(strip.Pixels(), frameBuffer, 120);

// Apply gamma if using NeoPixelBusLg
strip.ApplyPostAdjustments();
strip.Dirty();
strip.Show();
```

**Performance:**
- Frame build: ~5-10 μs
- `memcpy(120 bytes)`: ~2-3 μs
- `ApplyPostAdjustments()`: ~30-60 μs (if needed)
- **Total:** ~37-73 μs with gamma, ~7-13 μs without

**Pros:**
- Clean separation (build frame, then copy)
- Can build frame in advance (pre-compute)
- Easy to debug (inspect frameBuffer before copying)

**Cons:**
- Extra memory (120 bytes)
- Extra copy overhead
- Still need gamma if using NeoPixelBusLg

### Option D: Custom SPI DMA (Bypass NeoPixelBus)

**ESP-IDF SPI API:**
```cpp
#include "driver/spi_master.h"

spi_device_handle_t spi;
spi_transaction_t trans;

// Initialize SPI (40MHz)
spi_bus_config_t buscfg = {
    .mosi_io_num = 7,   // GPIO 7
    .sclk_io_num = 9,   // GPIO 9
    // ... other config
};

spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 40000000,  // 40 MHz
    .mode = 0,
    // ... other config
};

spi_bus_initialize(SPI2_HOST, &buscfg, 1);  // DMA channel 1
spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

// Send frame
uint8_t frame[120];
// ... build frame ...

memset(&trans, 0, sizeof(trans));
trans.length = 120 * 8;  // bits
trans.tx_buffer = frame;
spi_device_transmit(spi, &trans);  // Blocking, uses DMA
```

**Performance:**
- Frame build: ~5-10 μs
- SPI DMA transfer (120 bytes @ 40MHz): ~24 μs (theoretical)
- **Total:** ~30-35 μs

**Pros:**
- Complete control over SPI timing
- Can optimize DMA transfer parameters
- No library overhead

**Cons:**
- **Much more complex** (100+ lines of setup code)
- **Lose NeoPixelBus conveniences** (automatic framing, timing)
- **Must handle DotStar protocol manually** (start/end frames)
- **Debug overhead** (SPI errors are cryptic)
- **Maintenance burden** (ESP-IDF API changes between versions)

---

## 3. Recommended Approach

### **Primary Recommendation: Option B (Base NeoPixelBus + Direct Buffer)**

**Why:**
1. **10-20x performance improvement** (104-147 μs → 5-10 μs)
2. **Minimal code changes** (change one typedef, update rendering logic)
3. **Still uses NeoPixelBus** (keeps Show() timing, SPI infrastructure)
4. **No gamma correction overhead** (you're already at max luminance anyway)

**Code changes required:**

**Before (main.cpp line 62):**
```cpp
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
```

**After:**
```cpp
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Remove this line (line 187):
strip.SetLuminance(LED_LUMINANCE);  // No longer exists on base NeoPixelBus
```

**Rendering example (new helper function):**
```cpp
/**
 * Fast bulk pixel update - writes directly to buffer
 * Buffer format: [brightness][B][G][R] per LED (4 bytes each)
 */
inline void setPixelColorDirect(uint8_t* buffer, uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 31) {
    uint8_t* pixel = buffer + (index * 4);
    pixel[0] = 0xE0 | (brightness & 0x1F);  // Brightness (5 bits)
    pixel[1] = b;  // Blue (BGR order)
    pixel[2] = g;  // Green
    pixel[3] = r;  // Red
}

void renderMyEffect(const RenderContext& ctx) {
    uint8_t* buffer = strip.Pixels();

    // Example: Light 30 LEDs directly
    for (int i = 0; i < 30; i++) {
        setPixelColorDirect(buffer, i, 255, 0, 0);  // Red
    }

    strip.Dirty();  // Mark buffer dirty
    // strip.Show() called by main loop
}
```

**Brightness control (if needed):**
```cpp
// Global brightness (like old LED_LUMINANCE)
uint8_t globalBrightness = 31;  // 0-31 (was 127 on 0-255 scale)

// Apply when writing pixels:
setPixelColorDirect(buffer, i, r, g, b, globalBrightness);
```

**Visual impact:**
- Colors will be **slightly more saturated** (no gamma curve)
- SK9822 LEDs have built-in constant current control (colors should still look good)
- **Test visually** - if gamma is needed, use Option A instead

### **Fallback Recommendation: Option A (Keep NeoPixelBusLg + ApplyPostAdjustments)**

**If visual testing shows gamma correction is needed:**

```cpp
void renderMyEffect(const RenderContext& ctx) {
    uint8_t* buffer = strip.Pixels();

    // Write pixels directly
    for (int i = 0; i < 30; i++) {
        setPixelColorDirect(buffer, i, 255, 0, 0);
    }

    strip.ApplyPostAdjustments();  // Apply luminance + gamma (30-60 μs)
    strip.Dirty();
}
```

**Performance:** 35-70 μs (still 2-4x faster than current)

---

## 4. DMA/Custom SPI Section

### Why NOT to bypass NeoPixelBus

**NeoPixelBus already uses optimal SPI:**
- `DotStarSpi40MhzMethod` uses ESP32 hardware SPI at 40MHz
- Already uses DMA for transfers (if available on ESP32-S3)
- Handles DotStar protocol framing correctly

**What you'd gain from custom SPI:**
- ~5-10 μs saved (theoretical)
- More control over DMA buffer management

**What you'd lose:**
- 100+ lines of setup code
- SPI bus initialization complexity
- DotStar protocol handling (start/end frames)
- Compatibility with NeoPixelBus ecosystem
- Maintainability (ESP-IDF API changes)

**Verdict:** Not worth it. The bottleneck is gamma correction, not SPI transfer.

**Proof:** Your current measurements show `strip.Show()` is only ~20-30 μs. The 104-147 μs is in the rendering loop (SetPixelColor calls).

---

## 5. Performance Comparison Table

| Approach | Time (μs) | Speedup | Complexity | Visual Quality |
|----------|-----------|---------|------------|----------------|
| **Current (SetPixelColor × 30)** | 104-147 | 1x | Low | Gamma corrected |
| **Option A (Direct + ApplyPostAdjustments)** | 35-70 | 2-4x | Low | Gamma corrected |
| **Option B (Direct, no gamma)** | 5-10 | 10-20x | Low | Slightly more saturated |
| **Option C (Pre-build + memcpy + gamma)** | 37-73 | 2-4x | Medium | Gamma corrected |
| **Option C (Pre-build + memcpy, no gamma)** | 7-13 | 10-15x | Medium | Slightly more saturated |
| **Option D (Custom SPI DMA)** | 30-35 | 3-5x | **Very High** | Same as Option B |

---

## 6. Implementation Plan

### Step 1: Test Option B (Recommended)

1. Change `NeoPixelBusLg` → `NeoPixelBus` in main.cpp
2. Remove `strip.SetLuminance()` call
3. Add `setPixelColorDirect()` helper function
4. Update one effect to use direct buffer access
5. **Measure timing** (add instrumentation)
6. **Visual test** (does it look good?)

### Step 2: If gamma needed, fallback to Option A

1. Revert to `NeoPixelBusLg`
2. Add `strip.ApplyPostAdjustments()` after direct buffer writes
3. **Measure timing** (should still be 2-4x faster)

### Step 3: Optimize all effects

Once proven, update all 4 effects to use direct buffer access:
- VirtualBlobs
- PerArmBlobs
- SolidArms
- RpmArc

---

## 7. Code Example: Complete Before/After

### Before (Current - Slow)
```cpp
void renderVirtualBlobs(const RenderContext& ctx) {
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = INNER_ARM_START + ledIdx;
        RgbColor ledColor(0, 0, 0);

        // ... calculate ledColor from blobs ...

        strip.SetPixelColor(physicalLed, ledColor);  // 3.5-4.9 μs each
    }
    // Total: ~104-147 μs for 30 LEDs
}
```

### After (Option B - Fast)
```cpp
inline void setPixelColorDirect(uint8_t* buffer, uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t* pixel = buffer + (index * 4);
    pixel[0] = 0xFF;  // Full brightness
    pixel[1] = b;     // Blue
    pixel[2] = g;     // Green
    pixel[3] = r;     // Red
}

void renderVirtualBlobs(const RenderContext& ctx) {
    uint8_t* buffer = strip.Pixels();

    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = INNER_ARM_START + ledIdx;

        // Calculate color (same logic)
        uint8_t r = 0, g = 0, b = 0;
        // ... calculate r, g, b from blobs ...

        setPixelColorDirect(buffer, physicalLed, r, g, b);  // ~0.2 μs each
    }

    strip.Dirty();
    // Total: ~5-10 μs for 30 LEDs (10-20x faster)
}
```

### After (Option A - Fast with gamma)
```cpp
void renderVirtualBlobs(const RenderContext& ctx) {
    uint8_t* buffer = strip.Pixels();

    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = INNER_ARM_START + ledIdx;
        uint8_t r = 0, g = 0, b = 0;
        // ... calculate r, g, b from blobs ...
        setPixelColorDirect(buffer, physicalLed, r, g, b);
    }

    strip.ApplyPostAdjustments();  // Apply gamma to all 30 pixels at once
    strip.Dirty();
    // Total: ~35-70 μs (2-4x faster)
}
```

---

## References

- **NeoPixelBus.h:** `/Users/coryking/projects/POV_Project/.pio/libdeps/lolin_s3_mini_debug/NeoPixelBus/src/NeoPixelBus.h`
  - Line 163-166: `Pixels()` method
  - Line 183-190: `SetPixelColor()` implementation
- **NeoPixelBusLg.h:** Line 162-166 (luminance shader overhead)
- **DotStarL4ByteFeature.h:** Line 34-42 (buffer format)
- **NeoByteElements.h:** Line 43-46 (`getPixelAddress()` helper)

---

## Conclusion

**Use Option B (base NeoPixelBus + direct buffer access) for 10-20x speedup.**

The 104-147 μs overhead is almost entirely from gamma correction in NeoPixelBusLg. By switching to base NeoPixelBus and writing directly to the buffer, you eliminate this overhead completely.

If visual quality requires gamma correction, Option A (keep NeoPixelBusLg + ApplyPostAdjustments) still gives 2-4x speedup by applying gamma once per frame instead of per pixel.

**Do NOT pursue Option D (custom SPI DMA).** The bottleneck is not SPI transfer - it's gamma correction. NeoPixelBus already uses optimal SPI.
