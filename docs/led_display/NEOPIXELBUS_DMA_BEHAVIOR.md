# NeoPixelBus DMA Behavior and Show() Synchronization

**Date:** 2025-12-11
**Context:** Benchmarking investigation revealing NeoPixelBus's automatic synchronization behavior
**Hardware:** ESP32-S3, SK9822/APA102 LEDs, 40MHz SPI

---

## Executive Summary

**NeoPixelBus `Show()` automatically synchronizes DMA transfers** - you cannot "overrun" it by calling `Show()` too quickly. Each `Show()` call blocks until the previous DMA transfer completes, then queues a new transfer and returns.

**Key Findings:**
- `Show()` uses **asynchronous DMA** for SPI transfers (CPU free during transfer)
- `Show()` **blocks on previous transfer** before queueing new one (automatic synchronization)
- `Show()` has **dirty tracking** - skips transfer if buffer unchanged (optimization)
- For POV displays: **Perfect behavior** - you can't start frame N+1 until frame N finishes anyway

**Measured Timing (33 LEDs @ 40MHz SPI):**
- First `Show()`: ~1-2 μs (no previous transfer to wait for)
- Subsequent `Show()`: ~50 μs (waits ~40 μs for previous DMA + queues new ~1 μs)
- Actual SPI transfer time: ~40 μs (happens in background via DMA)

---

## How NeoPixelBus Show() Works

### Source Code Analysis

**File:** `DotStarEsp32DmaSpiMethod.h` in NeoPixelBus library

```cpp
void Update(bool) {
    // STEP 1: Wait for previous DMA transfer to complete
    while(!IsReadyToUpdate())
        portYIELD();

    // STEP 2: Copy data to DMA buffer
    memcpy(_dmadata, _data, _spiBufferSize);

    // STEP 3: Configure SPI transaction
    _spiTransaction = { 0 };
    _spiTransaction.length = (_spiBufferSize) * 8; // in bits
    _spiTransaction.tx_buffer = _dmadata;

    // STEP 4: Queue DMA transfer (returns immediately)
    esp_err_t ret = spi_device_queue_trans(_spiHandle, &_spiTransaction, 0);
}
```

**IsReadyToUpdate() checks previous transfer status:**

```cpp
bool IsReadyToUpdate() const {
    spi_transaction_t t;
    spi_transaction_t* tptr = &t;

    // Try to get previous transaction result (timeout = 0)
    esp_err_t ret = spi_device_get_trans_result(_spiHandle, &tptr, 0);

    // Ready if previous transfer done OR no transfer was started
    return (ret == ESP_OK ||
            (ret == ESP_ERR_TIMEOUT && 0 == _spiTransaction.length));
}
```

### Behavior Timeline

**First Show() call:**
```
Time 0 μs:   Show() called
             - IsReadyToUpdate() = true (no previous transfer)
             - Queue DMA transfer
Time 1 μs:   Show() returns
Time 1-40 μs: DMA transfer happens in background (CPU FREE)
Time 40 μs:  DMA transfer completes
```

**Second Show() call (immediately after first):**
```
Time 41 μs:  Show() called
             - IsReadyToUpdate() = false (DMA still running from 1-40 μs)
             - Enters while(!IsReadyToUpdate()) loop
             - Calls portYIELD() to let other tasks run
Time 80 μs:  Previous DMA completes
             - IsReadyToUpdate() = true
             - Exit wait loop
             - Queue new DMA transfer
Time 81 μs:  Show() returns
Time 81-120 μs: New DMA transfer happens in background
```

---

## Dirty Tracking Optimization

**NeoPixelBus skips transfers if buffer hasn't changed:**

```cpp
// NeoPixelBus.h
void Show(bool maintainBufferConsistency = true) {
    if (!IsDirty() && !_method.AlwaysUpdate()) {
        return;  // EARLY RETURN - no transfer!
    }

    _method.Update(maintainBufferConsistency);
    ResetDirty();
}

bool IsDirty() const {
    return (_state & NEO_DIRTY);
}
```

