# POV Display Dimensionality and Timing Calculations

## Reference Guide for Rotating LED Arrays

**Document Version:** 1.0
**Last Updated:** 2024

---

## System Parameters

### Fixed Hardware Parameters

- **Number of arms:** 3
- **Angular spacing between arms:** 120°
- **Number of LEDs per arm:** 10
- **LED pitch:** 7mm
- **Innermost LED radius (LED 0):** 35mm
- **Outermost LED radius (LED 9):** 98mm

### LED Radial Positions

```
LED_radius[n] = 35mm + (n × 7mm)

where n = 0 to 9
```

| LED # | Radius (mm) | Circumference (mm) |
| ----- | ----------- | ------------------ |
| 0     | 35          | 219.91             |
| 1     | 42          | 263.89             |
| 2     | 49          | 307.88             |
| 3     | 56          | 351.86             |
| 4     | 63          | 395.84             |
| 5     | 70          | 439.82             |
| 6     | 77          | 483.81             |
| 7     | 84          | 527.79             |
| 8     | 91          | 571.77             |
| 9     | 98          | 615.75             |

### Variable Parameters

- **RPM:** Rotations per minute (typical range: 1600-2500)
- **PWM Frequency:**
  - SK9822: 4.6 kHz
  - HD108: 27 kHz
- **Global brightness:** 0-31 (5-bit control)
- **Per-channel brightness:** 0-255 (8-bit control)

---

## Core Timing Formulas

### Rotation Timing

**Rotations per second (RPS):**

```
RPS = RPM / 60
```

**Period per rotation (microseconds):**

```
T_rotation = 1,000,000 / RPS = 60,000,000 / RPM
```

**Time per degree:**

```
T_degree = T_rotation / 360 = 166,667 / RPM  (µs)
```

**Time per angular division:**

```
T_angular = T_rotation / N_divisions

where N_divisions = desired angular resolution (e.g., 360, 720, 1080)
```

### Quick Reference Table: Rotation Timing

| RPM  | RPS   | T_rotation (µs) | T_degree (µs) | T_0.5° (µs) |
| ---- | ----- | --------------- | ------------- | ----------- |
| 1000 | 16.67 | 60,000          | 166.67        | 83.33       |
| 1200 | 20.00 | 50,000          | 138.89        | 69.44       |
| 1400 | 23.33 | 42,857          | 119.05        | 59.52       |
| 1600 | 26.67 | 37,500          | 104.17        | 52.08       |
| 1800 | 30.00 | 33,333          | 92.59         | 46.30       |
| 2000 | 33.33 | 30,000          | 83.33         | 41.67       |
| 2200 | 36.67 | 27,273          | 75.76         | 37.88       |
| 2400 | 40.00 | 25,000          | 69.44         | 34.72       |
| 2500 | 41.67 | 24,000          | 66.67         | 33.33       |
| 2800 | 46.67 | 21,429          | 59.52         | 29.76       |
| 3000 | 50.00 | 20,000          | 55.56         | 27.78       |

---

## PWM and Strobing Calculations

### PWM Period

**PWM period (microseconds):**

```
T_PWM = 1,000,000 / f_PWM
```

For SK9822 (4.6 kHz):

```
T_PWM = 217.39 µs
```

For HD108 (27 kHz):

```
T_PWM = 37.04 µs
```

### PWM Cycles per Rotation

**Number of PWM cycles per complete rotation:**

```
N_PWM_cycles = T_rotation / T_PWM = (RPM × f_PWM) / 60
```

### Stroboscopic Analysis

**Angular width per PWM cycle (degrees):**

```
θ_PWM = 360 / N_PWM_cycles = 21,600 / (RPM × f_PWM)
```

**Beat frequency (when not synchronized):**

```
f_beat = |f_rotation - (f_PWM / n)|

where n is the nearest integer making the frequencies close
```

### PWM Strobing Quick Reference

#### SK9822 at 4.6 kHz

| RPM  | PWM cycles/rotation | Angular width/PWM (°) | Beat pattern period |
| ---- | ------------------- | --------------------- | ------------------- |
| 1000 | 76.67               | 4.696                 | ~13 rotations       |
| 1600 | 122.67              | 2.935                 | ~1.5 rotations      |
| 2000 | 153.33              | 2.348                 | ~3 rotations        |
| 2500 | 191.67              | 1.878                 | ~1.5 rotations      |
| 3000 | 230.00              | 1.565                 | Synchronized        |

#### HD108 at 27 kHz

