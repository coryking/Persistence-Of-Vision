#include "Arduino.h"
#include "HallEffectDriver.h"
#include "esp_timer.h"
#include "driver/gpio.h"

HallEffectDriver::HallEffectDriver(uint8_t sensorPin) : _sensorPin(sensorPin)
{
    if (HallEffectDriver::_triggerEventQueue == nullptr)
        HallEffectDriver::_triggerEventQueue = xQueueCreate(hallQueueLength, sizeof(HallEffectEvent));
}

void HallEffectDriver::start()
{
    setupISR();
}

void IRAM_ATTR HallEffectDriver::sensorTriggered_ISR(void *arg)
{
    HallEffectEvent event;
    event.triggerTimestamp = esp_timer_get_time();

    // Use xQueueOverwrite to ensure latest event is always in the queue
    // If queue is full, this overwrites the oldest item
    // This naturally handles debouncing - only the latest trigger timestamp matters
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xQueueOverwriteFromISR(_triggerEventQueue, &event, &higherPriorityTaskWoken);

    // Request context switch if a higher priority task was woken
    if (higherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}

QueueHandle_t HallEffectDriver::getEventQueue() const
{
    return HallEffectDriver::_triggerEventQueue;
}

esp_err_t HallEffectDriver::setupISR()
{
    gpio_num_t pin = static_cast<gpio_num_t>(_sensorPin);

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;  // Trigger on falling edge
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << _sensorPin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, &HallEffectDriver::sensorTriggered_ISR, nullptr);

    return ESP_OK;
}

QueueHandle_t HallEffectDriver::_triggerEventQueue = nullptr;
