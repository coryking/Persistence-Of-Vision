# AGENTS.md - IR Remote Test Project

This file provides guidance to Claude Code when working with the IR remote test project.

## Project Overview

**Purpose**: Simple test utility to receive and decode IR remote control signals. This project dumps IR codes to help identify which remote you're using and what codes each button sends.

**Long-term Goal**: This will become the basis for IR-controlled effect switching in the main POV display project. Instead of the current control mechanism, we'll use an IR receiver mounted at the top of the POV rotor.

**IMPORTANT**: This is a test utility with simple blocking code. It is NOT representative of the main POV firmware architecture. The main firmware uses non-blocking rendering loops and FreeRTOS tasks - this test just uses delay() calls.

## Build System: PlatformIO + uv

This project uses **uv** (installed in the parent POV_Project directory) to manage PlatformIO.

### Setup

```bash
# From parent directory (first time setup)
cd /Users/coryking/projects/POV_Project
uv sync
```

### Common Commands

```bash
# Navigate to this test project
cd /Users/coryking/projects/POV_Project/ir_remote_test

# Build the project
uv run pio run

# Upload to board
uv run pio run -t upload

# Open serial monitor (115200 baud)
uv run pio device monitor

# Clean build artifacts
uv run pio run -t clean
```

**Note**: All `uv run pio` commands must be run from the ir_remote_test directory or use `-d ir_remote_test` flag.

## Hardware Configuration

**Board**: ESP32-S3-Zero (programmed as `esp32-s3-devkitc-1` in PlatformIO)

**IR Receiver**: VS1838B (or HX1838) - Common 38kHz IR receiver module

**Pin Connections**:

| Wire Color | Connection | ESP32-S3 Pin | Notes |
|------------|------------|--------------|-------|
| Yellow     | GND        | Any GND      | Ground |
| Green      | VCC        | 3.3V         | Power (NOT 5V) |
| Orange     | Signal     | GPIO2        | IR data output |

**CRITICAL**: The VS1838B operates at 3.3V. Do not connect to 5V or you may damage the module or ESP32-S3.

## Code Architecture

### Single-File Structure

This is a simple test utility with all code in `src/main.cpp`:

- **Pin definitions**: GPIO2 for IR receiver signal
- **setup()**: Initialize serial (115200 baud), configure IR receiver, print ready message
- **loop()**: Poll for IR signals, decode and print results, small delay

### Why This Is Simple (and That's OK)

The code uses:
- **Blocking delay() calls**: Fine for test code, would NOT work in main POV firmware
- **Serial.print() in loop**: No problem here, but forbidden in POV rendering path
- **No FreeRTOS tasks**: Test code runs on Arduino loop, main firmware uses tasks
- **No timing optimization**: We just dump codes, don't care about microsecond precision

**This pattern is intentionally different from the main project.** For production integration, IR handling would need to be non-blocking and run in a separate FreeRTOS task.

## Dependencies

### IRremoteESP8266 Library

**Repository**: https://github.com/crankyoldgit/IRremoteESP8266

**Version**: `^2.8.6` (or latest compatible)

**Why This Library**:
- **Uses ESP32-S3 RMT peripheral**: Hardware-based IR decoding with minimal CPU overhead
- **Broad protocol support**: NEC, Sony, RC5, RC6, Samsung, LG, Panasonic, etc.
- **Well-maintained**: Active development, ESP-IDF 5.x compatible
- **Excellent utilities**: Built-in functions for human-readable output

**Why RMT Matters**:
The ESP32-S3's RMT (Remote Control) peripheral handles IR signal timing in hardware. This means:
- IR reception doesn't block the CPU
- Extremely accurate timing (microsecond precision)
- Low overhead - critical for future POV integration where LED rendering is timing-sensitive
- No interrupt-based bit-banging required

