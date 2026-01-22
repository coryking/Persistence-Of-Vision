#ifndef POV_ESPNOW_CONFIG_H
#define POV_ESPNOW_CONFIG_H

#include <stdint.h>
#include <WiFi.h>

// MAC Addresses (verified via USB serial numbers)
// LED Controller:   30:30:F9:33:E4:60 (Seeed XIAO ESP32S3)
// Motor Controller: 34:B7:DA:53:00:B4 (ESP32-S3-Zero)
const uint8_t MOTOR_CONTROLLER_MAC[] = {0x34, 0xB7, 0xDA, 0x53, 0x00, 0xB4};
const uint8_t DISPLAY_MAC[] = {0x30, 0x30, 0xF9, 0x33, 0xE4, 0x60};

// ESP-NOW Configuration
const uint8_t ESPNOW_CHANNEL = 4;
const wifi_power_t ESPNOW_TX_POWER = WIFI_POWER_8_5dBm;  // Medium-low, devices are close together

#endif // POV_ESPNOW_CONFIG_H
