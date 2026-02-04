# Render/Output Pipeline Architecture - Design Document

## Overview

This document describes the dual-core render/output pipeline architecture for the POV display. The design splits rendering (Core 1) and output (Core 0) into separate tasks coordinated by a BufferManager, enabling parallel processing and roughly doubling angular resolution.

## Key Insights

1. **BufferManager decouples tasks** - RenderTask and OutputTask never know about each other. They communicate only through BufferManager using opaque handles.

2. **RenderTask owns timing** - RenderTask computes target times and angles. OutputTask is subordinate, executing what RenderTask prepared.

3. **Timing and buffer availability are orthogonal** - BufferManager handles "is a buffer available?" while RenderTask handles "when should this frame fire?"

4. **Release after copy, not after show** - OutputTask releases buffers immediately after copying to the LED strip's internal buffer, before waiting for target time. This maximizes parallelism since the RenderContext buffer is no longer needed after copy.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│  loop() - Housekeeping (Core 1, priority 1)                         │
│  - processCommands() (IR remote, effect switching)                  │
│  - Power state management (suspend/resume tasks)                    │
│  - Runs when RenderTask yields                                      │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│  RenderTask (Core 1, priority 2)                                    │
│  - Compute timing (slot, angle, target time)                        │
│  - Acquire write buffer from BufferManager                          │
│  - Render effect into buffer                                        │
│  - Release buffer to BufferManager with target time                 │
│  - Suspends when display disabled                                   │
└─────────────────────────────────────────────────────────────────────┘
        │
        │ BufferManager (opaque handles)
        │
┌─────────────────────────────────────────────────────────────────────┐
│  OutputTask (Core 0, priority 2)                                    │
│  - Acquire read buffer from BufferManager (with target time)        │
│  - Copy pixels to LED strip buffer                                  │
│  - Release buffer immediately (no longer needed)                    │
│  - Wait for target time                                             │
│  - Fire DMA (strip.Show())                                          │
│  - Suspends when display disabled                                   │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│  hallProcessingTask() - Hall Events (Core 1, priority 3)            │
│  - Feeds RevolutionTimer with timestamps                            │
│  - Already exists, no changes                                       │
└─────────────────────────────────────────────────────────────────────┘
```

## BufferManager Design

### Responsibilities

- **Buffer ownership** - Owns the two RenderContext buffers
- **State tracking** - Tracks which buffer is free, ready, or in-use
- **Synchronization** - Hides FreeRTOS primitives (binary semaphores)
- **Alternation logic** - Handles ping-pong pattern (buffer 0 → 1 → 0 → 1)
- **Opaque handles** - Tasks receive handles, not raw buffer indices

### Object Model

```cpp
class BufferManager {
public:
    // Opaque handle - tasks don't interpret this value
    using BufferHandle = uint8_t;  // Internally 0 or 1, but tasks treat as opaque

    struct WriteBuffer {
        BufferHandle handle;      // Opaque - hand back to releaseWriteBuffer()
        RenderContext* ctx;       // Buffer to render into
    };

    struct ReadBuffer {
        BufferHandle handle;      // Opaque - hand back to releaseReadBuffer()
        RenderContext* ctx;       // Buffer to read from
        timestamp_t targetTime;   // When to fire DMA (computed by RenderTask)
    };

    // Initialization
    void init();

    // For RenderTask
    WriteBuffer acquireWriteBuffer(TickType_t timeout);
    void releaseWriteBuffer(BufferHandle handle, timestamp_t targetTime);

    // For OutputTask
    ReadBuffer acquireReadBuffer(TickType_t timeout);
    void releaseReadBuffer(BufferHandle handle);

private:
    RenderContext buffers_[2];
    timestamp_t targetTimes_[2];

    // Per-buffer binary semaphores (4 total)
    SemaphoreHandle_t freeSignal_[2];   // Signaled when buffer N is free for writing
    SemaphoreHandle_t readySignal_[2];  // Signaled when buffer N is ready for reading

