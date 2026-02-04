#include "FrameProfiler.h"
#include "Arduino.h"

// Global instances (exist in both enabled/disabled builds)
RenderProfiler g_renderProfiler;
OutputProfiler g_outputProfiler;

#ifdef ENABLE_TIMING_INSTRUMENTATION

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG_RESOLUTION = "RESOLUTION_CHANGE";

// Analytics queue and task
static QueueHandle_t g_analyticsQueue = nullptr;

// ============================================================================
// Analytics Task - Consumes samples and prints aggregated stats
// ============================================================================

static void printRenderStats(const RenderAggregator& agg) {
    // Use ESP_LOGI instead of Serial.printf to share mutex with ESP_LOG* calls
    // from other components (ESP-NOW, etc.) - prevents character interleaving
    ESP_LOGI(TAG_RENDER, "n=%u effect=%u waitForWriteBuffer_us=%u/%u/%u render_us=%u/%u/%u "
                  "queue_us=%u/%u/%u",
        agg.sampleCount, agg.lastEffect,
        agg.waitForWriteBuffer.min, agg.waitForWriteBuffer.avg(), agg.waitForWriteBuffer.max,
        agg.render.min, agg.render.avg(), agg.render.max,
        agg.queue.min, agg.queue.avg(), agg.queue.max);
}

static void printOutputStats(const OutputAggregator& agg) {
    ESP_LOGI(TAG_OUTPUT, "n=%u waitForReadBuffer_us=%u/%u/%u copy_us=%u/%u/%u "
                  "wait_us=%u/%u/%u show_us=%u/%u/%u",
        agg.sampleCount,
        agg.waitForReadBuffer.min, agg.waitForReadBuffer.avg(), agg.waitForReadBuffer.max,
        agg.copy.min, agg.copy.avg(), agg.copy.max,
        agg.wait.min, agg.wait.avg(), agg.wait.max,
        agg.show.min, agg.show.avg(), agg.show.max);
}

static void analyticsTask(void* param) {
    (void)param;
    ProfilerSample sample;
    RenderAggregator renderAgg;
    OutputAggregator outputAgg;

    while (true) {
        if (xQueueReceive(g_analyticsQueue, &sample, portMAX_DELAY) == pdTRUE) {
            if (sample.source == ProfilerSource::RENDER_CORE) {
                // Check for resolution change FIRST
                if (sample.render.angularResolution != renderAgg.lastAngularResolution) {
                    // Print current stats immediately
                    if (renderAgg.sampleCount > 0) {
                        printRenderStats(renderAgg);
                    }
                    if (outputAgg.sampleCount > 0) {
                        printOutputStats(outputAgg);
                    }

                    // Print resolution change event
                    ESP_LOGI(TAG_RESOLUTION, "from=%.1f to=%.1f "
                                  "render_avg=%u output_avg=%u usec_per_rev=%u",
                        renderAgg.lastAngularResolution,
                        sample.render.angularResolution,
                        renderAgg.render.avg(),
                        outputAgg.copy.avg() + outputAgg.show.avg(),
                        sample.render.microsecondsPerRev);

                    // Reset everything
                    renderAgg = {};
                    outputAgg = {};
                    renderAgg.lastAngularResolution = sample.render.angularResolution;
                }

                // Record metrics
                renderAgg.waitForWriteBuffer.record(sample.render.waitForWriteBufferUs);
                renderAgg.render.record(sample.render.renderUs);
                renderAgg.queue.record(sample.render.queueUs);
                renderAgg.lastEffect = sample.render.effectIndex;
                renderAgg.lastMicrosecondsPerRev = sample.render.microsecondsPerRev;

                if (++renderAgg.sampleCount >= 100) {
                    printRenderStats(renderAgg);
                    renderAgg.waitForWriteBuffer.reset();
                    renderAgg.render.reset();
                    renderAgg.queue.reset();
                    renderAgg.sampleCount = 0;
                }
            } else {
                // OUTPUT sample
                outputAgg.waitForReadBuffer.record(sample.output.waitForReadBufferUs);
                outputAgg.copy.record(sample.output.copyUs);
                outputAgg.wait.record(sample.output.waitUs);
                outputAgg.show.record(sample.output.showUs);

                if (++outputAgg.sampleCount >= 100) {
                    printOutputStats(outputAgg);
                    outputAgg.waitForReadBuffer.reset();
                    outputAgg.copy.reset();
                    outputAgg.wait.reset();
                    outputAgg.show.reset();
                    outputAgg.sampleCount = 0;
                }
            }
        }
    }
}

