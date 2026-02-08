# Anti-Aliasing on Polar Raster Displays

## Reference for POV display rendering on ESP32-S3

---

## The fundamental problem

A POV display is a polar raster. Its two sampling dimensions — radial (ring) and angular (slot) — have wildly different resolutions:

- **Angular:** Hundreds of slots per revolution, giving sub-millimeter arc lengths (especially near center)
- **Radial:** Only 40 rings, fixed at ~2.3 mm spacing — coarse and unchangeable

This creates a fundamental **sampling asymmetry**. When Cartesian geometry is rendered onto this polar grid:

- Lines running roughly **radially** ("across the grain") look clean — the high angular resolution places lit points precisely along the line
- Lines running roughly **tangentially** ("along the grain") produce ugly staircases — multiple adjacent angular slots light the same ring, then abruptly jump to the next

The radial axis is always the bottleneck. At ring 20 (~46 mm radius) with 360 angular slots, angular arc is ~0.8 mm while radial spacing is 2.3 mm — a 2.9:1 anisotropy. At ring 5 (~11.5 mm), it's 0.2 mm vs 2.3 mm — 11.5:1. **All AA strategies should focus almost exclusively on smoothing across the radial axis.** The angular axis can be ignored, which halves the cost of every technique.

This exact problem has a direct analog in **radar PPI (Plan Position Indicator) displays**, which face the identical Cartesian-to-polar resampling issue. Radar scan converters have used zone-based filtering along the coarser axis since the 1960s.

From signal processing theory, the Nyquist spatial frequency of the radial axis is 1/(2 × 2.3 mm) ≈ 0.22 cycles/mm. Any Cartesian content with spatial frequency above this in the radial direction will alias.

---

## Techniques ranked by suitability

| Rank | Technique | Visual gain | Per-pixel cost | Best for |
|------|-----------|-------------|----------------|----------|
| 1 | SDF linear ramp | ★★★★★ | ~50–80 cycles | Geometric primitives (grids, lines, circles) |
| 2 | Precomputed LUT | ★★★★★ | ~5–10 cycles | Static or slowly-changing geometry |
| 3 | Wu-style radial blending | ★★★★ | ~30–50 cycles | Dynamic thin features (needles, cursors) |
| 4 | 1D radial supersampling | ★★★★ | ~100–200 cycles | Complex content that resists analytical SDF |
| 5 | Per-ring adaptive AA width | ★★★ | +10 cycles | Refinement of technique #1 |
| 6 | PIE SIMD batch blending | ★★ (perf only) | ~5 cycles amortized | Budget-constrained batch operations |
| 7 | MSAA / FXAA / SMAA / TAA | N/A | N/A | **Do not attempt** — wrong problem domain |

---

## Technique 1: SDF linear ramp

### Concept

The core insight from Signed Distance Field anti-aliasing is that **distance is opacity**. Instead of a binary decision ("is this pixel within X mm of a grid line?"), compute the exact signed distance to the nearest feature and convert it to a smooth blend factor through a linear ramp.

The standard GPU shader formulation is `smoothstep(-w/2, w/2, d)` where `d` is the signed distance and `w` is the AA transition width. However, analysis of SDF AA confirms that **the linear ramp `clamp(0.5 + d/w, 0, 1)` is visually indistinguishable from smoothstep when the transition spans only ~1 pixel width**. The Hermite smoothness of smoothstep only matters for multi-pixel blur widths where Mach bands become visible. On a 40-ring display, every transition is ≤1 ring wide — use the linear ramp.

### The key parameter: transition width `w`

In GPU shaders, the transition width is obtained via `fwidth(d)` (the screen-space derivative of the distance field). For the polar display, the dominant pixel dimension is always the **2.3 mm radial ring spacing**, so `w` is the ring spacing expressed in whatever coordinate system the SDF distance uses. Since this is constant across all rings, it becomes a compile-time constant.

A refined version can precompute per-ring transition widths that account for anisotropy:

