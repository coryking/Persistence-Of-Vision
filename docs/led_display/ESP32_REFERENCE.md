# ESP32 Quick Reference

Reference documentation for ESP32-S3 performance characteristics and integer math patterns used in the render path.

## Integer Math System

**The render path uses NO floating-point math.** All angles and timing use integer types for speed and precision.

### Angle Units

Angles use `angle_t` (uint16_t) where **3600 units = 360 degrees** (0.1 degree precision):

```cpp
angle_t angleUnits = arm.angleUnits;        // 0-3599
uint8_t pattern = angleUnits / 180;         // Exact integer division for 18 degree patterns
```

Key constants in `types.h`:
- `ANGLE_FULL_CIRCLE = 3600`
- `ANGLE_PER_PATTERN = 180` (18 degrees)
- `INNER_ARM_PHASE = 1200` (120 degrees)
- `OUTER_ARM_PHASE = 2400` (240 degrees)

### Speed: Use microsPerRev, NOT RPM

**Do NOT convert to RPM** - it requires float division. Use `microsPerRev` directly:

```cpp
// BAD - requires float division:
float rpm = 60000000.0f / microsPerRev;

// GOOD - use raw measurement:
uint8_t speed = speedFactor8(ctx.microsPerRev);  // Returns 0-255 (faster = higher)
```

Speed ranges:
- 700 RPM (slow) = ~85,714 us/rev
- 2800 RPM (fast) = ~21,428 us/rev

### FastLED Integer Helpers

Use FastLED's optimized functions instead of float math:

| Instead of | Use | Notes |
|------------|-----|-------|
| `float * float` | `scale8(val, scale)` | 8-bit multiply |
| `std::min(255, a+b)` | `qadd8(a, b)` | Saturating add |
| `lerp(a, b, t)` | `lerp8by8(a, b, frac)` | frac is 0-255 |
| `sin(angle)` | `sin16(phase)` | phase 0-65535 |
| `fmod(angle, 360)` | `angle % 3600` | Integer modulo |

### Key Files

- `include/types.h` - `angle_t` and constants
- `include/polar_helpers.h` - Integer angle helpers (`isAngleInArcUnits`, `arcIntensityUnits`, `speedFactor8`)
- `include/RenderContext.h` - `arms[].angleUnits` (not `angle`)

## ESP32-S3 Floating-Point Performance

The ESP32-S3 includes a single-precision hardware floating point unit (FPU) that provides decent performance for `float` operations, with single-precision multiplications taking approximately 4 CPU clock cycles. However, even with hardware acceleration, floating-point calculations are still consistently 2x slower than integer operations on the S3. **This project uses pure integer math in the render path to maximize performance.**

Double-precision (`double`) operations are software-emulated and extremely slow - **never use double in timing-critical code**.

### Further Reading

- **Espressif Official Documentation**: [Speed Optimization - ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html) - Official performance guidelines
- **FPU Overview**: [Floating-Point Units on Espressif SoCs](https://developer.espressif.com/blog/2025/10/cores_with_fpu/) - Explains which Espressif chips have FPUs and why they matter
- **Benchmarking Study**: [Integer vs Float Performance on the ESP32-S3: Why TinyML Loves Quantization](https://medium.com/@sfarrukhm/integer-vs-float-performance-on-the-esp32-s3-why-tinyml-loves-quantization-227eca11bd35) - Real-world benchmarks showing 2x performance difference
- **ESP32-S2 Comparison**: [No, the ESP32-S2 is not faster at floating point operations](https://blog.llandsmeer.com/tech/2021/04/08/esp32-s2-fpu.html) - Detailed analysis of FPU performance and optimization techniques
- **Forum Discussion**: [FPU Documentation for S3](https://esp32.com/viewtopic.php?t=40615) - Community discussion about FPU cycle counts
