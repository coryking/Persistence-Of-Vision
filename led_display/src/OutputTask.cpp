#include "OutputTask.h"
#include "BufferManager.h"
#include "SlotTiming.h"
#include "FrameProfiler.h"
#include "RotorDiagnosticStats.h"
#include "RevolutionTimer.h"
#include "EffectManager.h"
#include "StatsOverlay.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <NeoPixelBus.h>

static const char* TAG = "OUTPUT";

OutputTask outputTask;

extern NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip;
extern RevolutionTimer revTimer;
extern EffectManager effectManager;

// Stats overlay instance
static StatsOverlay statsOverlay;

void OutputTask::start() {
    xTaskCreatePinnedToCore(taskFunction, "output", STACK_SIZE, this, PRIORITY, &handle_, CORE);
    ESP_LOGI(TAG, "Started on Core %d", CORE);
}

void OutputTask::stop() {
    if (handle_) {
        vTaskDelete(handle_);
        handle_ = nullptr;
    }
}

void OutputTask::suspend() {
    if (handle_) vTaskSuspend(handle_);
}

void OutputTask::resume() {
    if (handle_) vTaskResume(handle_);
}

void OutputTask::taskFunction(void* params) {
    static_cast<OutputTask*>(params)->run();
}

void OutputTask::run() {
    while (true) {
        // 1. Acquire read buffer (with timeout to avoid wedging)
        int64_t receiveStart = esp_timer_get_time();
        auto rb = bufferManager.acquireReadBuffer(pdMS_TO_TICKS(100));
        int64_t receiveEnd = esp_timer_get_time();
        uint32_t receiveUs = static_cast<uint32_t>(receiveEnd - receiveStart);

        if (rb.ctx == nullptr) {
            continue;
        }

        // 2. Track timing
        int64_t copyStart = esp_timer_get_time();

        g_outputProfiler.markStart(rb.ctx->frameNumber, receiveUs);

        // 3. Copy to strip's internal buffer
        copyPixelsToStrip(*rb.ctx, strip);

        // 3.5. Render stats overlay if enabled (after brightness, so stats are full brightness)
        if (effectManager.isStatsEnabled()) {
            statsOverlay.render(*rb.ctx, strip, revTimer);
        }

        g_outputProfiler.markCopyEnd();

        int64_t copyEnd = esp_timer_get_time();

        // 4. Release buffer IMMEDIATELY after copy - strip has its own DMA buffer
        bufferManager.releaseReadBuffer(rb.handle);

        // 5. Wait for target time (computed by RenderTask)
        waitForTargetTime(rb.targetTime);
        g_outputProfiler.markWaitEnd();

        int64_t showStart = esp_timer_get_time();

        // 6. Fire DMA transfer
        strip.Show();
        g_outputProfiler.markShowEnd();

        int64_t showEnd = esp_timer_get_time();

        // 7. Record successful render in diagnostics
        RotorDiagnosticStats::instance().recordRenderEvent(true, false);

        // 8. Track output time (copy + show, NOT wait) for resolution calc
        uint32_t outputTime = static_cast<uint32_t>((copyEnd - copyStart) + (showEnd - showStart));
        revTimer.recordOutputTime(outputTime);

        // 9. Emit profiler output
        g_outputProfiler.emit();

        // 10. Yield to help same-priority tasks get CPU time
        taskYIELD();
    }
}