**Alternative libraries** (why we didn't use them):
- Arduino-IRremote (original): Doesn't use ESP32 RMT, relies on interrupts (higher overhead)
- Bit-banging approaches: Would steal CPU cycles from LED rendering in POV display

## Expected Behavior

### Serial Output Example

When you point a remote at the receiver and press buttons, you should see output like:

```
========================================
IR Remote Test - Code Dump Utility
========================================

Hardware Configuration:
  IR Receiver: VS1838B/HX1838 on GPIO2
  Buffer Size: 1024 bytes
  Timeout: 15 ms

IR receiver initialized and ready!
Point your remote at the receiver and press buttons.
Codes will be displayed below:
----------------------------------------
[5234 ms] NEC - Code: 0x20DF10EF (32 bits)

[7891 ms] NEC - Code: 0x20DF906F (32 bits)

[9542 ms] SAMSUNG - Code: 0xE0E040BF (32 bits)

[11203 ms] RC5 - Code: 0x1ABC (12 bits)
```

### What Each Field Means

- **Timestamp**: `[5234 ms]` - Milliseconds since board started
- **Protocol**: `NEC`, `SAMSUNG`, `RC5`, etc. - Type of IR encoding used
- **Code**: `0x20DF10EF` - The actual data value sent (in hexadecimal)
- **Bits**: `32 bits` - How many bits were in the transmission

### Common Protocols

- **NEC**: Very common in consumer electronics (TVs, media players)
- **Sony**: Sony devices (TVs, receivers, Blu-ray)
- **Samsung**: Samsung TVs and appliances
- **RC5/RC6**: Philips standard, used in many European remotes
- **LG**: LG TVs and appliances

## Troubleshooting

### No Codes Appearing

**Problem**: Serial monitor shows ready message but no codes when pressing remote buttons

**Possible Causes**:
1. **Wrong pin**: Verify orange wire is on GPIO2
2. **Dead batteries**: Try fresh batteries in remote
3. **Wrong receiver angle**: IR receivers are directional, point remote directly at sensor
4. **Interference**: Fluorescent lights or sunlight can interfere with 38kHz IR

**How to Test**:
- Use your phone camera to verify remote is sending IR (you'll see a faint purple/white light when pressing buttons)
- Try different remotes to rule out broken remote
- Verify wiring: Yellow=GND, Green=3.3V, Orange=GPIO2

### Continuous Noise/Garbage Codes

**Problem**: Random codes appearing without pressing buttons

**Possible Causes**:
1. **Ambient IR interference**: Sunlight, fluorescent lights, or other IR sources
2. **Floating pin**: Verify connections are secure
3. **Wrong voltage**: Confirm green wire is on 3.3V (NOT 5V)

**Solutions**:
- Move board away from windows/bright lights
- Shield the receiver with your hand to test if ambient light is the issue
- Check all connections are firmly seated

### Unknown Protocol

**Problem**: Serial shows `UNKNOWN` protocol instead of NEC/Sony/etc.

**Meaning**: The IR signal doesn't match any known protocol in the library. This is actually fine - you can still see the raw timing data.

**What to Do**:
- Uncomment the `resultToTimingInfo(&results)` line in main.cpp to see raw timing
- The remote might use a proprietary or obscure protocol
- Try a different remote with a more common protocol (NEC is most universal)

### Serial Monitor Not Showing Output

**Problem**: Upload succeeds but serial monitor is blank

**Solutions**:
1. Verify baud rate is **115200** in monitor settings
2. Press **RST button** on ESP32-S3 board (code waits 2 seconds in setup)
3. Ensure USB cable supports data (not just power)
4. Check that `ARDUINO_USB_CDC_ON_BOOT=1` is in platformio.ini build flags

## Differences from Main POV Project

| Aspect | ir_remote_test | Main POV Firmware |
|--------|----------------|-------------------|
| **Architecture** | Single file, simple loop | Multi-file, FreeRTOS tasks |
| **Timing** | Blocking delay() OK | Non-blocking, microsecond precision required |
| **Serial Output** | Verbose, anytime | Minimal, never in rendering path |
| **IR Handling** | Polled in main loop | Would need dedicated task |
| **Memory** | No optimization needed | Pre-allocated buffers, no malloc in rendering |
| **Error Handling** | Print and continue | Reset on critical errors |

**When integrating into POV display**: IR decoding will need to run in a separate FreeRTOS task that sets flags/queues for effect switching without blocking LED rendering.

## Next Steps

### After Validating IR Codes

1. **Document your remote**: Note which codes correspond to which buttons
2. **Choose control scheme**:
   - Number buttons for effect selection (0-9 = 10 effects)
   - Arrow keys for brightness/speed adjustment
   - Play/Pause for freeze/resume rotation effects
3. **Plan integration**: IR task will communicate with main firmware via FreeRTOS queues or shared variables

### Integration into Main POV Project

The IR receiver will mount at the top of the POV rotor (stationary, not spinning). Key considerations:

- **Wiring**: Signal wire from GPIO2 will route through slip ring or use wireless module
- **FreeRTOS Task**: Create dedicated `irTask` at lower priority than `hallTask` and rendering
- **Effect Switching**: IR codes update shared variable read by `EffectScheduler`
- **Timing**: RMT peripheral means IR reception won't interfere with LED rendering

## Reference Documentation

- **IRremoteESP8266 Library**: https://github.com/crankyoldgit/IRremoteESP8266
- **ESP32-S3 RMT Peripheral**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/rmt.html
- **Main POV Project**: See `/Users/coryking/projects/POV_Project/AGENTS.md`
- **VS1838B Datasheet**: Common 38kHz IR receiver module specs
