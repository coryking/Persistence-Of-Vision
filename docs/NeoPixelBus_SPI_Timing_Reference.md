# NeoPixelBus SPI/DMA Timing Reference — ESP32-S3

## Quick Reference

All timing follows: **T = overhead + (per_led × n)** where n = number of LEDs.

| Method | Overhead (µs) | Per-LED (µs) | 30 LEDs | 100 LEDs | Show() Blocks? |
|--------|---------------|--------------|---------|----------|-----------------|
| DMA 40 MHz | 26 | 0.82 | 51 µs | 108 µs | No* |
| DMA 20 MHz | 27 | 1.63 | 76 µs | 190 µs | No* |
| DMA 10 MHz | 25 | 3.25 | 123 µs | 350 µs | No* |
| Sync 40 MHz | 16 | 1.01 | 46 µs | 117 µs | Yes |
| Sync 20 MHz | 17 | 1.83 | 72 µs | 201 µs | Yes |
| Sync 10 MHz | 21 | 3.46 | 125 µs | 367 µs | Yes |

*DMA Show() returns in ~26 µs. Wire transfer continues in background. Calling
Show() again before completion blocks until previous transfer finishes.

Per-LED wire time is determined by physics: **32 bits ÷ SPI clock = µs/LED**.
Sync adds ~0.2 µs/LED software overhead. DMA does not.

BGR vs LBGR feature: no timing difference. Buffer mode (copy vs swap): no timing difference.

---

## Findings

### The Two Timing Models

**Sync (Arduino SPI)** — `Show()` blocks the CPU for the entire transfer. The call
doesn't return until every byte is clocked out on the wire. Total time is:

    T_sync = 16 µs + (32/freq + 0.21) × n

The 16 µs is SPI bus setup (approximately constant across clock speeds, after
accounting for start/end frame wire time). The 0.21 µs/LED is Arduino SPI HAL
overhead — about 50 ns per byte, or ~12 CPU cycles at 240 MHz.

**DMA (ESP-IDF SPI)** — `Show()` hands the buffer to DMA hardware and returns
immediately. The CPU is free while the transfer runs in the background.

    T_show_return = ~26 µs          (constant, independent of n and SPI clock)
    T_wire        = (32/freq) × n   (runs in background via DMA)

If you call `Show()` again before the previous wire transfer completes, the call
blocks for the remaining wire time. This is measured as the "blocking" time in
the test data and equals the full wire transfer duration.

### What Controls Overhead (the 'a' term)

Sync overhead (~16 µs) comes from SPI peripheral initialization, start/end frame
serialization, and Arduino HAL transaction management. It varies slightly by clock
speed because the start frame (4 bytes) and end frame (4+ bytes) are included in
the transfer — adding ~1.6 µs at 40 MHz up to ~6.4 µs at 10 MHz to a ~14 µs
software base.

DMA overhead (~26 µs) comes from ESP-IDF SPI DMA descriptor setup, buffer
preparation, and DMA initiation. It is independent of SPI clock speed because the
function returns before any wire clocking begins. There is a negligible per-LED
component (~0.01 µs/LED, likely buffer copy/swap housekeeping) that adds only
~4 µs even at 400 LEDs.

### What Controls Per-LED Cost (the 'b' term)

The dominant factor is **physics**: each SK9822/APA102 LED requires 32 bits on
the wire. At a given SPI clock frequency, this takes exactly 32/freq microseconds.
Measured DMA wire times confirm this to 4 significant figures:

| Nominal | Theoretical µs/LED | Measured DMA µs/LED | Effective MHz |
|---------|-------------------|--------------------|--------------:|
| 10 MHz  | 3.200             | 3.255              | 9.83          |
| 20 MHz  | 1.600             | 1.633              | 19.60         |
| 40 MHz  | 0.800             | 0.820              | 39.03         |

The small excess (~2%) above theoretical is the APA102 end frame, which grows
at ~1 bit per 2 LEDs and is absorbed into the linear fit.

Sync per-LED cost adds ~0.2 µs on top of wire time. This is the Arduino SPI HAL's
per-transaction overhead — the driver processes bytes through a software layer
rather than bulk-DMA. At 40 MHz this overhead dominates: sync achieves only
~32 MHz effective throughput despite requesting 40 MHz.

### Effective Clock Rates

| Config | Nominal | Effective | Efficiency |
|--------|---------|-----------|-----------|
| DMA 40 MHz | 40.0 | 39.0 | 98% |
| DMA 20 MHz | 20.0 | 19.6 | 98% |
| DMA 10 MHz | 10.0 | 9.8 | 98% |
| Sync 40 MHz | 40.0 | 31.7 | 79% |
| Sync 20 MHz | 20.0 | 17.5 | 87% |
| Sync 10 MHz | 10.0 | 9.2 | 93% |

Sync efficiency improves at lower clock speeds because the constant ~0.2 µs/LED
software overhead becomes a smaller fraction of the total per-LED time.

### Things That Don't Matter

**BGR vs LBGR feature** — Identical timing in all configurations. Both transmit
4 bytes per LED. The luminance byte in LBGR is computed in software before the
SPI transaction begins, adding no measurable transfer time.

**Buffer mode (copy vs swap)** — `maintainBuffer=true` (copy) and
`maintainBuffer=false` (swap) produce identical timing. The copy/swap operation
is a pointer swap or fast memcpy that is negligible compared to SPI transfer.

### Measurement Stability

Timing measurements are highly deterministic. Standard deviations across 22
iterations are typically <1 µs:

