# POV Project Architecture Comparison

Quick reference guide for understanding the different POV display projects and their architectural patterns.

## Project Overview

### POV_Project (Current - this repository)
**Location**: `/Users/coryking/projects/POV_Project`
**Hardware**: ESP32-S3, 3 arms × 11 LEDs (33 total SK9822), single hall sensor
**Status**: Active development
**RPM Range**: 1200-1940 RPM measured

**Key Files**:
- `src/main.cpp` - Main rendering loop
- `include/RevolutionTimer.h` - RPM tracking and timing
- `include/RollingAverage.h` - Simple rolling average implementation
- `include/HallEffectDriver.h` (new) - FreeRTOS queue-based hall sensor
- `src/HallEffectDriver.cpp` (new) - ISR implementation

**Architecture Notes**:
- **Hybrid Arduino/FreeRTOS**: Uses Arduino `loop()` for rendering + FreeRTOS task for hall processing
- **Current branch `feature/freertos-hall-processing`**: Testing dedicated hall sensor task
- **Main branch**: Flag-based ISR processing in loop() (prone to missed revolutions)

---

### POV_Top (Proven, stepper-based)
**Location**: `/Users/coryking/projects/POV_Top`
**Hardware**: Stepper motor control, LED strip via hardware SPI
**Status**: Proven working implementation

**Key Files to Study**:
- `src/main.cpp` - **EMPTY loop()** - all work done in tasks
- `src/HallEffectDriver.cpp` - Queue-based ISR pattern (queue size 1)
- `src/HallEffectDriver.h` - ISR + queue interface
- `src/StepIntervalCalculator.cpp` - Task that processes hall events
- `lib/RPMLib/RPMLib.h` - Rolling average with multi-magnet support
- `lib/RPMLib/ExponentialMovingAverage.h` - EMA implementation (commented out but available)
- `lib/RPMLib/RollingAverage.h` - Early-fill optimization (tracks `sampleCount`)
- `include/config.h` - `RPM_SMOOTHING_LEVEL = 20`, `NUM_MAGNETS = 2`

**Architecture Pattern**:
```
ISR (minimal)
  ↓ xQueueOverwriteFromISR (queue size 1)
  ↓
FreeRTOS Task (dedicated, high priority)
  ↓ processes timestamp immediately
  ↓
RPMLib (rolling average: 20 revolutions × 2 magnets = 40 samples)
```

**Key Learnings**:
- Pure task-based architecture (no Arduino loop)
- Queue size 1 = natural debouncing (latest timestamp always wins)
- ISR does minimal work: capture timestamp, post to queue
- Multi-magnet sampling (2 triggers/revolution) = 2× sample rate
- 40-sample rolling average = ~1 second of history at typical RPM

---

### POV_IS_COOL (Motor controller with tachometer)
**Location**: `/Users/coryking/projects/POV_IS_COOL`
**Hardware**: L298N motor driver, hall sensor tachometer, OLED display, rotary encoder
**Status**: Working motor control system

**Key Files to Study**:
- `src/main.cpp` - **EMPTY loop()** (except debug modes) - all work in tasks
- `src/Tachometer.cpp` - Hall sensor processing pattern
- `src/Tachometer.h` - Tachometer class interface
- `src/SpeedController.cpp` - PID control using RPM feedback
- `src/MotorController.cpp` - PWM motor control
- `src/DisplayHandler.cpp` - OLED display task

**Architecture Pattern**:
```
ISR (Tachometer::onHallSensorTriggerISR)
  ↓ xQueueOverwriteFromISR (queue size 1)
  ↓
FreeRTOS Task (queueListener, high priority)
  ↓ processes timestamp via RPMSmoother
  ↓
RPMLib (same as POV_Top)
  ↓
SpeedController (PID loop task)
```

**Key Learnings**:
- Pure FreeRTOS architecture with multiple communicating tasks
- Queue-based inter-task communication (queues for setpoint, current RPM, motor speed)
- Hall sensor processing identical to POV_Top pattern
- PID control loop runs in separate task
- All tasks coordinated via queues (no shared state)

---

### POV3 (ESP-IDF native, no Arduino)
**Location**: `/Users/coryking/projects/POV3`
**Hardware**: Similar to POV_Top
**Status**: ESP-IDF native implementation

