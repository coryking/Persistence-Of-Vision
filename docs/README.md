# POV Display System Documentation

## LED Display Documentation

See docs/led_display/ for LED display-specific documentation:

- **BUILD.md** - Build system, environments, debugging, IntelliSense setup
- **ESP32_REFERENCE.md** - Integer math system, angle units, FPU performance
- **HARDWARE.md** - Physical hardware (power system, components, sensors)
- **TIMING_ANALYSIS.md** - Deep dive on jitter, why NeoPixelBus is critical
- **TIMESTAMP_CAPTURE_OPTIONS.md** - ISR vs hardware timer capture (GPTimer, RMT, PCNT)
- **FREERTOS_INTEGRATION.md** - FreeRTOS patterns and pitfalls
- **PROJECT_COMPARISON.md** - Why queue-based hall sensor works, flag-based fails
- **EFFECT_SYSTEM_DESIGN.md** - Effect architecture and scheduler design
- **POV_DISPLAY.md** - Art-first philosophy, polar coordinates, design principles
- **Timing and Dimensionality Calculations.md** - Mathematical reference

## Motor Controller Documentation

See docs/motor_controller/ for motor controller-specific documentation:

- **datasheets/** - L298N motor driver datasheets
- **archive/** - Outdated docs from previous ESP32/PID/FreeRTOS architecture

## Shared Documentation

- **datasheets/** - Shared hardware datasheets (SK9822 LEDs, etc.)
- **integration/** - Future integration docs (IR communication, telemetry)
