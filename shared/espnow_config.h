#ifndef POV_ESPNOW_CONFIG_H
#define POV_ESPNOW_CONFIG_H

#include <stdint.h>
#include <esp_wifi.h>

// MAC Addresses - Update these with actual device MACs
// Run the MAC address sketch on each device to get these values
// (See docs/ir-control-spec.md Phase 0.5)
const uint8_t MOTOR_CONTROLLER_MAC[] = {0x34, 0xB7, 0xDA, 0x53, 0x00, 0xB4};  // ESP32-S3-Zero
const uint8_t DISPLAY_MAC[] = {0x30, 0x30, 0xF9, 0x33, 0xE4, 0x60};           // Seeed XIAO ESP32S3

// ESP-NOW Configuration
// Both devices sit <100mm apart when assembled, so low power is fine
const uint8_t ESPNOW_CHANNEL = 4;
const wifi_power_t ESPNOW_TX_POWER = WIFI_POWER_5dBm;

#endif // POV_ESPNOW_CONFIG_H
