#include "remote_input.h"
#include "sagetv_buttons.h"
#include "hardware_config.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>

static IRrecv irrecv(PIN_IR_RECV, 1024, 15, true);
static decode_results results;

void remoteInputInit() {
    irrecv.enableIRIn();
    Serial.println("[IR] Receiver initialized on pin " + String(PIN_IR_RECV));
}

Command remoteInputPoll() {
    if (!irrecv.decode(&results)) {
        return Command::None;
    }

    if (results.decode_type == UNKNOWN) {
        irrecv.resume();
        return Command::None;
    }

    uint16_t code = rc5StripToggleBit((uint16_t)results.value);
    irrecv.resume();

    switch (code) {
        case SAGETV_BTN_1: return Command::Effect1;
        case SAGETV_BTN_2: return Command::Effect2;
        case SAGETV_BTN_3: return Command::Effect3;
        case SAGETV_BTN_4: return Command::Effect4;
        case SAGETV_BTN_5: return Command::Effect5;
        case SAGETV_BTN_6: return Command::Effect6;
        case SAGETV_BTN_7: return Command::Effect7;
        case SAGETV_BTN_8: return Command::Effect8;
        case SAGETV_BTN_9: return Command::Effect9;
        case SAGETV_BTN_0: return Command::Effect10;
        case SAGETV_BTN_VOL_UP: return Command::BrightnessUp;
        case SAGETV_BTN_VOL_DOWN: return Command::BrightnessDown;
        default: return Command::None;
    }
}