| RPM  | PWM cycles/rotation | Angular width/PWM (°) | Beat pattern period |
| ---- | ------------------- | --------------------- | ------------------- |
| 1000 | 450.00              | 0.800                 | Synchronized        |
| 1600 | 720.00              | 0.500                 | Synchronized        |
| 2000 | 900.00              | 0.400                 | Synchronized        |
| 2500 | 1125.00             | 0.320                 | Synchronized        |
| 3000 | 1350.00             | 0.267                 | Synchronized        |

**Note:** HD108's higher PWM frequency eliminates most visible strobing effects.

---

## Spatial Resolution Calculations

### Arc Length per Angular Division

**Physical arc length at radius r for angular division θ:**

```
arc_length = r × θ × (π / 180)

where:
  r = radius in mm
  θ = angular division in degrees
```

**Arc length per degree at each LED:**

```
arc_1° = r × 0.017453  (mm)
```

### Arc Width During PWM Cycle

**Arc swept during one PWM period at LED position n:**

```
arc_PWM[n] = LED_radius[n] × θ_PWM × (π / 180)
```

### Arc Width Table (1° resolution)

| LED # | Radius (mm) | Arc/degree (mm) | Arc/PWM @ 2000 RPM, SK9822 (mm) | Arc/PWM @ 2000 RPM, HD108 (mm) |
| ----- | ----------- | --------------- | ------------------------------- | ------------------------------ |
| 0     | 35          | 0.611           | 1.434                           | 0.244                          |
| 1     | 42          | 0.733           | 1.721                           | 0.293                          |
| 2     | 49          | 0.855           | 2.008                           | 0.342                          |
| 3     | 56          | 0.977           | 2.295                           | 0.391                          |
| 4     | 63          | 1.100           | 2.582                           | 0.440                          |
| 5     | 70          | 1.222           | 2.869                           | 0.489                          |
| 6     | 77          | 1.344           | 3.156                           | 0.537                          |
| 7     | 84          | 1.466           | 3.443                           | 0.586                          |
| 8     | 91          | 1.588           | 3.730                           | 0.635                          |
| 9     | 98          | 1.710           | 4.017                           | 0.684                          |

---

## Data Transfer Timing

### SPI Transfer Time (Measured - NeoPixelBus @ 40MHz)

**Measured Show() call times (SK9822/APA102 via DotStarSpi40MhzMethod):**

```
30 LEDs: T_SPI = 45 µs (avg), DMA transfer ~38 µs
33 LEDs: T_SPI = 50 µs (avg), DMA transfer ~40 µs
42 LEDs: T_SPI = 58 µs (avg), DMA transfer ~48 µs
```

**Note:** Show() time includes waiting for previous DMA completion + queuing new DMA. Actual DMA transfer happens asynchronously in background (see NEOPIXELBUS_DMA_BEHAVIOR.md).

**Angular movement during Show() call:**

```
θ_SPI = (T_SPI / T_rotation) × 360 = (T_SPI × RPM) / 166,667
```

### SPI Transfer Angular Movement (30 LEDs, 45µs)

| RPM  | Angular movement during 45µs transfer (degrees) |
| ---- | ----------------------------------------------- |
| 700  | 0.189                                           |
| 1000 | 0.270                                           |
| 1600 | 0.432                                           |
| 2000 | 0.540                                           |
| 2500 | 0.675                                           |
| 2800 | 0.756                                           |
| 3000 | 0.810                                           |

### SPI Transfer Angular Movement (33 LEDs, 50µs)

| RPM  | Angular movement during 50µs transfer (degrees) |
| ---- | ----------------------------------------------- |
| 700  | 0.210                                           |
| 1000 | 0.300                                           |
| 1600 | 0.480                                           |
| 2000 | 0.600                                           |
| 2500 | 0.750                                           |
| 2800 | 0.840                                           |
| 3000 | 0.900                                           |

### SPI Transfer Angular Movement (42 LEDs, 58µs)

| RPM  | Angular movement during 58µs transfer (degrees) |
| ---- | ----------------------------------------------- |
| 700  | 0.244                                           |
| 1000 | 0.348                                           |
| 1600 | 0.557                                           |
| 2000 | 0.696                                           |
| 2500 | 0.870                                           |
| 2800 | 0.974                                           |
| 3000 | 1.044                                           |

### Maximum Update Rate

**Maximum updates per rotation (limited by Show() call time):**

```
N_max_updates = T_rotation / T_SPI
```

| RPM  | 30 LEDs (45µs)<br>Updates/rot | 33 LEDs (50µs)<br>Updates/rot | 42 LEDs (58µs)<br>Updates/rot |
| ---- | ---- | ---- | ---- |
| 700  | 1905 | 1714 | 1478 |
| 1000 | 1333 | 1200 | 1034 |
| 1600 | 833  | 750  | 647  |
| 2000 | 667  | 600  | 517  |
| 2500 | 533  | 480  | 414  |
| 2800 | 476  | 429  | 370  |
| 3000 | 444  | 400  | 345  |

