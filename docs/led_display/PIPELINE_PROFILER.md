# Dual-Core Pipeline Profiler

This document describes the profiler output format, metrics, and how to interpret pipeline health.

## Dual-Core Pipeline Overview

The POV display uses a dual-core render pipeline:

| Name | ESP32 Core | Task | Role |
|------|------------|------|------|
| **Render** | Core 1 | Arduino `loop()` | Producer - calculates slots, renders effects |
| **Output** | Core 0 | `outputTask()` | Consumer - copies to strip, waits for angle, fires DMA |

### Two-Queue Buffer Pool Pattern

The pipeline uses a standard FreeRTOS producer-consumer pattern with two queues:

```
g_freeBufferQueue:  [0, 1]  →  initially both buffers free
g_readyFrameQueue:  []      →  initially empty

Render:  receive(freeQueue) → render → send(readyQueue)
Output:  receive(readyQueue) → copy/wait/show → send(freeQueue)
```

Both receives block until available. Both sends block if full. This provides proper backpressure - if one core is slow, the other blocks waiting.

### How Parallelism Works

While Core 0 outputs frame N, Core 1 renders frame N+1:

```
Time →

Core 1: [render N] [render N+1] [render N+2] ...
Core 0:            [output N]   [output N+1]  ...
```

The wait time (busy-wait for target angle) is absorbed by parallelism - Core 1 renders during Core 0's wait.

## Enabling Profiler Output

Build with the `seeed_xiao_esp32s3_profiling` environment:

```bash
uv run pio run -e seeed_xiao_esp32s3_profiling
```

This enables `ENABLE_TIMING_INSTRUMENTATION` which activates the profiler classes.

## Profiler Output Format

Two interleaved log lines per frame (every Nth frame, configurable):

```
RENDER: frame,effect,acquire_us,render_us,queue_us,freeQ,readyQ,slot,angle,usec_per_rev,slot_size,rev_count,angular_res
OUTPUT: frame,receive_us,copy_us,wait_us,show_us,freeQ,readyQ
```

### Example Output

```
RENDER: 100,1,2,180,3,1,0,45,450,20000,10,50,0.5
OUTPUT: 100,5,48,285,198,1,0
```

## Metrics Reference

### Render Profiler (Core 1)

| Metric | Description |
|--------|-------------|
| `frame` | Frame counter (correlates RENDER and OUTPUT lines) |
| `effect` | Effect index (0-N) |
| `acquire_us` | Time blocked waiting for free buffer (Output slow indicator) |
| `render_us` | Time spent in effect `render()` |
| `queue_us` | Time to send frame to Output queue |
| `freeQ` | Free buffer queue depth (0-2) |
| `readyQ` | Ready frame queue depth (0-2) |
| `slot` | Slot number within revolution |
| `angle` | Target angle in units (0-3599) |
| `usec_per_rev` | Microseconds per revolution |
| `slot_size` | Slot size in angle units |
| `rev_count` | Revolution counter |
| `angular_res` | Angular resolution in degrees |

### Output Profiler (Core 0)

| Metric | Description |
|--------|-------------|
| `frame` | Frame counter (correlates with RENDER line) |
| `receive_us` | Time blocked waiting for rendered frame (Render slow indicator) |
| `copy_us` | Time to copy pixels from RenderContext to strip buffer |
| `wait_us` | Time busy-waiting for target angle |
| `show_us` | Time for SPI transfer (`strip.Show()`) |
| `freeQ` | Free buffer queue depth (0-2) |
| `readyQ` | Ready frame queue depth (0-2) |

## Interpreting Pipeline Health

### What "Healthy" Looks Like

```
RENDER: 100,1,2,180,3,1,0,...     # acquire=2us, freeQ=1, readyQ=0
OUTPUT: 100,5,48,285,198,1,0     # receive=5us, freeQ=1, readyQ=0
```

- **Low acquire_us (< 50us):** Buffer was immediately available
- **Low receive_us (< 50us):** Frame was immediately available
- **freeQ=1, readyQ=0:** Balanced pipeline (one buffer in use, one free)
- **Positive wait_us:** Ahead of schedule (good!)

### What "Unhealthy" Looks Like

**Output-limited (Output can't keep up):**
```
RENDER: 100,1,450,180,3,0,1,...   # acquire=450us (!!) freeQ=0, readyQ=1
OUTPUT: 100,2,48,0,198,0,1       # receive=2us, wait=0us (late!)
```

- High `acquire_us` = Render blocked waiting for buffer
- `freeQ=0` = both buffers in use by Output
- `wait_us=0` = arrived late, no time to wait

**Render-limited (Render can't keep up):**
```
RENDER: 100,1,2,800,3,2,0,...     # acquire=2us, freeQ=2 (both free!)
OUTPUT: 100,450,48,600,198,1,0   # receive=450us (!!) Output starved
```

- High `receive_us` = Output blocked waiting for frame
- `freeQ=2` = both buffers sitting free (Render too slow)

### Queue Depth Interpretation

| freeQ | readyQ | Meaning |
|-------|--------|---------|
| 2 | 0 | Both free, Output starved (Render-limited) |
| 1 | 1 | Balanced, good pipelining |
| 1 | 0 | Normal, one in flight |
| 0 | 1 | Output working, Render blocked |
| 0 | 2 | Both rendered, Output behind (Output-limited) |

Note: `freeQ + readyQ` should always equal ~2 (accounting for in-flight frames).

## Angular Resolution Calculation

The pipeline uses parallel-aware resolution calculation:

```
effectiveTime = max(renderTime, outputTime)
minResolution = effectiveTime / microsecondsPerDegree
```

**Why max()?** In a parallel pipeline, throughput is limited by the slower stage.

**What's included in outputTime?** Only `copy_us + show_us`, NOT `wait_us`. Wait time is absorbed by parallelism (Core 1 renders during wait).

### When Parallelism Helps

Sequential frame time:
```
render + copy + wait + show = 180 + 48 + 285 + 198 = 711us
```

Parallel throughput:
```
max(render, copy+show) = max(180, 246) = 246us
Improvement = 711/246 = 2.9x faster (wait absorbed!)
```

### When Parallelism Doesn't Help Much

If `render >> copy+show` (render-limited), both cores serialize anyway:
```
max(800, 246) = 800us ≈ just render time
```

## Justifying the Complexity

The parallel pipeline is worth the complexity when:

1. **copy+show is significant** relative to render time
2. **wait time is significant** (otherwise no time to absorb)
3. **angular resolution is a bottleneck** (need more slots per revolution)

If `acquire_us` is consistently high, we're not getting parallelism - we're running sequentially with extra overhead.

**Key insight:** Low `acquire_us` AND low `receive_us` = good pipelining. High in either = bottleneck on that side.
