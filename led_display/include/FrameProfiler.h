#pragma once

#include <cstdint>

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

#if ENABLE_TIMING_INSTRUMENTATION

/**
 * Render profiler - runs on Core 1 (Arduino loop)
 *
 * Tracks:
 * - render_us: Time spent in effect render()
 * - queue_us: Time to hand off to output task
 *
 * Output format: RENDER,frame,render_us,queue_us
 */
class RenderProfiler {
    int64_t t_start_ = 0;
    int64_t t_renderEnd_ = 0;
    int64_t t_queueEnd_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t sampleInterval_ = 100;
    bool headerPrinted_ = false;

public:
    void markStart(uint32_t frameCount);
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
 * - copy_us: Time to copy pixels to strip
 * - wait_us: Time spent busy-waiting for target angle
 * - show_us: Time for SPI transfer (strip.Show())
 *
 * Output format: OUTPUT,frame,copy_us,wait_us,show_us
 */
class OutputProfiler {
    int64_t t_start_ = 0;
    int64_t t_copyEnd_ = 0;
    int64_t t_waitEnd_ = 0;
    int64_t t_showEnd_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t sampleInterval_ = 100;
    bool headerPrinted_ = false;

public:
    void markStart(uint32_t frameCount);
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
    inline void markStart(uint32_t) {}
    inline void markRenderEnd() {}
    inline void markQueueEnd() {}
    inline void emit() {}
    inline void setSampleInterval(uint32_t) {}
    inline void reset() {}
};

class OutputProfiler {
public:
    inline void markStart(uint32_t) {}
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
