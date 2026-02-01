#pragma once

#include <cstdint>
#include "geometry.h"         // For SlotTarget
#include "RevolutionTimer.h"  // For TimingSnapshot

/**
 * Frame timing profiler - outputs CSV data when ENABLE_TIMING_INSTRUMENTATION is defined.
 *
 * Persistent instance design: A single global instance tracks inter-frame timing
 * across loop iterations. All delta calculations are encapsulated within the class.
 *
 * When disabled, all methods become empty inlines that compile to zero overhead.
 *
 * Usage:
 *   // In loop():
 *   g_frameProfiler.markFrameStart(frameCount, effectIndex, target, timing, revCount);
 *   // ... render ...
 *   g_frameProfiler.markRenderEnd();
 *   // ... copy pixels ...
 *   g_frameProfiler.markCopyEnd();
 *   // ... wait for target time ...
 *   g_frameProfiler.markWaitEnd();
 *   // ... Show() ...
 *   g_frameProfiler.markShowEnd();
 *   g_frameProfiler.emit();
 *
 * Measurement overhead: Each esp_timer_get_time() call takes ~1μs.
 * The instrumented path has 6 timing calls, adding ~6μs total overhead.
 */

#if ENABLE_TIMING_INSTRUMENTATION

class FrameProfiler {
    // Timestamps for current frame
    int64_t t_frameStart_ = 0;
    int64_t t_renderEnd_ = 0;
    int64_t t_copyEnd_ = 0;
    int64_t t_waitEnd_ = 0;
    int64_t t_showEnd_ = 0;

    // Persists across frames for inter-frame timing
    int64_t lastShowEnd_ = 0;
    int64_t lastEmitEnd_ = 0;  // To measure Serial overhead separately
    bool headerPrinted_ = false;

    // Frame metadata (captured at markFrameStart)
    uint32_t frameCount_ = 0;
    uint8_t effectIndex_ = 0;
    const SlotTarget* target_ = nullptr;
    const TimingSnapshot* timing_ = nullptr;
    uint32_t revCount_ = 0;

    // Sample interval: only print every Nth frame to reduce Serial interference
    uint32_t sampleInterval_ = 100;

public:
    /**
     * Mark frame start - captures timing snapshot and slot info
     *
     * @param frameCount Current frame number
     * @param effectIndex Current effect index (0-based)
     * @param target Slot target for this frame (stored by pointer, must remain valid)
     * @param timing Timing snapshot (stored by pointer, must remain valid)
     * @param revCount Revolution count
     */
    void markFrameStart(uint32_t frameCount,
                        uint8_t effectIndex,
                        const SlotTarget& target,
                        const TimingSnapshot& timing,
                        uint32_t revCount);

    /** Mark end of effect render phase */
    void markRenderEnd();

    /** Mark end of pixel copy phase */
    void markCopyEnd();

    /** Mark end of busy-wait phase */
    void markWaitEnd();

    /** Mark end of Show() call */
    void markShowEnd();

    /**
     * Compute deltas and output CSV data
     *
     * Only prints every sampleInterval_ frames to reduce Serial interference
     * with DMA timing. Inter-frame tracking is updated every frame regardless.
     */
    void emit();

    /**
     * Set sample interval (default: 100)
     *
     * Only every Nth frame is printed to Serial. Set to 1 to print every frame.
     * Lower values give more data but more Serial interference.
     */
    void setSampleInterval(uint32_t interval) { sampleInterval_ = interval > 0 ? interval : 1; }

    /**
     * Reset inter-frame tracking
     *
     * Call when rotation stops or display is disabled to ensure
     * next frame doesn't compute bogus inter-frame time.
     */
    void reset();
};

#else

// Stub implementation - all methods are empty inlines, optimized away completely
class FrameProfiler {
public:
    inline void markFrameStart(uint32_t, uint8_t, const SlotTarget&,
                               const TimingSnapshot&, uint32_t) {}
    inline void markRenderEnd() {}
    inline void markCopyEnd() {}
    inline void markWaitEnd() {}
    inline void markShowEnd() {}
    inline void emit() {}
    inline void setSampleInterval(uint32_t) {}
    inline void reset() {}
};

#endif

// Global instance (exists in both enabled/disabled builds)
extern FrameProfiler g_frameProfiler;
