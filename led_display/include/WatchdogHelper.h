#pragma once
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * Task Watchdog Timer (TWDT) helper for real-time tasks.
 *
 * Problem: OutputTask runs a tight loop on Core 0 and may never block when
 * RenderTask is fast (simple effects). This starves the IDLE task, causing
 * TWDT timeout after ~8 seconds.
 *
 * Solution: Unsubscribe IDLE0 from TWDT and subscribe OutputTask instead.
 * OutputTask feeds the watchdog each iteration, proving it's making progress.
 *
 * Render vs Output relationship:
 * - OutputTask: constant time (varies only with LED count - hardware change)
 * - RenderTask: variable time (depends on effect complexity - runtime changeable)
 *
 * When Render is slow: OutputTask blocks on semaphore -> IDLE runs -> WDT happy
 * When Render is fast: OutputTask never blocks -> must feed WDT explicitly
 */
namespace WatchdogHelper {

/**
 * Unsubscribe the IDLE task for a specific core from TWDT.
 * Call from setup() after real-time tasks start.
 *
 * @param core The core whose IDLE task should be unsubscribed (0 or 1)
 */
inline void unsubscribeIdleTask(BaseType_t core) {
    TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(core);
    if (idle) {
        esp_task_wdt_delete(idle);
    }
}

/**
 * Subscribe the current task to TWDT.
 * Call from task function before entering main loop.
 */
inline void subscribeCurrentTask() {
    esp_task_wdt_add(NULL);
}

/**
 * Feed the watchdog timer to prevent timeout.
 * Call each iteration of tight loops.
 */
inline void feed() {
    esp_task_wdt_reset();
}

}  // namespace WatchdogHelper