```cpp
// Per-ring AA width accounting for anisotropy (precomputed once)
for (int r = 0; r < 40; r++) {
    float r_mm    = ringRadiusMM(r);
    float arc_mm  = r_mm * (2.0f * M_PI / num_slots);
    float px_size = fmaxf(2.3f, arc_mm);  // dominant pixel dimension
    aa_inv_w[r]   = (uint16_t)(256.0f / (px_size / GRID_SPACING_MM));
}
```

At outer rings where angular arc approaches radial spacing, this tightens the AA band for crisper rendering.

### Implementation: integer (Q8.8 fixed-point)

This replaces the binary `isNearGridLine()` with a smooth opacity function. Division is eliminated entirely — replaced by precomputed reciprocals.

```cpp
// ── Precomputed at startup ─────────────────────────────────
static int16_t sin_tab[360];   // Q8.8 sin(θ) for each degree
static int16_t cos_tab[360];   // Q8.8 cos(θ)
static uint16_t recip_grid;    // (1<<16) / gridSpacing_q88
static uint16_t inv_w_q88;     // 256 / aa_width

void init_tables() {
    for (int a = 0; a < 360; a++) {
        float rad = a * (M_PI / 180.0f);
        sin_tab[a] = (int16_t)(sinf(rad) * 256.0f);
        cos_tab[a] = (int16_t)(cosf(rad) * 256.0f);
    }
    recip_grid = (uint16_t)(65536.0f / GRID_SPACING_Q88);
    inv_w_q88  = (uint16_t)(256.0f / AA_WIDTH);
}

// ── Per-pixel: returns 0–255 opacity ───────────────────────
uint8_t gridOpacity(uint8_t ring, uint16_t angle_idx) {
    // Polar → Cartesian in Q8.8
    int16_t r_q88 = ((int16_t)ring << 8) | 0x80;
    int32_t x = ((int32_t)r_q88 * cos_tab[angle_idx]) >> 8;
    int32_t y = ((int32_t)r_q88 * sin_tab[angle_idx]) >> 8;

    // Distance to nearest vertical grid line (fmod via multiply-by-reciprocal)
    int32_t ax = (x < 0) ? -x : x;
    int32_t qx = (ax * recip_grid) >> 16;
    int16_t rx = ax - qx * GRID_SPACING_Q88;
    int16_t half = GRID_SPACING_Q88 >> 1;
    int16_t dx = (rx > half) ? (GRID_SPACING_Q88 - rx) : rx;

    // Same for horizontal grid line
    int32_t ay = (y < 0) ? -y : y;
    int32_t qy = (ay * recip_grid) >> 16;
    int16_t ry = ay - qy * GRID_SPACING_Q88;
    int16_t dy = (ry > half) ? (GRID_SPACING_Q88 - ry) : ry;

    // SDF: min distance to any grid line, minus line half-width
    int16_t dist = (dx < dy) ? dx : dy;
    int16_t sdf  = dist - LINE_HALFWIDTH_Q88;

    // Linear ramp: opacity = clamp(128 - (sdf * inv_w) >> 8, 0, 255)
    int32_t scaled = ((int32_t)sdf * inv_w_q88) >> 8;
    int16_t val    = 128 - (int16_t)scaled;
    if (val < 0)   return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

// ── Color application ──────────────────────────────────────
void renderPixel(CRGB16* pixel, CRGB16 bg, CRGB16 fg, uint8_t ring, uint16_t angle) {
    uint8_t alpha = gridOpacity(ring, angle);
    *pixel = blend16(bg, fg, (uint16_t)alpha << 8);  // 16-bit blend
}
```

**Cycle budget:** ~20 integer multiplies + ~15 adds/compares ≈ 50–60 cycles for the opacity computation. The `blend()` call adds ~12 cycles (three `scale8` operations). Total: **~70 cycles (~0.3 µs)** at 240 MHz.

### Implementation: float (simpler, comparable speed)

Because the ESP32-S3 FPU handles `fadd.s` and `fmul.s` in ~2 cycles each, float works too. The critical constraint: **avoid division and library trig at runtime**. All reciprocals and trig are precomputed.

