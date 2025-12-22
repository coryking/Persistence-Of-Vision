# FreeRTOS Integration Guide for POV Display

## Table of Contents
1. [FreeRTOS Fundamentals](#freertos-fundamentals)
2. [Arduino + FreeRTOS on ESP32](#arduino--freertos-on-esp32)
3. [Task Priorities and Scheduling](#task-priorities-and-scheduling)
4. [Queue-Based Communication](#queue-based-communication)
5. [POV Display Architecture Patterns](#pov-display-architecture-patterns)
6. [Timing Critical Operations](#timing-critical-operations)
7. [Pitfalls to Avoid](#pitfalls-to-avoid)
8. [Code Examples](#code-examples)

---

## FreeRTOS Fundamentals

### What is FreeRTOS?

FreeRTOS is a **preemptive real-time operating system** kernel that manages multiple concurrent tasks on a microcontroller. On ESP32, it's not optional - it's built into the ESP-IDF and Arduino frameworks.

### Scheduling Overview

FreeRTOS uses **Fixed Priority Preemptive Scheduling** with **Time Slicing**:

- **Preemptive**: Higher priority tasks can interrupt lower priority tasks immediately
- **Fixed Priority**: Each task has a static priority (higher number = higher priority)
- **Time Slicing**: Tasks of equal priority share CPU time in round-robin fashion

### Task States

A task can be in one of four states:

1. **Running**: Currently executing on CPU
2. **Ready**: Ready to run, waiting for CPU time
3. **Blocked**: Waiting for an event (queue, semaphore, delay)
4. **Suspended**: Manually suspended, not scheduled

### Context Switching Overhead

**Overhead**: 2-5 microseconds per context switch on ESP32 (varies with CPU frequency)

Context switching involves:
- Saving current task's CPU registers to stack
- Loading next task's registers from stack
- Updating scheduler state

**Impact**: Minimal for most applications. At 1000Hz tick rate, overhead is typically <1% of CPU time.

### Blocking vs. Non-Blocking

**Blocking operations** (recommended for most tasks):
- `xQueueReceive(queue, &data, portMAX_DELAY)` - wait forever
- `vTaskDelay(pdMS_TO_TICKS(100))` - sleep for time period
- Task yields CPU while waiting, allowing other tasks to run

**Non-blocking operations**:
- `xQueueReceive(queue, &data, 0)` - return immediately if no data
- Useful in tight timing loops where you can't afford to block

---

## Arduino + FreeRTOS on ESP32

### The Key Insight: `loop()` IS a FreeRTOS Task

**Arduino's `loop()` function runs as a FreeRTOS task called `loopTask`** with:
- **Priority**: 1 (configurable, but 1 by default)
- **Stack**: 8192 bytes (Arduino-ESP32)
- **Core affinity**: Core 1 on dual-core ESP32, Core 0 on ESP32-S3

### Initialization Sequence

When ESP32 boots with Arduino framework:

```
1. ESP-IDF initializes FreeRTOS kernel
2. Arduino core creates loopTask (priority 1)
3. setup() runs WITHIN loopTask
4. setup() returns
5. loopTask enters infinite loop calling loop()
```

**Critical implication**: Any FreeRTOS tasks you create in `setup()` with priority > 1 will preempt `setup()` immediately!

### Getting/Setting loop() Priority

```cpp
// Get current task priority (call from within loop)
UBaseType_t priority = uxTaskPriorityGet(NULL);

// Change loop() priority (call from within loop)
vTaskPrioritySet(NULL, 2);  // Increase to priority 2
```

### Hybrid vs. Pure FreeRTOS Approaches

#### Hybrid Approach (Current POV Display)
```cpp
void setup() {
    // Initialize hardware
    strip.Begin();
    hallDriver.start();

    // Create high-priority task for hall processing
    xTaskCreate(hallProcessingTask, "hallProcessor",
                2048, nullptr, 3, nullptr);  // Priority 3
}

void loop() {
    // Main rendering logic runs at priority 1
    renderEffect();
    strip.Show();
}
```

**Pros**:
- Familiar Arduino structure
- Easy to prototype
- Can still use Arduino libraries expecting loop()

**Cons**:
- Mixed programming models
- Harder to reason about task priorities
- Loop timing depends on what else is scheduled

#### Pure FreeRTOS Approach
```cpp
void renderTask(void* params) {
    while (1) {
        renderEffect();
        strip.Show();
        // Optionally yield or delay
    }
}

void setup() {
    // Initialize hardware
    strip.Begin();
    hallDriver.start();

    // Create all tasks
    xTaskCreate(hallProcessingTask, "hallProc", 2048, nullptr, 3, nullptr);
    xTaskCreate(renderTask, "render", 4096, nullptr, 2, nullptr);
}

void loop() {
    // Empty or minimal monitoring code
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

**Pros**:
- Explicit task priorities and responsibilities
- Better control over timing and preemption
- Clearer task separation

**Cons**:
- More FreeRTOS boilerplate
- Need to understand task lifecycle
- Some Arduino libraries may expect loop() to run regularly

---

## Task Priorities and Scheduling

### ESP32 Priority Levels

- **0**: Idle task (system reserved)
- **1**: Default Arduino loop() priority
- **2-24**: User tasks (higher = more urgent)
- **25+**: System tasks (WiFi, Bluetooth, etc.)

### ESP32-S3 Dual-Core Scheduling

ESP32-S3 has **two cores** (Core 0 and Core 1), each with independent schedulers:

1. **Task Affinity**: Tasks can be pinned to a core or float between cores
2. **Independent Scheduling**: Each core picks highest-priority ready task
3. **Preemption Preference**: Scheduler prefers current core when multiple cores can run a task

### Priority Assignment Strategy

For POV display timing requirements (85-139 microseconds per degree at 1200-1940 RPM):

```
Priority 5: ISR Handler (GPIO interrupt) - HIGHEST
    ↓
Priority 3: Hall Processing Task (xQueueReceive from ISR)
    ↓ (processes timestamp, updates RevolutionTimer)
    ↓
Priority 2: Rendering Task (if pure FreeRTOS approach)
    ↓ (calculates angles, renders pixels, calls strip.Show())
    ↓
Priority 1: Main loop / background tasks
    ↓
Priority 0: Idle task (FreeRTOS manages CPU sleep)
```

**Rationale**:
- **ISR priority highest**: Hardware interrupt fires immediately on hall sensor trigger
- **Hall processing priority 3**: Process timestamp with minimal latency to avoid jitter
- **Rendering priority 2**: Higher than loop, but yields to hall processing
- **Loop priority 1**: Background tasks like serial monitoring, effect switching

### Time Slicing Behavior

Tasks at **same priority** share CPU time via **round-robin scheduling**:
- Default tick rate: 1000 Hz (1ms time slice)
- Each task gets at most 1ms before scheduler switches to next ready task at same priority

For timing-critical rendering, avoid having multiple tasks at same priority competing for CPU.

---

## Queue-Based Communication

### What Are FreeRTOS Queues?

Queues are **thread-safe FIFO buffers** for passing data between tasks or from ISRs to tasks.

**Key Properties**:
- Data is **copied** into queue (not passed by reference)
- Supports **blocking** reads/writes with timeout
- **ISR-safe** variants for interrupt context (`xQueueSendFromISR`, `xQueueReceiveFromISR`)

### Queue Operations

```cpp
// Create queue
QueueHandle_t queue = xQueueCreate(
    10,                    // Queue length (max items)
    sizeof(MyDataType)     // Size of each item
);

// Send to queue (from task)
MyDataType data = {...};
xQueueSend(queue, &data, portMAX_DELAY);  // Block forever until space available

// Send from ISR
BaseType_t higherPriorityTaskWoken = pdFALSE;
xQueueSendFromISR(queue, &data, &higherPriorityTaskWoken);
if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();  // Request context switch
}

// Receive from queue (blocks until data available)
MyDataType received;
if (xQueueReceive(queue, &received, portMAX_DELAY) == pdPASS) {
    // Process received data
}
```

### Queue Variants

1. **xQueueSend**: Add to back of queue (FIFO)
2. **xQueueSendToFront**: Add to front (priority message)
3. **xQueueOverwrite**: Overwrites oldest item if queue full (size-1 queue)
4. **xQueuePeek**: Read without removing from queue

### POV Display Usage Pattern

Current implementation uses **xQueueOverwrite** for hall sensor events:

```cpp
// In ISR (HallEffectDriver.cpp)
void IRAM_ATTR HallEffectDriver::sensorTriggered_ISR(void *arg) {
    HallEffectEvent event;
    event.triggerTimestamp = esp_timer_get_time();

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xQueueOverwriteFromISR(_triggerEventQueue, &event, &higherPriorityTaskWoken);

    if (higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();  // Wake hall processing task immediately
    }
}

// In hall processing task (main.cpp)
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();

    while (1) {
        // Block waiting for hall sensor event
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);  // Process immediately
        }
    }
}
```

**Why xQueueOverwrite?**
- Only the **latest** hall sensor timestamp matters
- Queue size of 1 saves memory
- Naturally handles debouncing - older triggers are discarded
- No blocking in ISR (overwrite always succeeds)

---

## POV Display Architecture Patterns

### Current Hybrid Architecture

```
[GPIO ISR] (Priority ~5, runs in interrupt context)
    ↓ (captures timestamp via esp_timer_get_time())
    ↓ (sends to queue via xQueueOverwriteFromISR)
    ↓
[Hall Processing Task] (Priority 3)
    ↓ (blocks on xQueueReceive)
    ↓ (processes timestamp, updates RevolutionTimer)
    ↓
[Arduino loop()] (Priority 1)
    ↓ (calculates arm angles from RevolutionTimer)
    ↓ (renders effect based on angles)
    ↓ (calls strip.Show() to update LEDs)
```

**Timing Analysis**:
- ISR to task wakeup: ~5-10 microseconds (context switch + queue receive)
- Timestamp capture accuracy: Sub-microsecond (esp_timer resolution)
- Rendering loop timing: Depends on loop() scheduling (non-deterministic)

### Pure FreeRTOS Architecture (Alternative)

```
[GPIO ISR] (Priority ~5)
    ↓ (captures timestamp)
    ↓ (sends to queue)
    ↓
[Hall Processing Task] (Priority 3, pinned to Core 0)
    ↓ (processes timestamp)
    ↓ (sends angle update to rendering task via queue)
    ↓
[Rendering Task] (Priority 2, pinned to Core 1)
    ↓ (blocks on angle queue)
    ↓ (renders and calls strip.Show())
```

**Advantages for timing**:
- Rendering task priority explicit (priority 2)
- Core pinning prevents migration overhead
- Clear data flow via queues

**When to use pure approach**:
- Need guaranteed render task priority over other Arduino code
- Want to eliminate jitter from loop() scheduling
- Complex multi-task architecture

---

## Timing Critical Operations

### Achieving Deterministic Timing

For POV display with 85-139 microsecond window per degree at 1940-1200 RPM:

#### 1. ISR Timestamp Capture (Current Implementation)

```cpp
void IRAM_ATTR hallISR() {
    timestamp_t now = esp_timer_get_time();  // <1us resolution
    // ... send to queue
}
```

**Critical**: Use `esp_timer_get_time()` in ISR for sub-microsecond accuracy.
- **Do NOT** use `millis()` or `micros()` - they may have tick granularity issues
- **Do** mark ISR with `IRAM_ATTR` to run from RAM (no flash cache latency)

#### 2. High-Priority Processing Task

```cpp
xTaskCreate(hallProcessingTask, "hallProc",
            2048, nullptr, 3, nullptr);  // Priority 3 > loop priority 1
```

**Why priority 3?**
- Preempts loop() immediately when ISR sends queue event
- Processes timestamp while it's still relevant
- Prevents lower-priority code from delaying timestamp processing

#### 3. Non-Blocking Rendering Loop

Current implementation runs **tight loop** in `loop()`:

```cpp
void loop() {
    if (revTimer.isWarmupComplete() && isRotating) {
        // Calculate angles
        timestamp_t now = esp_timer_get_time();
        timestamp_t elapsed = now - lastHallTime;
        double angle = (elapsed / microsecondsPerDegree) % 360.0;

        // Render at current angle
        renderEffect(angle);
        strip.Show();
    }
    // NO DELAY - loop runs continuously
}
```

**Key insight**: Loop runs as fast as possible to "catch" the right angle.

**Alternative with task delay**:
```cpp
void renderTask(void* params) {
    while (1) {
        // Render at current angle
        timestamp_t now = esp_timer_get_time();
        double angle = calculateCurrentAngle(now);
        renderEffect(angle);
        strip.Show();

        // Yield CPU briefly
        vTaskDelay(1);  // 1 tick = 1ms delay
    }
}
```

**Trade-off**: Yielding reduces CPU usage but introduces up to 1ms jitter.

#### 4. SPI Transfer Speed

Current configuration uses **40MHz SPI** for SK9822/APA102 LEDs:

```cpp
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
```

**Measured Show() times (NeoPixelBus @ 40MHz)**:
- 30 LEDs: ~45 μs
- 33 LEDs: ~50 μs
- 42 LEDs: ~58 μs

At 1940 RPM (85.9 μs per 1° column):
- 30 LEDs: Leaves ~41 μs for rendering (48% headroom)
- 33 LEDs: Leaves ~36 μs for rendering (42% headroom)
- 42 LEDs: Leaves ~28 μs for rendering (33% headroom)

### Jitter Sources and Mitigation

#### Source 1: ISR Latency
**Problem**: Other interrupts can delay hall sensor ISR

**Mitigation**:
- Use `gpio_install_isr_service(0)` for dedicated ISR handler
- Mark ISR with `IRAM_ATTR` to avoid flash cache misses
- Keep ISR minimal (timestamp capture + queue send only)

#### Source 2: Task Scheduling Delays
**Problem**: Lower priority task can delay timestamp processing

**Current solution**: Hall processing task at priority 3 preempts loop()

**Alternative**: Use even higher priority (4-5) if WiFi/BLE tasks interfere

#### Source 3: Cache Misses in Rendering Code
**Problem**: Code execution from flash cache can have variable latency

**Mitigation**:
- Keep rendering code simple (no heavy math in tight loop)
- Use lookup tables in RAM instead of calculations
- Consider `IRAM_ATTR` for critical render functions

#### Source 4: Tick Interrupt Preemption
**Problem**: 1ms tick interrupt preempts all user tasks briefly

**Impact**: Minimal (~few microseconds every 1ms)

**If critical**: Can increase tick rate or use high-resolution timers

---

## Pitfalls to Avoid

### 1. Priority Inversion

**Problem**: Low-priority task holds resource needed by high-priority task

**Example**:
```cpp
// Task A (priority 3) waiting for mutex held by Task C (priority 1)
// Task B (priority 2) runs instead, starving Task A
```

**Solution**: Use **priority inheritance** mutexes:
```cpp
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();  // Automatically enables priority inheritance
```

**POV Display**: Current design avoids mutexes by using one-way queues (ISR → Task)

### 2. Task Starvation

**Problem**: High-priority task never yields, blocking all lower-priority tasks

**Example**:
```cpp
void hallProcessingTask(void* params) {
    while (1) {
        // Process event
        // BUG: No delay/yield - blocks lower tasks forever
    }
}
```

**Solution**: Always block on queue or use delays:
```cpp
void hallProcessingTask(void* params) {
    while (1) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            // Process - task automatically yields when queue empty
        }
    }
}
```

**POV Display**: Hall task blocks on `xQueueReceive` (correct)

### 3. Watchdog Timeouts

ESP32 has **two watchdogs**:

#### Interrupt Watchdog Timer (IWDT)
**Monitors**: ISR execution and task switching
**Timeout**: ~300ms of blocked task switching
**Trigger**: `Interrupt wdt timeout on CPU0`

**How to avoid**:
- Keep ISRs short (<10us typical, <1ms absolute max)
- Don't disable interrupts for long periods
- Use `taskENTER_CRITICAL` sparingly

#### Task Watchdog Timer (TWDT)
**Monitors**: Specific tasks (including idle tasks)
**Timeout**: Tasks not yielding for configured period (default 5 seconds)
**Trigger**: `Task watchdog got triggered`

**How to avoid**:
- Add delays/yields to long-running tasks
- Subscribe/unsubscribe tasks to TWDT if needed
- Don't starve idle task with priority 1+ tasks that never yield

**POV Display**: Current rendering loop has **no delay**, but:
- Loop priority 1 allows preemption by higher tasks
- Hall task yields when queue empty
- Idle task can run when all tasks blocked/yielded

### 4. Stack Overflow

**Problem**: Task stack too small for local variables + call depth

**Symptoms**: Random crashes, corruption, watchdog resets

**Example**:
```cpp
xTaskCreate(myTask, "task", 1024, nullptr, 1, nullptr);  // Only 1KB stack

void myTask(void* params) {
    char bigBuffer[2048];  // OVERFLOW - exceeds 1KB stack
    // ...
}
```

**Solution**: Monitor stack usage and increase size:
```cpp
// Check remaining stack (from within task)
UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
Serial.printf("Stack remaining: %d bytes\n", stackRemaining);

// Increase stack size if needed
xTaskCreate(myTask, "task", 4096, nullptr, 1, nullptr);  // 4KB stack
```

**POV Display**: Hall processing task uses 2048 bytes (2KB) - should monitor

### 5. Blocking in ISRs

**Never do this in an ISR**:
```cpp
void IRAM_ATTR myISR() {
    vTaskDelay(100);              // WRONG - ISR can't block
    xQueueSend(queue, &data, 10); // WRONG - uses blocking variant
    Serial.println("ISR");        // WRONG - Serial.print can block
}
```

**Correct ISR pattern**:
```cpp
void IRAM_ATTR myISR() {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(queue, &data, &woken);  // Non-blocking ISR variant
    portYIELD_FROM_ISR(woken);                // Request context switch if needed
}
```

**POV Display**: Current ISR correctly uses `xQueueOverwriteFromISR`

### 6. Float Operations in ISRs

**Problem**: ESP32 FPU context not saved/restored in ISR

**Example**:
```cpp
void IRAM_ATTR myISR() {
    float angle = 3.14159 * someValue;  // May corrupt FPU state
}
```

**Solution**: Avoid float/double math in ISRs, use integer arithmetic

**POV Display**: Current ISR only captures timestamp (integer) - correct

---

## Code Examples

### Example 1: Current POV Display Pattern (Hybrid)

```cpp
// ISR captures timestamp with minimal latency
void IRAM_ATTR HallEffectDriver::sensorTriggered_ISR(void *arg) {
    HallEffectEvent event;
    event.triggerTimestamp = esp_timer_get_time();  // Integer timestamp

    BaseType_t woken = pdFALSE;
    xQueueOverwriteFromISR(_triggerEventQueue, &event, &woken);

    if (woken) {
        portYIELD_FROM_ISR();  // Wake hall processing task
    }
}

// High-priority task processes timestamp
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();

    while (1) {
        // Block waiting for event (yields CPU)
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);
        }
    }
}

