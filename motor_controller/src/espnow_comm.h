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

// Send IR RIGHT button to display
void sendEffectRight();

// Send IR LEFT button to display
void sendEffectLeft();

// Send IR UP button to display
void sendEffectUp();

// Send IR DOWN button to display
void sendEffectDown();

// Send IR ENTER button to display
void sendEffectEnter();

// Print ESP-NOW receive statistics (for debugging) - DEPRECATED, use getters below
void printEspNowStats();

// Reset ESP-NOW receive statistics
void resetEspNowStats();

// Get ESP-NOW receive statistics (for unified STATUS command)
uint32_t getRxHallPackets();
uint32_t getRxAccelPackets();
uint32_t getRxAccelSamples();
uint32_t getRxRotorStatsPackets();

// Send reset command to LED display (resets RotorDiagnosticStats counters)
void sendResetRotorStats();

// Send display power command to LED display (follows motor power state)
void sendDisplayPower(bool enabled);

#endif // ESPNOW_COMM_H
