#include "espnow_comm.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "messages.h"
#include "espnow_config.h"
#include "telemetry_capture.h"

// ESP-NOW receive callback - runs on WiFi task
static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len < 1) return;

    uint8_t msgType = data[0];

    // Route high-rate telemetry to capture (or discard if not recording)
    // No serial output for these - prevents buffer overflow
    if (msgType == MSG_ACCEL_SAMPLES || msgType == MSG_HALL_EVENT) {
        if (isCapturing()) {
            captureWrite(msgType, data, len);
        }
        return;
    }

    switch (msgType) {
        case MSG_TELEMETRY: {
            if (len < sizeof(TelemetryMsg)) {
                Serial.println("[ESPNOW] Telemetry msg too short");
                return;
            }

            // Capture if recording
            if (isCapturing()) {
                captureWrite(msgType, data, len);
            }

            // Keep RPM display (low rate, useful feedback)
            const TelemetryMsg* msg = reinterpret_cast<const TelemetryMsg*>(data);
            uint32_t rpm = (msg->hall_avg_us > 0) ? (60000000UL / msg->hall_avg_us) : 0;
            Serial.printf("[TELEMETRY] %lu RPM\n", rpm);
            break;
        }

        default:
            Serial.printf("[ESPNOW] Unknown message type: %u\n", msgType);
            break;
    }
}

// ESP-NOW send callback - optional, for debugging
// Note: ESP-IDF 5.x uses wifi_tx_info_t instead of mac address
static void onDataSent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESPNOW] Send failed");
    }
}

void setupESPNow() {
    Serial.println("[ESPNOW] Initializing...");

    // Initialize WiFi in station mode
    WiFi.mode(WIFI_STA);

    // Set channel (must match display)
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Set low TX power (devices are <100mm apart)
    WiFi.setTxPower(ESPNOW_TX_POWER);

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init failed!");
        return;
    }

    // Verify ESP-NOW version (need v2 for 1470 byte payloads)
    uint32_t version;
    if (esp_now_get_version(&version) == ESP_OK) {
        Serial.printf("[ESPNOW] Version: %lu (need 2 for v2.0 large payloads)\n", version);
    }

    // Register callbacks
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    // Add display as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, DISPLAY_MAC, 6);
    peer.channel = 0;  // Use current channel
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW] Failed to add display peer!");
        return;
    }

    Serial.printf("[ESPNOW] My MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("[ESPNOW] Target (display) MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        DISPLAY_MAC[0], DISPLAY_MAC[1], DISPLAY_MAC[2],
        DISPLAY_MAC[3], DISPLAY_MAC[4], DISPLAY_MAC[5]);
}

void sendBrightnessUp() {
    BrightnessUpMsg msg;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.println("[ESPNOW] Sent brightness UP");
    }
}

void sendBrightnessDown() {
    BrightnessDownMsg msg;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.println("[ESPNOW] Sent brightness DOWN");
    }
}

void sendSetEffect(uint8_t effectNumber) {
    SetEffectMsg msg;
    msg.effect_number = effectNumber;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.printf("[ESPNOW] Effect -> %d\n", effectNumber);
    }
}

void sendEffectModeNext() {
    EffectModeNextMsg msg;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.println("[ESPNOW] Sent effect mode NEXT");
    }
}

void sendEffectModePrev() {
    EffectModePrevMsg msg;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.println("[ESPNOW] Sent effect mode PREV");
    }
}

void sendEffectParamUp() {
    EffectParamUpMsg msg;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.println("[ESPNOW] Sent effect param UP");
    }
}

void sendEffectParamDown() {
    EffectParamDownMsg msg;
    esp_err_t result = esp_now_send(DISPLAY_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.println("[ESPNOW] Sent effect param DOWN");
    }
}
