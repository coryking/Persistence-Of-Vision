#include "RenderTask.h"
#include "BufferManager.h"
#include "RevolutionTimer.h"
#include "EffectManager.h"
#include "SlotTiming.h"
#include "FrameProfiler.h"
#include "RotorDiagnosticStats.h"
#include "geometry.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "RENDER";

RenderTask renderTask;

extern RevolutionTimer revTimer;
extern EffectManager effectManager;

// Global frame counter
static uint32_t globalFrameCount = 0;

void RenderTask::start() {
    xTaskCreatePinnedToCore(taskFunction, "render", STACK_SIZE, this, PRIORITY, &handle_, CORE);
    ESP_LOGI(TAG, "Started on Core %d", CORE);
}

void RenderTask::stop() {
    if (handle_) {
        vTaskDelete(handle_);
        handle_ = nullptr;
    }
}

void RenderTask::suspend() {
    if (handle_) vTaskSuspend(handle_);
}

void RenderTask::resume() {
    if (handle_) vTaskResume(handle_);
}

void RenderTask::taskFunction(void* params) {
    static_cast<RenderTask*>(params)->run();
}

void RenderTask::run() {
    while (true) {
        // 1. Get atomic snapshot of timing values
        TimingSnapshot timing = revTimer.getTimingSnapshot();

        // 2. Handle not-rotating state
        if (!timing.isRotating || !timing.warmupComplete) {
            RotorDiagnosticStats::instance().recordRenderEvent(false, true);  // notRotating=true
            lastRenderedSlot_ = -1;
            g_renderProfiler.reset();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 3. Calculate next slot to render
        SlotTarget target = calculateNextSlot(lastRenderedSlot_, timing);

        // 4. Check if we're behind schedule (past target time)
        timestamp_t now = esp_timer_get_time();
        if (now > target.targetTime) {
            // We're behind - skip this slot
            RotorDiagnosticStats::instance().recordRenderEvent(false, false);  // skip
            lastRenderedSlot_ = target.slotNumber;
            vTaskDelay(0);
            continue;
        }

        // 5. Acquire write buffer
        int64_t acquireStart = esp_timer_get_time();
        auto wb = bufferManager.acquireWriteBuffer(pdMS_TO_TICKS(100));
        int64_t acquireEnd = esp_timer_get_time();
        uint32_t acquireUs = static_cast<uint32_t>(acquireEnd - acquireStart);

        if (wb.ctx == nullptr) {
            // Timeout - pipeline is stalled
            lastRenderedSlot_ = target.slotNumber;
            continue;
        }

        // 6. Render effect
        uint32_t thisFrame = globalFrameCount++;

        revTimer.startRender();

        g_renderProfiler.markStart(thisFrame, effectManager.getCurrentEffectIndex(),
                                    target, timing, revTimer.getRevolutionCount(),
                                    acquireUs);

        // Populate render context with target angle
        interval_t microsecondsPerRev = timing.lastActualInterval;
        if (microsecondsPerRev == 0) microsecondsPerRev = timing.microsecondsPerRev;

        wb.ctx->frameCount = thisFrame;
        wb.ctx->timeUs = static_cast<uint32_t>(now);
        wb.ctx->microsPerRev = microsecondsPerRev;
        wb.ctx->slotSizeUnits = target.slotSize;

        // Set arm angles from target - arms[0]=outer, arms[1]=middle(hall), arms[2]=inside
        wb.ctx->arms[0].angleUnits = (target.angleUnits + OUTER_ARM_PHASE) % ANGLE_FULL_CIRCLE;
        wb.ctx->arms[1].angleUnits = target.angleUnits;
        wb.ctx->arms[2].angleUnits = (target.angleUnits + INSIDE_ARM_PHASE) % ANGLE_FULL_CIRCLE;

        // Render current effect
        Effect* current = effectManager.current();
        if (current) {
            current->render(*wb.ctx);
        }
        g_renderProfiler.markRenderEnd();

        revTimer.endRender();

        // 7. Release buffer with target time
        bufferManager.releaseWriteBuffer(wb.handle, target.targetTime);
        g_renderProfiler.markQueueEnd();

        // 8. Emit profiler output
        g_renderProfiler.emit();

        lastRenderedSlot_ = target.slotNumber;
    }
}
