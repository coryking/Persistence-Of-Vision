# POV Display Timing Fix - November 28, 2025

## Summary

Fixed visual glitches ("black snow", "split pie") in the SolidArms diagnostic effect caused by timing issues in the render loop.

## Original Symptoms

1. **"Black snow"** - Sporadic black pixels appearing in solid color blocks, specifically in arm 2 (outer arm), patterns 16-18 (angles 288°-342°)

2. **"Split pie"** - Pattern 15 appeared to "split apart" from pattern 16, like the pie wedge was larger than it should be. The boundary at 288° was unstable.

3. **Periodic transposition** - Occasionally the entire pattern would glitch and appear on the wrong side of the disc (~180° offset)

4. **Slow RPM crash** - At ~934 RPM, the display would become "incoherent" and eventually reset

5. **Key observation**: Glitches appeared at the **same angular positions regardless of RPM**, suggesting a calculation issue rather than random timing jitter.

## Root Causes Identified

### 1. Race Condition in Timing Reads

**Problem:** Main loop read timing values non-atomically:
```cpp
timestamp_t now = esp_timer_get_time();
timestamp_t lastHallTime = revTimer.getLastTimestamp();  // Hall task can update between these!
interval_t microsecondsPerRev = revTimer.getMicrosecondsPerRevolution();
```

If the hall processing task updated `lastTimestamp` between reading `now` and `lastHallTime`, the calculated `elapsed` could wrap to a huge value (or go negative), causing angle jumps.

**Fix:** Added `TimingSnapshot` struct and `getTimingSnapshot()` method that reads all values atomically using a spinlock:
```cpp
TimingSnapshot timing = revTimer.getTimingSnapshot();
// All values now consistent with each other
```

### 2. Open-Loop Rendering (Multiple Renders Per Angular Position)

**Problem:** The render loop ran "as fast as possible" with no synchronization to angular position. This meant:
- Same angular position might be rendered multiple times
- At pattern boundaries, could oscillate between patterns on consecutive frames
- Angular resolution varied with render time and RPM

**Fix:** Added angle-slot gating - only render when we've moved to a new angular slot:
```cpp
int currentSlot = static_cast<int>(angleMiddle / slotSize);
if (currentSlot != lastSlot) {
    lastSlot = currentSlot;
    // ... render ...
} else {
    return;  // Skip - still in same slot
}
```

### 3. Using Smoothed Average for Angle Calculation

**Problem:** Angle was calculated using `smoothedInterval` (rolling average of 20 revolutions):
```cpp
double microsecondsPerDegree = smoothedInterval / 360.0;
double angle = elapsed / microsecondsPerDegree;
```

If actual motor speed differed from the smoothed average, error accumulated through the revolution:
- At 0°: error ≈ 0
- At 288° (80% through): error could be several degrees!

This caused the pattern 15/16 boundary to appear in the wrong place, creating the "split pie" effect.

**Fix:** Use `lastActualInterval` (most recent actual revolution time) for angle calculation:
```cpp
interval_t microsecondsPerRev = timing.lastActualInterval;
if (microsecondsPerRev == 0) {
    microsecondsPerRev = timing.microsecondsPerRev;  // Fallback for first rev
}
```

The smoothed interval is still used for RPM display and resolution calculation.

### 4. Dynamic Angular Resolution

**Problem:** Fixed slot size (e.g., 1°) might be too fine for high RPM where render time exceeds available time per slot.

At 2800 RPM with ~75µs render time:
- Time per revolution: ~21,400 µs
- Time per degree: ~59.5 µs
- Render time (75µs) > time per degree = can't keep up with 1° slots

**Fix:** Dynamic slot sizing based on render performance:
- Measure actual render time (rolling average)
- Calculate optimal slot size with safety margin
- Only use resolutions that evenly divide 360° for clean alignment
- Resolution updates once per revolution (at hall trigger)

```cpp
float minResolution = (renderTime * SAFETY_MARGIN) / microsecondsPerDegree;
// Find smallest valid resolution >= minResolution
```

Valid resolutions: 0.5°, 1°, 1.5°, 2°, 2.5°, 3°, 4°, 4.5°, 5°, 6°, 8°, 9°, 10°, 12°, 15°, 18°, 20°

### 5. WiFi/Bluetooth Jitter

**Problem:** ESP32 WiFi and Bluetooth stacks cause interrupt jitter even when not actively used.

**Fix:** Explicitly disable at startup:
```cpp
WiFi.mode(WIFI_OFF);
btStop();
```

## Files Modified

- `include/RevolutionTimer.h` - Added TimingSnapshot, atomic reads, render timing, dynamic resolution
- `src/main.cpp` - Angle-slot gating, use actual interval for angles, disable WiFi/BT
- `src/effects/SolidArms.cpp` - Added reference marker (white line at 0°) for debugging

## Key Architecture Decisions

1. **Effects still get raw data** - RenderContext exposes real angles, real timing. No buffering or abstraction hiding the hardware.

2. **Slot gating in loop(), not effects** - The gating logic lives in main.cpp. Effects don't need to know about it.

3. **Resolution adapts to performance** - System automatically finds the best angular resolution it can sustain.

4. **Actual vs smoothed intervals** - Use actual for angle calculation (accuracy within revolution), smoothed for display/resolution (stability across revolutions).

## Instrumentation Added

Under `ENABLE_DETAILED_TIMING` flag:
- Race condition detection (wraparound, large elapsed values)
- Angle jump detection (>90° jumps between frames)
- Pattern boundary flicker detection
- CSV output with frame timing, angle, RPM, resolution

## Current State

- Black snow: Eliminated
- Split pie: Significantly reduced (should be gone with lastActualInterval fix)
- Transposition glitches: Reduced (atomic snapshot helps)
- Slow RPM crash: Still needs investigation

## Future Considerations

1. The `lastSlot` variable should potentially reset when resolution changes or at revolution boundaries

2. Could add skip detection to log when angular slots are missed

3. May want to investigate the slow RPM crash separately - could be watchdog, stack overflow, or edge case in timing logic
