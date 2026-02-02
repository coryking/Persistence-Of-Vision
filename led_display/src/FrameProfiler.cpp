#include "FrameProfiler.h"
#include "Arduino.h"
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

RenderProfiler::RenderProfiler() {
    ESP_LOGI(TAG_RENDER, "RenderProfiler initialized");
}

void RenderProfiler::markStart(uint32_t frameCount, uint8_t effectIndex,
                                const SlotTarget& target, const TimingSnapshot& timing,
                                uint32_t revCount, uint32_t acquireUs,
                                uint8_t freeQueueDepth, uint8_t readyQueueDepth) {
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
    effectIndex_ = effectIndex;
    target_ = &target;
    timing_ = &timing;
    revCount_ = revCount;
    acquireUs_ = acquireUs;
    freeQueueDepth_ = freeQueueDepth;
    readyQueueDepth_ = readyQueueDepth;
}

void RenderProfiler::markRenderEnd() {
    t_renderEnd_ = esp_timer_get_time();
}

void RenderProfiler::markQueueEnd() {
    t_queueEnd_ = esp_timer_get_time();
}

void RenderProfiler::emit() {
    // Debug: log every 1000 frames to see if emit is being called
    static uint32_t debugCounter = 0;
    if (++debugCounter % 1000 == 0) {
        ESP_LOGW(TAG_RENDER, "emit() called, frameCount_=%u, sampleInterval_=%u, mod=%u",
                 frameCount_, sampleInterval_, frameCount_ % sampleInterval_);
    }

    // Only print every Nth frame to reduce log volume
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    int64_t renderUs = t_renderEnd_ - t_start_;
    int64_t queueUs = t_queueEnd_ - t_renderEnd_;

    if (!headerPrinted_) {
        ESP_LOGW(TAG_RENDER, "# RENDER: frame,effect,acquire_us,render_us,queue_us,freeQ,readyQ,slot,angle,usec_per_rev,slot_size,rev_count,angular_res");
        ESP_LOGW(TAG_OUTPUT, "# OUTPUT: frame,receive_us,copy_us,wait_us,show_us,freeQ,readyQ");
        headerPrinted_ = true;
    }
    ESP_LOGW(TAG_RENDER, "%u,%u,%u,%lld,%lld,%u,%u,%d,%u,%llu,%u,%u,%f",
              frameCount_,
              effectIndex_,
              acquireUs_,
              renderUs,
              queueUs,
              freeQueueDepth_,
              readyQueueDepth_,
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

OutputProfiler::OutputProfiler() {
    ESP_LOGI(TAG_OUTPUT, "OutputProfiler initialized");
}

void OutputProfiler::markStart(uint32_t frameCount, uint32_t receiveUs,
                                uint8_t freeQueueDepth, uint8_t readyQueueDepth) {
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
    receiveUs_ = receiveUs;
    freeQueueDepth_ = freeQueueDepth;
    readyQueueDepth_ = readyQueueDepth;
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
    // Debug: log every 1000 frames to see if emit is being called
    static uint32_t debugCounter = 0;
    if (++debugCounter % 1000 == 0) {
        ESP_LOGW(TAG_OUTPUT, "OutputProfiler::emit() called, frameCount_=%u", frameCount_);
    }

    // Only print every Nth frame to reduce log volume
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    int64_t copyUs = t_copyEnd_ - t_start_;
    int64_t waitUs = t_waitEnd_ - t_copyEnd_;
    int64_t showUs = t_showEnd_ - t_waitEnd_;

    ESP_LOGW(TAG_OUTPUT, "%u,%u,%lld,%lld,%lld,%u,%u",
              frameCount_, receiveUs_, copyUs, waitUs, showUs,
              freeQueueDepth_, readyQueueDepth_);
}

#endif