**Buffer becomes dirty when:**
- `SetPixelColor()` is called
- `ClearTo()` is called
- Direct buffer manipulation via `Pixels()` (must manually call `Dirty()`)

**Buffer becomes clean when:**
- `Show()` completes successfully
- `ResetDirty()` is called

### Benchmark Implications

**This optimization broke initial benchmarks!** Calling `Show()` repeatedly without modifying the buffer resulted in:
- First `Show()`: ~40 μs (actual transfer)
- Subsequent `Show()`: ~1 μs (early return, no transfer!)

**Solution:** Must modify buffer between iterations:
```cpp
for (int i = 0; i < NUM_ITERATIONS; i++) {
    // Force buffer dirty
    RgbColor toggle = (i % 2 == 0)
        ? RgbColor(128, 128, 128)
        : RgbColor(128, 128, 129);
    strip.SetPixelColor(0, toggle);

    // Now Show() will actually transfer
    strip.Show();
}
```

---

## Measured Performance

### Test Setup
- **Hardware:** ESP32-S3 (Seeed XIAO)
- **LEDs:** SK9822/APA102 (APA102-compatible DotStar)
- **SPI Speed:** 40 MHz
- **Method:** `DotStarSpi40MhzMethod`
- **Pin Config:** GPIO 9 (clock), GPIO 7 (data)

### Results

| LED Count | Show() Call Time | Total Time (incl DMA wait) | SPI Transfer Time |
|-----------|------------------|----------------------------|-------------------|
| 30 LEDs   | 45 μs           | 45-47 μs                   | ~38 μs            |
| 33 LEDs   | 50 μs           | 50-56 μs                   | ~40 μs            |
| 42 LEDs   | 57 μs           | 58-63 μs                   | ~48 μs            |

**Notes:**
- **Show() call time** ≈ wait for previous DMA + queue new transfer
- **Total time** ≈ Show() call time (because wait happens inside Show())
- **SPI transfer time** = actual hardware transfer (measured separately)

### Theoretical SPI Timing

**SK9822/APA102 Protocol:**
```
Start frame:  4 bytes × 8 bits = 32 bits
LED data:     N LEDs × 4 bytes × 8 bits = 32N bits
End frame:    4 bytes × 8 bits = 32 bits
Total:        32 + 32N + 32 = 64 + 32N bits
```

**At 40 MHz:**
| LED Count | Total Bits | Theoretical Time | Measured Time | Overhead |
|-----------|------------|------------------|---------------|----------|
| 30 LEDs   | 1,024 bits | 25.6 μs         | ~38 μs        | +48%     |
| 33 LEDs   | 1,120 bits | 28.0 μs         | ~40 μs        | +43%     |
| 42 LEDs   | 1,408 bits | 35.2 μs         | ~48 μs        | +36%     |

**Overhead sources:**
- DMA setup time
- SPI peripheral initialization per transaction
- Frame timing requirements
- ESP32 SPI driver overhead

---

## Implications for POV Display

### Perfect Synchronization Behavior

**Good news:** You don't need to manually check `CanShow()` in production code!

```cpp
// POV rendering loop - CORRECT USAGE
void loop() {
    if (revTimer.isWarmupComplete() && isRotating) {
        // Calculate angles
        updateArmAngles();

        // Render effects
        currentEffect->render(renderCtx);

        // Show() automatically waits for previous transfer
        strip.Show();  // Blocks ~50 μs, then returns

        // NO DELAY - loop immediately for next frame
    } else {
        strip.ClearTo(RgbColor(0, 0, 0));
        strip.Show();
        delay(10);  // Only delay when NOT rendering
    }
}
```

**Why this works:**
1. `Show()` waits for previous frame's DMA to complete
2. `Show()` queues current frame's DMA transfer
3. `Show()` returns - CPU is free while DMA runs
4. Loop calculates next frame angles while DMA transfers
5. Next `Show()` call waits for step 3's DMA, repeat

### Timing Budget Analysis

**At 1940 RPM (worst case):**
- Revolution period: 30,928 μs
- Time per 1° column: 85.9 μs
- `Show()` time: ~50 μs
- **Remaining time for rendering:** 35.9 μs per column

