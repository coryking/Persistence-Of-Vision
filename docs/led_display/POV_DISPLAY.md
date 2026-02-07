# LED Mapping

> **Remember: We're here to make art, not write code.** The code serves the art. Every hardware constraint — the timing, the interleaving, the color fringing — is part of the medium. Work with it, not against it.

The POV display is composed of three arms with uneven LED counts: ARM3 (outer) has 14 LEDs, while ARM1 and ARM2 have 13 LEDs each. ARM3's extra LED is positioned at the hub end (1/3 pitch further inward than the other arms). When spinning fast, they form a "virtual display" of 40 radial pixels.

---

## What Is This?

A persistence of vision (POV) display — a spinning disc with LED strips that trace out images in mid-air as they rotate. Your eye blends the rapidly flashing LEDs into a stable circular image.

**Physical structure:**

- A disc spins at 1200–1940 RPM driven by a brushed motor (speed is software-controllable)
- Three arms extend outward: ARM3 has 14 LEDs, ARM1 and ARM2 have 13 LEDs each
- 41 physical LEDs total: 1 level shifter (always dark) + 40 display LEDs
- The arms are spaced 120° apart
- A hall effect sensor provides one timing pulse per revolution

**The hub (inner ~90mm diameter):**

- ESP32-S3 microcontroller (Seeed XIAO or similar)
- Power regulation and distribution
- Wireless power receiver (the whole thing spins, so no wires!)
- The "brains" that compute what each LED should display at each instant

**The display area (donut-shaped):**

- LEDs begin ~45mm from center (outside the hub)
- LEDs extend to ~115mm from center (45mm + 70mm arm length)
- This creates a ring-shaped canvas, not a full circle
- The inner void is where all the electronics live

When spinning, the 40 LEDs sweep through 360° many times per second. By carefully timing when each LED turns on and what color it shows, we paint images in the air.

**Why 1200–1940 RPM?** This is the current measured operating range. At 1940 RPM (32 rev/sec), we get good image persistence with minimal flicker. At 1200 RPM (20 rev/sec), images are still visible but may show more flicker. The slower speeds are mechanically safer during development and give plenty of timing margin (85-139μs per degree vs 44μs SPI update time).

---

## Coordinate System

This is a **polar coordinate** display. Every point is defined by (r, θ):

### θ (Theta) — Angular Position

- **Range:** 0.0 to 360.0 degrees
- **Origin:** Hall effect sensor position (0°)
- **Direction:** Increases with spin direction
- **Preferred storage:** `float`

**Why float?** Physical angular resolution depends on hardware factors we're still tuning:

- SPI transfer time (~44μs currently)
- LED PWM frequency
- Rotation speed (1200–1940 RPM measured)

At 1940 RPM with 44μs SPI, resolution is ~0.5° (44μs / 85.9μs per degree). At 1200 RPM it's ~0.3° (44μs / 138.9μs per degree). Floats give us room to optimize without refactoring.

### Angular: Arcs, Not Pixels

Unlike the radial dimension (which has 30 discrete LEDs), the angular dimension has no fixed pixels. An LED turns on, the arm sweeps, the LED turns off — you're painting an arc in time.

**Arc width = on-duration × rotation speed**

This means angular "resolution" is fluid, not fixed. It depends on how fast you can switch LEDs and how fast the disc spins.

**PWM and stroboscopic effects:** The LED's PWM frequency interacts with rotation to create interesting artifacts — what can appear as floating angular "pixels" whose width relates to brightness. This is an area for future exploration and potential artistic exploitation.

### r — Radial Position

- **Range:** Depends on address space (0–13 for ARM3, 0–12 for ARM1/ARM2, or 0–39 virtual)
- **Origin:** Center of display (innermost = 0)
- **Direction:** Increases outward
- **Preferred storage:** integer (`uint8_t`)

**Why integer?** There are exactly 40 discrete LEDs. Animation math can use floats internally, but rendering always quantizes to an integer LED index.

### Polar Coordinate Reminder

Because this is polar, not Cartesian:

- A "rectangle" with constant radial height on both sides forms a **trapezoid** (wider at the outside)
- A true visual rectangle requires the outer edge to have a smaller angular span than the inner edge
- "Straight lines" that aren't purely radial will appear curved

Keep this in mind when designing effects.

---

## Timing

A hall effect sensor triggers once per revolution when **Arm A** passes it. This is the only external timing reference.

From this single trigger, we derive:

1. **Revolution period** — averaged over recent revolutions for stability
2. **Microseconds per degree** — revolution period / 360
3. **Current angle of Arm A** — (elapsed time since trigger) / (microseconds per degree)
4. **Current angle of Arms B and C** — derived from Arm A's angle + phase offset

At any instant, if Arm A is at angle θ:

- Arm B is at (θ + 120) mod 360
- Arm C is at (θ + 240) mod 360

Effects receive the current timestamp and can compute angular positions as needed. They have direct access to the entire LED strip and decide what to light up at each instant.

---

## Physical Wiring

LEDs are wired with a level shifter first, then inside-to-outside on each arm, daisy-chained arm to arm:

| Physical Address | Component | Notes                                  |
| ---------------- | --------- | -------------------------------------- |
| 0                | Level Shifter | Always dark (3.3V→5V conversion)  |
| 1–13             | ARM1 (arm[2]) | Inside, 13 LEDs, normal order     |
| 14–26            | ARM2 (arm[1]) | Middle, 13 LEDs, normal order, hall sensor reference |
| 27–40            | ARM3 (arm[0]) | Outer, 14 LEDs, **REVERSED** order |

