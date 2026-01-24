#include "ESPNowComm.h"
#include "EffectManager.h"
#include "RotorDiagnosticStats.h"
#include "messages.h"

#include <cstddef>  // for offsetof
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
        case MSG_EFFECT_MODE_NEXT: {
            EffectCommand cmd = {EffectCommandType::EFFECT_MODE_NEXT, 0};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
            break;
        }
        case MSG_EFFECT_MODE_PREV: {
            EffectCommand cmd = {EffectCommandType::EFFECT_MODE_PREV, 0};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
            break;
        }
        case MSG_EFFECT_PARAM_UP: {
            EffectCommand cmd = {EffectCommandType::EFFECT_PARAM_UP, 0};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
            break;
        }
        case MSG_EFFECT_PARAM_DOWN: {
            EffectCommand cmd = {EffectCommandType::EFFECT_PARAM_DOWN, 0};
            xQueueSend(effectManager.getCommandQueue(), &cmd, 0);
            break;
        }
        case MSG_RESET_ROTOR_STATS: {
            // Reset diagnostic stats counters
            RotorDiagnosticStats::instance().reset();
            Serial.println("[ESPNOW] Rotor stats reset");
            break;
        }
        default:
            Serial.printf("[ESPNOW] Unknown message type: %u\n", msgType);
            break;
    }
}

// ESP-NOW send callback
// Note: ESP-IDF 5.x uses wifi_tx_info_t instead of mac address
static void onDataSent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESPNOW] No ACK from peer");
    }
}

// Helper to send and log errors
// Records result in RotorDiagnosticStats for monitoring
static bool espnowSend(const uint8_t* data, size_t len, const char* msgName) {
    esp_err_t result = esp_now_send(MOTOR_CONTROLLER_MAC, data, len);
    bool success = (result == ESP_OK);

    // Record send result for diagnostics (except for stats messages themselves
    // which are already tracked in RotorDiagnosticStats::sendViaEspNow)
    // We track all sends here except MSG_ROTOR_STATS to avoid double-counting
    uint8_t msgType = data[0];
    if (msgType != MSG_ROTOR_STATS) {
        RotorDiagnosticStats::instance().recordEspNowResult(success);
    }

    if (!success) {
        // Possible errors:
        // ESP_ERR_ESPNOW_NOT_INIT - not initialized
        // ESP_ERR_ESPNOW_ARG - invalid argument
        // ESP_ERR_ESPNOW_NO_MEM - internal TX buffer full
        // ESP_ERR_ESPNOW_NOT_FOUND - peer not registered
        // ESP_ERR_ESPNOW_IF - WiFi interface mismatch
        Serial.printf("[ESPNOW] %s queue failed: %s\n", msgName, esp_err_to_name(result));
    }
    return success;
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

    // Verify ESP-NOW version (need v2 for 1470 byte payloads)
    uint32_t version;
    if (esp_now_get_version(&version) == ESP_OK) {
        Serial.printf("[ESPNOW] Version: %lu (need 2 for v2.0 large payloads)\n", version);
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

    // Configure PHY rate for high throughput (devices are <100mm apart)
    // Default 1 Mbps 802.11b has 192µs preamble overhead per packet
    // 54 Mbps 802.11g has 20µs preamble - 34x more packets/sec capacity
    esp_now_rate_config_t rateConfig = {
        .phymode = WIFI_PHY_MODE_11G,
        .rate = WIFI_PHY_RATE_54M,
        .ersu = false,
        .dcm = false
    };
    if (esp_now_set_peer_rate_config(MOTOR_CONTROLLER_MAC, &rateConfig) == ESP_OK) {
        Serial.println("[ESPNOW] PHY rate set to 54 Mbps (802.11g)");
    } else {
        Serial.println("[ESPNOW] Warning: PHY rate config failed, using default 1 Mbps");
    }

    Serial.printf("[ESPNOW] My MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("[ESPNOW] Target (motor controller) MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        MOTOR_CONTROLLER_MAC[0], MOTOR_CONTROLLER_MAC[1], MOTOR_CONTROLLER_MAC[2],
        MOTOR_CONTROLLER_MAC[3], MOTOR_CONTROLLER_MAC[4], MOTOR_CONTROLLER_MAC[5]);
}

// =============================================================================
// Calibration data functions
// =============================================================================

void sendAccelSamples(const AccelSampleMsg& msg) {
    // Calculate actual message size based on sample count
    // Header: type(1) + sample_count(1) + base_timestamp(8) + start_sequence(2) = 12 bytes
    // Plus sample_count * sizeof(AccelSampleWire) = sample_count * 8 bytes
    size_t msgSize = ACCEL_MSG_HEADER_SIZE + (msg.sample_count * sizeof(AccelSampleWire));
    espnowSend(reinterpret_cast<const uint8_t*>(&msg), msgSize, "AccelSamples");
}

void sendHallEvent(timestamp_t timestamp_us, period_t period_us, rotation_t rotation_num) {
    HallEventMsg msg;
    msg.timestamp_us = timestamp_us;
    msg.period_us = period_us;
    msg.rotation_num = rotation_num;
    espnowSend(reinterpret_cast<uint8_t*>(&msg), sizeof(msg), "HallEvent");
}