**Breakdown per frame:**
```
Total frame time: ~85.9 μs per 1° column
├─ Show() waits for previous DMA:  ~40 μs
├─ Show() queues new DMA:           ~1 μs
├─ Angle calculations (3 arms):     ~3 μs
├─ Effect rendering:               ~10-30 μs (varies)
└─ Headroom:                        ~10-30 μs
```

**During Show() execution:**
- CPU blocked: ~1 μs (queueing DMA)
- CPU free: ~40 μs (DMA transfer in background)
- Hall ISR can fire and post to queue
- Rendering calculations can continue after Show() returns

### No Manual Synchronization Needed

**You don't need:**
```cpp
// NOT NEEDED - Show() does this automatically!
while (!strip.CanShow()) {
    delayMicroseconds(1);
}
strip.Show();
```

**Just call Show() directly:**
```cpp
// CORRECT - automatic synchronization
strip.Show();
```

### Dirty Flag Behavior in POV

**Normal POV rendering:**
- Every frame modifies pixel colors (effect rendering)
- Buffer is always dirty
- `Show()` always transfers
- Dirty tracking optimization doesn't affect us

**Only relevant when:**
- Showing static patterns (off state, solid colors)
- Buffer genuinely unchanged between Show() calls
- Optimization saves CPU time by skipping unnecessary transfers

---

## Comparison with FastLED

### FastLED (Previous POV projects)

**FastLED on ESP32 has broken SPI implementation:**
- Measured: 100s of microseconds for 30 LEDs
- Expected: ~25-40 μs
- **Project-killing performance** - invalidated entire architecture

**Reference:** See `docs/led_display/POV_PROJECT_ARCHITECTURE_LESSONS.md` and `docs/led_display/TIMING_ANALYSIS.md` for full analysis

### NeoPixelBus (Current POV_Project)

**Fast, consistent SPI performance:**
- Measured: ~50 μs for 33 LEDs
- Expected: ~40 μs (+ ~10 μs overhead)
- **Excellent performance** - leaves headroom for rendering

**Why NeoPixelBus is faster:**
- Uses ESP-IDF's native SPI driver correctly
- Hardware DMA eliminates bit-banging overhead
- Proper SPI clock configuration (40 MHz)
- Minimal abstraction overhead

---

## Testing Methodology

### Initial Approach (BROKEN)

```cpp
// WRONG - dirty flag optimization breaks measurement!
for (int i = 0; i < 10; i++) {
    int64_t start = esp_timer_get_time();
    strip.Show();
    int64_t elapsed = esp_timer_get_time() - start;
    // First iteration: ~40 μs
    // Later iterations: ~1 μs (early return, no transfer!)
}
```

### Corrected Approach

```cpp
// CORRECT - force buffer dirty each iteration
for (int i = 0; i < 10; i++) {
    // Toggle pixel to force dirty flag
    RgbColor toggle = (i % 2 == 0)
        ? RgbColor(128, 128, 128)
        : RgbColor(128, 128, 129);
    strip.SetPixelColor(0, toggle);

    // Measure Show() call time
    int64_t start = esp_timer_get_time();
    strip.Show();
    int64_t after_show = esp_timer_get_time();

    // Wait for DMA completion
    while (!strip.CanShow()) {
        delayMicroseconds(1);
    }
    int64_t after_dma = esp_timer_get_time();

    // Record both timings
    show_time = after_show - start;    // ~50 μs
    total_time = after_dma - start;    // ~50 μs (same - wait inside Show())
}
```

---

## Key Takeaways

1. **NeoPixelBus Show() is self-synchronizing** - cannot overrun, blocks automatically
2. **DMA is asynchronous** - SPI transfer happens in background, CPU is free
3. **Dirty tracking optimizes unchanged buffers** - skips transfer if no changes
4. **For POV: Just call Show()** - no manual synchronization needed
5. **Excellent performance** - ~50 μs for 33 LEDs leaves ample rendering headroom

**Bottom line:** NeoPixelBus's DMA behavior is perfect for POV displays. The automatic synchronization ensures frame N+1 never starts before frame N completes, while async DMA keeps CPU free for rendering calculations.