    // Internal alternation state (hidden from tasks)
    uint8_t nextWriteBuffer_ = 0;
    uint8_t nextReadBuffer_ = 0;
};
```

### FreeRTOS Primitives

**Why binary semaphores, not task notifications?**

Task notifications are per-task, not per-buffer. We need to independently signal:
- "Buffer 0 is free" vs "Buffer 1 is free"
- "Buffer 0 is ready" vs "Buffer 1 is ready"

Four binary semaphores (one per buffer × two states) provide exactly this granularity.

**Initialization:**

```cpp
void BufferManager::init() {
    for (int i = 0; i < 2; i++) {
        freeSignal_[i] = xSemaphoreCreateBinary();
        readySignal_[i] = xSemaphoreCreateBinary();
        xSemaphoreGive(freeSignal_[i]);  // Both buffers start FREE
        // readySignal_[i] starts empty (no buffers ready initially)
    }
}
```

**Implementation:**

```cpp
WriteBuffer BufferManager::acquireWriteBuffer(TickType_t timeout) {
    uint8_t buf = nextWriteBuffer_;

    // Block until this buffer is free
    if (xSemaphoreTake(freeSignal_[buf], timeout) != pdTRUE) {
        return {0xFF, nullptr};  // Timeout
    }

    // Alternate for next call (internal state, tasks don't see this)
    nextWriteBuffer_ = 1 - nextWriteBuffer_;

    return {buf, &buffers_[buf]};
}

void BufferManager::releaseWriteBuffer(BufferHandle handle, timestamp_t targetTime) {
    targetTimes_[handle] = targetTime;
    xSemaphoreGive(readySignal_[handle]);  // Signal buffer is ready for reading
}

ReadBuffer BufferManager::acquireReadBuffer(TickType_t timeout) {
    uint8_t buf = nextReadBuffer_;

    // Block until this buffer is ready
    if (xSemaphoreTake(readySignal_[buf], timeout) != pdTRUE) {
        return {0xFF, nullptr, 0};  // Timeout
    }

    nextReadBuffer_ = 1 - nextReadBuffer_;

    return {buf, &buffers_[buf], targetTimes_[buf]};
}

void BufferManager::releaseReadBuffer(BufferHandle handle) {
    xSemaphoreGive(freeSignal_[handle]);  // Signal buffer is free for writing
}
```

### Buffer State Machine

Each buffer cycles through states:

```
FREE ──────► WRITING ──────► READY ──────► READING ──────► FREE
   (acquire)   (render)   (release)   (acquire)   (release)
```

With double buffering, the ping-pong pattern ensures the two buffers are always at different stages:

```
Time    Buffer 0 State       Buffer 1 State
────    ──────────────       ──────────────
T0      WRITING              FREE
T1      READY                WRITING
T2      READING              READY
T3      FREE                 READING
T4      WRITING              FREE
...     (repeats)            (repeats)
```

## RenderTask Design

### Responsibilities

- **Timing computation** - Calculate slot number, angle, target time
- **Skip detection** - Recognize when behind schedule and skip frame
- **Effect invocation** - Call `effectManager.current()->render()`
- **Buffer coordination** - Use BufferManager to get/release buffers
- **Does NOT** - Know about OutputTask, manage buffers directly, or track buffer indices

### Task Loop

```cpp
class RenderTask {
public:
    void start();
    void stop();
    void suspend();   // For display power-off
    void resume();    // For display power-on

private:
    static void taskFunction(void* params);
    void run();

    TaskHandle_t handle_ = nullptr;
    static constexpr UBaseType_t PRIORITY = 2;
    static constexpr uint32_t STACK_SIZE = 8192;
    static constexpr BaseType_t CORE = 1;
};

