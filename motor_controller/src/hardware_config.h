#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// Platform: Seeed Studio XIAO RP2040
// Pinout reference: docs/xiao-rp2040.webp

// L298N Motor Driver Pins
#define PIN_MOTOR_ENA  D10  // D10 - PWM speed control (green wire)
#define PIN_MOTOR_IN1  D8   // D8  - Direction control (blue wire)
#define PIN_MOTOR_IN2  D9   // D9  - Direction control (yellow wire)

// Rotary Encoder Pins
#define PIN_ENCODER_CLK  D6  // D6 - Clock (green wire)
#define PIN_ENCODER_DT   D5  // D5 - Data (blue wire)
#define PIN_ENCODER_SW   D4  // D4 - Button (yellow wire)

// RGB LED Pins (active LOW - common anode)
#define PIN_LED_R  17  // GPIO 17 - Red (4.7kΩ resistor)
#define PIN_LED_G  16  // GPIO 16 - Green (11kΩ resistor)
#define PIN_LED_B  25  // GPIO 25 - Blue (2.2kΩ resistor)

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