// Setup creates task with priority 3
void setup() {
    strip.Begin();
    hallDriver.start();

    xTaskCreate(hallProcessingTask, "hallProcessor",
                2048, nullptr, 3, nullptr);  // Priority 3
}

// Main loop renders at priority 1
void loop() {
    if (revTimer.isWarmupComplete()) {
        timestamp_t now = esp_timer_get_time();
        double angle = calculateAngle(now);
        renderEffect(angle);
        strip.Show();
    }
    // No delay - tight loop for timing precision
}
```

**Why this works**:
- ISR captures timestamp with <1us latency
- High-priority task processes immediately
- Loop runs continuously to catch precise angles
- Total latency: ~5-15us from trigger to processing

### Example 2: Pure FreeRTOS with Angle Queue

```cpp
// Angle update message
typedef struct {
    float innerArmDegrees;
    float middleArmDegrees;
    float outerArmDegrees;
    timestamp_t timestamp;
} AngleUpdate;

QueueHandle_t angleQueue;

// Hall processing task calculates angles
void hallProcessingTask(void* params) {
    HallEffectEvent event;
    QueueHandle_t hallQueue = hallDriver.getEventQueue();

    while (1) {
        if (xQueueReceive(hallQueue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);

            // Calculate angles and send to render task
            AngleUpdate update;
            timestamp_t now = esp_timer_get_time();
            timestamp_t elapsed = now - event.triggerTimestamp;
            double microsecondsPerDeg = revTimer.getMicrosecondsPerRevolution() / 360.0;

            update.middleArmDegrees = (elapsed / microsecondsPerDeg) % 360.0;
            update.innerArmDegrees = fmod(update.middleArmDegrees + 120.0, 360.0);
            update.outerArmDegrees = fmod(update.middleArmDegrees + 240.0, 360.0);
            update.timestamp = now;

            // Send to render task (non-blocking)
            xQueueOverwrite(angleQueue, &update);
        }
    }
}