---

## Brightness and Duty Cycle

### Global Brightness Control

**Effective duty cycle with global brightness:**

```
duty_cycle_effective = (PWM_value / 255) × (brightness / 31)

where:
  PWM_value = 0-255 (per channel)
  brightness = 0-31 (global, 5-bit)
```

**Actual ON time per PWM cycle:**

```
T_on = T_PWM × duty_cycle_effective
```

**Arc width of visible light at LED n:**

```
arc_visible[n] = arc_PWM[n] × duty_cycle_effective
```

### Brightness Examples at 2000 RPM, SK9822, LED 9 (outermost)

| Global | PWM Value | Duty Cycle | Arc Width Visible (mm) |
| ------ | --------- | ---------- | ---------------------- |
| 31     | 255       | 100%       | 4.017                  |
| 31     | 128       | 50%        | 2.009                  |
| 16     | 255       | 51.6%      | 2.073                  |
| 16     | 128       | 25.8%      | 1.036                  |
| 8      | 255       | 25.8%      | 1.036                  |
| 8      | 128       | 12.9%      | 0.518                  |

---

## Optimal Angular Resolution

### Choosing Angular Divisions

**Recommended angular divisions based on RPM:**

```
N_divisions = floor(T_rotation / (T_SPI + T_compute))

where T_compute = time needed for frame computation
```

**For minimum jitter, choose divisors of 360:**

- 360 (1.0°)
- 720 (0.5°)
- 180 (2.0°)
- 120 (3.0°)

### Practical Update Intervals

**Time budget per angular update:**

```
T_update = T_rotation / N_divisions
T_compute_available = T_update - T_SPI
```

#### 30 LEDs (T_SPI = 45µs)

| RPM  | For 360 divisions    | For 720 divisions         |
| ---- | -------------------- | ------------------------- |
|      | T_update / T_compute | T_update / T_compute      |
| 1600 | 104.17µs / 59.17µs   | 52.08µs / 7.08µs          |
| 2000 | 83.33µs / 38.33µs    | 41.67µs / **-3.33µs** ❌  |
| 2500 | 66.67µs / 21.67µs    | 33.33µs / **-11.67µs** ❌ |

#### 33 LEDs (T_SPI = 50µs)

| RPM  | For 360 divisions    | For 720 divisions         |
| ---- | -------------------- | ------------------------- |
|      | T_update / T_compute | T_update / T_compute      |
| 1600 | 104.17µs / 54.17µs   | 52.08µs / 2.08µs          |
| 2000 | 83.33µs / 33.33µs    | 41.67µs / **-8.33µs** ❌  |
| 2500 | 66.67µs / 16.67µs    | 33.33µs / **-16.67µs** ❌ |

#### 42 LEDs (T_SPI = 58µs)

| RPM  | For 360 divisions    | For 720 divisions         |
| ---- | -------------------- | ------------------------- |
|      | T_update / T_compute | T_update / T_compute      |
| 1600 | 104.17µs / 46.17µs   | 52.08µs / **-5.92µs** ❌  |
| 2000 | 83.33µs / 25.33µs    | 41.67µs / **-16.33µs** ❌ |
| 2500 | 66.67µs / 8.67µs     | 33.33µs / **-24.67µs** ❌ |

**Note:** At higher RPM, 720 divisions become impossible with measured SPI transfer times.

---

## Practical Implementation Formulas

### Hall Sensor Based Timing

**Measure RPM from hall sensor period:**

```
measured_period = time_between_hall_triggers (µs)
current_RPM = 60,000,000 / measured_period
```

**Calculate update interval for desired resolution:**

```
update_interval = measured_period / N_divisions
```

**Set hardware timer:**

```
timer_frequency = 1,000,000 / update_interval  (Hz)
```

### Angular Position Tracking

**Current angular position (degrees):**

```
θ_current = (micros() - last_hall_trigger_time) × 360 / measured_period

// Wrap to 0-360
θ_current = θ_current % 360
```

**Predict future position (for pre-rendering):**

```
θ_future = θ_current + (T_SPI × 360 / measured_period)
```

### Frame Buffer Addressing

**For a circular frame buffer with N angular divisions:**

```
buffer_index = (θ_degrees × N_divisions / 360) % N_divisions
```

**For three-arm interleaving:**

```
arm_A_angle = θ_current
arm_B_angle = (θ_current + 120) % 360
arm_C_angle = (θ_current + 240) % 360

// Get buffer indices for each arm
idx_A = (arm_A_angle × N_divisions / 360)
idx_B = (arm_B_angle × N_divisions / 360)
idx_C = (arm_C_angle × N_divisions / 360)
```

