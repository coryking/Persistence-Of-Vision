# ESP32-S3 Profiling Guide

Hotspot analysis tool for finding where your CPU time goes in the POV display.

## GCOV - Code Hotspot Analysis
**Shows:** Which lines of code execute most often (execution counts)
**Good for:** "This line in the render loop ran 50,000 times per second"
**Output:** HTML report with heat-mapped source code

**Note:** SystemView timeline tracing is disabled due to Arduino+ESP-IDF framework compatibility issues. GCOV provides the critical hotspot data you need.

---

## Prerequisites

Install lcov for GCOV report generation:
```bash
brew install lcov
```

---

## Workflow: GCOV Hotspot Analysis

### Step 1: Build and Upload
```bash
# Build profiling firmware with GCOV instrumentation
uv run pio run -e seeed_xiao_esp32s3_profiling

# Upload to device
uv run pio run -e seeed_xiao_esp32s3_profiling -t upload
```

### Step 2: Run Your Code
Let the device run for a while to collect execution data. The longer it runs, the more representative the hotspot data.

### Step 3: Extract GCOV Data

**Terminal 1 - Start OpenOCD:**
```bash
~/.platformio/packages/tool-openocd-esp32/bin/openocd -f board/esp32s3-builtin.cfg
```

**Terminal 2 - Dump GCOV Data:**
```bash
# Connect to OpenOCD
telnet localhost 4444

# Trigger GCOV dump (wait ~10 seconds for completion)
esp gcov dump
```

You'll see output like:
```
Targets connected.
Open On-Chip Debugger
> esp gcov dump
Targets disconnected.
```

### Step 4: Generate HTML Report
```bash
# Find your .gcda files
find .pio/build/seeed_xiao_esp32s3_profiling -name "*.gcda"

# Generate coverage report
GCOV=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcov
lcov --gcov-tool $GCOV \
     --capture \
     --directory .pio/build/seeed_xiao_esp32s3_profiling \
     --output-file coverage.info

# Generate HTML
genhtml coverage.info --output-directory gcov_report
open gcov_report/index.html
```

### Interpreting GCOV Results

The HTML report shows:
- **High execution counts (red/hot)**: Lines that run frequently - optimization targets
- **Medium counts (yellow/warm)**: Moderately executed code
- **Low counts (green/cold)**: Rarely executed code

**Example:**
```cpp
100000: for (uint8_t arm = 0; arm < 3; arm++) {        // HOT - loop header
 50000:     uint16_t angle = getAngle(arm);            // WARM
 50000:     CRGB color = calculateColor(angle);        // WARM - potential hotspot
 50000:     strip.SetPixelColor(led, color);           // WARM
      : }
```

**Look for:**
- Inner loops with high counts (optimization targets)
- Expensive operations in hot paths (sin/cos, division, malloc)
- Surprisingly high counts (unexpected repeated work)

---

## Quick Reference

**Build profiling firmware:**
```bash
uv run pio run -e seeed_xiao_esp32s3_profiling -t upload
```

**GCOV hotspot analysis:**
```bash
# 1. Run device
# 2. Terminal 1: ~/.platformio/packages/tool-openocd-esp32/bin/openocd -f board/esp32s3-builtin.cfg
# 3. Terminal 2: telnet localhost 4444 → esp gcov dump
# 4. ./extract_gcov.sh (or manual steps above)
```

---

## Troubleshooting

**"Can't find .gcda files"**
- Make sure you ran `esp gcov dump` in telnet
- Check that firmware was built with `--coverage` flag
- Verify OpenOCD was connected during dump

**"OpenOCD can't connect"**
- Check USB cable is connected
- Try unplugging and replugging device
- Make sure no other process is using the USB port
- Check that board is in bootloader mode if needed

**"GCOV report shows no hotspots"**
- Let device run longer before dumping (need more samples)
- Check that TEST_MODE is enabled (simulates rotation)
- Verify render loop is actually running

---

## What to Look For

### GCOV Hotspots:
1. **Render loop inner code** - Should be highest execution counts
2. **Color calculation functions** - Check for expensive math
3. **LED update calls** - Verify they're not called too often
4. **Unexpected high counts** - Functions that shouldn't run often

**Example:**
- GCOV shows: "Line 42 in renderEffect() ran 500,000 times"
- Conclusion: Line 42 is a hotspot - optimize this first
