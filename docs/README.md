# POV Display System — Documentation

Complete document map for the POV display project. Every documentation file is listed here.

## LED Display — System Reference

Core technical docs for the LED display firmware (`led_display/`):

- **[HARDWARE.md](led_display/HARDWARE.md)** — Physical geometry, LED specs, pin assignments, IMU mounting, calibration
- **[EFFECT_SYSTEM_DESIGN.md](led_display/EFFECT_SYSTEM_DESIGN.md)** — Effect base class API, RenderContext, polar helpers, EffectManager, registration (authoritative)
- **[COORDINATE_SYSTEMS.md](led_display/COORDINATE_SYSTEMS.md)** — Polar/Cartesian math, projections, globe rendering
- **[TIMING_ANALYSIS.md](led_display/TIMING_ANALYSIS.md)** — Timing model, angular resolution, jitter analysis
- **[TIMING_MATH_SUPPLEMENT.md](led_display/TIMING_MATH_SUPPLEMENT.md)** — Mathematical derivations supporting TIMING_ANALYSIS.md
- **[EMPIRICAL_TIMING_MEASUREMENTS.md](led_display/EMPIRICAL_TIMING_MEASUREMENTS.md)** — Measured render/output timing data at 2800 RPM
- **[RENDER_OUTPUT_ARCHITECTURE.md](led_display/RENDER_OUTPUT_ARCHITECTURE.md)** — Dual-core render/output pipeline with BufferManager
- **[PIPELINE_PROFILER.md](led_display/PIPELINE_PROFILER.md)** — Queue-based timing instrumentation and pipeline health metrics
- **[BUILD.md](led_display/BUILD.md)** — Build system (uv + PlatformIO), environments, IntelliSense
- **[POV_DISPLAY.md](led_display/POV_DISPLAY.md)** — Art-first philosophy, polar coordinates, design principles

## LED Display — Reference Guides

- **[ESP32_REFERENCE.md](led_display/ESP32_REFERENCE.md)** — ESP32-S3 integer math, angle unit conventions (3600 units/360°)
- **[SIMD_REFERENCE.md](led_display/SIMD_REFERENCE.md)** — ESP32-S3 PIE SIMD instruction reference
- **[ANTI_ALIASING_GUIDE.md](led_display/ANTI_ALIASING_GUIDE.md)** — SDF-based anti-aliasing for polar rendering
- **[FASTLED_REFERENCE.md](led_display/FASTLED_REFERENCE.md)** — FastLED utility functions, color math, effect building blocks
- **[FASTLED_FL_NAMESPACE_REFERENCE.md](led_display/FASTLED_FL_NAMESPACE_REFERENCE.md)** — FastLED `fl::` namespace API reference
- **[FASTLED_EVALUATION.md](led_display/FASTLED_EVALUATION.md)** — Evaluation of FastLED for this project (why NeoPixelBus was chosen instead)
- **[POV_Perlin_Noise_Color_Theory.md](led_display/POV_Perlin_Noise_Color_Theory.md)** — Perlin noise palette mapping and ColorFromPaletteExtended
- **[FREERTOS_INTEGRATION.md](led_display/FREERTOS_INTEGRATION.md)** — FreeRTOS task scheduling, queues, and POV architecture patterns
- **[TIMESTAMP_CAPTURE_OPTIONS.md](led_display/TIMESTAMP_CAPTURE_OPTIONS.md)** — Hall sensor timing: ISR vs hardware timer capture (GPTimer, RMT, PCNT)
- **[POV_PROJECT_ARCHITECTURE_LESSONS.md](led_display/POV_PROJECT_ARCHITECTURE_LESSONS.md)** — Architecture lessons learned from 3 prior POV projects

## LED Display — NeoPixelBus

- **[NEOPIXELBUS_SPI_TIMING_REFERENCE.md](led_display/NEOPIXELBUS_SPI_TIMING_REFERENCE.md)** — SPI clock rates, DMA transfer timing, byte-level protocol
- **[NEOPIXELBUS_DMA_BEHAVIOR.md](led_display/NEOPIXELBUS_DMA_BEHAVIOR.md)** — DMA synchronization and Show() blocking behavior
- **[NEOPIXELBUS_BULK_UPDATE_INVESTIGATION.md](led_display/NEOPIXELBUS_BULK_UPDATE_INVESTIGATION.md)** — Direct buffer access optimizations vs per-pixel calls

## Cross-Device Communication

- **[ESP-NOW_ARCHITECTURE.md](ESP-NOW_ARCHITECTURE.md)** — ESP-NOW protocol between motor controller and LED display
- **[ir-control-spec.md](ir-control-spec.md)** — IR remote control protocol specification
- **[adding-remote-commands.md](adding-remote-commands.md)** — Checklist for adding new IR remote commands

## Rotor Balancing & Telemetry

- **[ROTOR_BALANCING.md](ROTOR_BALANCING.md)** — Balancing theory, sensor selection rationale
- **[POV_TELEMETRY_ANALYSIS_GUIDE.md](POV_TELEMETRY_ANALYSIS_GUIDE.md)** — IMU/hall sensor analysis methodology for imbalance detection
- **[VALIDATING_SINGLE_PLANE_DISC_BALANCING.md](VALIDATING_SINGLE_PLANE_DISC_BALANCING.md)** — ISO validation of on-rotor IMU balancing methodology

## Motor Controller

- **[telemetry_capture.md](motor_controller/telemetry_capture.md)** — High-rate sensor data capture to flash with Python CLI download

## Effects — Deep Dives

- **[Phosphor_Decay_Algorithms.md](effects/Phosphor_Decay_Algorithms.md)** — Exponential and inverse power-law CRT phosphor decay reference
- **[authentic-ppi-radar-display-physics.md](effects/authentic-ppi-radar-display-physics.md)** — CRT phosphor behavior and P7 decay physics for radar effect

## FastLED Investigation

HD gamma research and hybrid architecture evaluation. See **[fastled-investigation/README.md](fastled-investigation/README.md)** for the full index.

## Resolved Issues

- **[ARDUINO_USB_CDC_BUG.md](led_display/ARDUINO_USB_CDC_BUG.md)** — Multi-core serial output bug in Arduino-ESP32 (known, unfixed)
- **[dual-core-pipeline-instability.md](bugs/dual-core-pipeline-instability.md)** — Fixed: queue-based architecture crashes, migrated to BufferManager
- **[TIMING_FIX_2025-11-28.md](led_display/TIMING_FIX_2025-11-28.md)** — Fixed: black snow and split pie visual glitches from timing errors

## Project Layout

- **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** — Complete directory layout reference
- **datasheets/** — Hardware datasheets (Hall sensor, HD107S LEDs, ESP32-S3, MPU-9250, etc.)
- **photos/** — Hardware photos and effect recordings

## Archive

- **motor_controller/archive/** — Outdated docs from the previous ESP32/PID/FreeRTOS motor controller architecture
