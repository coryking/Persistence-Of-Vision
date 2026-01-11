#include "serial_command.h"
#include "telemetry_capture.h"
#include "espnow_comm.h"
#include "motor_speed.h"
#include "motor_control.h"
#include "led_indicator.h"
#include "command_processor.h"
#include <Arduino.h>
#include <cstring>
#include <cctype>
#include <cstdlib>

static char s_buf[32];
static size_t s_len = 0;

// Convert CaptureState enum to string
static const char* captureStateStr(CaptureState state) {
    switch (state) {
        case CaptureState::IDLE:      return "IDLE";
        case CaptureState::RECORDING: return "RECORDING";
        case CaptureState::FULL:      return "FULL";
        default:                      return "UNKNOWN";
    }
}

// Unified STATUS command - returns all state in key: value format
static void statusSerial() {
    Serial.printf("motor_enabled: %d\n", isEnabled() ? 1 : 0);
    Serial.printf("speed_position: %d\n", getPosition());
    Serial.printf("capture_state: %s\n", captureStateStr(getCaptureState()));
    Serial.printf("rx_hall_packets: %u\n", getRxHallPackets());
    Serial.printf("rx_accel_packets: %u\n", getRxAccelPackets());
    Serial.printf("rx_accel_samples: %u\n", getRxAccelSamples());
}

// MOTOR_ON - idempotent power on
static void motorOnSerial() {
    if (!motorPowerOn()) {
        Serial.println("ERR: Already running");
        return;
    }
    motorSetSpeed(getCurrentPWM());
    ledShowRunning();
    Serial.println("OK");
}

// MOTOR_OFF - idempotent power off
static void motorOffSerial() {
    if (!motorPowerOff()) {
        Serial.println("ERR: Already stopped");
        return;
    }
    motorSetSpeed(0);  // Triggers brake sequence
    ledShowStopped();
    Serial.println("OK");
}

// BUTTON <n> - trigger a Command enum value (emulates IR button press)
static void buttonSerial(int cmdNum) {
    if (cmdNum < 0 || cmdNum > 255) {
        Serial.println("ERR: Invalid command number");
        return;
    }
    processCommand(static_cast<Command>(cmdNum));
    Serial.println("OK");
}

static void dispatch(char* cmd) {
    // Uppercase for case-insensitive matching
    for (char* p = cmd; *p; p++) *p = toupper(*p);

    // Handle commands with arguments first
    if (strncmp(cmd, "BUTTON ", 7) == 0) {
        int cmdNum = atoi(cmd + 7);
        buttonSerial(cmdNum);
        return;
    }

    // Simple commands
    if (strcmp(cmd, "START") == 0)       captureStartSerial();
    else if (strcmp(cmd, "STOP") == 0)   captureStopSerial();
    else if (strcmp(cmd, "DUMP") == 0)   captureDump();
    else if (strcmp(cmd, "DELETE") == 0) captureDeleteSerial();
    else if (strcmp(cmd, "STATUS") == 0) statusSerial();
    else if (strcmp(cmd, "LIST") == 0)   captureList();
    else if (strcmp(cmd, "MOTOR_ON") == 0)  motorOnSerial();
    else if (strcmp(cmd, "MOTOR_OFF") == 0) motorOffSerial();
    else if (strcmp(cmd, "RXRESET") == 0) { resetEspNowStats(); Serial.println("OK"); }
    else Serial.println("ERR: Unknown command");
}

void serialCommandPoll() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_len > 0) {
                s_buf[s_len] = '\0';
                dispatch(s_buf);
                s_len = 0;
            }
        } else if (s_len < sizeof(s_buf) - 1) {
            s_buf[s_len++] = c;
        }
    }
}
