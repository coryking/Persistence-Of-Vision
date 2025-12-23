# Hardware Timing Exploration

## The Core Concept: Repeatability Over Accuracy

POV displays don't require knowing the absolute angular position of the hall sensor. What matters is **consistency**: hitting the exact same physical mark on the wheel every revolution when sending data to the LEDs.

This is a real-time computing concept. The system needs to be **deterministic**, not precise in absolute terms.

### Why This Matters

Consider a POV display rendering a vertical line:

**Scenario A: Consistent 50us latency**
```
Revolution 1:  Hall → [50us] → Render column 0 at physical position X
Revolution 2:  Hall → [50us] → Render column 0 at physical position X
Revolution 3:  Hall → [50us] → Render column 0 at physical position X
```
Result: Sharp vertical line (rotated by some fixed offset from the magnet - nobody notices or cares)

**Scenario B: Variable 20-80us latency**
```
Revolution 1:  Hall → [45us] → Render column 0 at physical position X
Revolution 2:  Hall → [72us] → Render column 0 at physical position X + 0.3°
Revolution 3:  Hall → [38us] → Render column 0 at physical position X - 0.1°
```
Result: Blurred vertical line (each revolution paints in a slightly different physical location)

### The Enemy Is Jitter, Not Latency

| Term | Definition | Impact on POV |
|------|------------|---------------|
| **Latency** | Fixed delay from trigger to action | Image rotates by fixed offset (imperceptible) |
| **Jitter** | Variance in that delay | Image shimmers/blurs (very visible) |

A system with 100us consistent latency produces sharper images than one with 20us average latency but 40us variance.

---

## Current Implementation: Software ISR Timing

The current hall sensor approach:

```cpp
void IRAM_ATTR hallISR() {
    uint64_t timestamp = esp_timer_get_time();  // Capture when ISR runs
    xQueueOverwriteFromISR(queue, &timestamp, &woken);
}
```

**Jitter sources:**
1. **Interrupt latency** (1-2us) - varies based on what CPU was doing
2. **Cache state** - code/data cache misses add variable delay
3. **Other interrupts** - higher-priority interrupts delay this one
4. **Timer read** - `esp_timer_get_time()` itself has small variance

**Measured jitter:** ~1-2us typical, potentially more under load

**At 1940 RPM:**
- 1us = 0.012° angular error
- 2us jitter = 0.024° variance per revolution

This is already quite good, but the jitter accumulates in the rolling average and takes time to smooth out.

---

## ESP32 Hardware Timing Alternatives

### Option 1: GPTimer Capture Mode

The ESP32's general-purpose timer can capture timestamps in hardware on GPIO edges.

**How it works:**
- Timer runs continuously at 80MHz (12.5ns resolution)
- GPIO edge triggers hardware latch of timer value to capture register
- Zero software in the critical path - hardware does the timestamping
- ISR or polling reads the already-captured value

**Jitter:** ~12.5ns (clock resolution only)

**Architecture change:**
```cpp
// Hardware captures timestamp automatically on hall edge
// Software just reads it when convenient

void hallISR() {
    uint64_t timestamp = gptimer_get_captured_count();  // Already captured by hardware
    // ISR latency doesn't affect timestamp accuracy
}

// Or poll from render loop:
void loop() {
    if (capture_available()) {
        uint64_t timestamp = gptimer_get_captured_count();
        uint64_t time_since_edge = now - timestamp;  // Can compensate for poll latency
    }
}
```

**Key insight:** The timestamp reflects when the edge actually happened, not when software got around to noticing it. Polling latency affects reaction time but not measurement accuracy.

### Option 2: RMT (Remote Control Transceiver)

Originally designed for IR remote protocols, RMT captures pulse timing into a hardware FIFO.

**How it works:**
- Hardware state machine records duration between signal transitions
- Buffer holds 48 symbols (ESP32-S3) - approximately 24 revolution periods
- No CPU intervention during capture
- Read buffer periodically to get batch of precise timings

**Jitter:** ~12.5-25ns

**Potential use:**
```
Hall pulses:   ___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___
RMT captures:     |31ms |31ms |31ms |31ms |  (stored in hardware FIFO)
```

Let hardware accumulate ~20 revolution timings, then read them out and average. The averaging is still done in software, but the raw timing capture is hardware-precise.

