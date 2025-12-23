#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <cstdint>

// Initialize ESP-NOW communication with display
// Sets up WiFi in STA mode, configures channel/power, adds display as peer
void setupESPNow();

// Send brightness increment command to display
void sendBrightnessUp();

// Send brightness decrement command to display
void sendBrightnessDown();

// Send set effect command to display
void sendSetEffect(uint8_t effectNumber);

#endif // ESPNOW_COMM_H
