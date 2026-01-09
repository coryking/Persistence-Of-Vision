"""
PlatformIO pre-upload script - validates correct device is connected.
Checks that the expected MAC address appears in ioreg USB device list.
"""
Import("env")
import subprocess
import sys

def validate_device(source, target, env):
    expected_mac = env.GetProjectOption("custom_expected_mac", "")

    if not expected_mac:
        return  # No validation configured

    print(f"Validating device MAC: {expected_mac}")

    result = subprocess.run(
        f'ioreg -p IOUSB -l -w 0 | grep -i "{expected_mac}"',
        shell=True, capture_output=True
    )

    if result.returncode != 0:
        print("\n" + "=" * 60)
        print(f"ERROR: Device with MAC {expected_mac} not found!")
        print("=" * 60)
        print("Is the correct device plugged into the correct USB port?")
        print()
        env.Exit(1)

    print("Device validation passed!")

env.AddPreAction("upload", validate_device)