void RenderTask::run() {
    int lastRenderedSlot = -1;

    while (true) {
        // 1. Get timing snapshot
        TimingSnapshot timing = revTimer.getTimingSnapshot();

        // 2. Check if rotating/warmup complete
        if (!timing.isRotating || !timing.warmupComplete) {
            lastRenderedSlot = -1;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 3. Calculate next slot (RenderTask owns timing)
        SlotTarget target = calculateNextSlot(lastRenderedSlot, timing);

        // 4. Check if behind schedule
        timestamp_t now = esp_timer_get_time();
        if (now > target.targetTime) {
            lastRenderedSlot = target.slotNumber;
            vTaskDelay(0);  // Yield to prevent watchdog
            continue;
        }

        // 5. Acquire write buffer (blocks if both in use)
        auto wb = bufferManager.acquireWriteBuffer(pdMS_TO_TICKS(100));
        if (wb.ctx == nullptr) {
            // Timeout - skip this slot
            lastRenderedSlot = target.slotNumber;
            continue;
        }

        // 6. Populate context and render
        populateRenderContext(*wb.ctx, target, timing);
        effectManager.current()->render(*wb.ctx);

        // 7. Release buffer with target time
        bufferManager.releaseWriteBuffer(wb.handle, target.targetTime);

        lastRenderedSlot = target.slotNumber;
    }
}
```

**Key points:**

- No `buf = 1 - buf` logic - BufferManager handles alternation
- Just acquire, render, release - simple loop
- Timing computation happens before acquiring buffer
- Opaque handle passed back to BufferManager

## OutputTask Design

### Responsibilities

- **Buffer acquisition** - Get ready buffer from BufferManager
- **Pixel copy** - Copy from RenderContext to LED strip's internal buffer
- **Timing gate** - Wait for target time computed by RenderTask
- **DMA trigger** - Fire `strip.Show()` at precise moment
- **Buffer release** - Signal BufferManager that buffer is free
- **Does NOT** - Compute timing, know about RenderTask, or track buffer indices

### Task Loop

```cpp
class OutputTask {
public:
    void start();
    void stop();
    void suspend();
    void resume();

private:
    static void taskFunction(void* params);
    void run();

    TaskHandle_t handle_ = nullptr;
    static constexpr UBaseType_t PRIORITY = 2;
    static constexpr uint32_t STACK_SIZE = 8192;
    static constexpr BaseType_t CORE = 0;
};

void OutputTask::run() {
    while (true) {
        // 1. Acquire read buffer (blocks until ready)
        auto rb = bufferManager.acquireReadBuffer(pdMS_TO_TICKS(100));
        if (rb.ctx == nullptr) {
            continue;  // Timeout
        }

        // 2. Copy to LED strip's internal buffer
        copyPixelsToStrip(*rb.ctx, strip);

        // 3. Release buffer IMMEDIATELY after copy
        // RenderContext no longer needed - strip has its own buffer for DMA
        bufferManager.releaseReadBuffer(rb.handle);

        // 4. Wait for target time (computed by RenderTask)
        waitForTargetTime(rb.targetTime);

        // 5. Fire DMA at precise moment
        strip.Show();

        taskYIELD();
    }
}
```

**Key points:**

- Release after copy, NOT after show - maximizes parallelism
- No timing computation - just executes what RenderTask prepared
- Opaque handle received from and returned to BufferManager
- No alternation logic

### Why Release After Copy?

**The buffer chain:**
1. RenderContext buffer (in BufferManager) - holds rendered pixels
2. LED strip's internal buffer (in NeoPixelBus) - copied from RenderContext
3. DMA reads from LED strip buffer, not RenderContext

**After `copyPixelsToStrip()`:**
- RenderContext buffer is no longer needed
- Strip buffer holds the pixels for DMA
- Safe to release RenderContext immediately

**Parallelism gained:**
```
With release-after-copy:
RenderTask: [render A][render B][render A][render B]
OutputTask:     [copy A][wait][show]
                        [copy B][wait][show]
                                [copy A][wait][show]

With release-after-show:
RenderTask: [render A]       [render B]       [render A]
OutputTask:     [copy A][wait][show]
                             [copy B][wait][show]
                                          [copy A][wait][show]
```

Release-after-copy allows RenderTask to start the next frame while OutputTask is waiting/showing, improving parallelism.

## Timing Flow

### Single-Core Baseline (before split)

```
[compute target] → [render] → [copy] → [wait for time] → [show DMA]
└────────────── Sequential, ~170µs total ───────────────────────────┘
Angular resolution = render_time + output_time
```

### Dual-Core with BufferManager

```
RenderTask:  [compute][render A]     [compute][render B]     [compute][render A]
                      ↓                        ↓                        ↓
BufferMgr:        (handoff)              (handoff)              (handoff)
                      ↓                        ↓                        ↓
OutputTask:          [copy][wait][show]     [copy][wait][show]     [copy][wait][show]
                                      ↑                        ↑
                               (buffer freed)          (buffer freed)
```

**Angular resolution = max(render_time, output_time)**

If render ≈ output, gain is ~2x. If one dominates, gain is less but still better than sequential.

### Semaphore State Trace

Initial state: `free=[1,1], ready=[0,0]`

```
Time  Action                        State           Tasks Blocked
────  ────────────────────────────  ──────────────  ─────────────────
T0    Render: wait(free[0]) ✓       free=[0,1]      Output waiting
T1    Render: render buf 0          free=[0,1]      Output waiting
T2    Render: signal(ready[0])      ready=[1,0]     None
      Render: wait(free[1]) ✓       free=[0,0]      None
T3    Output: wait(ready[0]) ✓      ready=[0,0]     None
      Output: copy buf 0            ready=[0,0]     None
      Output: signal(free[0])       free=[1,0]      None
T4    Output: waiting/showing       free=[1,0]      Render on buf 1
T5    Render: render buf 1          free=[1,0]      Output waiting
T6    Render: signal(ready[1])      ready=[0,1]     None
      Render: wait(free[0]) ✓       free=[0,0]      None (just freed)
T7    Output: wait(ready[1]) ✓      ready=[0,0]     None
      Output: copy buf 1            ready=[0,0]     None
      Output: signal(free[1])       free=[0,1]      None
...   (pattern repeats)
```

The ping-pong pattern is self-sustaining. As long as both tasks alternate their internal buffer indices, the semaphores keep them synchronized.

## Integration with Existing Code

### What Changes

**NEW FILES:**
- `BufferManager.h` / `BufferManager.cpp` - Buffer coordination
- `RenderTask.h` / `RenderTask.cpp` - Render loop refactored from loop()
- `OutputTask.h` / `OutputTask.cpp` - Output loop refactored from outputTask()

**MODIFIED FILES:**
- `main.cpp` - Slim down to housekeeping + task instantiation
- `platformio.ini` - May need increased stack sizes if profiling

**REMOVED:**
- `g_freeBufferQueue` - Replaced by BufferManager semaphores
- `g_readyFrameQueue` - Replaced by BufferManager semaphores
- `FrameCommand` struct - Replaced by BufferManager's ReadBuffer

### What Stays the Same

- **EffectManager** - No changes, still handles effect switching
- **RevolutionTimer** - No changes, still provides timing snapshots
- **SlotTiming.h helpers** - `calculateNextSlot()`, `waitForTargetTime()`, `copyPixelsToStrip()` stay
- **hallProcessingTask** - No changes
- **Effect system** - Effects still render to RenderContext
- **FrameProfiler** - Minor changes to track new metrics (wait times instead of queue depths)

### Main.cpp Refactoring

**Before (current):**
```cpp
void loop() {
    // 100+ lines of render logic
}

void outputTask(void* params) {
    // 60+ lines of output logic
}
```

**After:**
```cpp
static BufferManager bufferManager;
static RenderTask renderTask;
static OutputTask outputTask;

void setup() {
    // ... existing init ...

    bufferManager.init();
    renderTask.start(bufferManager);
    outputTask.start(bufferManager);
}

void loop() {
    effectManager.processCommands();

    // Power state management
    if (!effectManager.isDisplayEnabled()) {
        renderTask.suspend();
        outputTask.suspend();
        // blank display
    } else {
        renderTask.resume();
        outputTask.resume();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
```

Clean separation: housekeeping in loop(), rendering in RenderTask, output in OutputTask.

## Task Interaction Summary

### What RenderTask Knows

- RevolutionTimer (for timing snapshots)
- EffectManager (for current effect)
- BufferManager (for buffer acquisition)
- SlotTiming helpers (calculateNextSlot, populateContext)

**Does NOT know:** OutputTask exists

### What OutputTask Knows

- BufferManager (for buffer acquisition)
- LED strip (for copy/show)
- SlotTiming helpers (waitForTargetTime, copyPixelsToStrip)

**Does NOT know:** RenderTask exists, how timing is computed

### What BufferManager Knows

- Both tasks' existence (holds TaskHandles? No, uses semaphores)
- Buffer state (free/ready tracking)
- Alternation logic (ping-pong pattern)

**Does NOT know:** Timing computation, effects, LED strips

Complete decoupling through BufferManager abstraction.

## Performance Characteristics

### Memory Overhead

**Binary semaphores:** 4 × ~80 bytes (FreeRTOS queue struct) = ~320 bytes

**Removed:** 2 queues × ~80 bytes = ~160 bytes saved

**Net increase:** ~160 bytes (negligible)

### Latency

**Acquire/release operations:** ~1-2µs (semaphore give/take)

**Compared to queue operations:** Similar or slightly faster

**Busy-wait in waitForTargetTime():** Unchanged (still necessary for precision)

### Throughput

**Theoretical maximum angular resolution:**
- At 1600 RPM (37.5ms/rev): `max(render_time, output_time)` per slot
- If render=100µs, output=70µs: 100µs per slot → 375 slots/rev
- Single-core: 170µs per slot → 220 slots/rev
- **Gain: ~1.7x** (not quite 2x due to render dominance)

**At balanced timing (render≈output=85µs):**
- Dual-core: 85µs per slot → 441 slots/rev
- Single-core: 170µs per slot → 220 slots/rev
- **Gain: ~2.0x**

## Error Handling and Edge Cases

### Buffer Timeout

If `acquireWriteBuffer()` or `acquireReadBuffer()` timeout:
- Task skips that frame
- Continues to next frame
- System recovers naturally (no cascading failure)

### Motor Speed Change

- RenderTask computes fresh timing each frame from RevolutionTimer snapshot
- Target time is based on current speed, not stale predictions
- System adapts naturally to RPM changes

### Display Power Off

- `loop()` calls `renderTask.suspend()` and `outputTask.suspend()`
- Buffers remain in their current state
- On power-on, tasks resume where they left off
- May skip a few frames during transition (acceptable)

### Task Watchdog

- RenderTask yields via `vTaskDelay(0)` when skipping frames
- OutputTask yields after each frame via `taskYIELD()`
- Both tasks regularly yield, preventing watchdog timeouts

## Testing Strategy

### Unit Testing (Not Required for Art Project)

BufferManager could be tested in isolation, but given this is a hobby project, integration testing via actual rotation is preferred.

### Integration Testing

1. **Spin test at ~1000 RPM** - Verify stable display, no artifacts
2. **Speed ramp (600 → 2000 RPM)** - Verify adaptive angular resolution
3. **Effect switching during rotation** - Verify no crashes or glitches
4. **Power cycling** - Verify clean suspend/resume

### Performance Validation

1. **Profiler CSV output** - Compare old queue depths vs new wait times
2. **Visual inspection** - Higher angular resolution should be visible
3. **Timing analysis** - Confirm render/output overlap in profiler data

## Summary

This architecture achieves:
1. **Clean separation of concerns** - RenderTask (timing), OutputTask (display), BufferManager (coordination)
2. **Zero coupling between tasks** - Opaque handles, no direct task-to-task communication
3. **Maximum parallelism** - Release-after-copy, overlapped render/output
4. **Minimal code changes** - Refactor existing logic, don't rewrite
5. **Robust synchronization** - Binary semaphores provide clear buffer states

The key insight: **BufferManager as the single point of coordination** eliminates circular dependencies and makes both tasks simple, focused, and testable.
