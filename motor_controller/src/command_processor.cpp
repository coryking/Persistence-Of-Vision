#include "command_processor.h"
#include "espnow_comm.h"
#include "motor_speed.h"
#include "motor_control.h"
#include "led_indicator.h"

void processCommand(Command cmd) {
    switch (cmd) {
        case Command::Effect1:  sendSetEffect(1);  break;
        case Command::Effect2:  sendSetEffect(2);  break;
        case Command::Effect3:  sendSetEffect(3);  break;
        case Command::Effect4:  sendSetEffect(4);  break;
        case Command::Effect5:  sendSetEffect(5);  break;
        case Command::Effect6:  sendSetEffect(6);  break;
        case Command::Effect7:  sendSetEffect(7);  break;
        case Command::Effect8:  sendSetEffect(8);  break;
        case Command::Effect9:  sendSetEffect(9);  break;
        case Command::Effect10: sendSetEffect(10); break;
        case Command::BrightnessUp:   sendBrightnessUp();   break;
        case Command::BrightnessDown: sendBrightnessDown(); break;
        case Command::EffectModeNext: sendEffectModeNext(); break;
        case Command::EffectModePrev: sendEffectModePrev(); break;
        case Command::EffectParamUp:  sendEffectParamUp();  break;
        case Command::EffectParamDown: sendEffectParamDown(); break;
        case Command::PowerToggle:
            togglePower();
            motorSetSpeed(getCurrentPWM());
            if (isEnabled()) {
                ledShowRunning();
            } else {
                ledShowStopped();
            }
            break;
        case Command::SpeedUp:
            if (isEnabled()) {
                speedUp();
                motorSetSpeed(getCurrentPWM());
            }
            break;
        case Command::SpeedDown:
            if (isEnabled()) {
                speedDown();
                motorSetSpeed(getCurrentPWM());
            }
            break;
        case Command::None:
        default:
            break;
    }
}
