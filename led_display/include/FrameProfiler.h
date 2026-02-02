#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "geometry.h"         // For SlotTarget
#include "RevolutionTimer.h"  // For TimingSnapshot

/**
 * Dual-core frame timing profilers
 *
 * Split into two profilers that run on different cores:
 * - RenderProfiler (Core 1): Tracks render and queue timing in Arduino loop()
 * - OutputProfiler (Core 0): Tracks copy, wait, and show timing in output task
 *
 * Both use frameCount as correlation key for post-hoc analysis.
 * CSV output is interleaved; filter by prefix (RENDER/OUTPUT) to separate.
 *
 * When ENABLE_TIMING_INSTRUMENTATION is not defined, all methods compile to no-ops.
 */

#ifdef ENABLE_TIMING_INSTRUMENTATION

// ============================================================================
// Queue-Based Analytics Data Structures
// ============================================================================

enum class ProfilerSource : uint8_t { RENDER_CORE, OUTPUT_CORE };

struct RenderSample {
    uint32_t frameCount;
    uint8_t effectIndex;
    uint32_t acquireUs;
    uint32_t renderUs;
    uint32_t queueUs;
    uint8_t freeQueueDepth;
    uint8_t readyQueueDepth;
    float angularResolution;
    uint32_t microsecondsPerRev;
};

struct OutputSample {
    uint32_t frameCount;
    uint32_t receiveUs;
    uint32_t copyUs;
    uint32_t waitUs;
    uint32_t showUs;
};

struct ProfilerSample {
    ProfilerSource source;
    union {
        RenderSample render;
        OutputSample output;
    };
};

struct MetricStats {
    uint32_t min = UINT32_MAX;
    uint32_t max = 0;
    uint64_t sum = 0;
    uint32_t count = 0;

    void record(uint32_t value) {
        if (value < min) min = value;
        if (value > max) max = value;
        sum += value;
        count++;
    }

    uint32_t avg() const {
        return count > 0 ? static_cast<uint32_t>(sum / count) : 0;
    }

    void reset() {
        min = UINT32_MAX;
        max = 0;
        sum = 0;
        count = 0;
    }
};

struct RenderAggregator {
    MetricStats acquire, render, queue;
    MetricStats freeQ, readyQ;
    uint8_t lastEffect = 0;
    float lastAngularResolution = 3.0f;
    uint32_t lastMicrosecondsPerRev = 0;
    uint32_t sampleCount = 0;
};

struct OutputAggregator {
    MetricStats receive, copy, wait, show;
    uint32_t sampleCount = 0;
};

// Initialize analytics system (call once in setup)
void initProfilerAnalytics();

/**
 * Render profiler - runs on Core 1 (Arduino loop)
 *
 * Tracks timing and slot/revolution context:
 * - acquire_us: Time blocked waiting for free buffer (Output slow indicator)
 * - render_us: Time spent in effect render()
 * - queue_us: Time to hand off to output task
 * - Slot info: slot number, angle, resolution
 * - Revolution info: period, angular resolution, rev count
 * - Queue depths: freeQ, readyQ (pipeline health indicators)
 *
 * New approach: Sends ProfilerSample to analytics queue instead of ESP_LOG
 */
class RenderProfiler {
    int64_t t_start_ = 0;
    int64_t t_renderEnd_ = 0;
    int64_t t_queueEnd_ = 0;
    uint32_t frameCount_ = 0;

    // Slot/timing context (captured at markStart)
    uint8_t effectIndex_ = 0;
    const TimingSnapshot* timing_ = nullptr;

    // Pipeline metrics (captured at markStart)
    uint32_t acquireUs_ = 0;
    uint8_t freeQueueDepth_ = 0;
    uint8_t readyQueueDepth_ = 0;

public:
    RenderProfiler();
    void markStart(uint32_t frameCount, uint8_t effectIndex,
                   const SlotTarget& target, const TimingSnapshot& timing,
                   uint32_t revCount, uint32_t acquireUs,
                   uint8_t freeQueueDepth, uint8_t readyQueueDepth);
    void markRenderEnd();
    void markQueueEnd();
    void emit();

    void reset() {}
};

/**
 * Output profiler - runs on Core 0 (output task)
 *
 * Tracks:
 * - receive_us: Time blocked waiting for rendered frame (Render slow indicator)
 * - copy_us: Time to copy pixels to strip
 * - wait_us: Time spent busy-waiting for target angle
 * - show_us: Time for SPI transfer (strip.Show())
 * - Queue depths: freeQ, readyQ (pipeline health indicators)
 *
 * New approach: Sends ProfilerSample to analytics queue instead of ESP_LOG
 */
class OutputProfiler {
    int64_t t_start_ = 0;
    int64_t t_copyEnd_ = 0;
    int64_t t_waitEnd_ = 0;
    int64_t t_showEnd_ = 0;
    uint32_t frameCount_ = 0;

    // Pipeline metrics (captured at markStart)
    uint32_t receiveUs_ = 0;
    uint8_t freeQueueDepth_ = 0;
    uint8_t readyQueueDepth_ = 0;

   public:
    OutputProfiler();
    void markStart(uint32_t frameCount, uint32_t receiveUs,
                   uint8_t freeQueueDepth, uint8_t readyQueueDepth);
    void markCopyEnd();
    void markWaitEnd();
    void markShowEnd();
    void emit();

    void reset() {}
};

#else

// Stub implementations - compile to zero overhead
class RenderProfiler {
public:
    inline RenderProfiler() {
        ESP_LOGI(TAG_RENDER, "Profiler disabled. Using Dummy Profiler");
    }
    inline void markStart(uint32_t, uint8_t, const SlotTarget&,
                          const TimingSnapshot&, uint32_t, uint32_t,
                          uint8_t, uint8_t) {}
    inline void markRenderEnd() {}
    inline void markQueueEnd() {}
    inline void emit() {}
    inline void reset() {}
};

class OutputProfiler {
public:
    inline OutputProfiler() {
        ESP_LOGI(TAG_OUTPUT, "Profiler disabled. Using Dummy Profiler");
    }
    inline void markStart(uint32_t, uint32_t, uint8_t, uint8_t) {}
    inline void markCopyEnd() {}
    inline void markWaitEnd() {}
    inline void markShowEnd() {}
    inline void emit() {}
    inline void reset() {}
};

// No-op when profiling disabled
inline void initProfilerAnalytics() {}

#endif

// Global instances
extern RenderProfiler g_renderProfiler;
extern OutputProfiler g_outputProfiler;

// Legacy compatibility - keep old name working for any code that might reference it
// This can be removed once all references are updated
#define g_frameProfiler g_renderProfiler
