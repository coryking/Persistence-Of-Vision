#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <cstdint>

// Initialize ESP-NOW communication with motor controller
// Sets up WiFi in STA mode, configures channel/power, adds motor controller as peer
void setupESPNow();

// Send telemetry to motor controller
// Called from hall processing task every ROLLING_AVERAGE_SIZE revolutions
void sendTelemetry(uint32_t timestamp_us, uint16_t hall_avg_us, uint16_t revolutions);

#endif // ESPNOW_COMM_H
