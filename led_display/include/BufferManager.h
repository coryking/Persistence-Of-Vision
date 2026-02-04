#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "RenderContext.h"
#include "geometry.h"

class BufferManager {
public:
    using BufferHandle = uint8_t;

    struct WriteBuffer {
        BufferHandle handle;
        RenderContext* ctx;
    };

    struct ReadBuffer {
        BufferHandle handle;
        RenderContext* ctx;
        timestamp_t targetTime;
    };

    void init();

    // For RenderTask
    WriteBuffer acquireWriteBuffer(TickType_t timeout);
    void releaseWriteBuffer(BufferHandle handle, timestamp_t targetTime);

    // For OutputTask
    ReadBuffer acquireReadBuffer(TickType_t timeout);
    void releaseReadBuffer(BufferHandle handle);

private:
    RenderContext buffers_[2];
    timestamp_t targetTimes_[2];

    SemaphoreHandle_t freeSignal_[2];   // Buffer N is free for writing
    SemaphoreHandle_t readySignal_[2];  // Buffer N is ready for reading

    uint8_t nextWriteBuffer_ = 0;
    uint8_t nextReadBuffer_ = 0;
};

extern BufferManager bufferManager;
