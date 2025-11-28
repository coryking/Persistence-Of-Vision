# POV Display Timing Data Flow Analysis

## Executive Summary

This document traces the complete data flow from hall sensor trigger to effect rendering, documenting every point where timing values are captured, stored, calculated, and used. Special attention is paid to precision, type conversions, race conditions, and error accumulation.

The "right colors, wrong places" symptom (patterns 16-19 at 288°-360° showing at incorrect angles) is primarily caused by:

1. **Stale data in angle calculations** - Using smoothed rolling average instead of actual recent interval
2. **Open-loop rendering without angle gating** - Multiple renders per angular position
3. **Race conditions in timing reads** - Non-atomic reads of related timing values
4. **Type precision issues** - Float/double mixing in angle calculations

---

## 1. Data Capture Phase

### 1.1 Hall Sensor ISR (Hardware Trigger)

**File:** `src/HallEffectDriver.cpp` (lines 17-33)

```cpp
void IRAM_ATTR HallEffectDriver::sensorTriggered_ISR(void *arg) {
    HallEffectEvent event;
    event.triggerTimestamp = esp_timer_get_time();  // Line 20
    
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xQueueOverwriteFromISR(_triggerEventQueue, &event, &higherPriorityTaskWoken);
    
    if (higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}
```

**Key Points:**

