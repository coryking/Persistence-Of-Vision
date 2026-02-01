#include "FrameProfiler.h"

// Global instances (exist in both enabled/disabled builds)
RenderProfiler g_renderProfiler;
OutputProfiler g_outputProfiler;

#ifdef ENABLE_TIMING_INSTRUMENTATION

#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG_RENDER = "RENDER";
static const char* TAG_OUTPUT = "OUTPUT";

// ============================================================================
// RenderProfiler (Core 1 - Arduino loop)
// ============================================================================

void RenderProfiler::markStart(uint32_t frameCount, uint8_t effectIndex,
                                const SlotTarget& target, const TimingSnapshot& timing,
                                uint32_t revCount) {
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
    effectIndex_ = effectIndex;
    target_ = &target;
    timing_ = &timing;
    revCount_ = revCount;
}

void RenderProfiler::markRenderEnd() {
    t_renderEnd_ = esp_timer_get_time();
}

void RenderProfiler::markQueueEnd() {
    t_queueEnd_ = esp_timer_get_time();
}

void RenderProfiler::emit() {
    // Only print every Nth frame to reduce log volume
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    int64_t renderUs = t_renderEnd_ - t_start_;
    int64_t queueUs = t_queueEnd_ - t_renderEnd_;

    if (!headerPrinted_) {
        ESP_LOGD(TAG_RENDER, "# RENDER: frame,effect,render_us,queue_us,slot,angle,usec_per_rev,slot_size,rev_count,angular_res");
        ESP_LOGD(TAG_OUTPUT, "# OUTPUT: frame,copy_us,wait_us,show_us");
        headerPrinted_ = true;
    }

    ESP_LOGD(TAG_RENDER, "%u,%u,%lld,%lld,%d,%u,%llu,%u,%u,%f",
              frameCount_,
              effectIndex_,
              renderUs,
              queueUs,
              target_->slotNumber,
              target_->angleUnits,
              timing_->microsecondsPerRev,
              target_->slotSize,
              revCount_,
              timing_->angularResolution);
}

// ============================================================================
// OutputProfiler (Core 0 - output task)
// ============================================================================

void OutputProfiler::markStart(uint32_t frameCount) {
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
}

void OutputProfiler::markCopyEnd() {
    t_copyEnd_ = esp_timer_get_time();
}

void OutputProfiler::markWaitEnd() {
    t_waitEnd_ = esp_timer_get_time();
}

void OutputProfiler::markShowEnd() {
    t_showEnd_ = esp_timer_get_time();
}

void OutputProfiler::emit() {
    // Only print every Nth frame to reduce log volume
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    int64_t copyUs = t_copyEnd_ - t_start_;
    int64_t waitUs = t_waitEnd_ - t_copyEnd_;
    int64_t showUs = t_showEnd_ - t_waitEnd_;

    ESP_LOGD(TAG_OUTPUT, "%u,%lld,%lld,%lld", frameCount_, copyUs, waitUs, showUs);
}

#endif