```cpp
static float sin_f[360], cos_f[360];
static float recip_grid_f, inv_w_f;

uint8_t gridOpacityFloat(uint8_t ring, uint16_t angle) {
    float r = ring + 0.5f;
    float x = r * cos_f[angle];
    float y = r * sin_f[angle];

    // Fast fmod: y - grid * truncf(y * recip_grid)
    float ay = fabsf(y);
    float ry = ay - GRID_SPACING * truncf(ay * recip_grid_f);
    if (ry > GRID_SPACING * 0.5f) ry = GRID_SPACING - ry;

    float ax = fabsf(x);
    float rx = ax - GRID_SPACING * truncf(ax * recip_grid_f);
    if (rx > GRID_SPACING * 0.5f) rx = GRID_SPACING - rx;

    float dist = (rx < ry) ? rx : ry;
    float sdf  = dist - LINE_HALFWIDTH;
    float alpha = 0.5f - sdf * inv_w_f;
    if (alpha <= 0.0f) return 0;
    if (alpha >= 1.0f) return 255;
    return (uint8_t)(alpha * 255.0f);
}
```

~15 float operations × 2 cycles = ~30 FPU cycles, plus overhead ≈ **50–80 total cycles**. `truncf()` compiles to the `trunc.s` instruction (1 cycle).

### Further reading