- Sync Show(): σ < 0.5 µs (essentially jitter-free)
- DMA non-blocking Show(): σ < 0.5 µs  
- DMA blocking Show(): σ ≈ 1-3 µs (occasional DMA scheduling variance)

The R² values for all linear fits exceed 0.999 (DMA wire time) and 0.9999 (sync
total time), confirming the linear model is correct with no hidden nonlinearities.

---

## Test Methodology

### Hardware

- MCU: Seeed Studio XIAO ESP32-S3 (dual-core 240 MHz, 8 MB PSRAM)
- SPI bus: FSPI (SPI2) on IOMUX pins — Data: D10 (GPIO3), Clock: D8 (GPIO7)
- LED protocol: SK9822/APA102-compatible (32-bit SPI, MSB-first)
- No physical LEDs connected — measures SPI bus timing only

### Software

- Framework: Arduino + NeoPixelBus library
- Timer: `esp_timer_get_time()` (1 µs resolution hardware timer)
- Serial output: 921600 baud CSV

### Test Matrix

Each configuration tested with LED counts: 1, 10, 20, 40, 60, 80, 100, 150,
200, 300, 400.

- SPI clock: 10, 20, 40 MHz
- Method: Sync (Arduino SPI via `DotStarSpiXXMhzMethod`) and DMA (ESP-IDF SPI
  via `DotStarEsp32DmaSpiMethodBase`)
- Feature: BGR (`DotStarBgrFeature`) and LBGR (`DotStarLbgrFeature`)
- Buffer mode: `maintainBuffer=true` (copy) and `maintainBuffer=false` (swap)
- 25 iterations per config, first 3 discarded as warmup

### Three Show() Measurements Per Iteration

Each iteration captures three `Show()` calls with different preconditions:

1. **show1** — Normal call. No pending DMA from previous iteration (15 ms settle
   delay preceded it). For sync, this is a clean baseline. For DMA, this is
   also clean but may show first-call effects.

2. **show2** — Burst call. Immediately follows show1. For DMA, this call must
   wait for show1's DMA transfer to complete before starting its own, so it
   measures the full blocking time (DMA wait + new transfer initiation).
   For sync, identical to show1 since sync always blocks.

3. **show3** — Spaced call. Follows a 15 ms settle delay after show2. Any
   previous DMA has long since completed. This measures the pure non-blocking
   `Show()` overhead for DMA (buffer prep + DMA kickoff, no waiting).
   For sync, identical to show1/show2.

**Derived metric:** `wire_time = show2 - show3` isolates the actual SPI wire
transfer duration for DMA methods. For sync methods this is ~0 (noise) because
both show2 and show3 include the wire transfer time.

---

## Analysis

### Derivation of Timing Equations

Linear regression of `time = a + b × n` was performed on averaged data (buffer
modes pooled since they're identical) for each (spi_mhz, method) combination.

The model fits the data with R² > 0.999 across all configurations, confirming
that transfer time is purely linear in LED count with no quadratic or other terms.
Maximum residual across all fits is 8 µs (occurring at 40 MHz DMA with n=1, an
edge case where DMA setup dominates).

### Sync Overhead Decomposition

Sync total time can be decomposed as:

    T_sync = T_software_base + T_framing_wire + (T_wire_per_led + T_software_per_led) × n

Where:
- T_software_base ≈ 14 µs (SPI peripheral init, transaction setup)
- T_framing_wire = ~64 bits / freq (start + end frames)
- T_wire_per_led = 32 / freq µs (physics)
- T_software_per_led ≈ 0.21 µs (Arduino HAL per-byte overhead)

The combined intercept varies from ~16 µs (40 MHz) to ~21 µs (10 MHz) because
T_framing_wire scales with clock speed while T_software_base does not.

### DMA Overhead Decomposition

    T_show_return = T_dma_setup + T_buffer_prep
                  ≈ 26 µs (constant)

    T_wire_background = (32 / freq) × n

The DMA setup cost (~26 µs) includes ESP-IDF SPI driver overhead: DMA descriptor
allocation, bus arbitration, and transfer initiation. This is independent of both
LED count and SPI clock speed. The ~0.01 µs/LED scaling in Show() return time
reflects buffer housekeeping and is negligible for practical LED counts.

### Why 40 MHz Sync Only Achieves 32 MHz Effective

At 40 MHz, each LED's 32 bits take 0.80 µs on the wire but the Arduino SPI HAL
adds 0.21 µs of per-LED overhead, yielding 1.01 µs/LED total. The software
overhead is constant in time (not in clock cycles), so it represents a larger
fraction of the transfer at higher clock speeds. This effect makes sync at 40 MHz
less efficient (79%) than sync at 10 MHz (93%), even though the absolute
throughput is still higher.

DMA avoids this entirely by transferring the entire buffer in a single hardware
DMA transaction with no per-byte software intervention.

### Test Conditions and Limitations

- No physical LEDs were connected. The SPI bus was driven open or into a
  terminated load. LED capacitive loading on a real strip could affect signal
  integrity at 40 MHz but would not change SPI peripheral timing.
- ESP32-S3 was running single-threaded test code. In a dual-core application
  with WiFi/BLE on Core 0, DMA timing should be unaffected (DMA is hardware)
  but sync timing could be impacted by interrupts.
- NeoPixelBus version and ESP-IDF version were not recorded. SPI driver overhead
  could vary across versions.
- Measurements use `esp_timer_get_time()` with 1 µs resolution. Sub-microsecond
  effects are below measurement precision.
