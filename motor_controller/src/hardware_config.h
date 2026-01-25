#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// Platform: Waveshare ESP32-S3-Zero
// Pin assignments per docs/ir-control-spec.md

// L298N Motor Driver Pins
#define PIN_MOTOR_IN1    7   // GPIO7 - Direction control (blue wire)
#define PIN_MOTOR_IN2    8   // GPIO8 - Direction control (yellow wire)
#define PIN_MOTOR_ENA    9   // GPIO9 - PWM speed control (green wire)

// IR Receiver Pin
#define PIN_IR_RECV      2   // GPIO2 - IR signal (orange wire)

// RGB LED Pins (active LOW - common anode)
// TODO: Verify these work on ESP32-S3-Zero
#define PIN_LED_R  17  // GPIO 17 - Red (4.7kΩ resistor)
#define PIN_LED_G  16  // GPIO 16 - Green (11kΩ resistor)
#define PIN_LED_B  21  // GPIO 21 - Blue (2.2kΩ resistor) - changed from 25

// LED timing
#define LED_BLINK_INTERVAL_MS  500  // 500ms on, 500ms off when stopped

// Motor timing
#define BRAKE_DURATION_MS 15000  // Active brake duration when motor stops (15 seconds)

#define PWM_FREQ_HZ   18000  // this sounds good
#define PWM_MAX_VALUE   255  // 8-bit resolution (0-255)

// Speed → PWM Mapping (IR remote control, 10 positions)
// NOTE: Slip ring adds significant drag - PWM boosted
// Positions 1-10 map linearly to 65%-80% PWM
#define SPEED_MIN_POS      1
#define SPEED_MAX_POS     10
#define PWM_MIN_PERCENT   65.0f  // Minimum for reliable starting
#define PWM_MAX_PERCENT  100.0f  // Maximum speed

#endif // HARDWARE_CONFIG_H
