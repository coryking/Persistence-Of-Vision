# Dual-Core Pipeline Profiler

This document describes the queue-based profiler analytics system, output format, metrics, and how to interpret pipeline health.

## Dual-Core Pipeline Overview

The POV display uses a dual-core render pipeline:

| Name | ESP32 Core | Task | Role |
|------|------------|------|------|
| **Render** | Core 1 | Arduino `loop()` | Producer - calculates slots, renders effects |
| **Output** | Core 0 | `outputTask()` | Consumer - copies to strip, waits for angle, fires DMA |
| **Analytics** | Core 0 | `analyticsTask()` | Low priority - aggregates profiler samples, prints stats |

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

## Analytics Architecture

The profiler uses a queue-based analytics pipeline to avoid garbled output from dual-core ESP_LOG calls:

```
Core 1 (Render)              Core 0 (Output)
     │                            │
     │    ProfilerSample          │
     └──────────┬─────────────────┘
                ▼
         g_analyticsQueue (32 slots)
                │
                ▼
         AnalyticsTask (Core 0, priority 1)
                │
                ▼
          Serial.printf()
```

Both Render and Output profilers send samples to a queue. A single consumer task aggregates samples and prints statistics every 100 frames. This eliminates character-level interleaving from concurrent ESP_LOG calls.

## Enabling Profiler Output

Build with the `seeed_xiao_esp32s3_profiling` environment:

```bash
uv run pio run -e seeed_xiao_esp32s3_profiling
```

This enables `ENABLE_TIMING_INSTRUMENTATION` which:
1. Activates the profiler classes
2. Creates the analytics queue (32 slots)
3. Starts the analytics task on Core 0 (priority 1)

## Profiler Output Format

The analytics task prints aggregated statistics every 100 samples:

```
[RENDER] n=100 effect=3 acquire_us=min/avg/max render_us=min/avg/max queue_us=min/avg/max freeQ=min/avg/max readyQ=min/avg/max
[OUTPUT] n=100 receive_us=min/avg/max copy_us=min/avg/max wait_us=min/avg/max show_us=min/avg/max
[RESOLUTION_CHANGE] from=1.5 to=3.0 render_avg=156 output_avg=89 usec_per_rev=40000
```

### Example Output

```
[RENDER] n=100 effect=1 acquire_us=0/2/15 render_us=45/78/156 queue_us=1/3/12 freeQ=0/1/2 readyQ=0/0/1
[OUTPUT] n=100 receive_us=1/8/45 copy_us=12/18/32 wait_us=45/187/312 show_us=52/54/58
```

### Resolution Change Events

When angular resolution changes (e.g., RPM change), the profiler prints partial stats and a transition event:

```
[RESOLUTION_CHANGE] from=1.5 to=3.0 render_avg=156 output_avg=89 usec_per_rev=40000
[RENDER] n=47 effect=3 ...  (partial stats before change)
[OUTPUT] n=47 ...
```

This lets you correlate render/output times with the new resolution setting.

## Metrics Reference

All timing metrics show **min/avg/max** over the sample window (100 frames).

### Render Profiler (Core 1)

| Metric | Description |
|--------|-------------|
| `n` | Number of samples in this report (100 normally, less on resolution change) |
| `effect` | Current effect index (0-N) |
| `acquire_us` | Time blocked waiting for free buffer (Output slow indicator) |
| `render_us` | Time spent in effect `render()` |
| `queue_us` | Time to send frame to Output queue |
| `freeQ` | Free buffer queue depth (0-2) |
| `readyQ` | Ready frame queue depth (0-2) |

### Output Profiler (Core 0)

| Metric | Description |
|--------|-------------|
| `n` | Number of samples in this report (100 normally, less on resolution change) |
| `receive_us` | Time blocked waiting for rendered frame (Render slow indicator) |
| `copy_us` | Time to copy pixels from RenderContext to strip buffer |
| `wait_us` | Time busy-waiting for target angle |
| `show_us` | Time for SPI transfer (`strip.Show()`) |

