#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class OutputTask {
public:
    void start();
    void stop();
    void suspend();
    void resume();

private:
    static void taskFunction(void* params);
    void run();

    TaskHandle_t handle_ = nullptr;

    static constexpr UBaseType_t PRIORITY = 1;  // Lowered from 2 to let USB CDC run
    static constexpr uint32_t STACK_SIZE = 8192;
    static constexpr BaseType_t CORE = 0;
};

extern OutputTask outputTask;
