# Phosphor Decay Algorithms for Display Simulation

Reference document for implementing authentic CRT phosphor decay effects, particularly for PPI radar display simulation.

---

## Fundamental Decay Types

### 1. Exponential Decay (Fast Phosphors)

Used by: P1, P3, P11, P12, P13, P31

```
I(t) = I₀ × e^(-t/τ)
```

Where:
- `I(t)` = intensity at time t
- `I₀` = initial intensity at t=0
- `τ` (tau) = time constant (time to decay to 37% of initial)
- `e` = Euler's number (~2.718)

**Frame buffer implementation:**
```cpp
decay_factor = exp(-dt / tau);
new_brightness = old_brightness * decay_factor;
```

For discrete frame updates at fixed intervals:
```cpp
// Precompute for your frame rate
const float decay_per_frame = exp(-frame_time_seconds / tau);

// Each frame:
brightness *= decay_per_frame;
```

---

### 2. Inverse Power Law / Hyperbolic Decay (Long Persistence)

Used by: P2, P7, P14 (radar phosphors)

```
I(t) = I₀ / (1 + γt)^n
```

Where:
- `γ` (gamma) = time scaling constant, typically `1/τ`
- `n` = power exponent (0.5 to 2, typically ~1 for radar)

**Simplified form when n=1:**
```
I(t) = I₀ / (1 + t/τ)
```

**Frame buffer implementation:**
```cpp
// Track time since last excitation for each pixel
float time_since_hit;  // in seconds

// Inverse power law (n=1)
float brightness = initial_brightness / (1.0f + time_since_hit / tau);

// Or for arbitrary n:
float brightness = initial_brightness / pow(1.0f + time_since_hit / tau, n);
```

**Key difference from exponential:** Power law decays fast initially then slows dramatically, holding a dim glow far longer than exponential predicts. This creates the characteristic "trails that never quite disappear" look of real radar.

---

### 3. Multi-Exponential (Most Accurate)

Research shows triple-exponential models fit real phosphor data best:

```
I(t) = A₁×e^(-t/τ₁) + A₂×e^(-t/τ₂) + A₃×e^(-t/τ₃)
```

Where:
- `τ₁` = fast fluorescence component (microseconds)
- `τ₂` = medium decay component (milliseconds)  
- `τ₃` = long afterglow component (seconds)
- `A₁ + A₂ + A₃ = 1.0` (weights sum to unity)

---

## Phosphor Time Constants Reference

### Oscilloscope / Fast Phosphors

| Phosphor | Color | Persistence (to 10%) | Time Constant (τ) | Decay Type |
|----------|-------|---------------------|-------------------|------------|
| P31 | Yellow-green | 38μs | ~16μs | Exponential |
| P4 | White | 60μs | ~25μs | Combined |
| P11 | Blue | 2ms | ~0.9ms | Exponential |
| P1 | Green | 20-24ms | ~10ms | Exponential |

### Radar / Long Persistence Phosphors

| Phosphor | Color | Persistence | Decay Type | Notes |
|----------|-------|-------------|------------|-------|
| P2 | Blue-green/green | >30 sec | Inverse power (n≈1) | |
| P7 | Blue-white → Yellow | >60 sec | Inverse power (n≈1) | Cascade dual-layer |
| P12 | Orange | few seconds | Exponential | τ ≈ 1-2s |
| P14 | Violet → Orange | >30 sec | Inverse power | Dual-layer |
| P19 | Orange | Very long | | |
| P28 | Yellow-green | 600ms | | τ ≈ 250ms |

### TV Phosphors (P22 RGB)

| Color | Composition | τ (1/e time) | Decay Shape |
|-------|-------------|--------------|-------------|
| Red | Y₂O₂S:Eu³⁺ | 150μs | Single exponential |
| Green | Zn₂SiO₄:Mn²⁺ | 10-15ms | Multi-exponential, visible tail ~1s |
| Blue | ZnS:Ag,Cl | 100-200μs | Non-single exponential |

---

## P7 Cascade Phosphor (Classic Radar)

P7 is a **dual-layer cascade** phosphor requiring two separate decay calculations:

### Layer 1: Blue-White Flash (Top)
- Composition: ZnS:Ag (zinc sulfide, silver activated)
- Peak wavelength: 440nm
- Decay: Sub-millisecond exponential
- τ ≈ 0.1-0.5ms