**Key Files to Study**:
- `src/main.cpp` - Uses `app_main()` (ESP-IDF), **no Arduino loop at all**
- `src/HallEffectDriver.cpp` - Same queue pattern as POV_Top
- `src/StepIntervalCalculator.cpp` - Task-based processing

**Architecture Pattern**:
- Pure ESP-IDF (no Arduino framework)
- Identical queue + task pattern for hall sensor
- Proves the pattern works outside Arduino framework

**Key Learnings**:
- Arduino `loop()` is NOT required for ESP32 projects
- FreeRTOS is the native RTOS (Arduino is just a wrapper)
- Same hall sensor patterns work in pure ESP-IDF

---

## Hall Sensor ISR Patterns Comparison

### Pattern 1: Flag-based (POV_Project main branch - PROBLEMATIC)
```cpp
// ISR
volatile bool flag = false;
volatile timestamp_t timestamp = 0;
void IRAM_ATTR hallISR() {
    timestamp = esp_timer_get_time();
    flag = true;
}

// loop()
if (flag) {
    flag = false;
    revTimer.addTimestamp(timestamp);
}
```

**Problem**: If ISR fires twice before loop processes, second timestamp overwrites first.
**Result**: Missed revolutions → double-interval samples → jittery RPM

---

### Pattern 2: Queue-based (POV_Top/POV_IS_COOL/POV3 - PROVEN)
```cpp
// ISR
QueueHandle_t queue; // size 1
void IRAM_ATTR hallISR() {
    HallEffectEvent event = { .timestamp = esp_timer_get_time() };
    BaseType_t woken = pdFALSE;
    xQueueOverwriteFromISR(queue, &event, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// FreeRTOS Task (priority > loop priority)
void hallTask(void* params) {
    while(1) {
        HallEffectEvent event;
        xQueueReceive(queue, &event, portMAX_DELAY); // blocks
        revTimer.addTimestamp(event.timestamp);
    }
}
```

**Advantage**: Task wakes immediately, processes every revolution even if rendering is busy
**Result**: Clean interval samples → stable RPM measurement

---

## RPM Filtering Comparison

### POV_Project
- **Current**: `RollingAverage<double, 20>` - 20 revolutions
- **Sampling**: 1 sample per revolution (single hall sensor)
- **Window size**: ~0.6-1 second at 1200-1940 RPM
- **Issue**: Missing early-fill optimization (averages with zeros during startup)

### POV_Top
- **Filter**: `RollingAverage<double, 40>` - 40 samples (20 revolutions × 2 magnets)
- **Sampling**: 2 samples per revolution (dual hall sensor or 2-magnet wheel)
- **Window size**: ~1 second at typical RPM
- **Has**: Early-fill optimization (`sampleCount` tracks actual samples)
- **Alternative**: `ExponentialMovingAverage<T>` available but commented out

### POV_IS_COOL
- **Same as POV_Top**: Uses shared `RPMLib` implementation

---

## Loop vs. Task-based Rendering

### POV_Project (Current - Hybrid)
```cpp
void loop() {
    // Check rotation state
    // Calculate arm angles
    // Render effect to LEDs
    strip.Show(); // ← BLOCKS for ~44μs during SPI transfer
}
```
**Issue**: While loop() is in `strip.Show()`, ISR can fire multiple times
**Fix attempt**: Add high-priority task for hall processing (current branch)

### POV_Top/POV_IS_COOL/POV3 (Pure Task-based)
```cpp
void loop() {
    // EMPTY - or just delay()
}

// Rendering happens in FreeRTOS task
void renderTask(void* params) {
    while(1) {
        // Calculate angles
        // Render
        // Show
    }
}
```
**Advantage**: Complete control over task priorities and scheduling
**Complexity**: More setup, need to manage task lifecycle

---

## Architecture Decision Tree

### When to use Flag-based ISR
- ✅ Simple projects with no rendering or blocking operations in loop()
- ✅ Slow RPM (> 100ms per revolution)
- ❌ Fast rendering loops that might miss ISR events

### When to use Queue + Task
- ✅ Fast hall sensor events (< 50ms intervals)
- ✅ Blocking operations in loop() (SPI, I2C, delays)
- ✅ Need guaranteed event processing
- ✅ Multi-task architecture

