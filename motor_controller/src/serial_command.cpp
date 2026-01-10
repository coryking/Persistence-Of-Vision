#include "serial_command.h"
#include "telemetry_capture.h"
#include <Arduino.h>
#include <cstring>
#include <cctype>

static char s_buf[32];
static size_t s_len = 0;

static void dispatch(char* cmd) {
    // Uppercase for case-insensitive matching
    for (char* p = cmd; *p; p++) *p = toupper(*p);

    if (strcmp(cmd, "START") == 0)       captureStartSerial();
    else if (strcmp(cmd, "STOP") == 0)   captureStopSerial();
    else if (strcmp(cmd, "DUMP") == 0)   captureDump();
    else if (strcmp(cmd, "DELETE") == 0) captureDeleteSerial();
    else if (strcmp(cmd, "STATUS") == 0) captureStatus();
    else if (strcmp(cmd, "LIST") == 0)   captureList();
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