### Layer 2: Yellow-Green Afterglow (Bottom)
- Composition: ZnS:CdS:Cu (zinc-cadmium sulfide, copper activated)
- Peak wavelength: 558nm
- Decay: Inverse power law
- Persistence: >60 seconds

### Cascade Mechanism

The blue layer emits UV/blue light that **excites** the yellow layer. They are not independent.

**Important:** Both layers respond to ALL electron beam excitation - sweep line AND targets. This is a physical/chemical cascade, not a selective system. The sequence is:

1. Electron beam hits phosphor (any hit - sweep or target)
2. Blue layer (top) fluoresces immediately - bright blue-white flash, decays in <1ms
3. Blue/UV light from that flash excites the yellow layer below
4. Yellow layer (bottom) phosphoresces slowly - yellow-green glow persisting 60+ seconds

### Sweep vs Target Appearance

The visual difference between sweep and targets comes from **intensity**, not which layer responds:

| Element | Beam Intensity | Blue Flash | Yellow Excitation | Result |
|---------|---------------|------------|-------------------|--------|
| Sweep line | Low (barely unblanked) | Dim | Modest | Faint visible trail |
| Targets | High (Z-axis modulated) | Bright | Strong | Bright persistent blip |

When you see that classic radar look - bright sweep line with targets glowing behind it - *everything* is going through both layers. Targets just got hit harder initially, so they charged up the yellow layer more and persist longer/brighter.

**Implementation implication:** You don't need separate systems for sweep vs targets - just vary the initial excitation intensity. Both feed into the same dual-layer decay model:

```cpp
// Simplified P7 simulation
struct P7Pixel {
    float blue_intensity;      // Fast decay
    float yellow_excitation;   // Accumulated from blue
    float yellow_intensity;    // Slow decay
};

void updateP7(P7Pixel& p, float dt, float new_hit) {
    // Blue layer: fast exponential decay
    const float tau_blue = 0.0003f;  // 0.3ms
    p.blue_intensity = new_hit + p.blue_intensity * exp(-dt / tau_blue);
    
    // Yellow layer accumulates excitation from blue
    p.yellow_excitation += p.blue_intensity * dt;
    
    // Yellow layer: inverse power law decay
    // (simplified - real impl tracks time since excitation)
    const float tau_yellow = 5.0f;  // seconds
    p.yellow_intensity = p.yellow_excitation / (1.0f + dt / tau_yellow);
}
```

### P7 Color Transition

For RGB simulation, P7 transitions through these approximate colors:

| Phase | Time | RGB (approximate) |
|-------|------|-------------------|
| Initial flash | 0-1ms | (200, 200, 255) blue-white |
| Early decay | 1-100ms | (150, 200, 150) transitioning |
| Mid decay | 100ms-1s | (120, 180, 80) yellow-green |
| Late decay | 1s-60s | (80, 140, 50) dim yellow-green |
| Final glow | >60s | (50, 100, 25) barely visible |

---

## Converting Specifications to Constants

### "Persistence" to Time Constant

Persistence specs are usually "time to decay to 10% of initial brightness."

**For exponential decay:**
```
τ = persistence / ln(10) = persistence / 2.303
```

**For inverse power law (n=1):**
```
τ = persistence / 9
```

### Frame Rate Considerations

For 60fps rendering with exponential decay:
```cpp
const float frame_time = 1.0f / 60.0f;  // 16.67ms
const float tau = 0.010f;  // 10ms time constant

// Precompute decay factor
const float decay_per_frame = exp(-frame_time / tau);
// For tau=10ms at 60fps: decay_per_frame ≈ 0.189
```

For inverse power law, you must track elapsed time per pixel:
```cpp
// Each pixel needs:
float time_since_excitation;  // Reset to 0 on new hit, increment each frame

// Brightness calculation:
float brightness = peak / (1.0f + time_since_excitation / tau);
```

---

## Implementation Patterns

### Pattern 1: Simple Exponential (Sweep Trail)

Good for: sweep line glow, fast-decay elements

```cpp
void updateSweepTrail(uint8_t* buffer, int width, float sweep_angle, float dt) {
    const float tau = 0.5f;  // 500ms persistence
    const float decay = exp(-dt / tau);
    
    for (int i = 0; i < width; i++) {
        // Decay existing
        buffer[i] = (uint8_t)(buffer[i] * decay);
        
        // Add new sweep position
        if (isAtSweepAngle(i, sweep_angle)) {
            buffer[i] = 255;
        }
    }
}
```

### Pattern 2: Power Law with Time Tracking (Targets)

Good for: radar blips, long-persistence targets

