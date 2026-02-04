# Dual-Core Pipeline Profiler

This document describes the queue-based profiler analytics system, output format, metrics, and how to interpret pipeline health.

## Profiling Modes

Two mutually exclusive profiling modes are available:

### Pipeline Profiler (`ENABLE_TIMING_INSTRUMENTATION`)

Queue-based analytics for production frame timing. Shows min/avg/max over 100 samples.

```bash
uv run pio run -e seeed_xiao_esp32s3_profiling
```

Output:
```
[RENDER] n=100 effect=1 acquire_us=0/2/15 render_us=45/78/156 queue_us=1/3/12 freeQ=0/1/2 readyQ=0/0/1
[OUTPUT] n=100 receive_us=1/8/45 copy_us=12/18/32 wait_us=45/187/312 show_us=52/54/58
```

### Effect Timing (`ENABLE_EFFECT_TIMING`)

Effect-specific internal breakdown via ESP_LOG. Each effect manages its own averaging.

```bash
uv run pio run -e seeed_xiao_esp32s3_effect_timing
```

Output (Radar):
```
[RADAR] TIMING: render=78, targets=3, sweep=1, phosphor=32, blips=6 (avg over 1000 frames)
```

### Why Mutually Exclusive?

Both modes output to serial. The pipeline profiler uses a queue-based system to prevent interleaving; effect timing uses direct ESP_LOG. Running both simultaneously causes garbled output. Choose the mode appropriate for what you're investigating.

## Dual-Core Pipeline Overview

The POV display uses a dual-core render pipeline:

| Name | ESP32 Core | Task | Role |
|------|------------|------|------|
| **Render** | Core 1 | `RenderTask` | Producer - calculates slots, renders effects |
| **Output** | Core 0 | `OutputTask` | Consumer - copies to strip, waits for angle, fires DMA |
| **Analytics** | Core 0 | `analyticsTask()` | Low priority - aggregates profiler samples, prints stats |

### BufferManager Coordination Pattern

The pipeline uses BufferManager with binary semaphores for coordination:

```
BufferManager:
  buffers_[2]         →  two RenderContext buffers
  freeSignal_[2]      →  binary semaphore per buffer (free for writing)
  readySignal_[2]     →  binary semaphore per buffer (ready for reading)

RenderTask:  acquireWriteBuffer() → render → releaseWriteBuffer(targetTime)
OutputTask:  acquireReadBuffer() → copy → release → wait → show
```

Both tasks block on semaphore acquisition if buffers aren't in the expected state. This provides proper backpressure - if one core is slow, the other blocks waiting.

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
[RENDER] n=100 effect=3 waitForWriteBuffer_us=min/avg/max render_us=min/avg/max queue_us=min/avg/max
[OUTPUT] n=100 waitForReadBuffer_us=min/avg/max copy_us=min/avg/max wait_us=min/avg/max show_us=min/avg/max
[RESOLUTION_CHANGE] from=1.5 to=3.0 render_avg=156 output_avg=89 usec_per_rev=40000
```

### Example Output

```
[RENDER] n=100 effect=1 waitForWriteBuffer_us=0/2/15 render_us=45/78/156 queue_us=1/3/12
[OUTPUT] n=100 waitForReadBuffer_us=1/8/45 copy_us=12/18/32 wait_us=45/187/312 show_us=52/54/58
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
| `waitForWriteBuffer_us` | Time blocked waiting for write buffer (Output slow indicator) |
| `render_us` | Time spent in effect `render()` |
| `queue_us` | Time to release buffer to BufferManager |

### Output Profiler (Core 0)

| Metric | Description |
|--------|-------------|
| `n` | Number of samples in this report (100 normally, less on resolution change) |
| `waitForReadBuffer_us` | Time blocked waiting for read buffer (Render slow indicator) |
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
[RENDER] n=100 effect=1 waitForWriteBuffer_us=0/2/15 render_us=45/78/156 queue_us=1/3/12
[OUTPUT] n=100 waitForReadBuffer_us=1/8/45 copy_us=12/18/32 wait_us=45/187/312 show_us=52/54/58
```

- **Low waitForWriteBuffer_us avg (< 50us):** Buffers immediately available
- **Low waitForReadBuffer_us avg (< 50us):** Frames immediately available
- **Positive wait_us avg:** Ahead of schedule (good!)

### What "Unhealthy" Looks Like

**Output-limited (Output can't keep up):**
```
[RENDER] n=100 effect=1 waitForWriteBuffer_us=200/450/800 render_us=45/78/156 queue_us=1/3/12
[OUTPUT] n=100 waitForReadBuffer_us=0/2/10 copy_us=12/18/32 wait_us=0/0/5 show_us=52/54/58
```

- High `waitForWriteBuffer_us` avg/max = Render frequently blocked waiting for buffer
- `wait_us` avg near 0 = consistently arriving late

**Render-limited (Render can't keep up):**
```
[RENDER] n=100 effect=1 waitForWriteBuffer_us=0/2/10 render_us=400/800/1200 queue_us=1/3/12
[OUTPUT] n=100 waitForReadBuffer_us=200/450/800 copy_us=12/18/32 wait_us=100/300/600 show_us=52/54/58
```

- High `waitForReadBuffer_us` avg/max = Output frequently blocked waiting for frame
- High `wait_us` avg = plenty of time to spare (Render is bottleneck)

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

If `waitForWriteBuffer_us` avg is consistently high (> 100us), we're not getting parallelism - we're running sequentially with extra overhead.

**Key insight:** Low `waitForWriteBuffer_us` avg AND low `waitForReadBuffer_us` avg = good pipelining. High in either = bottleneck on that side.

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
