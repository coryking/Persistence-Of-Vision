# LED Mapping

> **Remember: We're here to make art, not write code.** The code serves the art. Every hardware constraint — the timing, the interleaving, the color fringing — is part of the medium. Work with it, not against it.

The POV display is composed of three arms with 10 LEDs each located 120 degrees apart. The LED strips on each arm are offset radially by 1/3 the radial height of an LED so that when spinning fast, they form a "virtual display" of 30 radial pixels.

---

## What Is This?

A persistence of vision (POV) display — a spinning disc with LED strips that trace out images in mid-air as they rotate. Your eye blends the rapidly flashing LEDs into a stable circular image.

**Physical structure:**

- A disc spins at 1200–1940 RPM driven by a brushed motor (speed is software-controllable)
- Three arms extend outward, each carrying 10 LEDs
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

When spinning, the 30 LEDs sweep through 360° many times per second. By carefully timing when each LED turns on and what color it shows, we paint images in the air.

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

- **Range:** Depends on address space (0–9 per arm, or 0–29 virtual)
- **Origin:** Center of display (innermost = 0)
- **Direction:** Increases outward
- **Preferred storage:** integer (`uint8_t`)

**Why integer?** There are exactly 30 discrete LEDs. Animation math can use floats internally, but rendering always quantizes to an integer LED index.

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

LEDs are wired inside-to-outside on each arm, then daisy-chained arm to arm:

| Physical Address | Arm | Notes                                  |
| ---------------- | --- | -------------------------------------- |
| 0–9              | A   | Innermost; triggers hall sensor at θ=0 |
| 10–19            | B   | Middle radial position                 |
| 20–29            | C   | Outermost                              |

`strip.SetPixelColor()` expects physical addresses 0–29.

---

## Virtual Display (Radial Mapping)

Because the arms are staggered radially by 1/3 LED pitch, the three arms interleave when spinning to form 30 contiguous virtual radial positions.

**This is the whole point of the staggered design** — it unlocks 30 pixels of radial resolution instead of just 10. Effects that use virtual addressing can draw continuous shapes from center to edge without worrying about which physical arm each LED belongs to.

| Virtual (Radial) | Arm | Physical |
| ---------------- | --- | -------- |
| 0                | A   | 0        |
| 1                | B   | 10       |
| 2                | C   | 20       |
| 3                | A   | 1        |
| 4                | B   | 11       |
| 5                | C   | 21       |
| 6                | A   | 2        |
| 7                | B   | 12       |
| 8                | C   | 22       |
| ...              | ... | ...      |
| 27               | A   | 9        |
| 28               | B   | 19       |
| 29               | C   | 29       |

Pattern repeats: A, B, C, A, B, C... (innermost to outermost)

**Lookup table:**

```cpp
const uint8_t VIRTUAL_TO_PHYSICAL[30] = {
     0, 10, 20,  // virtual 0-2
     1, 11, 21,  // virtual 3-5
     2, 12, 22,  // virtual 6-8
     3, 13, 23,  // virtual 9-11
     4, 14, 24,  // virtual 12-14
     5, 15, 25,  // virtual 15-17
     6, 16, 26,  // virtual 18-20
     7, 17, 27,  // virtual 21-23
     8, 18, 28,  // virtual 24-26
     9, 19, 29   // virtual 27-29
};
```

**Reverse lookup (which virtual position is this physical LED?):**

```cpp
const uint8_t PHYSICAL_TO_VIRTUAL[30] = {
     0,  3,  6,  9, 12, 15, 18, 21, 24, 27,  // physical 0-9 (Arm A)
     1,  4,  7, 10, 13, 16, 19, 22, 25, 28,  // physical 10-19 (Arm B)
     2,  5,  8, 11, 14, 17, 20, 23, 26, 29   // physical 20-29 (Arm C)
};
```

---

## Per-Arm Reference

Quick reference for working with individual arms:

| Arm | Physical Range | Radial Position | Phase Offset | Hall Sensor               |
| --- | -------------- | --------------- | ------------ | ------------------------- |
| A   | 0–9            | Innermost       | 0°           | **Yes** (triggers at θ=0) |
| B   | 10–19          | Middle          | +120°        | No                        |
| C   | 20–29          | Outermost       | +240°        | No                        |

---

## Physical Layout Details

This section helps visualize the hardware. **Don't encode millimeters in code or data structures** — all code should work in LED indices and degrees. These measurements are for mental models only.

### Arm Geometry

- **LED strip:** 144 LEDs/meter (SK9822/APA102 compatible)
- **LED pitch:** ~7mm
- **LEDs per arm:** 10
- **Arm length:** ~70mm
- **Angular spacing:** 120° between arms

### Radial Stagger

The arms are intentionally offset by 1/3 LED pitch (~2.33mm) to create the interleaved virtual display:

| Arm | Radial Offset   |
| --- | --------------- |
| A   | 0mm (innermost) |
| B   | +2.33mm         |
| C   | +4.66mm         |

This stagger means that when spinning, the 30 LEDs appear as 30 evenly-spaced radial pixels rather than 3 groups of 10.

### RGB Sub-Pixel Positions

The 5050 package LEDs have their R, G, and B elements at slightly different radial positions within the die. When spinning at high RPM, these trace out three slightly different arcs rather than perfectly overlapping. This creates color fringing at the edges of illuminated regions.

This is a constraint of the medium, not a bug. Possible approaches:

- Physical diffuser to blur the point sources into softer light
- Sub-pixel rendering techniques (adjusting arc timing per color channel)
- Lean into it — design effects that exploit the fringing artistically

---

## Notes

- **Angular wrapping:** Effects must handle the 360° → 0° wraparound. An arc from 350° to 10° spans 20°, not -340°.
