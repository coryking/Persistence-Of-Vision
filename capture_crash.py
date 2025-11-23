#!/usr/bin/env python3
"""
Continuously monitor serial port and capture crash output.
Auto-reconnects when device disappears and reappears.
"""
import serial
import time
import sys

PORT = "/dev/cu.usbmodem2101"
BAUD = 115200

print(f"Crash capture monitor starting...")
print(f"Watching for {PORT} at {BAUD} baud")
print(f"Press Ctrl+C to exit\n")

while True:
    try:
        # Try to open the serial port
        ser = serial.Serial(PORT, BAUD, timeout=0.5)
        print(f"\n{'='*60}")
        print(f"Connected to {PORT}")
        print(f"{'='*60}\n")

        while True:
            try:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting)
                    text = data.decode('utf-8', errors='replace')
                    print(text, end='', flush=True)
            except serial.SerialException:
                print(f"\n\n{'='*60}")
                print(f"Device disconnected! Waiting for reconnect...")
                print(f"{'='*60}\n")
                ser.close()
                break
            except Exception as e:
                print(f"\nRead error: {e}")
                break

    except serial.SerialException:
        # Port doesn't exist yet, wait a bit
        time.sleep(0.1)
    except KeyboardInterrupt:
        print("\n\nExiting...")
        sys.exit(0)
    except Exception as e:
        print(f"\nUnexpected error: {e}")
        time.sleep(1)

