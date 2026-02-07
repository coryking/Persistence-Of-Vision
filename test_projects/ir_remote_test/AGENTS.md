# IR Remote Test — Agent Guide

Test utility for decoding IR remote signals. Used to identify button codes for the SageTV RC5 remote.

**This is a simple test utility. Don't apply POV display architecture patterns here.**

## Build

```bash
cd test_projects/ir_remote_test
uv run pio run                    # Build
uv run pio run -t upload          # Upload
uv run pio device monitor         # Monitor (115200 baud)
```

## Hardware

- Board: ESP32-S3-Zero (PlatformIO target: `esp32-s3-devkitc-1`)
- IR Receiver: VS1838B on GPIO2 (3.3V only — do NOT connect to 5V)

## Dependency

Uses IRremoteESP8266 library (`^2.8.6`) — leverages ESP32-S3 RMT peripheral for hardware-based IR decoding with minimal CPU overhead.
