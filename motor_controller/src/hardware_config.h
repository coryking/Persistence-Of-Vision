#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// Platform: Waveshare ESP32-S3-Zero
// Pin assignments per docs/ir-control-spec.md

// L298N Motor Driver Pins
#define PIN_MOTOR_IN1    7   // GPIO7 - Direction control (blue wire)
#define PIN_MOTOR_IN2    8   // GPIO8 - Direction control (yellow wire)
#define PIN_MOTOR_ENA    9   // GPIO9 - PWM speed control (green wire)

// Rotary Encoder Pins
#define PIN_ENCODER_CLK  3   // GPIO3 - Clock (green wire)
#define PIN_ENCODER_DT   4   // GPIO4 - Data (blue wire)
#define PIN_ENCODER_SW   5   // GPIO5 - Button (yellow wire)

// IR Receiver Pin (for Phase 2)
#define PIN_IR_RECV      2   // GPIO2 - IR signal (orange wire)

// RGB LED Pins (active LOW - common anode)
// TODO: Verify these work on ESP32-S3-Zero
#define PIN_LED_R  17  // GPIO 17 - Red (4.7kΩ resistor)
#define PIN_LED_G  16  // GPIO 16 - Green (11kΩ resistor)
#define PIN_LED_B  21  // GPIO 21 - Blue (2.2kΩ resistor) - changed from 25

// LED timing
#define LED_BLINK_INTERVAL_MS  500  // 500ms on, 500ms off when stopped

#define PWM_FREQ_HZ   18000  // this sounds good
#define PWM_MAX_VALUE   255  // 8-bit resolution (0-255)

// Encoder → PWM Mapping (linear, calibrated for slip ring configuration)
// NOTE: Slip ring adds significant drag - PWM boosted to 70%-100% range
// Old calibration (without slip ring): RPM = -8170.97 + 205.2253*PWM - 0.809611*PWM²
// Position 0 = OFF, positions 1-40 = 70%-100% PWM
#define ENCODER_MIN_POS      0
#define ENCODER_MAX_POS     40
#define PWM_MIN_PERCENT   60.0f  // Boosted for slip ring drag
#define PWM_MAX_PERCENT  100.0f  // Full power

#endif // HARDWARE_CONFIG_H
