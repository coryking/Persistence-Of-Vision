#!/bin/bash
# Extract GCOV profiling data from ESP32-S3 and generate HTML report
# Usage: ./extract_gcov.sh

set -e

GCOV_PREFIX="/Users/coryking/projects/POV_Project/gcov_data"
BUILD_DIR=".pio/build/seeed_xiao_esp32s3_profiling"

echo "=== ESP32-S3 GCOV Hotspot Analysis ==="
echo ""

# Step 1: Start OpenOCD
echo "[1/5] Starting OpenOCD..."
~/.platformio/packages/tool-openocd-esp32/bin/openocd -f board/esp32s3-builtin.cfg &
OPENOCD_PID=$!
sleep 2

# Step 2: Trigger GCOV dump on device
echo "[2/5] Triggering GCOV data dump on device..."
echo "Connect via telnet and run: esp gcov dump"
echo ""
echo "Press ENTER after running 'esp gcov dump' in telnet..."
read

# Step 3: Kill OpenOCD
echo "[3/5] Stopping OpenOCD..."
kill $OPENOCD_PID

# Step 4: Find GCOV files
echo "[4/5] Searching for .gcda files..."
find ${BUILD_DIR} -name "*.gcda" -o -name "*.gcno"

# Step 5: Generate HTML report
echo "[5/5] Generating HTML coverage report..."
mkdir -p gcov_report

# Use xtensa-esp32s3-elf-gcov from PlatformIO toolchain
GCOV_TOOL=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcov

if [ ! -f "$GCOV_TOOL" ]; then
    echo "ERROR: xtensa-esp32s3-elf-gcov not found"
    echo "Expected at: $GCOV_TOOL"
    exit 1
fi

# Generate coverage report
lcov --gcov-tool ${GCOV_TOOL} --capture --directory ${BUILD_DIR} --output-file gcov_report/coverage.info
genhtml gcov_report/coverage.info --output-directory gcov_report/html

echo ""
echo "=== Done! ==="
echo "HTML report: gcov_report/html/index.html"
echo "Open with: open gcov_report/html/index.html"