```cpp
struct Target {
    float angle;
    float range;
    float time_since_painted;  // seconds
    float peak_intensity;
};

float getTargetBrightness(const Target& t) {
    const float tau = 5.0f;  // 5 second characteristic time
    const float n = 1.0f;    // power exponent
    
    return t.peak_intensity / pow(1.0f + t.time_since_painted / tau, n);
}
```

### Pattern 3: Dual-Layer P7 Simulation

Good for: authentic WWII/Cold War radar look

```cpp
struct RadarPixel {
    float blue;           // Fast layer (ms scale)
    float yellow;         // Slow layer (seconds scale)
    float yellow_charge;  // Accumulated excitation
};

void updatePixel(RadarPixel& p, float dt, float hit_intensity) {
    const float tau_blue = 0.001f;    // 1ms
    const float tau_yellow = 10.0f;   // 10s
    const float cascade_rate = 0.5f;  // How much blue excites yellow
    
    // Blue: exponential decay, add new hit
    p.blue = hit_intensity + p.blue * exp(-dt / tau_blue);
    
    // Yellow: charge from blue, power-law discharge
    p.yellow_charge += p.blue * cascade_rate * dt;
    p.yellow = p.yellow_charge / (1.0f + dt / tau_yellow);
    p.yellow_charge *= 0.999f;  // Slow charge decay
}

// Render with color blend
void renderPixel(const RadarPixel& p, uint8_t& r, uint8_t& g, uint8_t& b) {
    // Blue-white component from fast layer
    // Yellow-green component from slow layer
    r = (uint8_t)min(255.0f, p.blue * 200);
    g = (uint8_t)min(255.0f, p.blue * 200 + p.yellow * 140);
    b = (uint8_t)min(255.0f, p.blue * 255 + p.yellow * 50);
}
```

---

## Radar Antenna Rotation Speeds

Authentic rotation speeds vary significantly by application. This affects how long targets must persist between refreshes.

### Marine Radar

| Type | RPM | Seconds/Revolution | Notes |
|------|-----|-------------------|-------|
| Conventional vessels | **24 RPM** | 2.5 sec | Standard commercial shipping |
| High-speed vessels | **44-48 RPM** | 1.25-1.4 sec | Fast ferries, patrol boats |
| Adjustable range | 20-50 RPM | 1.2-3 sec | Modern variable-speed systems |

### Air Traffic Control (Airport Surveillance Radar)

| Type | RPM | Seconds/Revolution | Notes |
|------|-----|-------------------|-------|
| ASR-9, ASR-11 | **12.5 RPM** | 4.8 sec | Standard US airport radar |
| General ASR range | 12-15 RPM | 4-5 sec | ICAO/EUROCONTROL standard |
| Primary Surveillance | 5-12 RPM | 5-12 sec | Varies by range requirements |

### En Route / Long Range

| Type | RPM | Seconds/Revolution | Notes |
|------|-----|-------------------|-------|
| En route Mode S | **5 RPM** | 12 sec | Long-range air surveillance |
| WWII-era systems | 4-6 RPM | 10-15 sec | Historical reference |

### Choosing Rotation Speed for Simulation

| Desired Feel | RPM | Period | Phosphor Persistence Needed |
|--------------|-----|--------|----------------------------|
| Marine / urgent | ~24 | 2.5 sec | Targets visible 3+ seconds |
| ATC / deliberate | ~12 | 5 sec | Targets visible 6+ seconds |
| Long-range / dramatic | ~6 | 10 sec | Targets visible 12+ seconds |
| WWII / cinematic | ~4 | 15 sec | Targets visible 20+ seconds |

**Key insight:** The slower ATC radar rotation is why classic air traffic control displays have those long phosphor trails - targets must remain visible for the full 5 seconds between refreshes. This is precisely why P7's 60+ second persistence was essential for these applications.

### Rotation Speed vs Display Update Math

For a POV display with angular resolution of 1°:

```cpp
// Time per degree at different RPMs
float time_per_degree = 60.0f / (rpm * 360.0f);  // seconds

// Examples:
// 24 RPM: 60/(24*360) = 6.94ms per degree
// 12 RPM: 60/(12*360) = 13.89ms per degree
//  6 RPM: 60/(6*360)  = 27.78ms per degree
```

---

## Radar Range and Target Speeds

Understanding what radar actually tracks helps calibrate expectations for how fast things move on the display.

### Typical Radar Ranges

