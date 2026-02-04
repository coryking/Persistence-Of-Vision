#include "BufferManager.h"
#include "esp_log.h"

static const char* TAG = "BUFFER";

BufferManager bufferManager;

void BufferManager::init() {
    for (int i = 0; i < 2; i++) {
        freeSignal_[i] = xSemaphoreCreateBinary();
        readySignal_[i] = xSemaphoreCreateBinary();
        xSemaphoreGive(freeSignal_[i]);  // Both start FREE
    }
    ESP_LOGI(TAG, "Initialized with 2 buffers");
}

BufferManager::WriteBuffer BufferManager::acquireWriteBuffer(TickType_t timeout) {
    uint8_t buf = nextWriteBuffer_;

    if (xSemaphoreTake(freeSignal_[buf], timeout) != pdTRUE) {
        return {0xFF, nullptr};
    }

    nextWriteBuffer_ = 1 - nextWriteBuffer_;
    return {buf, &buffers_[buf]};
}

void BufferManager::releaseWriteBuffer(BufferHandle handle, timestamp_t targetTime) {
    targetTimes_[handle] = targetTime;
    xSemaphoreGive(readySignal_[handle]);
}

BufferManager::ReadBuffer BufferManager::acquireReadBuffer(TickType_t timeout) {
    uint8_t buf = nextReadBuffer_;

    if (xSemaphoreTake(readySignal_[buf], timeout) != pdTRUE) {
        return {0xFF, nullptr, 0};
    }

    nextReadBuffer_ = 1 - nextReadBuffer_;
    return {buf, &buffers_[buf], targetTimes_[buf]};
}

void BufferManager::releaseReadBuffer(BufferHandle handle) {
    xSemaphoreGive(freeSignal_[handle]);
}
