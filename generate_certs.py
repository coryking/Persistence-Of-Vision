"""
Generate certificate assembly files for ESP-IDF components.

This script runs as a PlatformIO pre-build action to convert .crt files
to .S assembly files that can be embedded in the firmware.
"""
import os
import subprocess
from pathlib import Path

Import("env")

def generate_cert_asm_files(source, target, env):
    """Generate .S files from .crt certificate files."""

    # Get build environment
    project_dir = env.get("PROJECT_DIR")
    build_dir = env.get("BUILD_DIR")

    # Get ESP-IDF path via platform object (correct way for PlatformIO)
    platform = env.PioPlatform()
    idf_path = platform.get_package_dir("framework-espidf")

    # Path to the conversion script
    conversion_script = os.path.join(idf_path, "tools/cmake/scripts/data_file_embed_asm.cmake")

    if not os.path.exists(conversion_script):
        print(f"Warning: Certificate conversion script not found at {conversion_script}")
        return

    # Find all .crt files in managed_components
    managed_components = os.path.join(project_dir, "managed_components")
    if not os.path.exists(managed_components):
        return

    cert_files = []
    for root, dirs, files in os.walk(managed_components):
        for file in files:
            if file.endswith('.crt'):
                cert_files.append(os.path.join(root, file))

    if not cert_files:
        return

    print(f"Found {len(cert_files)} certificate file(s) to convert...")

    # Generate .S files for each certificate
    for cert_file in cert_files:
        cert_name = os.path.basename(cert_file)
        output_file = os.path.join(build_dir, cert_name.replace('.crt', '.crt.S'))

        # Run the CMake conversion script
        cmd = [
            "cmake",
            f"-DDATA_FILE={cert_file}",
            f"-DSOURCE_FILE={output_file}",
            "-DFILE_TYPE=BINARY",
            "-P", conversion_script
        ]

        try:
            subprocess.run(cmd, check=True, capture_output=True, text=True)
            print(f"  Generated: {os.path.basename(output_file)}")
        except subprocess.CalledProcessError as e:
            print(f"  Error generating {cert_name}: {e}")
            print(f"  stdout: {e.stdout}")
            print(f"  stderr: {e.stderr}")

# Run immediately during SCons configuration (before CMake compilation)
generate_cert_asm_files(None, None, env)
