"""Constants for POV tools.

Button command numbers correspond to the Command enum in
motor_controller/src/commands.h. Use these with DeviceConnection.button()
to programmatically trigger remote button presses.
"""

from enum import IntEnum


class ButtonCommand(IntEnum):
    """Command enum values for BUTTON serial command.

    These match the Command enum in motor_controller/src/commands.h.
    Use with DeviceConnection.button() to trigger IR button emulation.

    Example:
        conn.button(ButtonCommand.EFFECT10)  # Set calibration effect
        conn.button(ButtonCommand.SPEED_UP)   # Increase motor speed
    """

    # Effects (1-10)
    EFFECT1 = 1
    EFFECT2 = 2
    EFFECT3 = 3
    EFFECT4 = 4
    EFFECT5 = 5
    EFFECT6 = 6
    EFFECT7 = 7
    EFFECT8 = 8
    EFFECT9 = 9
    EFFECT10 = 10  # Calibration effect

    # Brightness
    BRIGHTNESS_UP = 11
    BRIGHTNESS_DOWN = 12

    # Power (prefer MOTOR_ON/MOTOR_OFF serial commands for idempotent control)
    POWER_TOGGLE = 13

    # Speed
    SPEED_UP = 14
    SPEED_DOWN = 15

    # Effect mode/param controls
    EFFECT_MODE_NEXT = 16
    EFFECT_MODE_PREV = 17
    EFFECT_PARAM_UP = 18
    EFFECT_PARAM_DOWN = 19

    # Capture controls (prefer serial commands START/STOP/DELETE for automation)
    CAPTURE_RECORD = 20
    CAPTURE_STOP = 21
    CAPTURE_PLAY = 22
    CAPTURE_DELETE = 23


# Convenience aliases
CALIBRATION_EFFECT = ButtonCommand.EFFECT10
MAX_SPEED_POSITION = 10
