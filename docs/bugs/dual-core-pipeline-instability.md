# Dual-Core Render Pipeline Instability

**STATUS: RESOLVED** - Replaced queue-based architecture with BufferManager (binary semaphores). See `docs/led_display/RENDER_OUTPUT_ARCHITECTURE.md` for the new design.

## Problem Summary

The dual-core render pipeline experienced crashes, task watchdog timeouts, and garbled serial output with the original queue-based architecture.

## Symptoms

### 1. Task Watchdog Timeout on Output Task
```
task_wdt: 0: output
```
The output task (Core 0) triggers the task watchdog, indicating it's not yielding for extended periods.

### 2. Corrupted Serial Output
Serial output is garbled at the character level. Even panic/crash messages are corrupted:
```
(281: Tasks currently g.
```
This happens even in the `fake_hall_sensor` build (no profiling), though that build doesn't crash - just has "laggy" serial that prints in bursts (as in it bursts probably because esp-now only is used periodically to send telemetry, but the output is garbled)

### 3. ESP-NOW Messages Interleaved
```
eer
eer
om peer
om peer
er
o ACK from peer
rom peer
m peer
 onDataSent(): [ESPNOW] No ACK from peer
```
The "No ACK from peer" warning is getting fragmented/interleaved.

### 4. Backtraces Point to Pixel Copy Loop
Decoded backtraces show the crash occurs in:
- `copyPixelsToStrip()` in SlotTiming.h:133
- Specifically in NeoPixelBus's `applyPixelColor()`

This is a simple loop over 33 LEDs - should complete quickly.

## Build Configurations Tested

| Build | Effect Timing | Pipeline Profiler | Result |
|-------|---------------|-------------------|--------|
| `seeed_xiao_esp32s3_profiling` | OFF | ON | Crashes with task_wdt |
| `seeed_xiao_esp32s3_fake_hall_sensor` | OFF | OFF | Runs but garbled serial |
| Normal build | ON | OFF | "Works" (unconfirmed if stable) |

The presence of effect timing (~78us overhead per frame) seems to mask the issue.

## Architecture Changes Since Original Implementation

Original dual-core commit: `c980817` ("Implement dual-core render pipeline")

**Original design (c980817):**
```cpp
g_frameQueue = xQueueCreate(1, sizeof(FrameCommand));  // Depth 1
g_bufferFree[0] = xSemaphoreCreateBinary();            // Semaphores for buffer mgmt
g_bufferFree[1] = xSemaphoreCreateBinary();
xQueueSend(g_frameQueue, &cmd, 0);                     // Non-blocking send
g_writeBuffer = 1 - g_writeBuffer;                     // Simple buffer flip
```

**Current design:**
```cpp
g_freeBufferQueue = xQueueCreate(2, sizeof(uint8_t));      // Depth 2
g_readyFrameQueue = xQueueCreate(2, sizeof(FrameCommand)); // Depth 2
xQueueSend(g_readyFrameQueue, &cmd, portMAX_DELAY);        // Blocking send
// Buffer index flows through queues, no simple flip
```

**Other changes since c980817:**
- `Serial.print` replaced with `ESP_LOG` for "thread-safe" logging
- Queue-based profiler analytics added (separate task aggregates samples)
- Various profiler instrumentation points added

**Note:** It's unclear if the original c980817 implementation ever worked correctly - logging changes were made shortly after, potentially masking issues.

## What Was Tried

1. **Added `taskYIELD()` to output task** - Did not resolve the crash

2. **Examined ESP-NOW callback logging** - The `onDataSent()` callback logs warnings which could contribute to serial contention, but this doesn't explain the crash.  also esp-now errors are expected when the motor-controller is down... this is normal behavior and has always worked fine.

3. **Compared architectures** - Identified the semaphore→two-queue change but did not test reverting it (requires matching FrameProfiler API changes)

## Resolution

The queue-based architecture was replaced with a BufferManager using per-buffer binary semaphores. Key changes:

1. **BufferManager with binary semaphores** - One semaphore per buffer per state (free/ready), providing granular buffer state tracking
2. **RenderTask and OutputTask** - Refactored render/output logic into dedicated FreeRTOS tasks
3. **Release after copy** - OutputTask releases buffers immediately after copying to LED strip, maximizing parallelism
4. **Hall processing pinned to Core 1** - All Core 1 tasks now on same core for better cache locality

See `docs/led_display/RENDER_OUTPUT_ARCHITECTURE.md` for the full architecture.

## Key Questions (Resolved)

1. ~~Did the original c980817 dual-core implementation ever work?~~ **Moot - new architecture**
2. ~~Is the two-queue pattern fundamentally different in behavior from the semaphore pattern?~~ **Yes - queue overhead and state tracking were problematic**
3. ~~Why does effect timing overhead (~78us) mask the problem?~~ **Unknown, but irrelevant with new architecture**
4. ~~What's causing character-level serial corruption across all builds?~~ **Likely ESP_LOG mutex contention - resolved with BufferManager**
5. ~~is our new dual-buffer architecture fatally flawed?  is it even the right architecture?~~ **Old queue-based architecture was flawed, new BufferManager architecture is correct**

## Relevant Files

- `led_display/src/main.cpp` - Pipeline orchestration, outputTask, loop()
- `led_display/include/SlotTiming.h` - copyPixelsToStrip(), waitForTargetTime()
- `led_display/src/FrameProfiler.cpp` - Profiler with analytics queue
- `led_display/include/FrameProfiler.h` - Profiler interface

## Commits

- `c980817` - Original dual-core implementation
- `6d6523c` - Queue-based profiler analytics (changed buffer management?)
- `0fa51ff` - Current HEAD (mutual exclusion between profiler and effect timing)