`strip.SetPixelColor()` expects physical addresses 0–40. See `include/hardware_config.h` for authoritative configuration.

---

## Virtual Display (Radial Mapping)

Because the arms are staggered radially by 1/3 LED pitch, the three arms interleave when spinning to form 40 contiguous virtual radial positions.

**This is the whole point of the staggered design** — it unlocks 40 pixels of radial resolution instead of 13-14. Effects that use virtual addressing can draw continuous shapes from center to edge without worrying about which physical arm each LED belongs to.

### Asymmetric Arm Lengths

ARM3 (arm[0], outer) has 14 LEDs, while ARM1 and ARM2 have 13 LEDs each. ARM3's extra LED is at the **hub end**, positioned 1/3 LED pitch further inward than the innermost LEDs of ARM1/ARM2. This creates a virtual display where:

- Virtual pixel 0 is ARM3's extra inner LED (no matching LED on other arms)
- Virtual pixels 1-39 interleave normally across all three arms

| Virtual (Radial) | arm[0] (ARM3) | arm[1] (ARM2) | arm[2] (ARM1) |
| ---------------- | ------------- | ------------- | ------------- |
| 0                | pixels[0]     | -             | -             |
| 1-3              | pixels[1]     | pixels[0]     | pixels[0]     |
| 4-6              | pixels[2]     | pixels[1]     | pixels[1]     |
| ...              | ...           | ...           | ...           |
| 37-39            | pixels[13]    | pixels[12]    | pixels[12]    |

### Lookup Table Implementation

The virtual pixel mapping uses lookup tables (defined in `RenderContext.h`) because the non-uniform arm lengths prevent a simple formula:

```cpp
// Which arm for each virtual pixel (0-39)
static constexpr uint8_t VIRT_ARM[40] = {
    0,          // v=0: ARM3's extra inner
    0, 1, 2,    // v=1-3: radial row 1
    0, 1, 2,    // v=4-6: radial row 2
    // ... repeats for radial rows 3-13 ...
};

// Which pixel index on that arm
static constexpr uint8_t VIRT_PIXEL[40] = {
    0,              // v=0: ARM3's extra inner
    1, 0, 0,        // v=1-3: radial row 1
    2, 1, 1,        // v=4-6: radial row 2
    // ... arm[0] pixel = radial+1, others = radial ...
};
```

**Access virtual pixel directly from RenderContext:**
```cpp
ctx.virt(virtualPos) = color;  // Sets the LED at virtual position (0-39)
```

### Effects and Buffer Sizing

Effects render to `LEDS_PER_ARM` (14) pixel slots per arm. The unused `pixels[13]` slot on 13-LED arms is simply not copied to hardware by `copyPixelsToStrip()`. This avoids complicating effect code while wasting minimal CPU time.

---

## Per-Arm Reference

Quick reference for working with individual arms (see `include/hardware_config.h` for authoritative values):

| Arm Index | Name  | Physical Range | LED Count | Radial Position | Phase Offset | Hall Sensor |
| --------- | ----- | -------------- | --------- | --------------- | ------------ | ----------- |
| arm[2]    | ARM1  | 1–13           | 13        | Inside          | +120°        | No          |
| arm[1]    | ARM2  | 14–26          | 13        | Middle          | 0°           | **Yes** (triggers at θ=0) |
| arm[0]    | ARM3  | 27–40          | 14        | Outer           | +240°        | No          |

Note: Physical index 0 is the level shifter (always dark).

---

## Physical Layout Details

This section helps visualize the hardware. **Don't encode millimeters in code or data structures** — all code should work in LED indices and degrees. These measurements are for mental models only.

### Arm Geometry

- **LED strip:** 144 LEDs/meter (HD107s, SK9822/APA102 compatible)
- **LED pitch:** ~7mm
- **LEDs per arm:** ARM3 has 14, ARM1/ARM2 have 13 each (40 total display LEDs)
- **Physical strip:** 41 LEDs including level shifter at index 0
- **Angular spacing:** 120° between arms

### Radial Stagger

The arms are intentionally offset by 1/3 LED pitch (~2.33mm) to create the interleaved virtual display:

| Arm   | Radial Offset                         |
| ----- | ------------------------------------- |
| ARM3  | -2.33mm (has extra inner LED)         |
| ARM1  | 0mm (innermost of the 13-LED arms)    |
| ARM2  | +2.33mm                               |

This stagger means that when spinning, the 40 LEDs appear as 40 evenly-spaced radial pixels rather than 3 groups of ~13.

### RGB Sub-Pixel Positions

The 5050 package LEDs have their R, G, and B elements at slightly different radial positions within the die. When spinning at high RPM, these trace out three slightly different arcs rather than perfectly overlapping. This creates color fringing at the edges of illuminated regions.

This is a constraint of the medium, not a bug. Possible approaches:

- Physical diffuser to blur the point sources into softer light
- Sub-pixel rendering techniques (adjusting arc timing per color channel)
- Lean into it — design effects that exploit the fringing artistically

---

## Notes

- **Angular wrapping:** Effects must handle the 360° → 0° wraparound. An arc from 350° to 10° spans 20°, not -340°.
