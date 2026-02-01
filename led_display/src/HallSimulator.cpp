#include "HallSimulator.h"

#ifdef TEST_MODE
#include <cmath>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "HallEffectDriver.h"  // For HallEffectEvent type
#include "types.h"

static const char* TAG = "HALLSIM";

namespace {
    esp_timer_handle_t s_hallTimer = nullptr;
    QueueHandle_t s_hallQueue = nullptr;

    static void IRAM_ATTR hallTimerCallback(void* arg) {
        HallEffectEvent event;
        event.triggerTimestamp = esp_timer_get_time();

        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueOverwriteFromISR(static_cast<QueueHandle_t>(arg), &event, &higherPriorityTaskWoken);

        if (higherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }

#if TEST_VARY_RPM
    esp_timer_handle_t s_rpmUpdaterTimer = nullptr;

    static void IRAM_ATTR rpmUpdaterCallback(void* arg) {
        (void)arg;
        // Calculate new RPM using sinusoidal function
        timestamp_t now = esp_timer_get_time();
        double timeSec = now / 1000000.0;
        double rpm = 700.0 + 1050.0 * (1.0 + sin(timeSec * 0.5));

        // Calculate new interval
        uint64_t newIntervalUs = static_cast<uint64_t>(60000000.0 / rpm);

        // Reconfigure main hall timer
        esp_timer_stop(s_hallTimer);
        esp_timer_start_periodic(s_hallTimer, newIntervalUs);
    }
#endif // TEST_VARY_RPM
} // anonymous namespace
#endif // TEST_MODE

namespace HallSimulator {

QueueHandle_t begin(float targetRpm, bool enableVariableRpm) {
#ifdef TEST_MODE
    ESP_LOGI(TAG, "Initializing timer-based simulation");

    // Create event queue (size 1, same as HallEffectDriver)
    s_hallQueue = xQueueCreate(1, sizeof(HallEffectEvent));
    if (s_hallQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create hall simulation queue");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Create main hall timer
    esp_timer_create_args_t hallTimerArgs = {
        .callback = hallTimerCallback,
        .arg = static_cast<void*>(s_hallQueue),
        .dispatch_method = ESP_TIMER_TASK,
        .name = "hall_sim_timer",
        .skip_unhandled_events = false
    };

    esp_err_t err = esp_timer_create(&hallTimerArgs, &s_hallTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create hall timer: %d", err);
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Calculate interval from target RPM
    uint64_t intervalUs = static_cast<uint64_t>(60000000.0 / targetRpm);
    ESP_LOGI(TAG, "Starting at %.1f RPM (interval: %llu us)",
                  targetRpm, intervalUs);

    // Start periodic timer
    err = esp_timer_start_periodic(s_hallTimer, intervalUs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start hall timer: %d", err);
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

#if TEST_VARY_RPM
    if (enableVariableRpm) {
        // Create RPM updater timer (fires every 100ms to adjust RPM)
        esp_timer_create_args_t rpmUpdaterArgs = {
            .callback = rpmUpdaterCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "rpm_updater",
            .skip_unhandled_events = false
        };

        err = esp_timer_create(&rpmUpdaterArgs, &s_rpmUpdaterTimer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create RPM updater timer: %d", err);
            while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
        }

        // Start updater timer (100ms period)
        err = esp_timer_start_periodic(s_rpmUpdaterTimer, 100000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start RPM updater timer: %d", err);
            while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
        }
        ESP_LOGI(TAG, "Variable RPM enabled");
    }
#else
    (void)enableVariableRpm;
#endif // TEST_VARY_RPM

    ESP_LOGI(TAG, "Initialized");
    return s_hallQueue;
#else
    (void)targetRpm;
    (void)enableVariableRpm;
    return nullptr;
#endif // TEST_MODE
}

QueueHandle_t getEventQueue() {
#ifdef TEST_MODE
    return s_hallQueue;
#else
    return nullptr;
#endif
}

} // namespace HallSimulator
