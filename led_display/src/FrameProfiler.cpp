#include "FrameProfiler.h"

// Global instances (exist in both enabled/disabled builds)
RenderProfiler g_renderProfiler;
OutputProfiler g_outputProfiler;

#if ENABLE_TIMING_INSTRUMENTATION

#include <Arduino.h>
#include "esp_timer.h"

// ============================================================================
// RenderProfiler (Core 1 - Arduino loop)
// ============================================================================

void RenderProfiler::markStart(uint32_t frameCount) {
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
}

void RenderProfiler::markRenderEnd() {
    t_renderEnd_ = esp_timer_get_time();
}

void RenderProfiler::markQueueEnd() {
    t_queueEnd_ = esp_timer_get_time();
}

void RenderProfiler::emit() {
    // Only print every Nth frame to reduce Serial interference
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    if (!headerPrinted_) {
        Serial.println("# RENDER: frame,render_us,queue_us");
        Serial.println("# OUTPUT: frame,copy_us,wait_us,show_us");
        headerPrinted_ = true;
    }

    int64_t renderUs = t_renderEnd_ - t_start_;
    int64_t queueUs = t_queueEnd_ - t_renderEnd_;

    Serial.printf("RENDER,%u,%lld,%lld\n", frameCount_, renderUs, queueUs);
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
    // Only print every Nth frame to reduce Serial interference
    if (frameCount_ % sampleInterval_ != 0) {
        return;
    }

    int64_t copyUs = t_copyEnd_ - t_start_;
    int64_t waitUs = t_waitEnd_ - t_copyEnd_;
    int64_t showUs = t_showEnd_ - t_waitEnd_;

    Serial.printf("OUTPUT,%u,%lld,%lld,%lld\n", frameCount_, copyUs, waitUs, showUs);
}

#endif
