#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

// Initialize ESP-NOW communication with display
// Sets up WiFi in STA mode, configures channel/power, adds display as peer
void setupESPNow();

// Send brightness increment command to display
void sendBrightnessUp();

// Send brightness decrement command to display
void sendBrightnessDown();

#endif // ESPNOW_COMM_H
