#include "remote_input.h"
#include "sagetv_buttons.h"
#include "hardware_config.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>

static IRrecv irrecv(PIN_IR_RECV, 1024, 15, true);
static decode_results results;

// Debounce timing - ignore repeated codes within this window
static const unsigned long DEBOUNCE_MS = 300;
static unsigned long lastCodeTime = 0;

void remoteInputInit() {
    irrecv.enableIRIn();
    Serial.println("[IR] Receiver initialized on pin " + String(PIN_IR_RECV));
}

Command remoteInputPoll() {
    if (!irrecv.decode(&results)) {
        return Command::None;
    }

    unsigned long now = millis();

    // Debounce - ignore codes that come too quickly (button held or repeat codes)
    if (now - lastCodeTime < DEBOUNCE_MS) {
        irrecv.resume();
        return Command::None;
    }

    // Debug: Show raw IR data
    Serial.printf("[IR] Raw: 0x%04X Type: %d\n", (uint16_t)results.value, results.decode_type);

    if (results.decode_type == UNKNOWN) {
        Serial.println("[IR] UNKNOWN type, ignoring");
        irrecv.resume();
        return Command::None;
    }

    lastCodeTime = now;

    uint16_t code = rc5StripToggleBit((uint16_t)results.value);
    Serial.printf("[IR] Stripped: 0x%04X\n", code);
    irrecv.resume();

    Command cmd = Command::None;
    switch (code) {
        case SAGETV_BTN_1: cmd = Command::Effect1; break;
        case SAGETV_BTN_2: cmd = Command::Effect2; break;
        case SAGETV_BTN_3: cmd = Command::Effect3; break;
        case SAGETV_BTN_4: cmd = Command::Effect4; break;
        case SAGETV_BTN_5: cmd = Command::Effect5; break;
        case SAGETV_BTN_6: cmd = Command::Effect6; break;
        case SAGETV_BTN_7: cmd = Command::Effect7; break;
        case SAGETV_BTN_8: cmd = Command::Effect8; break;
        case SAGETV_BTN_9: cmd = Command::Effect9; break;
        case SAGETV_BTN_0: cmd = Command::Effect10; break;
        case SAGETV_BTN_VOL_UP: cmd = Command::BrightnessUp; break;
        case SAGETV_BTN_VOL_DOWN: cmd = Command::BrightnessDown; break;
        case SAGETV_BTN_POWER: cmd = Command::PowerToggle; break;
        case SAGETV_BTN_FAST_FWD: cmd = Command::SpeedUp; break;
        case SAGETV_BTN_REWIND: cmd = Command::SpeedDown; break;
        default:
            Serial.printf("[IR] Unknown code 0x%04X\n", code);
            return Command::None;
    }

    Serial.printf("[IR] Decoded command: %d\n", (int)cmd);
    return cmd;
}
