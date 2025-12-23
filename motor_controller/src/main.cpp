#include <Arduino.h>
#include "motor_control.h"
#include "motor_speed.h"
#include "led_indicator.h"
#include "hardware_config.h"
#include "espnow_comm.h"
#include "remote_input.h"
#include "command_processor.h"

void setup() {
    Serial.begin(115200);
    Serial.println("POV Motor Controller - ESP32-S3-Zero");

    motorInit();
    motorSpeedInit();
    ledInit();
    setupESPNow();
    remoteInputInit();

    ledShowStopped();
    Serial.println("Ready. Use IR remote: POWER=on/off, REW/FF=speed control");
}

void loop() {
    ledLoop();
    motorLoop();  // Handle brake timing (non-blocking)
    processCommand(remoteInputPoll());
    delay(10);
}