| Application | Typical Max Range | Common Operating Scales |
|-------------|-------------------|-------------------------|
| Marine radar (recreational) | 24-72 nm | 0.5, 1, 3, 6, 12 nm |
| Marine radar (commercial) | 48-96 nm | 6, 12, 24, 48 nm |
| ATC airport surveillance (ASR) | 60 nm | 5, 10, 20, 40, 60 nm |
| En route ATC | 200+ nm | 60, 120, 200 nm |

The "classic radar look" typically shows 6-24 nm range scales.

### Ship Speeds (Marine Radar Targets)

| Vessel Type | Typical Speed | Notes |
|-------------|---------------|-------|
| Bulk carriers | 12-15 knots | Slow, heavy cargo |
| Oil tankers | 10-17 knots | Safest slow |
| Container ships | 16-24 knots | Time-sensitive cargo |
| Cruise ships | 20-25 knots | Passenger comfort |
| Fast ferries | 30-40 knots | High-speed vessels |
| Naval vessels | 25-35+ knots | Varies widely |

### Aircraft Speeds (ATC Radar Targets)

| Aircraft Type | Typical Speed | Context |
|---------------|---------------|--------|
| Small prop (approach) | 70-100 knots | Landing configuration |
| Turboprop | 150-200 knots | Terminal area |
| Jet (approach) | 130-170 knots | Final approach |
| Jet (terminal area) | 210-250 knots | Vectors to final |
| Jet (cruise) | 400-500 knots | En route |

### How Fast Do Targets Actually Move on the Display?

The angular velocity of a target on the display depends on its speed, range, and direction of travel. For a target moving perpendicular to the line of sight (worst case / fastest apparent motion):

```
Angular velocity ≈ speed (knots) / range (nm) degrees per minute
```

**Marine radar examples (12 nm range scale, 24 RPM antenna):**

| Target | Speed | Angular Motion | Movement Per 2.5 sec Scan |
|--------|-------|----------------|---------------------------|
| Tanker | 15 kn | ~1.2°/min | ~0.05° |
| Container ship | 20 kn | ~1.7°/min | ~0.07° |
| Fast ferry | 35 kn | ~2.9°/min | ~0.12° |

**ATC radar examples (40 nm range scale, 12 RPM antenna):**

| Target | Speed | Angular Motion | Movement Per 5 sec Scan |
|--------|-------|----------------|-------------------------|
| Prop plane | 100 kn | ~2.5°/min | ~0.2° |
| Jet on approach | 170 kn | ~4.3°/min | ~0.35° |
| Jet cruising | 450 kn | ~11°/min | ~0.9° |

### The Uncomfortable Truth

**Targets move incredibly slowly in angular terms.** A container ship on a 12 nm radar display moves less than 0.1° between sweeps. Even a jet aircraft on an ATC scope barely moves 1° per scan.

This is why:
- Radar operators stare at screens for hours watching nearly-stationary blips
- P7 phosphor's 60+ second persistence was necessary—targets had to remain visible through dozens of sweeps to show any perceptible motion
- The "comet tail" effect from moving targets is extremely subtle in real life
- Collision avoidance requires watching targets over many minutes to detect closing vectors

**For artistic purposes:** Real radar is, frankly, boring to watch. The dramatic sweeping displays in movies with rapidly-moving blips are wildly exaggerated. For visual effect, target speeds are typically increased 10-50× over reality, or the simulated antenna rotation is slowed dramatically to compress time.

---

## Common Mistakes to Avoid

1. **Linear decay instead of exponential** - Creates unnatural "fading" look
2. **Using exponential for long-persistence** - Fades too quickly; use power law
3. **Ignoring the blue→yellow transition** - P7's signature is the color shift
4. **Point targets instead of clusters** - Real radar returns are spread blobs
5. **Uniform brightness for all targets** - Vary by radar cross-section
6. **No noise floor** - Real displays have "grass" (fast-decaying random speckles)
7. **Separate sweep/target layer systems** - Both go through the same cascade; vary intensity instead

---

## References

- Shionoya & Yen, "Phosphor Handbook" (1998)
- W.T. Dyall, "A Study of the Persistence Characteristics of Various Cathode Ray Tube Phosphors", MIT Research Laboratory of Electronics Technical Report No. 56 (1948)
- Patrick Jankowiak KD5OEI, "Cathode Ray Tube Phosphors Of Interest To The Experimenter"
- TubeTime, "CRT Phosphor Video" (2015)
- Blur Busters Forums, "Math Formulas for phosphor decay"
