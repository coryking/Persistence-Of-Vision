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
 * Output format: RENDER,frame,effect,acquire_us,render_us,queue_us,freeQ,readyQ,slot,angle,usec_per_rev,slot_size,rev_count,angular_res
 */
class RenderProfiler {
    int64_t t_start_ = 0;
    int64_t t_renderEnd_ = 0;
    int64_t t_queueEnd_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t sampleInterval_ = 100;
    bool headerPrinted_ = false;

    // Slot/timing context (captured at markStart)
    uint8_t effectIndex_ = 0;
    const SlotTarget* target_ = nullptr;
    const TimingSnapshot* timing_ = nullptr;
    uint32_t revCount_ = 0;

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

    void setSampleInterval(uint32_t interval) { sampleInterval_ = interval > 0 ? interval : 1; }
    void reset() { headerPrinted_ = false; }
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
 * Output format: OUTPUT,frame,receive_us,copy_us,wait_us,show_us,freeQ,readyQ
 */
class OutputProfiler {
    int64_t t_start_ = 0;
    int64_t t_copyEnd_ = 0;
    int64_t t_waitEnd_ = 0;
    int64_t t_showEnd_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t sampleInterval_ = 100;
    bool headerPrinted_ = false;

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

    void setSampleInterval(uint32_t interval) { sampleInterval_ = interval > 0 ? interval : 1; }
    void reset() { headerPrinted_ = false; }
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
    inline void setSampleInterval(uint32_t) {}
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
    inline void setSampleInterval(uint32_t) {}
    inline void reset() {}
};

#endif

// Global instances
extern RenderProfiler g_renderProfiler;
extern OutputProfiler g_outputProfiler;

// Legacy compatibility - keep old name working for any code that might reference it
// This can be removed once all references are updated
#define g_frameProfiler g_renderProfiler