// Render task updates LEDs
void renderTask(void* params) {
    AngleUpdate angles;

    while (1) {
        if (xQueueReceive(angleQueue, &angles, portMAX_DELAY) == pdPASS) {
            RenderContext ctx = {
                .currentMicros = angles.timestamp,
                .innerArmDegrees = angles.innerArmDegrees,
                .middleArmDegrees = angles.middleArmDegrees,
                .outerArmDegrees = angles.outerArmDegrees
            };

            renderEffect(ctx);
            strip.Show();
        }
    }
}

void setup() {
    angleQueue = xQueueCreate(1, sizeof(AngleUpdate));

    // Create tasks with explicit priorities
    xTaskCreatePinnedToCore(hallProcessingTask, "hallProc",
                            4096, nullptr, 3, nullptr, 0);  // Core 0, Priority 3
    xTaskCreatePinnedToCore(renderTask, "render",
                            4096, nullptr, 2, nullptr, 1);  // Core 1, Priority 2
}

void loop() {
    // Minimal monitoring code
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

**Advantages**:
- Clear task separation and priorities
- Angle calculation in high-priority task
- Rendering on separate core (Core 1)
- Explicit queue for angle updates

**Disadvantages**:
- More complex than hybrid approach
- Higher memory usage (two large stacks)
- Overkill for current POV display needs

### Example 3: Render Task with Timing Budget

```cpp
void renderTask(void* params) {
    const uint32_t renderBudgetUs = 80;  // 80us budget per render

    while (1) {
        timestamp_t startTime = esp_timer_get_time();

        // Calculate current angle
        timestamp_t now = esp_timer_get_time();
        double angle = calculateCurrentAngle(now);

        // Render effect
        renderEffect(angle);
        strip.Show();

        // Check timing budget
        timestamp_t elapsed = esp_timer_get_time() - startTime;
        if (elapsed > renderBudgetUs) {
            Serial.printf("WARNING: Render took %lluus (budget %uus)\n",
                         elapsed, renderBudgetUs);
        }

        // Small delay to avoid tight loop (optional)
        vTaskDelay(1);  // Yield for 1 tick (~1ms)
    }
}
```

**When to use**:
- Monitoring render performance
- Detecting timing violations
- Development/debugging

### Example 4: Stack Monitoring

```cpp
void hallProcessingTask(void* params) {
    static uint32_t minStackRemaining = UINT32_MAX;

    while (1) {
        // Process events...

        // Periodically check stack usage
        static uint32_t checkCounter = 0;
        if (++checkCounter % 1000 == 0) {
            UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
            if (stackRemaining < minStackRemaining) {
                minStackRemaining = stackRemaining;
                Serial.printf("Hall task min stack remaining: %u bytes\n",
                             minStackRemaining);
            }
        }
    }
}
```

**When to use**:
- Determining optimal stack size
- Detecting near-overflow conditions
- Optimizing memory usage

---

## Recommendations for POV Display

### Current Architecture Assessment

**Strengths**:
- ISR timestamp capture is excellent (IRAM_ATTR, esp_timer_get_time)
- Queue-based ISR-to-task communication is correct pattern
- High-priority hall processing task prevents timestamp processing jitter
- Tight rendering loop provides precise angle-based rendering

**Potential Improvements**:

#### 1. Monitor Stack Usage
```cpp
// Add to hallProcessingTask
UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
Serial.printf("Hall task stack: %u bytes remaining\n", stackRemaining);
```

If remaining <512 bytes, increase stack size.

#### 2. Add Timing Instrumentation
```cpp
// In loop(), measure render time
static uint64_t maxRenderTime = 0;
timestamp_t renderStart = esp_timer_get_time();
renderEffect(ctx);
strip.Show();
timestamp_t renderTime = esp_timer_get_time() - renderStart;
if (renderTime > maxRenderTime) {
    maxRenderTime = renderTime;
    Serial.printf("Max render time: %lluus\n", maxRenderTime);
}
```

This helps verify you're within 85us window.

#### 3. Consider Core Pinning (If Needed)
```cpp
// Pin hall processing to Core 0, loop runs on Core 1 automatically
xTaskCreatePinnedToCore(hallProcessingTask, "hallProc",
                        2048, nullptr, 3, nullptr, 0);  // Core 0
```

**When to use**: If you add WiFi/BLE tasks and notice jitter.

#### 4. Pure FreeRTOS Only If Necessary

Don't switch to pure FreeRTOS approach unless:
- You measure unacceptable jitter in current implementation
- You need to add complex background tasks (WiFi, logging)
- You want explicit control over render task priority

Current hybrid approach is simpler and likely sufficient.

### Jitter Elimination Checklist

- ✅ ISR captures timestamp immediately (esp_timer_get_time)
- ✅ ISR runs from RAM (IRAM_ATTR)
- ✅ ISR minimal (timestamp + queue send only)
- ✅ Queue-based communication (no polling)
- ✅ High-priority processing task (priority 3)
- ✅ SPI fast enough (40MHz = 45-58us for 30-42 LEDs)
- ⚠️ Stack monitoring needed (verify 2048 bytes sufficient)
- ⚠️ Timing instrumentation recommended (measure actual jitter)
- 🔲 Consider core pinning if adding WiFi/BLE

---

## References and Further Reading

### Official Documentation
- [ESP-IDF FreeRTOS (IDF) Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)
- [ESP32 Watchdogs Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html)

### Task Management
- [ESP32 FreeRTOS Scheduler Explained](https://controllerstech.com/esp32-freertos-task-scheduler/)
- [ESP32 FreeRTOS Task Priority and Stack Management](https://controllerstech.com/esp32-freertos-task-priority-stack-management/)
- [Getting Started with FreeRTOS on ESP32](https://controllerstech.com/freertos-esp32-esp-idf-task-management/)

### Arduino Integration
- [ESP32 with FreeRTOS (Arduino IDE) - Random Nerd Tutorials](https://randomnerdtutorials.com/esp32-freertos-arduino-tasks/)
- [Multitasking on ESP32 with Arduino and FreeRTOS](https://simplyexplained.com/blog/multitasking-esp32-arduino-freertos/)
- [Using FreeRTOS with ESP32 and Arduino - Wolles Elektronikkiste](https://wolles-elektronikkiste.de/en/using-freertos-with-esp32-and-arduino)

### Queue Communication
- [ESP32 FreeRTOS Queues: Inter-Task Communication - Random Nerd Tutorials](https://randomnerdtutorials.com/esp32-freertos-queues-inter-task-arduino/)
- [ESP32 Arduino: Communication between tasks using FreeRTOS queues](https://techtutorialsx.com/2017/09/13/esp32-arduino-communication-between-tasks-using-freertos-queues/)
- [ESP32 FreeRTOS Inter Task Communication Tutorial](https://controllerstech.com/esp32-freertos-inter-task-communication/)

### Interrupt Handling
- [FreeRTOS Hardware Interrupts - DigiKey](https://www.digikey.com/en/maker/projects/introduction-to-rtos-solution-to-part-9-hardware-interrupts/3ae7a68462584e1eb408e1638002e9ed)
- [Interrupt Handling in FreeRTOS Context](https://circuitlabs.net/interrupt-handling-in-freertos-context/)
- [FreeRTOS Interrupt Management with Arduino](https://microcontrollerslab.com/freertos-interrupt-management-examples-with-arduino/)

### Performance and Timing
- [ESP32 Speed Optimization Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/speed.html)
- [FreeRTOS Context Switching Measurement](https://stackoverflow.com/questions/30975423/freertos-how-to-measure-context-switching-time)

---

**Document Version**: 1.0
**Last Updated**: 2025-11-27
**Author**: Research compilation for POV Display Project