---

## Measurement and Verification Formulas

### Measuring Actual Performance

**Frame rate (full image refreshes per second):**

```
frame_rate = RPS = RPM / 60
```

**Effective pixel rate:**

```
pixel_rate = N_LEDs × N_updates_per_rotation × RPS
           = 30 × N_divisions × (RPM / 60)  (pixels/second)
```

**Visual resolution test:**

```
min_distinguishable_feature = arc_width × duty_cycle

Measure minimum: display alternating on/off pixels
radially and angularly to find practical limits
```

### Jitter Measurement

**Timing jitter:**

```
jitter = max(update_times) - min(update_times)

// Should be < T_degree for acceptable quality
acceptable_jitter < 166,667 / RPM  (µs)
```

**Angular jitter (degrees):**

```
θ_jitter = (timing_jitter × 360) / T_rotation
         = (timing_jitter × RPM) / 166,667
```

---

## Design Trade-offs Summary

### SK9822 vs HD108 PWM Frequency

| Characteristic              | SK9822 (4.6 kHz)             | HD108 (27 kHz)      |
| --------------------------- | ---------------------------- | ------------------- |
| PWM Period                  | 217.39 µs                    | 37.04 µs            |
| Strobing visible?           | Yes, noticeable              | Minimal to none     |
| Arc width (LED 9, 2000 RPM) | 4.02 mm                      | 0.68 mm             |
| Artistic effects            | Strong stroboscopic patterns | Smoother appearance |
| Synchronization             | Harder to sync               | Easier to sync      |

**Recommendation:** Use HD108 for cleaner images, SK9822 if you want to exploit strobing effects artistically.

### Angular Resolution vs RPM

Higher RPM requires either:

1. Fewer angular divisions (lower resolution)
2. Faster computation (shorter T_compute)
3. Faster SPI (hardware limitation)

**Sweet spot:** 2000 RPM with 360 divisions (1° resolution) gives 39.33 µs for computation.

### Brightness and Visibility

Lower brightness reduces visible arc width:

```
visibility_improvement_factor = 1 / sqrt(duty_cycle)
```

This means 25% brightness requires 2× slower RPM for same effective resolution.

---

## Quick Calculation Checklist

To analyze any POV display configuration, calculate in order:

1. **Basic timing:**

   - T_rotation = 60,000,000 / RPM
   - T_degree = T_rotation / 360

2. **PWM characteristics:**

   - T_PWM = 1,000,000 / f_PWM
   - θ_PWM = 360 × T_PWM / T_rotation

3. **Spatial resolution:**

   - arc_length = radius × θ × (π/180)
   - arc_PWM = radius × θ_PWM × (π/180)

4. **Update timing:**

   - N_divisions = desired angular resolution
   - T_update = T_rotation / N_divisions
   - T_compute = T_update - T_SPI

5. **Verify feasibility:**
   - Is T_compute > 0?
   - Is θ_SPI < desired resolution?
   - Is jitter acceptable?

---

## Example Calculation Workflow

**Given:**

- RPM = 2000
- PWM frequency = 27 kHz (HD108)
- LED count = 33
- T_SPI = 50 µs (measured)
- Desired resolution = 1° (360 divisions)
- Global brightness = 16 (50%)
- LED of interest = LED 9 (98mm radius)

**Calculate:**

1. T_rotation = 60,000,000 / 2000 = 30,000 µs
2. T_degree = 30,000 / 360 = 83.33 µs
3. T_PWM = 1,000,000 / 27,000 = 37.04 µs
4. θ_PWM = 360 × 37.04 / 30,000 = 0.444°
5. arc_1° = 98 × 0.017453 = 1.710 mm
6. arc_PWM = 98 × 0.444 × (π/180) = 0.759 mm
7. T_update = 30,000 / 360 = 83.33 µs
8. T_compute = 83.33 - 50 = 33.33 µs ✓
9. θ_SPI = 50 × 360 / 30,000 = 0.600° ✓
10. duty_cycle = 16/31 = 51.6%
11. arc_visible = 0.759 × 0.516 = 0.392 mm

**Result:** Feasible configuration with 0.392mm visible arc width per PWM pulse at 50% brightness.

---

## Appendix: Unit Conversions

```
1 degree = 0.017453 radians
1 radian = 57.2958 degrees
1 RPM = 0.01667 RPS = 6 degrees/second
Circumference = 2π × radius
Arc length = radius × angle_radians
```

---

**End of Reference Document**

For implementation examples and code snippets, see separate programming guide.