### Resolution Change Event

| Field | Description |
|-------|-------------|
| `from` | Previous angular resolution (degrees) |
| `to` | New angular resolution (degrees) |
| `render_avg` | Average render time before change (us) |
| `output_avg` | Average output time (copy+show) before change (us) |
| `usec_per_rev` | New microseconds per revolution |

## Interpreting Pipeline Health

### What "Healthy" Looks Like

```
[RENDER] n=100 effect=1 acquire_us=0/2/15 render_us=45/78/156 queue_us=1/3/12 freeQ=0/1/2 readyQ=0/0/1
[OUTPUT] n=100 receive_us=1/8/45 copy_us=12/18/32 wait_us=45/187/312 show_us=52/54/58
```

- **Low acquire_us avg (< 50us):** Buffers immediately available
- **Low receive_us avg (< 50us):** Frames immediately available
- **freeQ avg ~1, readyQ avg ~0:** Balanced pipeline
- **Positive wait_us avg:** Ahead of schedule (good!)

### What "Unhealthy" Looks Like

**Output-limited (Output can't keep up):**
```
[RENDER] n=100 effect=1 acquire_us=200/450/800 render_us=45/78/156 queue_us=1/3/12 freeQ=0/0/1 readyQ=0/1/2
[OUTPUT] n=100 receive_us=0/2/10 copy_us=12/18/32 wait_us=0/0/5 show_us=52/54/58
```

- High `acquire_us` avg/max = Render frequently blocked waiting for buffer
- `freeQ` avg near 0 = both buffers in use by Output
- `wait_us` avg near 0 = consistently arriving late

**Render-limited (Render can't keep up):**
```
[RENDER] n=100 effect=1 acquire_us=0/2/10 render_us=400/800/1200 queue_us=1/3/12 freeQ=1/2/2 readyQ=0/0/0
[OUTPUT] n=100 receive_us=200/450/800 copy_us=12/18/32 wait_us=100/300/600 show_us=52/54/58
```

- High `receive_us` avg/max = Output frequently blocked waiting for frame
- `freeQ` avg near 2 = both buffers sitting free (Render too slow)
- High `wait_us` avg = plenty of time to spare (Render is bottleneck)

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
effectiveTime = max(renderTime_avg, outputTime_avg)
minResolution = effectiveTime / microsecondsPerDegree
```

**Why max()?** In a parallel pipeline, throughput is limited by the slower stage.

**What's included in outputTime?** Only `copy_us + show_us`, NOT `wait_us`. Wait time is absorbed by parallelism (Core 1 renders during wait).

### When Parallelism Helps

Sequential frame time (from example above):
```
render + copy + wait + show = 78 + 18 + 187 + 54 = 337us (avg)
```

Parallel throughput:
```
max(render_avg, copy_avg+show_avg) = max(78, 72) = 78us
Improvement = 337/78 = 4.3x faster (wait absorbed!)
```

### When Parallelism Doesn't Help Much

If `render_avg >> copy_avg+show_avg` (render-limited), both cores serialize anyway:
```
max(800, 72) = 800us ≈ just render time
```

Use the profiler output to see if you're getting parallelism benefits.

## Justifying the Complexity

The parallel pipeline is worth the complexity when:

1. **copy+show is significant** relative to render time
2. **wait time is significant** (otherwise no time to absorb)
3. **angular resolution is a bottleneck** (need more slots per revolution)

If `acquire_us` avg is consistently high (> 100us), we're not getting parallelism - we're running sequentially with extra overhead.

**Key insight:** Low `acquire_us` avg AND low `receive_us` avg = good pipelining. High in either = bottleneck on that side.

## Grep-Friendly Output

All output is tagged for easy filtering:

```bash
# View only render stats
grep "\[RENDER\]" output.log

# View only output stats
grep "\[OUTPUT\]" output.log

# Track resolution changes
grep "\[RESOLUTION_CHANGE\]" output.log
```

This makes it easy to extract and analyze specific metrics over time.