- [Perfecting anti-aliasing on signed distance functions (blog.pkh.me)](https://blog.pkh.me/p/44-perfecting-anti-aliasing-on-signed-distance-functions.html) — comprehensive comparison of smoothstep, linear ramp, and Gaussian AA on SDFs with visual examples
- Inigo Quilez's SDF resources at iquilezles.org — canonical reference for SDF primitives and their compositions

---

## Technique 2: Precomputed lookup tables

### Concept

Since the grid geometry is static (or changes rarely), and the framebuffer is tiny (40 × 360 = 14,400 pixels), the entire anti-aliased grid can be precomputed once and stored as a lookup table. Runtime rendering becomes a single array read per pixel.

### Memory costs

| Format | Size |
|--------|------|
| `uint8_t` opacity table (40 × 360) | 14,400 bytes |
| Full CRGB precomputed frame (40 × 360 × 3) | 43,200 bytes |
| Palette-indexed (40 × 360 × 1) | 14,400 bytes |

All fit comfortably in ESP32-S3 DRAM (~350 KB available). Tables can also live in flash (compiled as `const`) and the 32 KB data cache handles sequential access well.

### Implementation

```cpp
static uint8_t DRAM_ATTR grid_alpha[40][360];  // 14,400 bytes

void precomputeGrid() {
    for (uint8_t r = 0; r < 40; r++)
        for (uint16_t a = 0; a < 360; a++)
            grid_alpha[r][a] = gridOpacity(r, a);  // SDF function from above
}

// Runtime: ~5 cycles per pixel
void renderFrame(uint16_t angle_slot) {
    for (uint8_t r = 0; r < 40; r++) {
        uint8_t alpha = grid_alpha[r][angle_slot];
        leds[r] = blend(CRGB::Black, CRGB::White, alpha);
    }
}
```

### Handling motion

- **Rotation:** Just shift the angle index — `grid_alpha[r][(a + delta) % 360]`. Zero-cost.
- **Translation:** Requires recomputation. At 14,400 pixels × ~70 cycles each ≈ 1 million cycles ≈ ~4.2 ms — fast enough for real-time recomputation at 30+ FPS even on a single core.
- **Can be generated offline** by a Python script and compiled into flash as `const` for truly static overlays.

---

## Technique 3: Wu-style radial blending

### Concept

Xiaolin Wu's weighted-pixel principle: when a feature falls between ring `i` and ring `i+1`, distribute its brightness to both rings proportional to the fractional radial position. This is the same approach Wu published for circles in *Graphics Gems II* (1991), splitting brightness between two concentric pixel rows based on the fractional part of the radius.

Wu blending and SDF AA are **complementary** strategies. SDF computes "how much of this feature covers this pixel" analytically. Wu distributes a point sample across its two nearest rings. **Use SDF for filled shapes and grid lines; use Wu for thin dynamic features like moving lines or cursors.**

### Implementation

```cpp
void wuRadialPlot(int16_t r_q8, uint16_t angle, CRGB color) {
    uint8_t ring_i   = r_q8 >> 8;           // integer ring index
    uint8_t frac     = r_q8 & 0xFF;         // fractional part, 0–255
    uint8_t inv_frac = 255 - frac;

    if (ring_i < 40)
        leds[ring_i]   += scale8(color, inv_frac);  // inner ring
    if (ring_i + 1 < 40)
        leds[ring_i+1] += scale8(color, frac);      // outer ring
}
```

The `+=` with FastLED's saturating `qadd8` per channel prevents overflow when multiple features contribute to the same pixel. This is **additive blending** — multiple lines can contribute to the same ring without special handling.

**Cost:** 6 `scale8` calls (2 per channel × 2 rings) ≈ 12–18 cycles, plus fractional computation ≈ **30–40 total cycles**.

### Further reading

- [Xiaolin Wu's line algorithm (Wikipedia)](https://en.wikipedia.org/wiki/Xiaolin_Wu%27s_line_algorithm)
- Wu, X. "An Efficient Antialiasing Technique" — SIGGRAPH 1991
- *Graphics Gems II* — Wu's anti-aliased circle drawing chapter

---

## Technique 4: Radial-only supersampling

### Concept

When content is too complex for analytical SDF computation (texture-mapped images, arbitrary Cartesian graphics), supersampling works — but only along the **coarse radial axis**. The angular axis is already well-sampled and gains nothing from oversampling.

**2× radial supersampling** evaluates the render function at two radial offsets within each ring's footprint and averages. This transforms a single sharp ring boundary into a graduated transition.

### Implementation

```cpp
// 2x radial supersample
CRGB renderSupersampled(uint8_t ring, uint16_t angle) {
    int16_t r_lo = ((int16_t)ring << 8) + 0x40;  // ring - 0.25
    int16_t r_hi = ((int16_t)ring << 8) + 0xC0;  // ring + 0.25
    CRGB s1 = renderCartesian(r_lo, angle);
    CRGB s2 = renderCartesian(r_hi, angle);
    return blend(s1, s2, 128);  // average
}
```

**Cost:** 2× the single-sample render cost. 3× radial supersampling (three samples with tent-filter weights [1,2,1]/4) is also feasible within typical budgets.

### Adaptive variant

Only supersample near feature boundaries; skip pixels deep inside or outside features:

```cpp
int16_t dist = quickDistToGrid(ring, angle);  // rough SDF
if (dist < RING_SPACING && dist > -RING_SPACING) {
    pixel = renderSupersampled(ring, angle);  // near boundary
} else {
    pixel = (dist < 0) ? gridColor : bgColor; // far from boundary
}
```

This reduces average cost to ~1.2× baseline, since most pixels are far from grid lines.

### When to use

Supersampling is the fallback for content that resists analytical treatment. If you can express the feature as an SDF, prefer technique #1. If the content comes from a texture or complex procedural generator that's hard to differentiate analytically, supersample it.

For **photographic or texture content**, a 1D Gaussian pre-filter along the radial direction (σ ≈ 1.0–1.5 ring spacings) applied to the source image before polar sampling eliminates radial aliasing at zero runtime cost — it's a preprocessing step.

---

## Techniques to avoid (and why)

**MSAA** — assumes a GPU triangle rasterization pipeline with hardware depth buffers and per-sample coverage masks. No triangles, no depth buffer, no GPU.

**FXAA** — a post-process filter that scans a complete 2D rectangular framebuffer for luminance contrast edges. The polar display renders a 40-pixel radial strip at each angular position; there's no 2D framebuffer to post-process. FXAA's 3×3+ neighborhood sampling is meaningless on a 40-pixel strip.

**SMAA** — multi-pass morphological pattern matching on rectangular pixel grids at 1080p+ resolution. The 40×360 polar raster is too small, too non-rectangular, and too MCU-constrained for multi-pass operations.

**TAA** — accumulates samples across frames using motion vectors and history buffers. Requires per-pixel motion tracking and frame history infrastructure that makes no sense for a POV display.

**MLAA** — classifies edge patterns assuming axis-aligned rectangular pixels. Polar wedge-shaped pixels produce no recognizable morphological patterns for MLAA's classifiers.

All five share a common disqualification: they were designed for **rectangular megapixel framebuffers rendered by massively parallel GPUs**. The polar display is 14,400 wedge-shaped pixels rendered by a dual-core 240 MHz MCU. The problem domain is fundamentally different.

---

## ESP32-S3 hardware characteristics for rendering

### FPU performance

| Operation | Cycles | Notes |
|-----------|--------|-------|
| `fadd.s`, `fmul.s` | ~2 | Genuinely fast. The core arithmetic primitive. |
| `madd.s` (fused multiply-add) | ~2 | Excellent for FIR-style computation chains |
| `trunc.s` | 1 | Compiles from `truncf()` — use freely |
| Float division | ~30–40 | Via `recip0.s` + Newton-Raphson. Without trick: ~278 cycles. |
| `sinf()` / `cosf()` (newlib) | ~3,600 | **~15 µs. Never call this per-pixel.** |
| `sqrtf()` (newlib) | ~1,960 | Also avoid in hot path. Use FastLED `sqrt16()` or precompute. |
| `fmodf()` (newlib) | ~50–100+ | Uses division internally. Replace with multiply-by-reciprocal. |

**Practical rule:** Float add/mul are essentially free. Float division is tolerable with the `recip0.s` trick. Library trig and sqrt must be eliminated from the per-pixel path — use precomputed LUTs or FastLED's integer approximations.

### FastLED lib8tion toolkit

Key primitives available for AA implementation:

| Function | What it does | Cost |
|----------|-------------|------|
| `sin8(θ)` / `cos8(θ)` | 8-bit LUT trig. Input 0–255 = full circle, output 0–255 (128 = zero) | ~5 cycles |
| `sin16(θ)` / `cos16(θ)` | 16-bit piecewise-linear trig. Input 0–65535, output ±32767 | ~15 cycles |
| `scale8(val, scale)` | `(val × scale) >> 8` — the core blending/dimming primitive | ~3–4 cycles |
| `blend(c1, c2, amt)` | CRGB blend with 0–255 factor. Uses `scale8` internally. | ~12 cycles |
| `sqrt16(x)` / `sqrt8(x)` | Integer square root | ~20–40 cycles |
| `lerp8by8(a, b, frac)` | 8-bit linear interpolation | ~5 cycles |
| `q44`, `q88`, `q124` | Fixed-point fractional types with operator overloads | varies |

### PIE SIMD potential

The ESP32-S3's PIE extensions provide 8 × 128-bit vector registers processing 16 × 8-bit values simultaneously. Relevant for **batch blending** — applying precomputed alpha values to CRGB color arrays. The `EE.VMUL.U8` instruction does 16 parallel 8-bit multiplies in one cycle. However: no compiler support (inline assembly required), no float support, and 16-byte alignment required. Only worth considering if profiling shows the blending phase is a bottleneck.

### Memory placement

| Location | Access speed | Use for |
|----------|-------------|---------|
| DRAM (`DRAM_ATTR`) | 1 cycle | Sin/cos tables, alpha LUTs, frame buffers |
| IRAM (`IRAM_ATTR`) | 1 cycle | Render functions (eliminates flash cache miss jitter) |
| Flash (cached) | ~1 cycle (hit) / 200–400 ns (miss) | `const` tables, sequential-access LUTs |
| PSRAM | ~30 MB/s bandwidth-limited | Large textures only. Never for hot-path data. |

Place the render function in IRAM for timing-deterministic POV rendering. Place critical LUTs in DRAM. The 32 KB data cache per core handles sequential flash access well — a 40-byte angular column spans just two cache lines.

### Profiling

Use `esp_cpu_get_cycle_count()` for per-pixel measurement (single-cycle resolution at 240 MHz). `esp_timer_get_time()` at 1 µs resolution is too coarse for per-pixel work. Pin the measurement task to a single core at high priority with FPU warmup cycles before measuring.

---

## Notes on the existing POV ecosystem

No existing open-source POV display project was found implementing anti-aliasing. All surveyed projects use nearest-neighbor or bilinear sampling with no AA. The polar sampling asymmetry problem appears to be universally ignored in the hobbyist POV community — likely because most projects display pre-rendered bitmap images (where the aliasing is baked in at content creation time) rather than rendering Cartesian geometry in real-time.