void initProfilerAnalytics() {
    // Create queue (32 slots for bursts)
    g_analyticsQueue = xQueueCreate(32, sizeof(ProfilerSample));
    if (!g_analyticsQueue) {
        ESP_LOGE(TAG_RENDER, "Failed to create analytics queue");
        return;
    }

    // Start analytics task on Core 0 (low priority, runs when idle)
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        analyticsTask,
        "analytics",
        4096,
        nullptr,
        1,  // Low priority - runs when cores are idle
        nullptr,
        0   // Core 0
    );

    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG_RENDER, "Failed to create analytics task");
    } else {
        ESP_LOGI(TAG_RENDER, "Analytics task started on Core 0");
    }
}

// ============================================================================
// RenderProfiler (Core 1 - Arduino loop)
// ============================================================================

RenderProfiler::RenderProfiler() {
    ESP_LOGI(TAG_RENDER, "RenderProfiler initialized");
}

void RenderProfiler::markStart(uint32_t frameCount, uint8_t effectIndex,
                                const SlotTarget& target, const TimingSnapshot& timing,
                                uint32_t revCount, uint32_t waitForWriteBufferUs) {
    (void)target;  // Not needed anymore
    (void)revCount;  // Not needed anymore
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
    effectIndex_ = effectIndex;
    timing_ = &timing;
    waitForWriteBufferUs_ = waitForWriteBufferUs;
}

void RenderProfiler::markRenderEnd() {
    t_renderEnd_ = esp_timer_get_time();
}

void RenderProfiler::markQueueEnd() {
    t_queueEnd_ = esp_timer_get_time();
}

void RenderProfiler::emit() {
    if (!g_analyticsQueue) return;  // Queue not initialized yet

    // Build sample and send to analytics queue
    ProfilerSample sample;
    sample.source = ProfilerSource::RENDER_CORE;
    sample.render.frameCount = frameCount_;
    sample.render.effectIndex = effectIndex_;
    sample.render.waitForWriteBufferUs = waitForWriteBufferUs_;
    sample.render.renderUs = static_cast<uint32_t>(t_renderEnd_ - t_start_);
    sample.render.queueUs = static_cast<uint32_t>(t_queueEnd_ - t_renderEnd_);
    sample.render.angularResolution = timing_->angularResolution;
    sample.render.microsecondsPerRev = timing_->microsecondsPerRev;

    // Non-blocking send - drop if queue full (shouldn't happen with 32 slots)
    xQueueSend(g_analyticsQueue, &sample, 0);
}

// ============================================================================
// OutputProfiler (Core 0 - output task)
// ============================================================================

OutputProfiler::OutputProfiler() {
    ESP_LOGI(TAG_OUTPUT, "OutputProfiler initialized");
}

void OutputProfiler::markStart(uint32_t frameCount, uint32_t waitForReadBufferUs) {
    t_start_ = esp_timer_get_time();
    frameCount_ = frameCount;
    waitForReadBufferUs_ = waitForReadBufferUs;
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
    if (!g_analyticsQueue) return;  // Queue not initialized yet

    // Build sample and send to analytics queue
    ProfilerSample sample;
    sample.source = ProfilerSource::OUTPUT_CORE;
    sample.output.frameCount = frameCount_;
    sample.output.waitForReadBufferUs = waitForReadBufferUs_;
    sample.output.copyUs = static_cast<uint32_t>(t_copyEnd_ - t_start_);
    sample.output.waitUs = static_cast<uint32_t>(t_waitEnd_ - t_copyEnd_);
    sample.output.showUs = static_cast<uint32_t>(t_showEnd_ - t_waitEnd_);

    // Non-blocking send - drop if queue full (shouldn't happen with 32 slots)
    xQueueSend(g_analyticsQueue, &sample, 0);
}

#endif
