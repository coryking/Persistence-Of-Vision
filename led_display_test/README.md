# LED Display Test - Hardware Validation Utility

Simple test utility for validating LED strips and hall effect sensors before using the full POV display firmware.

## Purpose

This standalone project provides basic hardware validation:
- Tests LED strip connectivity (all 33 LEDs)
- Validates hall effect sensor triggering
- Verifies SPI data/clock signals
- Confirms power delivery to LED strip

Use this to verify your hardware is working correctly before loading the complex POV display firmware.

## Pin Configuration

| Wire Color | GPIO | Function | Notes |
|------------|------|----------|-------|
| Blue       | 8    | SPI Data (MOSI) | LED data signal |
| Purple     | 9    | SPI Clock (SCLK) | LED clock signal |
| Brown      | 10   | Hall Sensor | Active LOW (pullup enabled) |

**Note:** This pin configuration differs from the main POV display project:
- Main project uses: D7/D9/D10 (GPIO 6/8/9)
- This test uses: GPIO 8/9/10

## Expected Behavior

### Normal Operation

1. **Power on**: Serial output shows initialization message
2. **LED Pattern**: Moving dot cycles through all 33 LEDs (0 → 32)
   - Cycle speed: ~100ms per LED (~3.3 seconds full cycle)
   - Initial color: Red
3. **Hall Sensor**: Pass magnet over sensor to change color
   - Color sequence: Red → Green → Blue → White → Red...
   - Serial output confirms each trigger with new color

### Serial Output Example

```
========================================
LED Display Test - Hardware Validation
========================================
NUM_LEDS: 33
Pin Config: Data=8, Clock=9, Hall=10
----------------------------------------
NeoPixelBus initialized (SK9822/APA102)
Hall effect sensor configured (GPIO 10, FALLING edge)
----------------------------------------
Starting LED cycle with color: Red [255,0,0]
Pass magnet over hall sensor to change colors!
========================================

*** Hall triggered! New color: Green [0,255,0] ***

*** Hall triggered! New color: Blue [0,0,255] ***
```

## Building and Uploading

### Prerequisites

- **uv** package manager installed
- **PlatformIO** dependencies installed (`uv sync` in parent directory)
- ESP32-S3 board connected via USB

### Commands

```bash
# Navigate to test directory
cd led_display_test

# Build the firmware
uv run pio run

# Upload to board
uv run pio run -t upload

# Open serial monitor
uv run pio device monitor

# Build, upload, and monitor (all-in-one)
uv run pio run -t upload && uv run pio device monitor
```

### Setting Upload Port (Optional)

If you have multiple serial devices, specify the port in `platformio.ini`:

```ini
upload_port = /dev/cu.usbmodem2101
monitor_port = /dev/cu.usbmodem2101
```

## Troubleshooting

### No LEDs lighting up

1. **Check power connections**
   - LED strip needs external power (5V, sufficient amperage)
   - Ground must be common between ESP32 and LED strip power supply

2. **Verify pin connections**
   - Blue wire → GPIO 8 (Data)
   - Purple wire → GPIO 9 (Clock)
   - Check for loose connections or incorrect pin mapping

3. **Check serial output**
   - Should see "NeoPixelBus initialized" message
   - If missing, code may not be running

### Hall sensor not triggering

1. **Test sensor polarity**
   - Hall sensors detect specific magnetic pole (North or South)
   - Try flipping magnet orientation

2. **Check distance**
   - Hall sensors typically need magnet within ~5-10mm
   - Try moving magnet closer

3. **Verify serial output**
   - Should see "Hall effect sensor configured" message
   - Watch for "Hall triggered!" messages when passing magnet

4. **Check wiring**
   - Brown wire → GPIO 10
   - Hall sensor should pull GPIO 10 LOW when magnet detected

### Wrong LED count or pattern

1. **Adjust NUM_LEDS in `src/main.cpp`**
   - Default: 33 (11 per arm × 3 arms)
   - Change to match your LED strip length

2. **Check LED strip type**
   - Firmware configured for SK9822/APA102 (DotStar)
   - If using different LEDs (WS2812B, etc.), modify NeoPixelBus template

### Serial output garbled or missing

1. **Check baud rate**: Should be 115200
2. **Wait after upload**: 2-second delay before serial output starts
3. **Try resetting board**: Press reset button after upload completes

## Hardware Details

### LED Strip
- **Type**: SK9822 / APA102 (DotStar)
- **Count**: 33 LEDs (configurable in code)
- **Protocol**: 4-wire SPI (Clock + Data)
- **Color Order**: BGR

### Hall Effect Sensor
- **Type**: Digital hall effect sensor (e.g., A3144, OH137)
- **Trigger**: Active LOW (internal pullup enabled)
- **Debounce**: 200ms (prevents rapid re-triggers)

### ESP32-S3 Board
- **Board**: Seeed XIAO ESP32S3
- **USB**: CDC on boot enabled
- **SPI**: Hardware SPI pins (GPIO 8/9)

## Adjusting Behavior

### Change LED Cycle Speed

Edit `CYCLE_DELAY` in `src/main.cpp`:

```cpp
#define CYCLE_DELAY 100  // ms delay between LED updates
```

- Lower value = faster cycling (e.g., 50ms)
- Higher value = slower cycling (e.g., 200ms)

### Change Color Palette

Edit `colors[]` array in `src/main.cpp`:

```cpp
const RgbColor colors[] = {
    RgbColor(255, 0, 0),   // Red
    RgbColor(0, 255, 0),   // Green
    RgbColor(0, 0, 255),   // Blue
    RgbColor(255, 255, 255) // White
    // Add more colors here...
};
```

### Adjust Debounce Time

Edit `DEBOUNCE_MS` in `src/main.cpp`:

```cpp
const uint32_t DEBOUNCE_MS = 200; // Debounce time in milliseconds
```

## Differences from Main POV Project

This test utility is intentionally simplified:

| Feature | LED Test | Main POV Display |
|---------|----------|------------------|
| Pin Config | GPIO 8/9/10 | GPIO 6/8/9 (D7/D9/D10) |
| Rendering | Simple blocking loop | High-speed interrupt-driven |
| Timing | `delay()` calls (100ms) | Sub-millisecond precision |
| Hall Sensor | Color trigger only | Revolution timing & sync |
| Dependencies | NeoPixelBus only | NeoPixelBus + FastLED |
| Complexity | ~100 lines | ~1000+ lines |

Use this test to confirm hardware works, then move to the main POV firmware for actual spinning display.

## Next Steps

Once this test confirms your hardware is working:

1. Return to parent directory: `cd ..`
2. Build main POV firmware: `uv run pio run`
3. Upload main firmware: `uv run pio run -t upload`
4. See main project documentation for POV display usage

## License

Same as parent POV_Project (see top-level LICENSE if applicable)
