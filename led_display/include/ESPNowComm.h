#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <cstdint>
#include "messages.h"
#include "types.h"

// Initialize ESP-NOW communication with motor controller
// Sets up WiFi in STA mode, configures channel/power, adds motor controller as peer
void setupESPNow();

// =============================================================================
// Calibration data functions (used by CalibrationEffect)
// =============================================================================

// Send batch of accelerometer samples
// Called by CalibrationEffect when sample buffer is full
void sendAccelSamples(const AccelSampleMsg& msg);

// Send hall event (individual trigger)
// Called by hallProcessingTask when g_calibrationActive is true
void sendHallEvent(timestamp_t timestamp_us, period_t period_us, rotation_t rotation_num);

#endif // ESPNOW_COMM_H
