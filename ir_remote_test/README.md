# IR Remote Test - Code Dump Utility

Simple test project to identify IR remote control codes for future POV display integration.

## Purpose

This utility receives IR signals from any remote control and displays the decoded data (protocol type, hex code, bit count). Use this to:
- Test your VS1838B IR receiver module
- Identify which remote control you want to use
- Map button codes for future effect switching in the POV display

## Hardware Setup

### Components

- **ESP32-S3-Zero** board
- **VS1838B** (or HX1838) IR receiver module
- 3 jumper wires

### Pin Configuration

| Wire Color | IR Module | ESP32-S3-Zero |
|------------|-----------|---------------|
| Yellow     | GND (-)   | Any GND pin   |
| Green      | VCC (+)   | 3.3V          |
| Orange     | OUT (S)   | GPIO2         |

**WARNING**: Use 3.3V, NOT 5V. The VS1838B operates at 3.3V and connecting to 5V may damage the module or ESP32-S3.

### Wiring Diagram

```
VS1838B IR Receiver              ESP32-S3-Zero
┌─────────────────┐              ┌──────────┐
│                 │              │          │
│  [●] OUT (S) ◄──┼──Orange──────┤ GPIO2    │
│  [●] GND (-) ◄──┼──Yellow──────┤ GND      │
│  [●] VCC (+) ◄──┼──Green───────┤ 3.3V     │
│                 │              │          │
│   (Dome side)   │              └──────────┘
└─────────────────┘
     ^
     │
  Point remote here
```

## Building and Uploading

### First Time Setup

```bash
# Navigate to parent POV_Project directory
cd /Users/coryking/projects/POV_Project

# Install Python dependencies (uv and PlatformIO)
uv sync
```

### Build and Upload

```bash
# Navigate to ir_remote_test directory
cd /Users/coryking/projects/POV_Project/ir_remote_test

# Build the firmware
uv run pio run

# Upload to ESP32-S3
uv run pio run -t upload

# Open serial monitor (115200 baud)
uv run pio device monitor
```

## Expected Output

### Startup Message

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
```

### When Pressing Remote Buttons

Point your remote control at the IR receiver dome (the black bulb on the VS1838B) and press buttons. You should see output like:

```
[5234 ms] NEC - Code: 0x20DF10EF (32 bits)

[7891 ms] NEC - Code: 0x20DF906F (32 bits)

[9542 ms] SAMSUNG - Code: 0xE0E040BF (32 bits)
```

Each line shows:
- **Timestamp**: Milliseconds since startup
- **Protocol**: Type of IR encoding (NEC, Sony, Samsung, etc.)
- **Code**: The actual hex value sent by that button
- **Bits**: Number of bits in the transmission

## Troubleshooting

### No Codes Appearing

**Check this**:
1. Orange wire is connected to GPIO2
2. Remote has fresh batteries (use phone camera to see if IR LED lights up)
3. Point remote directly at the black dome on the VS1838B
4. Move away from fluorescent lights or windows (IR interference)

### Random Codes Without Pressing Buttons

**Likely causes**:
- Bright sunlight or fluorescent lights interfering
- Loose wiring (check all connections)

**Quick test**: Cup your hand over the IR receiver dome. If random codes stop, it's ambient light interference.

### "UNKNOWN" Protocol

This is fine! Your remote uses a protocol the library doesn't recognize. The codes will still be captured and can still be used for control. You'll see "UNKNOWN - Code: 0x..." which is perfectly usable.

### No Serial Output

1. Check baud rate is **115200** in your serial monitor
2. Press the **RST button** on the ESP32-S3 board
3. Verify USB cable supports data (not just charging)

## Documenting Your Remote

Create a simple mapping of buttons to codes. Example:

```
Remote: Generic TV Remote
Protocol: NEC

Button   | Code       | Notes
---------|------------|------------------
Power    | 0x20DF10EF | Could use for pause/resume
Vol+     | 0x20DF40BF | Brightness up
Vol-     | 0x20DFC03F | Brightness down
Ch+      | 0x20DF00FF | Next effect
Ch-      | 0x20DF807F | Previous effect
1        | 0x20DF8877 | Effect #1
2        | 0x20DF48B7 | Effect #2
...
```

## Next Steps

Once you've identified your remote codes:

1. **Choose control scheme**: Decide which buttons do what (number keys for effects, volume for brightness, etc.)
2. **Test range**: See how far away the remote works from the receiver
3. **Plan mounting**: IR receiver will mount at the top of POV rotor (stationary part)

## Integration with POV Display

This test code will eventually become the basis for IR control in the main POV display:
- IR receiver mounted at top of rotor (not spinning part)
- FreeRTOS task handles IR decoding without blocking LED rendering
- Button presses switch effects via EffectScheduler
- Uses ESP32-S3 RMT peripheral for hardware-based IR reception (minimal CPU overhead)

## Technical Details

### Libraries Used

- **IRremoteESP8266** (`^2.8.6`): Uses ESP32-S3's RMT peripheral for efficient IR reception

### Why RMT Peripheral Matters

The ESP32-S3's RMT (Remote Control) peripheral handles IR timing in hardware, meaning:
- IR reception doesn't block the CPU
- Microsecond-accurate timing
- Perfect for integration into timing-sensitive POV display

### Serial Monitor Settings

- **Baud rate**: 115200
- **Newline**: Any (CR, LF, or CRLF all work)

## Support

For issues specific to this test project, see `AGENTS.md` for detailed troubleshooting.

For main POV display integration questions, see parent directory's `AGENTS.md`.
