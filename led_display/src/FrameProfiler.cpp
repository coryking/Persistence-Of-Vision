#include "FrameProfiler.h"
#include "geometry.h"         // For SlotTarget
#include "RevolutionTimer.h"  // For TimingSnapshot

// Global instance (exists in both enabled/disabled builds)
FrameProfiler g_frameProfiler;

#if ENABLE_TIMING_INSTRUMENTATION

#include <Arduino.h>
#include "esp_timer.h"

void FrameProfiler::markFrameStart(uint32_t frameCount,
                                   uint8_t effectIndex,
                                   const SlotTarget& target,
                                   const TimingSnapshot& timing,
                                   uint32_t revCount) {
    t_frameStart_ = esp_timer_get_time();
    frameCount_ = frameCount;
    effectIndex_ = effectIndex;
    target_ = &target;
    timing_ = &timing;
    revCount_ = revCount;
}

void FrameProfiler::markRenderEnd() {
    t_renderEnd_ = esp_timer_get_time();
}

void FrameProfiler::markCopyEnd() {
    t_copyEnd_ = esp_timer_get_time();
}

void FrameProfiler::markWaitEnd() {
    t_waitEnd_ = esp_timer_get_time();
}

void FrameProfiler::markShowEnd() {
    t_showEnd_ = esp_timer_get_time();
}

void FrameProfiler::reset() {
    lastShowEnd_ = 0;
    lastEmitEnd_ = 0;
}

void FrameProfiler::emit() {
    // Always update lastShowEnd_ for accurate inter-frame timing
    // (even on frames we don't print)
    int64_t prevShowEnd = lastShowEnd_;
    lastShowEnd_ = t_showEnd_;

    // Only print every Nth frame to reduce Serial interference with DMA
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    if (!headerPrinted_) {
        Serial.println("frame,effect,total_us,render_us,copy_us,wait_us,show_us,"
                       "inter_frame_us,serial_us,slot,angle_units,usec_per_rev,"
                       "resolution_units,rev_count,angular_res,last_interval_us");
        headerPrinted_ = true;
    }

    // Compute all deltas
    int64_t totalUs = t_showEnd_ - t_frameStart_;
    int64_t renderUs = t_renderEnd_ - t_frameStart_;
    int64_t copyUs = t_copyEnd_ - t_renderEnd_;
    int64_t waitUs = t_waitEnd_ - t_copyEnd_;
    int64_t showUs = t_showEnd_ - t_waitEnd_;

    // Inter-frame: from last Show() to this frame's start
    // This captures loop overhead: processCommands, timing snapshot, slot calculation, etc.
    int64_t interFrameUs = (prevShowEnd > 0) ? (t_frameStart_ - prevShowEnd) : -1;

    // Serial overhead from PREVIOUS printed frame's emit
    // Note: with sampling, this spans multiple frames so it's less meaningful
    int64_t serialUs = (lastEmitEnd_ > 0 && prevShowEnd > 0)
                       ? (lastEmitEnd_ - prevShowEnd) : -1;

    Serial.printf("%u,%u,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d,%u,%llu,%u,%u,%f,%llu\n",
                  frameCount_,
                  effectIndex_,
                  totalUs,
                  renderUs,
                  copyUs,
                  waitUs,
                  showUs,
                  interFrameUs,
                  serialUs,
                  target_->slotNumber,
                  target_->angleUnits,
                  timing_->microsecondsPerRev,
                  target_->slotSize,
                  revCount_,
                  timing_->angularResolution,
                  timing_->lastActualInterval);

    lastEmitEnd_ = esp_timer_get_time();
}

#endif
