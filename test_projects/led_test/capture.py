#!/usr/bin/env python3
"""Capture SPI timing test results from ESP32.

Usage:
    ./capture.py              # Build, upload, and capture
    ./capture.py --no-build   # Just capture (skip build/upload)
"""

import serial
import subprocess
import sys
import time
from pathlib import Path

PORT = "/dev/cu.usbmodem2101"
BAUD = 921600
OUTPUT_DIR = Path(__file__).parent / "output"


def build_and_upload() -> bool:
    """Build and upload firmware."""
    print(f"\n{'='*60}")
    print("Building and uploading...")
    print('='*60)

    result = subprocess.run(
        ["pio", "run", "-t", "upload"],
        cwd=Path(__file__).parent
    )
    return result.returncode == 0


def capture_test() -> list[str]:
    """Run test and capture CSV output."""
    print(f"\nConnecting to {PORT}...")

    time.sleep(2)  # Wait for device reset

    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.5)

    print("Waiting for device...")
    while True:
        line = ser.readline().decode(errors='ignore').strip()
        if line:
            print(f"< {line}")
        if "Press any key" in line:
            break

    print("Starting test...")
    ser.write(b'\n')

    lines = []
    while True:
        line = ser.readline().decode(errors='ignore').strip()
        if not line:
            continue
        print(f"< {line}")
        if line.startswith("spi_mhz") or line[0].isdigit():
            lines.append(line)
        if line == "DONE":
            break

    ser.close()
    return lines


def main():
    OUTPUT_DIR.mkdir(exist_ok=True)

    skip_build = "--no-build" in sys.argv

    if not skip_build:
        if not build_and_upload():
            print("Build failed!")
            sys.exit(1)

    lines = capture_test()

    output_file = OUTPUT_DIR / "result.csv"
    with open(output_file, 'w') as f:
        f.write('\n'.join(lines) + '\n')

    data_rows = len(lines) - 1  # Subtract header
    print(f"\n{'='*60}")
    print(f"Saved {data_rows} data rows to {output_file}")
    print('='*60)


if __name__ == "__main__":
    main()
