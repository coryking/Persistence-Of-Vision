"""
Generate certificate assembly files for ESP-IDF components.

WHY THIS SCRIPT EXISTS:
-----------------------
The seeed_xiao_esp32s3_profiling environment uses dual frameworks (arduino + espidf).
This pulls in ESP-IDF managed components (esp_rainmaker, esp_insights, esp_secure_cert_mgr)
that contain .crt certificate files. ESP-IDF's build system expects these to be converted
to .S assembly files using data_file_embed_asm.cmake, but PlatformIO doesn't do this
automatically for dual-framework builds.

Without this script, the build fails with:
  *** Source `.pio/build/.../mqtt_server.crt.S' not found

WHEN THIS BECOMES VESTIGIAL:
-----------------------------
You can DELETE this script when ANY of these are true:
  1. You remove the dual-framework setup (framework = arduino, espidf) from platformio.ini
  2. You remove the managed_components that require certificates
  3. You switch to Arduino-only framework (no ESP-IDF components)
  4. PlatformIO fixes automatic certificate handling for dual-framework builds

Check if vestigial by commenting out 'extra_scripts = pre:generate_certs.py' in platformio.ini
and running a build. If it succeeds, you don't need this anymore.

CRITICAL IMPLEMENTATION DETAIL:
-------------------------------
Must use env.subst() to expand template variables like $BUILD_DIR. Using env.get()
returns unexpanded strings like "$PROJECT_BUILD_DIR/$PIOENV" which causes files to be
written to wrong locations.

DUAL-FRAMEWORK BUG WORKAROUND:
------------------------------
The dual-framework build has a path-doubling bug where files are expected at both:
  - .pio/build/seeed_xiao_esp32s3_profiling/file.S
  - .pio/build/seeed_xiao_esp32s3_profiling/seeed_xiao_esp32s3_profiling/file.S
This script copies to both locations to work around the bug.
"""
import os
import subprocess
from pathlib import Path

Import("env")

def generate_cert_asm_files(source, target, env):
    """Generate .S files from .crt certificate files."""

    # Get build environment - use subst() to expand template variables
    # NOTE: env.get() returns unexpanded "$PROJECT_BUILD_DIR/$PIOENV" - don't use it!
    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")

    # === DIAGNOSTIC OUTPUT (can be removed once script is proven stable) ===
    print(f"=== Certificate Generation Diagnostics ===")
    print(f"PROJECT_DIR: {project_dir}")
    print(f"BUILD_DIR: {build_dir}")

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
        print("No certificate files found.")
        return

    # === DIAGNOSTIC OUTPUT (can be removed once script is proven stable) ===
    print(f"Found {len(cert_files)} certificate file(s) to convert...")
    for cf in cert_files:
        print(f"  Input cert: {cf}")

    # WORKAROUND: Dual-framework build has a bug where paths get doubled
    # Create doubled path directory if it doesn't exist
    doubled_build_dir = os.path.join(build_dir, os.path.basename(build_dir))
    print(f"\nCreating doubled directory: {doubled_build_dir}")  # DIAGNOSTIC
    os.makedirs(doubled_build_dir, exist_ok=True)

    # Generate .S files for each certificate
    for cert_file in cert_files:
        cert_name = os.path.basename(cert_file)
        output_file = os.path.join(build_dir, cert_name.replace('.crt', '.crt.S'))

        print(f"\n--- Processing: {cert_name} ---")
        print(f"  Output file: {output_file}")

        # Run the CMake conversion script
        cmd = [
            "cmake",
            f"-DDATA_FILE={cert_file}",
            f"-DSOURCE_FILE={output_file}",
            "-DFILE_TYPE=BINARY",
            "-P", conversion_script
        ]

        print(f"  Running: {' '.join(cmd)}")

        try:
            subprocess.run(cmd, check=True, capture_output=True, text=True)

            # Verify file was created
            if os.path.exists(output_file):
                print(f"  ✓ Created: {output_file}")
            else:
                print(f"  ✗ MISSING: {output_file}")

            # WORKAROUND: Also copy to doubled path location (target_add_binary_data bug)
            doubled_output = os.path.join(doubled_build_dir, os.path.basename(output_file))
            import shutil
            shutil.copy2(output_file, doubled_output)

            if os.path.exists(doubled_output):
                print(f"  ✓ Copied to: {doubled_output}")
            else:
                print(f"  ✗ COPY FAILED: {doubled_output}")
        except subprocess.CalledProcessError as e:
            print(f"  ✗ Error generating {cert_name}: {e}")
            print(f"  stdout: {e.stdout}")
            print(f"  stderr: {e.stderr}")

    print("\n=== Certificate Generation Complete ===\n")

# Run immediately when SCons loads the environment
# Files will be in place when ninja starts compilation
generate_cert_asm_files(None, None, env)
