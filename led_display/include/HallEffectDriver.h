#ifndef HALLEFFECTDRIVER_H
#define HALLEFFECTDRIVER_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "types.h"

// Queue size of 1 - we only care about the latest hall sensor event
const uint8_t hallQueueLength = 1;

typedef struct
{
    timestamp_t triggerTimestamp;
} HallEffectEvent;

class HallEffectDriver
{
public:
    HallEffectDriver(uint8_t sensorPin);
    void start();

    static void IRAM_ATTR sensorTriggered_ISR(void *arg);
    QueueHandle_t getEventQueue() const;

private:
    const uint8_t _sensorPin;
    static QueueHandle_t _triggerEventQueue;
    esp_err_t setupISR();
};

#endif // HALLEFFECTDRIVER_H