### When to use Pure Task-based (Empty Loop)
- ✅ Complex multi-task systems
- ✅ Need precise priority control
- ✅ Want ESP-IDF features without Arduino limitations
- ❌ Adds complexity for simple projects

---

## Testing Guide

### Verify Hall Sensor Processing
1. Add debug prints in ISR (use ESP_EARLY_LOG macros)
2. Print timestamp deltas to serial
3. Look for:
   - **Double intervals**: Sign of missed revolutions
   - **Consistent intervals**: Hall processing is keeping up
   - **Outliers**: Mechanical jitter or sensor bounce

### Check for Task Interference
1. Monitor task stats: `vTaskGetRunTimeStats()`
2. Check for watchdog timeouts
3. Measure rendering frame rate
4. Look for LED stuttering or flickering

### RPM Measurement Quality
1. Compare raw intervals vs. smoothed intervals
2. Plot RPM over time (should be smooth line, not jittery)
3. Check warm-up convergence time (should be < 2 seconds)
4. Verify stable reading when motor speed is constant

---

## Quick Reference: Files to Check

### Understanding Hall Sensor Processing
1. Start: `/Users/coryking/projects/POV_Top/src/HallEffectDriver.cpp`
2. Compare: `/Users/coryking/projects/POV_IS_COOL/src/Tachometer.cpp`
3. Current: `/Users/coryking/projects/POV_Project/src/HallEffectDriver.cpp`

### Understanding RPM Filtering
1. Library: `/Users/coryking/projects/POV_Top/lib/RPMLib/`
2. Current: `/Users/coryking/projects/POV_Project/include/RevolutionTimer.h`
3. Early-fill optimization: Compare `RollingAverage.h` between projects

### Understanding Task Architecture
1. Pure tasks: `/Users/coryking/projects/POV_Top/src/main.cpp` (empty loop)
2. Motor control: `/Users/coryking/projects/POV_IS_COOL/src/main.cpp` (task coordination)
3. ESP-IDF: `/Users/coryking/projects/POV3/src/main.cpp` (app_main)
4. Hybrid: `/Users/coryking/projects/POV_Project/src/main.cpp` (tight render loop)

---

## Common Pitfalls

### 1. Missed Revolutions (Double Intervals)
**Symptom**: RPM readings jump between correct value and half-speed
**Cause**: ISR fires twice before processing
**Fix**: Use queue + task pattern

### 2. Sluggish RPM Response
**Symptom**: Takes 5-10 seconds to converge to actual speed
**Cause**: Window size too large (20+ revolutions)
**Fix**: Reduce window to 8-10 samples, or use EMA

### 3. Jittery RPM Despite Smoothing
**Symptom**: Rolling average doesn't smooth the jitter
**Causes**:
- Averaging with zeros during startup (missing `sampleCount` tracking)
- Double intervals from missed revolutions polluting average
- Window size too small for mechanical variation
**Fix**: Add early-fill optimization, use queue+task, increase window, or add EMA

### 4. Task Watchdog Timeouts
**Symptom**: ESP32 resets with "Task watchdog got triggered" errors
**Cause**: Task blocked for too long (> 5 seconds)
**Fix**: Add `vTaskDelay()` or `taskYIELD()` in long loops

### 5. Priority Inversion
**Symptom**: Hall task doesn't preempt loop() as expected
**Cause**: Arduino loop runs at priority 1, need hall task at priority > 1
**Fix**: Set hall task priority to 3+ (current implementation uses 3)

---

## Next Steps for POV_Project

### Current Status (Branch: feature/freertos-hall-processing)
- Added HallEffectDriver (queue-based ISR)
- Added hallProcessingTask (priority 3)
- **Still using Arduino loop() for rendering** (hybrid architecture)

### Testing Checklist
- [ ] Flash and verify no watchdog timeouts
- [ ] Check serial output for double intervals
- [ ] Measure RPM convergence time (should be < 1 sec)
- [ ] Verify no LED stuttering during rendering
- [ ] Compare RPM stability vs. main branch

### If Hybrid Architecture Doesn't Work
**Option 1**: Move rendering to task, empty loop() (like POV_Top)
**Option 2**: Reduce window size + add EMA as secondary filter
**Option 3**: Accept the trade-offs, tune current approach

### If It Works Well
- Merge to main
- Consider adding EMA for even smoother RPM
- Document task priorities and stack sizes
- Add RPM logging for long-term stability testing
