#ifndef TELEMETRY_TASK_H
#define TELEMETRY_TASK_H

/**
 * Telemetry Task - Decoupled accelerometer sampling for calibration
 *
 * This FreeRTOS task handles accelerometer data independently of the LED
 * render loop. When enabled, it:
 * - Drains the accelerometer timestamp queue
 * - Reads samples via I2C
 * - Batches samples with delta timestamps
 * - Sends batches via ESP-NOW to motor controller
 *
 * Benefits:
 * - No sample drops when render() skips or busy-waits
 * - Delta timestamps reduce ESP-NOW traffic by 84%
 * - Runs on Core 0 (same as WiFi stack)
 *
 * Usage:
 *   telemetryTaskInit();    // Called from setup()
 *   telemetryTaskStart();   // Called from CalibrationEffect::begin()
 *   telemetryTaskStop();    // Called from CalibrationEffect::end()
 */

/**
 * Initialize the telemetry task (call from setup())
 * Creates the FreeRTOS task but leaves it in waiting state.
 */
void telemetryTaskInit();

/**
 * Start telemetry sampling (call from CalibrationEffect::begin())
 * Wakes the task to begin processing accelerometer samples.
 */
void telemetryTaskStart();

/**
 * Stop telemetry sampling (call from CalibrationEffect::end())
 * Signals the task to flush remaining samples and return to waiting state.
 */
void telemetryTaskStop();

#endif // TELEMETRY_TASK_H