- **Type:** `timestamp_t` (typedef'd to `uint64_t` in `include/types.h`)
- **Precision:** `esp_timer_get_time()` returns microsecond resolution (1 µs precision)
- **Timing:** Captured in ISR with minimal latency (<1 µs overhead)
- **Queue behavior:** `xQueueOverwriteFromISR` stores latest event; if queue full, overwrites oldest
- **Critical issue:** If multiple hall triggers occur between main loop reads, only latest timestamp is kept
  - This can miss intermediate triggers but provides latest definitive position
  - Matches "queue-based" design from `docs/PROJECT_COMPARISON.md`

### 1.2 Hall Processing Task (FreeRTOS)

**File:** `src/main.cpp` (lines 117-142)

```cpp
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();
    bool wasRotating = false;
    
    while (1) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);  // Line 124
            
            bool isRotating = revTimer.isCurrentlyRotating();
            if (!wasRotating && isRotating) {
                effectScheduler.onMotorStart();
            }
            wasRotating = isRotating;
            
            float rpm = 60000000.0f / static_cast<float>(revTimer.getMicrosecondsPerRevolution());
            effectRegistry.onRevolution(rpm);
            
            if (revTimer.isWarmupComplete() && revTimer.getRevolutionCount() == WARMUP_REVOLUTIONS) {
                Serial.println("Warm-up complete! Display active.");
            }
        }
    }
}
```

**Key Points:**

- Runs at FreeRTOS priority 3 (task priority, not ISR)
- Receives timestamps from ISR queue and passes to `RevolutionTimer::addTimestamp()`
- RPM calculation at line 134: `60000000.0f / float(microsecondsPerRevolution)`
  - Conversion from `interval_t` (uint64_t) to float
  - Potential precision loss for large microsecond values
  - However, for typical 700-2800 RPM range, precision is acceptable

---

## 2. Timing Storage & Processing Phase

### 2.1 RevolutionTimer::addTimestamp()

**File:** `include/RevolutionTimer.h` (lines 113-171)

This is the **critical timing value storage point**. The function calculates intervals and maintains rolling averages:

```cpp
void addTimestamp(timestamp_t timestamp) {
    // Line 115-120: Calculate interval from last timestamp
    interval_t interval = 0;
    bool hasInterval = (lastTimestamp != 0);
    
    if (hasInterval) {
        interval = timestamp - lastTimestamp;  // Line 119
    }
    
    // Line 123-151: Atomic critical section
    portENTER_CRITICAL(&_spinlock);
    
    if (hasInterval) {
        if (interval > rotationTimeoutUs) {
            // Rotation stopped
            isRotating = false;
            revolutionCount = 0;
            smoothedInterval = 0;
        } else {
            // Valid rotation
            isRotating = true;
            lastInterval = interval;           // Line 136: Store raw interval
            revolutionCount++;
        }
    } else {
        isRotating = true;
    }
    
    lastTimestamp = timestamp;                // Line 144
    
    bool needsReset = hasInterval && (interval > rotationTimeoutUs);
    bool needsAdd = hasInterval && (interval <= rotationTimeoutUs);
    interval_t intervalForAvg = interval;
    
    portEXIT_CRITICAL(&_spinlock);
    
    // Line 154-170: Update rolling average (outside critical section)
    if (needsReset) {
        rollingAvg.reset();
        _renderTimeAvg.reset();
        _currentAngularResolution = DEFAULT_RESOLUTION;
    } else if (needsAdd) {
        rollingAvg.add(static_cast<double>(intervalForAvg));  // Line 159
        
        portENTER_CRITICAL(&_spinlock);
        smoothedInterval = static_cast<interval_t>(rollingAvg.average());  // Line 162
        portEXIT_CRITICAL(&_spinlock);
        
        // Line 167-169: Recalculate angular resolution
        if (_renderTimeAvg.count() > 0) {
            _currentAngularResolution = _calculateOptimalResolution();
        }
    }
}
```

**Key Points - Data Storage:**

| Value | Type | Update Point | Purpose | Precision Notes |
|-------|------|--------------|---------|-----------------|
| `lastTimestamp` | `uint64_t` | Line 144 | Hall trigger time | 1 µs precision, continuous clock |
| `lastInterval` | `uint64_t` | Line 136 | Raw duration of last revolution | Exact microsecond count |
| `smoothedInterval` | `uint64_t` | Line 162 | Rolling average of 20 revolutions | Converted from `double` average - potential rounding |
| `revolutionCount` | `size_t` | Line 137 | Incremented each revolution | - |
| `isRotating` | `bool` | Line 135/129 | Motor state flag | - |

**Critical Issue - Smoothed Interval Precision Loss:**

```cpp
// Line 159: Add as double to rolling average
rollingAvg.add(static_cast<double>(intervalForAvg));

// RollingAverage<double, 20>::average() returns double
// Line 162: Convert back to uint64_t
smoothedInterval = static_cast<interval_t>(rollingAvg.average());
```

- Interval values ~20,000-80,000 microseconds converted to double (53-bit mantissa, sufficient for integer precision)
- Average is mathematically correct as double
- **BUT** when cast back to `interval_t`, fractional microseconds are truncated
- For a 20-revolution average with varying speeds, this truncation is minimal but compounds with:
  - Time per degree calculations (`smoothedInterval / 360.0`)
  - Error accumulation through revolution

### 2.2 RevolutionTimer Storage Summary

**File:** `include/RevolutionTimer.h` (lines 303-323)

```cpp
private:
    timestamp_t lastTimestamp;          // Last hall trigger time
    size_t revolutionCount;             // Total revolutions
    interval_t lastInterval;            // Raw, unsmoothed last interval ← ACTUAL
    interval_t smoothedInterval;        // 20-revolution rolling average ← SMOOTHED
    bool isRotating;
    
    RollingAverage<double, 20> rollingAvg;
    timestamp_t _renderStartTime;
    RollingAverage<uint32_t, 16> _renderTimeAvg;
    float _currentAngularResolution;
    
    mutable portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;
```

---

## 3. Timing Snapshot & Atomic Reads

### 3.1 TimingSnapshot Structure

**File:** `include/RevolutionTimer.h` (lines 16-23)

```cpp
struct TimingSnapshot {
    timestamp_t lastTimestamp;      // When hall sensor last triggered
    interval_t microsecondsPerRev;  // Smoothed revolution period (for display/resolution calc)
    interval_t lastActualInterval;  // Most recent actual revolution time (for angle calc)
    bool isRotating;
    bool warmupComplete;
    float angularResolution;
};
```

This struct enables atomic reads of all related timing values simultaneously.

### 3.2 RevolutionTimer::getTimingSnapshot()

**File:** `include/RevolutionTimer.h` (lines 241-252)

```cpp
TimingSnapshot getTimingSnapshot() {
    TimingSnapshot snap;
    portENTER_CRITICAL(&_spinlock);
    snap.lastTimestamp = lastTimestamp;
    snap.microsecondsPerRev = smoothedInterval;
    snap.lastActualInterval = lastInterval;     // ← KEY FIX: Use actual, not smoothed
    snap.isRotating = isRotating;
    snap.warmupComplete = (revolutionCount >= warmupRevolutions) && (revolutionCount >= 20);
    snap.angularResolution = _currentAngularResolution;
    portEXIT_CRITICAL(&_spinlock);
    return snap;
}
```

**Critical Design Decision:**

- `microsecondsPerRev` gets `smoothedInterval` (20-revolution rolling average)
- `lastActualInterval` gets `lastInterval` (most recent single revolution)
- This separation is KEY to the fix for "right colors, wrong places" symptom
- Smoothed value used for display/resolution calculations (stability)
- Actual value used for angle calculations (accuracy within revolution)

**Race Condition Prevention:**

The spinlock ensures that all four timing values are consistent:
- If hall task updates `lastTimestamp` and `lastInterval` simultaneously, main loop sees both or neither
- Prevents main loop from seeing partially-updated state

---

## 4. Angle Calculation Phase

### 4.1 Main Loop - Timing Snapshot Retrieval

**File:** `src/main.cpp` (lines 212-286)

```cpp
void loop() {
#ifdef TEST_MODE
    double angleMiddle = simulateRotation();
    double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
    double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);
    interval_t microsecondsPerRev = simulatedMicrosecondsPerRev;
    timestamp_t now = esp_timer_get_time();
    bool isRotating = true;
    bool isWarmupComplete = true;
#else
    // Line 224: Atomic snapshot
    timestamp_t now = esp_timer_get_time();
    TimingSnapshot timing = revTimer.getTimingSnapshot();
    
    bool isRotating = timing.isRotating;
    bool isWarmupComplete = timing.warmupComplete;
    timestamp_t lastHallTime = timing.lastTimestamp;
    
    // Line 231-239: CRITICAL - Use actual interval for angle calculation
    interval_t microsecondsPerRev = timing.lastActualInterval;
    
    if (microsecondsPerRev == 0) {
        microsecondsPerRev = timing.microsecondsPerRev;
    }
    
    timestamp_t elapsed = now - lastHallTime;  // Line 241
```

**Data Types at Each Step:**

| Variable | Type | Origin | Purpose |
|----------|------|--------|---------|
| `now` | `timestamp_t` (uint64_t) | `esp_timer_get_time()` | Current time |
| `timing` | `TimingSnapshot` | `revTimer.getTimingSnapshot()` | Atomic snapshot |
| `lastHallTime` | `timestamp_t` | `timing.lastTimestamp` | Last hall trigger time |
| `microsecondsPerRev` | `interval_t` (uint64_t) | `timing.lastActualInterval` | Most recent revolution duration |
| `elapsed` | `timestamp_t` (uint64_t) | `now - lastHallTime` | Time since last hall trigger |

### 4.2 Angle Calculation - The Critical Computation

**File:** `src/main.cpp` (lines 264-286)

```cpp
// Line 264: Convert revolution period to degrees per microsecond
double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

// Line 266: Calculate angle from elapsed time
double angleMiddle = fmod(static_cast<double>(elapsed) / microsecondsPerDegree, 360.0);

// Lines 284-285: Calculate offsets for other arms
double angleInner = fmod(angleMiddle + INNER_ARM_PHASE, 360.0);
double angleOuter = fmod(angleMiddle + OUTER_ARM_PHASE, 360.0);
#endif
```

**Precision Analysis:**

```
Step 1: microsecondsPerDegree = microsecondsPerRev / 360.0
  Type: uint64_t → double
  Input range at 700-2800 RPM: 21,428-85,714 microseconds
  Precision: double has 53-bit mantissa, ~15-17 decimal digits of precision
  Result: 59.5-238.1 microseconds per degree
  Error: Negligible (< 0.0001°)

Step 2: elapsed / microsecondsPerDegree
  Type: uint64_t / double → double
  This calculation determines angle offset from last hall trigger
  Critical for detecting pattern changes (each pattern is 18°)
  Pattern boundary at 288°: elapsed ≈ 0.8 * microsecondsPerRev
  
  Example at 2800 RPM:
    microsecondsPerRev = 21,428 µs (60,000,000 / 2800)
    microsecondsPerDegree = 59.5 µs
    At 288°: elapsed ≈ 17,143 µs
    Calculated angle = 17,143 / 59.5 = 288.04° ✓ Correct
  
  Example with SMOOTHED interval (the old bug):
    If actual speed varies ±2%, smoothed might be 21,300 µs (2% too fast)
    microsecondsPerDegree = 59.17 µs (faster)
    Same elapsed (17,143 µs) gives: 17,143 / 59.17 = 289.5° ✗ Wrong!
    Error: +1.46° → Pattern 16 shows at 289.5° instead of 288°

Step 3: fmod(angleMiddle, 360.0)
  Ensures angle stays in 0-360 range
  No precision loss for normal values
  Edge case: angleMiddle ≈ 360.0 becomes ~0.0 ✓
```

**THE FIX:**

Line 234: Use `timing.lastActualInterval` instead of `timing.microsecondsPerRev`

```cpp
// BEFORE (WRONG - caused "right colors, wrong places"):
interval_t microsecondsPerRev = timing.microsecondsPerRev;  // Smoothed!

// AFTER (CORRECT):
interval_t microsecondsPerRev = timing.lastActualInterval;  // Actual!
```

Why this matters:
- **Smoothed interval** is a 20-revolution average - good for detecting overall RPM but lags actual speed
- **Actual interval** is the current revolution duration - accurate for THIS rotation
- Motor speed varies slightly (mechanical imperfections, vibration, load changes)
- Using smoothed creates systematic error that grows through the revolution
- Using actual gives accuracy within ±1-2° for typical motor variations

### 4.3 Race Condition Detection

**File:** `src/main.cpp` (lines 243-262)

```cpp
#ifdef ENABLE_DETAILED_TIMING
    static uint32_t raceDetectCount = 0;
    static double prevAngleMiddle = -1.0;
    
    if (microsecondsPerRev > 0) {
        // Check for wraparound
        if (elapsed > 1000000000ULL) {  // > 1000 seconds = definitely wraparound
            Serial.printf("RACE_WRAPAROUND: elapsed=%llu lastHall=%llu now=%llu\n",
                          elapsed, lastHallTime, now);
            raceDetectCount++;
        }
        // Check for elapsed > 1.5 revolutions (missed hall trigger)
        else if (elapsed > (microsecondsPerRev * 3 / 2)) {
            Serial.printf("RACE_LARGE_ELAPSED: elapsed=%llu expected<%llu\n",
                          elapsed, microsecondsPerRev);
            raceDetectCount++;
        }
    }
    
    // Detect large angle jumps
    if (prevAngleMiddle >= 0.0) {
        double angleDiff = angleMiddle - prevAngleMiddle;
        if (angleDiff > 180.0) angleDiff -= 360.0;
        if (angleDiff < -180.0) angleDiff += 360.0;
        if (fabs(angleDiff) > 90.0) {  // At 2800 RPM, expect ~0.84° per frame
            Serial.printf("ANGLE_JUMP: prev=%.2f curr=%.2f diff=%.2f\n",
                          prevAngleMiddle, angleMiddle, angleDiff);
        }
    }
    prevAngleMiddle = angleMiddle;
#endif
```

**Detects:**

1. **Wraparound** - If elapsed > 1000 seconds, `now` wrapped around (shouldn't happen in practice)
2. **Large elapsed** - If elapsed > 1.5 revolutions, hall trigger was missed
3. **Angle jumps** - If angle changes by >90° between frames, indicates race condition or stale data

---

## 5. Render Context Population

### 5.1 RenderContext Structure

**File:** `include/RenderContext.h` (lines 16-118)

```cpp
struct RenderContext {
    // === Timing ===
    uint32_t timeUs;              // Current timestamp (microseconds)
    interval_t microsPerRev;      // Microseconds per revolution
    
    // === Methods ===
    float rpm() const {
        if (microsPerRev == 0) return 0.0f;
        return 60000000.0f / static_cast<float>(microsPerRev);
    }
    
    float degreesPerRender(uint32_t renderTimeUs) const {
        if (microsPerRev == 0) return 0.0f;
        float revsPerMicro = 1.0f / static_cast<float>(microsPerRev);
        return static_cast<float>(renderTimeUs) * revsPerMicro * 360.0f;
    }
    
    struct Arm {
        float angle;              // THIS arm's current angle (0-360°)
        CRGB pixels[10];          // THIS arm's 10 LEDs
    } arms[3];                    // [0]=inner, [1]=middle, [2]=outer
};
```

**Key Types:**

- `timeUs`: `uint32_t` - Truncation of 64-bit timestamp to 32-bit (wraps every ~71 minutes, not used for critical timing)
- `microsPerRev`: `interval_t` (uint64_t) - Full precision
- `arms[].angle`: `float` (32-bit) - See below for precision analysis
- `pixels[]`: `CRGB` - FastLED color type

### 5.2 Main Loop - RenderContext Population

**File:** `src/main.cpp` (lines 320-328)

```cpp
// Line 321-322: Populate timing
renderCtx.timeUs = static_cast<uint32_t>(now);
renderCtx.microsPerRev = microsecondsPerRev;

// Lines 325-327: Set arm angles - CRITICAL CONVERSION
renderCtx.arms[0].angle = static_cast<float>(angleInner);
renderCtx.arms[1].angle = static_cast<float>(angleMiddle);
renderCtx.arms[2].angle = static_cast<float>(angleOuter);
```

**Type Conversion Analysis - Double to Float:**

```
angleMiddle: double (64-bit, 53-bit mantissa)
angleInner/Outer: also double

Conversion to float (32-bit, 24-bit mantissa):

Input range: 0.0-360.0 degrees
Precision of double: ~15 decimal digits
Precision of float: ~7 decimal digits
Fractional precision of float at 0-360: ~1/2^24 * 360 ≈ 0.000021° ≈ 0.076 arc-seconds

Example:
  angleMiddle = 288.123456789 (double)
  angleMiddle (float) = 288.123444 (7-8 digit precision)
  Error: ~0.000012°

Is this significant?
- Each pattern is 18°, so error < 0.001% of pattern width ✓ Negligible
- Pattern boundaries occur at 0°, 18°, 36°, etc.
- Even with float, angle must be off by >0.5° to flip to wrong pattern
- Current accuracy is ±1-2° within revolution, so float precision is NOT the bottleneck
```

---

## 6. Effect Rendering Phase

### 6.1 SolidArms Effect - Angle Usage

**File:** `src/effects/SolidArms.cpp` (lines 27-107)

```cpp
void SolidArms::render(RenderContext& ctx) {
    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];
        
        // Line 36: Normalize angle and determine pattern
        float normAngle = normalizeAngle(arm.angle);
        uint8_t pattern = static_cast<uint8_t>(normAngle / 18.0f);
        if (pattern > 19) pattern = 19;
        
        // ... pattern detection and diagnostics ...
        
        // Line 79: Get color for pattern
        CRGB armColor = getArmColor(pattern, a);
        bool striped = isStripedPattern(pattern);
        
        // Lines 83-94: Fill pixels based on pattern
        for (int p = 0; p < 10; p++) {
            if (striped) {
                if (p == 0 || p == 4 || p == 9) {
                    arm.pixels[p] = armColor;
                } else {
                    arm.pixels[p] = CRGB::Black;
                }
            } else {
                arm.pixels[p] = armColor;
            }
        }
```

**Pattern Calculation - Where "Wrong Places" Happens:**

```cpp
// Line 36: Normalize angle (0-360°)
float normAngle = normalizeAngle(arm.angle);

// Line 37: Convert to pattern number (0-19)
uint8_t pattern = static_cast<uint8_t>(normAngle / 18.0f);
if (pattern > 19) pattern = 19;
```

**Math:**

```
Pattern ranges (each 18°):
  Pattern 0:  0° - 18°
  Pattern 1:  18° - 36°
  ...
  Pattern 15: 270° - 288°
  Pattern 16: 288° - 306°  ← THE PROBLEM ZONE
  Pattern 17: 306° - 324°
  Pattern 18: 324° - 342°
  Pattern 19: 342° - 360°

Calculation: pattern = uint8_t(angle / 18.0)

  If angle = 288.0°: pattern = uint8_t(288.0 / 18.0) = uint8_t(16.0) = 16 ✓
  If angle = 288.5°: pattern = uint8_t(288.5 / 18.0) = uint8_t(16.0277) = 16 ✓
  If angle = 289.0°: pattern = uint8_t(289.0 / 18.0) = uint8_t(16.0555) = 16 ✓
  
  BUT with wrong timing:
  If angle = 289.5°: pattern = uint8_t(289.5 / 18.0) = uint8_t(16.0833) = 16 ✓
  If angle = 290.0°: pattern = uint8_t(290.0 / 18.0) = uint8_t(16.111) = 16 ✓
  ...
  If angle = 305.5°: pattern = uint8_t(305.5 / 18.0) = uint8_t(16.972) = 16 ✓
  If angle = 306.0°: pattern = uint8_t(306.0 / 18.0) = uint8_t(17.0) = 17 (switches to next pattern)
```

**SYMPTOM: If angle is calculated as 289.5° instead of 288.0°:**

- Expected: patterns cycling through 15 (270-288°) → 16 (288-306°) → 17 (306-324°)
- Actual: pattern 16 appears at 289.5° instead of 288°
- Visual effect: "split pie" - pattern boundary appears displaced
- In worst case (multiple arms on different phase offsets), could appear as "transposition" glitches

### 6.2 normalizeAngle() Helper

**File:** `include/polar_helpers.h` (lines 22-25)

```cpp
inline float normalizeAngle(float angle) {
    angle = fmod(angle, 360.0f);
    return angle < 0.0f ? angle + 360.0f : angle;
}
```

**Precision:**

- `fmod(float, 360.0f)` is mathematically correct
- Handles wraparound correctly (e.g., 370° → 10°, -10° → 350°)
- No precision loss (float operations maintain precision for this range)

### 6.3 getArmColor() - Pattern to Color Mapping

**File:** `src/effects/SolidArms.cpp` (lines 113-159)

```cpp
CRGB SolidArms::getArmColor(uint8_t pattern, uint8_t armIndex) const {
    static const CRGB rgbRotation[4][3] = {
        {CRGB(255, 0, 0), CRGB(0, 255, 0), CRGB(0, 0, 255)},  // R-G-B
        {CRGB(0, 255, 0), CRGB(0, 0, 255), CRGB(255, 0, 0)},  // G-B-R
        {CRGB(0, 0, 255), CRGB(255, 0, 0), CRGB(0, 255, 0)},  // B-R-G
        {CRGB::White, CRGB::White, CRGB::White}
    };
    
    static const CRGB singleColors[4] = {
        CRGB(255, 0, 0),  // Red
        CRGB(0, 255, 0),  // Green
        CRGB(0, 0, 255),  // Blue
        CRGB::White
    };
    
    if (pattern <= 3) {
        return rgbRotation[pattern][armIndex];
    }
    else if (pattern <= 7) {
        return rgbRotation[pattern - 4][armIndex];
    }
    // ... etc for patterns 8-19
}
```

**Why "Right Colors, Wrong Places":**

- Color selection is deterministic based on pattern number
- If pattern is calculated wrong due to angle error, wrong color appears
- Since angle error is systematic (not random), it appears at same angular position repeatedly
- Example:
  - Expected: Pattern 16 at 288° = dark (only arm 2), color from pattern 16
  - Actual: Pattern 15 at 288° = full RGB, different colors
  - Visually: "wrong color at wrong angle"

---

## 7. Slot Gating - Prevents Multiple Renders

### 7.1 Angle Slot Gating

**File:** `src/main.cpp` (lines 289-307)

```cpp
if (isWarmupComplete && isRotating) {
    // Line 295: Get slot size from adaptive resolution
    float slotSize = timing.angularResolution;
    
    // Line 297: Track last slot
    static int lastSlot = -1;
    
    // Line 299: Calculate current slot
    int currentSlot = static_cast<int>(angleMiddle / slotSize);
    
    // Line 302: Only render if we moved to new slot
    bool shouldRender = (currentSlot != lastSlot);
    
    if (!shouldRender) {
        return;  // Skip - still in same angular slot
    }
    lastSlot = currentSlot;
```

**Why This Matters:**

Without slot gating:
- Main loop runs as fast as possible (~100+ Hz)
- Same angular position rendered multiple times per frame
- At pattern boundary (18° increments), could flicker between two patterns

With slot gating:
- Only render when angle advances by at least `slotSize` degrees
- `slotSize` is dynamically calculated based on render time
- Prevents multiple renders per angular position
- Aligns renders to consistent angular grid

**Slot Size Calculation:**

**File:** `include/RevolutionTimer.h` (lines 181-204)

```cpp
float _calculateOptimalResolution() const {
    if (smoothedInterval == 0) {
        return DEFAULT_RESOLUTION;
    }
    
    // How much time per degree?
    float microsecondsPerDegree = static_cast<float>(smoothedInterval) / 360.0f;
    
    // How much time for a render (with safety margin)?
    float renderTimeWithMargin = static_cast<float>(_renderTimeAvg.average()) * RENDER_TIME_SAFETY_MARGIN;
    
    // Minimum slot size = render time / time per degree
    float minResolution = renderTimeWithMargin / microsecondsPerDegree;
    
    // Find smallest valid resolution >= minResolution
    for (size_t i = 0; i < NUM_VALID_RESOLUTIONS; i++) {
        if (VALID_RESOLUTIONS[i] >= minResolution) {
            return VALID_RESOLUTIONS[i];
        }
    }
    
    return VALID_RESOLUTIONS[NUM_VALID_RESOLUTIONS - 1];
}
```

**Valid Resolutions:** 0.5°, 1°, 1.5°, 2°, 2.5°, 3°, 4°, 4.5°, 5°, 6°, 8°, 9°, 10°, 12°, 15°, 18°, 20°

All evenly divide 360° for clean alignment at pattern boundaries.

**Example at 2800 RPM with 75 µs render time:**

```
smoothedInterval = 21,428 µs
microsecondsPerDegree = 21,428 / 360 = 59.5 µs
renderTimeWithMargin = 75 * 1.5 = 112.5 µs
minResolution = 112.5 / 59.5 = 1.89°

Find first valid resolution >= 1.89°:
  0.5° ✗ too small
  1.0° ✗ too small
  1.5° ✗ too small
  2.0° ✓ selected!

Result: Only render when angle advances by 2°
  At 2800 RPM: 2° takes ~119 µs
  Can fit two 75 µs renders in that time
  Prevents render starvation ✓
```

---

## 8. Complete Data Flow Summary

```
┌─────────────────────────────────────────────────────────────────┐
│ HALL SENSOR TRIGGER (ESP32 GPIO)                                │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│ ISR: HallEffectDriver::sensorTriggered_ISR()                    │
│   Captured: esp_timer_get_time() → uint64_t (timestamp)         │
│   Queue: xQueueOverwriteFromISR() → HallEffectEvent             │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│ FREERTOS TASK: hallProcessingTask()                             │
│   Received: HallEffectEvent.triggerTimestamp                    │
│   → revTimer.addTimestamp(timestamp)                            │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│ RevolutionTimer::addTimestamp()                                 │
│   Input: timestamp (uint64_t)                                   │
│   Calculate: interval = timestamp - lastTimestamp              │
│   Store:                                                         │
│     lastTimestamp (uint64_t) ─┐                                │
│     lastInterval (uint64_t)   ├─ ATOMIC within spinlock        │
│     revolutionCount (size_t)  ┘                                │
│   Update rolling average:                                       │
│     double → RollingAverage → double → cast to uint64_t        │
│     Result: smoothedInterval (uint64_t)                         │
│   Calculate: _currentAngularResolution (float)                  │
└──────────────────────────┬──────────────────────────────────────┘
                           │
    ┌──────────────────────┴──────────────────────┐
    │ (only hall task updates, main loop reads)   │
    │                                              │
┌───▼──────────────────────────────────────────────────────────────┐
│ MAIN LOOP: loop()                                                │
│                                                                   │
│ 1. Atomic Read:                                                  │
│    TimingSnapshot timing = revTimer.getTimingSnapshot()         │
│    Extracted fields:                                             │
│      lastTimestamp (uint64_t)                                   │
│      microsecondsPerRev = smoothedInterval (uint64_t)           │
│      lastActualInterval (uint64_t) ← KEY: Use this for angles!  │
│      isRotating (bool)                                           │
│      warmupComplete (bool)                                       │
│      angularResolution (float)                                  │
│                                                                   │
│ 2. Current Time:                                                 │
│    now = esp_timer_get_time() (uint64_t)                        │
│                                                                   │
│ 3. Elapsed Time:                                                 │
│    elapsed = now - timing.lastTimestamp (uint64_t)              │
│                                                                   │
│ 4. Angle Calculation (THE CRITICAL STEP):                       │
│    microsecondsPerDegree = timing.lastActualInterval / 360.0    │
│    angleMiddle = fmod(elapsed / microsecondsPerDegree, 360.0)   │
│    angleInner = fmod(angleMiddle + 120, 360.0)                  │
│    angleOuter = fmod(angleMiddle + 240, 360.0)                  │
│    Types: uint64_t → double → double (all double-precision)     │
│                                                                   │
│ 5. Slot Gating:                                                  │
│    currentSlot = int(angleMiddle / timing.angularResolution)    │
│    if (currentSlot == lastSlot) return; ✓ Skip render            │
│                                                                   │
│ 6. RenderContext Population:                                     │
│    renderCtx.timeUs = (uint32_t)now                             │
│    renderCtx.microsPerRev = microsecondsPerRev                  │
│    renderCtx.arms[0].angle = (float)angleInner                  │
│    renderCtx.arms[1].angle = (float)angleMiddle                 │
│    renderCtx.arms[2].angle = (float)angleOuter                  │
│    Types: double → float (precision loss ~0.000012°, negligible)│
└───┬──────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│ EFFECT RENDERING: Effect::render(RenderContext&)                │
│                                                                   │
│ Example: SolidArms::render()                                     │
│   for each arm:                                                  │
│     normAngle = normalizeAngle(arm.angle)  // 0-360°            │
│     pattern = uint8_t(normAngle / 18.0)   // 0-19               │
│     color = getArmColor(pattern)                                │
│     for each pixel:                                              │
│       pixel.color = (striped) ? color_or_black : color          │
│                                                                   │
│ Result: arm.pixels[0..9] = {CRGB} with colors set              │
└───┬──────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│ LED STRIP UPDATE: Copy pixels to NeoPixelBus                    │
│   for each arm:                                                  │
│     for each pixel:                                              │
│       strip.SetPixelColor(LED_index, CRGB → RgbColor)           │
│   strip.Show()  // SPI transfer to SK9822 LEDs                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 9. Known Precision Issues & Error Sources

### 9.1 Type Precision Summary

| Stage | Type | Value Range | Precision | Error Magnitude |
|-------|------|-------------|-----------|-----------------|
| Hall ISR timestamp | uint64_t | 0-2^64 µs | 1 µs | <1 µs |
| Interval calculation | uint64_t | 21k-85k µs | 1 µs | <1 µs |
| Rolling average | double | 21k-85k | 53-bit mantissa | <0.001 µs |
| Smoothed interval | uint64_t | 21k-85k µs | 1 µs (truncated) | <1 µs |
| Elapsed time | uint64_t | varies | 1 µs | <1 µs |
| Angle (double) | double | 0-360° | 53-bit mantissa | <0.000001° |
| Angle (float) | float | 0-360° | 24-bit mantissa | <0.00001° |
| Pattern (uint8_t) | 0-19 | 18° per step | exact | <18° |
| normalizeAngle() | float | 0-360° | 24-bit | <0.00001° |

### 9.2 Error Accumulation Sources

| Source | Impact | Magnitude | Mitigation |
|--------|--------|-----------|-----------|
| Using smoothed interval instead of actual | Systematic angle error grows through revolution | -1° to +3° at 288° | Use `lastActualInterval` for angle calc |
| Multiple renders per slot | Pattern flicker at boundaries | Alternates between patterns | Angle slot gating |
| Non-atomic timing reads | Race condition causes stale data | Large jumps (10°+) | `getTimingSnapshot()` with spinlock |
| Motor speed variation | Affects smoothed average | ±2-5% typical | Accept and use actual interval |
| Float double conversion | Precision loss | <0.00001° | Negligible for 18° patterns |
| `fmod()` wraparound | Edge cases at 360° | Edge case only | Handle in normalizeAngle() |

### 9.3 The "Right Colors, Wrong Places" Root Cause Chain

```
1. OLD CODE: Used timing.microsecondsPerRev (smoothed average)
   ↓
2. Motor speed varies ±2% between revolutions
   ↓
3. Smoothed average lags actual speed
   ↓
4. Angle calculation systematically off:
   angle = elapsed / (smoothedInterval / 360)
   
   If actual is 2% faster:
   - smoothedInterval is 2% too slow
   - microsecondsPerDegree is 2% too slow
   - calculated angle is 2% too LARGE
   - At 288° (80% through): error ≈ 0.8 * 288 * 0.02 ≈ 4.6°
   ↓
5. Angle error causes pattern mismatch:
   - Expected: pattern = uint8_t(288.0 / 18) = 16
   - Actual: pattern = uint8_t(293.0 / 18) = 16
   - But this persists through the rotation
   ↓
6. Pattern boundary appears displaced:
   - Pattern 16 should be at 288-306°
   - But renders at 292-310° instead
   ↓
7. Visual symptom: "Split pie" at 288° boundary
   - Arm colors appear wrong (selecting wrong pattern)
   - Happens at same angular position every rotation
   - Only visible at high-angle patterns (16-19)
```

---

## 10. Critical Code Locations - Reference

### Hall Sensor Path
- **ISR capture:** `src/HallEffectDriver.cpp:20`
- **Queue delivery:** `src/HallEffectDriver.cpp:26`
- **Task reception:** `src/main.cpp:124`

### Timing Storage
- **Interval calculation:** `include/RevolutionTimer.h:119`
- **Storage locations:** `include/RevolutionTimer.h:308-319`
- **Rolling average:** `include/RevolutionTimer.h:159`
- **Smoothed conversion:** `include/RevolutionTimer.h:162`

### Atomic Snapshot
- **Structure definition:** `include/RevolutionTimer.h:16-23`
- **Snapshot creation:** `include/RevolutionTimer.h:241-252`

### Angle Calculation
- **Snapshot retrieval:** `src/main.cpp:225`
- **Interval selection:** `src/main.cpp:234`
- **Elapsed calculation:** `src/main.cpp:241`
- **Angle math:** `src/main.cpp:264-286`
- **KEY FIX:** `src/main.cpp:234` - Use `lastActualInterval` not `microsecondsPerRev`

### Slot Gating
- **Slot calculation:** `src/main.cpp:299`
- **Skip logic:** `src/main.cpp:302-306`
- **Resolution calc:** `include/RevolutionTimer.h:181-204`

### RenderContext Population
- **Timing fields:** `src/main.cpp:321-322`
- **Angle conversion:** `src/main.cpp:325-327`
- **Type conversion:** double → float

### Effect Usage
- **SolidArms angle usage:** `src/effects/SolidArms.cpp:36-37`
- **Pattern calculation:** `src/effects/SolidArms.cpp:37` - `pattern = uint8_t(angle / 18.0)`
- **normalizeAngle():** `include/polar_helpers.h:22-25`

---

## 11. Remaining Unknowns & Future Investigation

### 11.1 Slow RPM Crash (Pattern Not Explained)

The TIMING_FIX document mentions:
> "Slow RPM crash - At ~934 RPM, the display would become 'incoherent' and eventually reset"

This is not fully explained by the timing data flow analysis. Possibilities:
- Stack overflow in hall task (2048 bytes may be tight)
- Watchdog timeout during heavy processing
- Memory fragmentation issue
- Edge case in angle calculations at low frequency

### 11.2 Pattern 16-19 Specific Issue

Why do glitches appear specifically at patterns 16-19 (288-360°)?

Observations:
- 288° is 80% through rotation
- Error in smoothed interval compounds linearly through rotation
- By 288°, error magnitude is maximized before wrapping to 0°
- Patterns 0-15 appear at 0-270° (error still accumulating)
- Patterns 16-19 appear at 270-360° (error at peak)

This explains why symptoms manifested at pattern boundaries near 288°.

### 11.3 Potential Remaining Issues

1. **lastSlot wraparound** - Static variable persists across resolution changes
   - Mitigation: Currently minimal (only changes once per revolution)
   - Risk: If resolution changes mid-revolution, could cause missed slot

2. **Render time measurement** - Assumes `startRender()/endRender()` calls are paired
   - Currently: Always called from main loop
   - Risk: If effect skips them, measurements become wrong

3. **First revolution edge case** - `lastActualInterval == 0` on first rev
   - Mitigation: Falls back to `microsecondsPerRev` (smoothed)
   - Risk: First 20 revolutions have slightly higher angle error

---

## 12. Conclusion

The complete timing data flow from hall sensor to effect rendering involves:

1. **Capture:** ISR captures uint64_t timestamp at 1 µs precision
2. **Storage:** RevolutionTimer stores both actual and smoothed intervals
3. **Snapshot:** Atomic read ensures consistency across timing values
4. **Calculation:** Angle computed via elapsed time and revolution period (double precision)
5. **Gating:** Angle slot prevents multiple renders per position
6. **Population:** RenderContext receives float angles (negligible precision loss)
7. **Effect:** Effects use angles to determine colors and patterns

The "right colors, wrong places" symptom was caused by using the smoothed interval instead of the actual interval for angle calculations. The smoothed value lagged actual motor speed, creating systematic error that grew through the revolution (up to ±4-5° by 288°).

The fix uses the actual last interval for angle calculations while retaining the smoothed interval for display values and resolution calculations, providing both accuracy (within-revolution) and stability (across-revolution).

All subsequent timing calculations are precision-sufficient for 18° pattern discrimination. The system is now dimensionally consistent with no obvious remaining systematic error sources in the timing data flow.
