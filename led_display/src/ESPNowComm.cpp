#include "ESPNowComm.h"
#include "EffectManager.h"
#include "messages.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>

#include "espnow_config.h"

// Forward declare global (defined in main.cpp)
extern EffectManager effectManager;

// ESP-NOW receive callback - runs on WiFi task (Core 0)
// Keep this fast - just send commands to queue
static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len < 1) return;

    uint8_t msgType = data[0];

    switch (msgType) {
        case MSG_BRIGHTNESS_UP: {
            EffectCommand cmd = {EffectCommandType::BRIGHTNESS_UP, 0};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
            break;
        }
        case MSG_BRIGHTNESS_DOWN: {
            EffectCommand cmd = {EffectCommandType::BRIGHTNESS_DOWN, 0};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
            break;
        }
        case MSG_SET_EFFECT: {
            if (len < sizeof(SetEffectMsg)) {
                Serial.println("[ESPNOW] SetEffect msg too short");
                return;
            }
            const SetEffectMsg* msg = reinterpret_cast<const SetEffectMsg*>(data);
            EffectCommand cmd = {EffectCommandType::SET_EFFECT, msg->effect_number};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
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

    // Initialize WiFi in station mode (replaces WiFi.mode(WIFI_OFF))
    WiFi.mode(WIFI_STA);

    // Set channel (must match motor controller)
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

    // Register callbacks
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    // Add motor controller as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MOTOR_CONTROLLER_MAC, 6);
    peer.channel = 0;  // Use current channel
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW] Failed to add motor controller peer!");
        return;
    }

    Serial.printf("[ESPNOW] Ready. Motor controller MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        MOTOR_CONTROLLER_MAC[0], MOTOR_CONTROLLER_MAC[1], MOTOR_CONTROLLER_MAC[2],
        MOTOR_CONTROLLER_MAC[3], MOTOR_CONTROLLER_MAC[4], MOTOR_CONTROLLER_MAC[5]);
}

void sendTelemetry(uint32_t timestamp_us, uint16_t hall_avg_us, uint16_t revolutions) {
    TelemetryMsg msg;
    msg.timestamp_us = timestamp_us;
    msg.hall_avg_us = hall_avg_us;
    msg.revolutions = revolutions;

    esp_err_t result = esp_now_send(MOTOR_CONTROLLER_MAC, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    if (result == ESP_OK) {
        Serial.printf("[ESPNOW] Sent telemetry: hall=%uus revs=%u\n", hall_avg_us, revolutions);
    }
}
