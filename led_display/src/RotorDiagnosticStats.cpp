#include "RotorDiagnosticStats.h"
#include "messages.h"
#include "espnow_config.h"

#include <esp_timer.h>
#include <esp_now.h>
#include <Arduino.h>

// Singleton instance
RotorDiagnosticStats& RotorDiagnosticStats::instance() {
    static RotorDiagnosticStats s_instance;
    return s_instance;
}

void RotorDiagnosticStats::start(uint32_t intervalMs) {
    if (_timer != nullptr) {
        // Already running
        return;
    }

    // Initialize created timestamp on first start
    if (_created_us == 0) {
        _created_us = esp_timer_get_time();
    }

    // Create FreeRTOS software timer
    _timer = xTimerCreate(
        "rotorStats",                    // Name (for debugging)
        pdMS_TO_TICKS(intervalMs),       // Period
        pdTRUE,                          // Auto-reload
        this,                            // Timer ID (pointer to this for callback)
        timerCallback                    // Callback function
    );

    if (_timer == nullptr) {
        Serial.println("[RotorStats] ERROR: Failed to create timer");
        return;
    }

    if (xTimerStart(_timer, 0) != pdPASS) {
        Serial.println("[RotorStats] ERROR: Failed to start timer");
        xTimerDelete(_timer, 0);
        _timer = nullptr;
        return;
    }

    Serial.printf("[RotorStats] Started (interval=%ums)\n", intervalMs);
}

void RotorDiagnosticStats::stop() {
    if (_timer == nullptr) {
        return;
    }

    xTimerStop(_timer, 0);
    xTimerDelete(_timer, 0);
    _timer = nullptr;

    Serial.println("[RotorStats] Stopped");
}

void RotorDiagnosticStats::reset() {
    portENTER_CRITICAL(&_spinlock);

    _created_us = esp_timer_get_time();
    _lastUpdated_us = _created_us;
    _hallEventsTotal = 0;
    _hallOutliersFiltered = 0;
    _lastOutlierInterval_us = 0;
    _hallAvg_us = 0;
    _espnowSendAttempts = 0;
    _espnowSendFailures = 0;
    _renderCount = 0;
    _skipCount = 0;
    _notRotatingCount = 0;
    // Don't reset effectNumber, brightness, or reportSequence

    portEXIT_CRITICAL(&_spinlock);

    Serial.println("[RotorStats] Reset");
}

void RotorDiagnosticStats::recordHallEvent() {
    portENTER_CRITICAL(&_spinlock);
    _hallEventsTotal++;
    _lastUpdated_us = esp_timer_get_time();
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::recordOutlier(interval_t interval_us) {
    portENTER_CRITICAL(&_spinlock);
    _hallOutliersFiltered++;
    _lastOutlierInterval_us = static_cast<uint32_t>(interval_us);
    _lastUpdated_us = esp_timer_get_time();
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::recordEspNowResult(bool success) {
    portENTER_CRITICAL(&_spinlock);
    _espnowSendAttempts++;
    if (!success) {
        _espnowSendFailures++;
    }
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::recordRenderEvent(bool rendered, bool notRotating) {
    portENTER_CRITICAL(&_spinlock);
    if (rendered) {
        _renderCount++;
    } else if (notRotating) {
        _notRotatingCount++;
    } else {
        _skipCount++;
    }
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::setEffectNumber(uint8_t effectNum) {
    portENTER_CRITICAL(&_spinlock);
    _effectNumber = effectNum;
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::setBrightness(uint8_t brightness) {
    portENTER_CRITICAL(&_spinlock);
    _brightness = brightness;
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::setHallAvgUs(period_t avgUs) {
    portENTER_CRITICAL(&_spinlock);
    _hallAvg_us = avgUs;
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::print() const {
    portENTER_CRITICAL(&_spinlock);
    Serial.printf("[RotorStats] seq=%lu hall=%lu outliers=%lu lastOutlier=%luus "
                  "espnow=%lu/%lu render=%u skip=%u notRot=%u effect=%u bright=%u\n",
                  _reportSequence, _hallEventsTotal, _hallOutliersFiltered,
                  _lastOutlierInterval_us, _espnowSendAttempts - _espnowSendFailures,
                  _espnowSendAttempts, _renderCount, _skipCount, _notRotatingCount,
                  _effectNumber, _brightness);
    portEXIT_CRITICAL(&_spinlock);
}

void RotorDiagnosticStats::timerCallback(TimerHandle_t xTimer) {
    // Get the instance from timer ID
    RotorDiagnosticStats* self = static_cast<RotorDiagnosticStats*>(pvTimerGetTimerID(xTimer));
    if (self) {
        self->sendViaEspNow();
    }
}

void RotorDiagnosticStats::sendViaEspNow() {
    RotorStatsMsg msg;

    // Capture current values atomically
    portENTER_CRITICAL(&_spinlock);

    msg.reportSequence = _reportSequence++;
    msg.created_us = _created_us;
    msg.lastUpdated_us = esp_timer_get_time();

    // Hall sensor stats
    msg.hallEventsTotal = _hallEventsTotal;
    msg.hallOutliersFiltered = _hallOutliersFiltered;
    msg.lastOutlierInterval_us = _lastOutlierInterval_us;
    msg.hallAvg_us = _hallAvg_us;

    // ESP-NOW stats
    msg.espnowSendAttempts = _espnowSendAttempts;
    msg.espnowSendFailures = _espnowSendFailures;

    // Render stats (capture and reset - these are deltas)
    msg.renderCount = _renderCount;
    msg.skipCount = _skipCount;
    msg.notRotatingCount = _notRotatingCount;
    _renderCount = 0;
    _skipCount = 0;
    _notRotatingCount = 0;

    // Current state
    msg.effectNumber = _effectNumber;
    msg.brightness = _brightness;

    portEXIT_CRITICAL(&_spinlock);

    // Send via ESP-NOW (outside critical section)
    esp_err_t result = esp_now_send(MOTOR_CONTROLLER_MAC,
                                     reinterpret_cast<uint8_t*>(&msg),
                                     sizeof(msg));

    // Record this send attempt (will be included in next report)
    recordEspNowResult(result == ESP_OK);

    if (result != ESP_OK) {
        // Only log failures - success is the norm
        Serial.printf("[RotorStats] Send failed: %s\n", esp_err_to_name(result));
    }
}