**Limitation:** RMT is designed for "bursts" of activity with idle periods. Continuous hall pulses require careful idle-threshold configuration.

### Option 3: PCNT (Pulse Counter)

Hardware pulse counter, useful with multiple magnets.

**Current limitation:** With one magnet per revolution, there's nothing to count between readings.

**With multiple magnets (e.g., 4 evenly spaced):**
- PCNT counts 4 edges per revolution in hardware
- Combined with timer, gives sub-revolution speed measurement
- Can detect speed variations within a single revolution

This is more useful for future enhancement than current single-magnet design.

---

## Comparison: Software vs Hardware Timing

| Aspect | Software ISR | GPTimer Capture | RMT Buffer |
|--------|--------------|-----------------|------------|
| **Timestamp resolution** | 1us | 12.5ns | 12.5-25ns |
| **Jitter** | 1-2us typical | ~12.5ns | ~25ns |
| **CPU involvement** | ISR in critical path | ISR reads captured value | Periodic buffer read |
| **Complexity** | Current implementation | Moderate change | Significant change |
| **Batch capability** | No | No | Yes (48 samples) |

### What Hardware Timing Buys You

**Not:** Lower latency (latency doesn't matter)

**Yes:**
1. **Lower jitter** - 12.5ns vs 1-2us (80-160x improvement)
2. **More deterministic** - no variance from cache/interrupts
3. **Potentially simpler code** - remove ISR from timing-critical path
4. **Cleaner averaging** - 20 hardware-precise samples vs 20 jittery samples

### Practical Impact

Current jitter at 1940 RPM: ~0.024° variance
Hardware jitter at 1940 RPM: ~0.0003° variance

Both are imperceptible. The current implementation is already "good enough."

However, hardware timing would:
- Make the 20-sample rolling average converge faster (less noise to filter)
- Provide more headroom if other system load increases
- Be more robust against future code changes that might affect ISR timing

---

## Implementation Considerations

### GPTimer Capture (Recommended Starting Point)

ESP-IDF provides `gptimer` API with capture functionality:

```cpp
#include "driver/gptimer.h"

gptimer_handle_t timer;
gptimer_config_t config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 80000000,  // 80MHz = 12.5ns resolution
};
gptimer_new_timer(&config, &timer);

// Configure capture on GPIO edge
gptimer_capture_config_t capture_config = {
    .capture_edge = GPTIMER_CAPTURE_EDGE_NEG,  // Falling edge (hall sensor)
};
// ... additional setup
```

This keeps the overall architecture similar to current (still process events, still average) but moves timestamp capture to hardware.

### RMT Approach (More Complex)

Would require rethinking how timing data flows through the system - batch processing instead of event-by-event. More invasive change, potentially cleaner result.

### Polling vs Interrupt

With hardware capture, polling becomes viable:

**Polling advantages:**
- No ISR at all - simpler code
- Timestamp is hardware-precise regardless of poll timing
- Can compensate for poll latency: `actual_angle = elapsed_since_capture / us_per_degree`

**Polling disadvantages:**
- Reaction latency up to one loop iteration (~50-70us = 0.6-0.8° at 1940 RPM)
- Must check for new capture each loop iteration

Current loop rate (50-70us) means worst-case 0.8° reaction delay, but since we know the precise timestamp, we can compensate mathematically.

---

## Recommendation

**Current system works well.** The 1-2us software jitter translates to ~0.024° angular variance, which is imperceptible.

**Hardware timing is worth exploring if:**
1. Adding features that increase ISR load or loop time
2. Pursuing higher RPM operation where timing margins tighten
3. Wanting cleaner architecture (timestamp capture fully in hardware)
4. Experiencing unexplained shimmer/blur in rendered images

**Suggested exploration path:**
1. Prototype GPTimer capture on a test branch
2. Compare rolling average convergence time (software vs hardware timestamps)
3. Measure actual jitter reduction in practice
4. Decide if complexity is worth the improvement

The hardware capability exists and is well-documented. The question is whether the current "good enough" timing justifies the implementation effort for "slightly better" timing.

---

## References

- [ESP-IDF GPTimer Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gptimer.html)
- [ESP-IDF RMT Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/rmt.html)
- [ESP-IDF PCNT Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html)
- [esp32-freqcount (RMT+PCNT frequency measurement)](https://github.com/DavidAntliff/esp32-freqcount)
- TIMING_ANALYSIS.md - Current timing implementation details
