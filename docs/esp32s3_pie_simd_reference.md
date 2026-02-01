# ESP32-S3 PIE (Processor Instruction Extensions) SIMD Reference

---

## LLM Guidance: When and How to Use PIE SIMD

This section is written for LLMs helping developers optimize ESP32-S3 code. Use this to determine if PIE instructions are appropriate for a given problem.

### Decision Framework: Should I Recommend SIMD?

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. Is the data in INTERNAL RAM (IRAM/DRAM)?                     │
│    NO  → SIMD won't help. Memory bandwidth is the bottleneck.   │
│          PSRAM operations are ~30 MB/s regardless of method.    │
│    YES → Continue to step 2                                     │
├─────────────────────────────────────────────────────────────────┤
│ 2. Is this a hot path processing ≥64 bytes per call?            │
│    NO  → Setup overhead likely exceeds benefit. Use scalar.     │
│    YES → Continue to step 3                                     │
├─────────────────────────────────────────────────────────────────┤
│ 3. Is the operation parallelizable (same op on multiple data)?  │
│    NO  → SIMD cannot help. It requires data parallelism.        │
│    YES → Continue to step 4                                     │
├─────────────────────────────────────────────────────────────────┤
│ 4. Are the data types 8-bit, 16-bit, or 32-bit integers?        │
│    NO  → PIE has no floating-point support.                     │
│    YES → SIMD is likely beneficial. Proceed with implementation.│
└─────────────────────────────────────────────────────────────────┘
```

### Real-World Benchmark Data (from project-x51/esp32-s3-memorycopy)

**IRAM → IRAM (compute-bound — SIMD helps dramatically):**
| Method | Throughput | vs memcpy |
|--------|------------|-----------|
| 8-bit for loop | 45.78 MB/s | 0.13x |
| memcpy | 365.99 MB/s | 1.0x |
| PIE 128-bit (naive) | 1207.62 MB/s | 3.3x |
| PIE 128-bit (interleaved) | 1829.77 MB/s | **5.0x** |

**IRAM → PSRAM (memory-bound — SIMD doesn't help):**
| Method | Throughput | vs memcpy |
|--------|------------|-----------|
| memcpy | 32.52 MB/s | 1.0x |
| PIE 128-bit | 32.52 MB/s | 1.0x |

**Key insight:** When the bottleneck is PSRAM bandwidth (~30 MB/s), all methods perform identically. SIMD only helps when you're compute-bound on internal RAM.

### Minimum Viable Implementation Pattern

This is the simplest working pattern. Start here, get it correct, then optimize.

**Step 1: Create the assembly file (`simd_ops.S`)**

```asm
// simd_ops.S - Place in same directory as your .ino or in src/ for PlatformIO
#include "dsps_fft2r_platform.h"
#if (dsps_fft2r_sc16_aes3_enabled == 1)

    .text
    .align 4

// void simd_add_s16(int16_t *a, int16_t *b, int16_t *out, int count);
// Adds two arrays of 16-bit signed integers
// count must be multiple of 8, pointers must be 16-byte aligned
    .global simd_add_s16
    .type simd_add_s16,@function
simd_add_s16:
    entry   a1, 16              // Set up stack frame
    srli    a5, a5, 3           // count /= 8 (8 elements per 128-bit register)
    loopnez a5, .loop_end       // Zero-overhead loop
    ee.vld.128.ip  q0, a2, 16   // Load 8 values from a, a += 16
    ee.vld.128.ip  q1, a3, 16   // Load 8 values from b, b += 16
    ee.vadds.s16   q2, q0, q1   // q2 = saturate(q0 + q1)
    ee.vst.128.ip  q2, a4, 16   // Store 8 results to out, out += 16
.loop_end:
    movi.n  a2, 0               // Return 0
    retw.n                      // Return
    
#endif
```

**Step 2: Declare and call from C/C++**

```cpp
// Declare the external assembly function
extern "C" {
    void simd_add_s16(int16_t *a, int16_t *b, int16_t *out, int count);
}

// Data MUST be 16-byte aligned for PIE instructions
int16_t __attribute__((aligned(16))) buffer_a[256];
int16_t __attribute__((aligned(16))) buffer_b[256];
int16_t __attribute__((aligned(16))) buffer_out[256];

void process_data() {
    // count must be multiple of 8 for this implementation
    simd_add_s16(buffer_a, buffer_b, buffer_out, 256);
}
```

**Step 3: For PlatformIO, ensure the `.S` file is compiled**

PlatformIO should automatically pick up `.S` files in `src/`. If not, add to `platformio.ini`:
```ini
build_flags = -x assembler-with-cpp
```

### Inline Assembly Pattern (for simple operations in C)

When you need just a few SIMD instructions without a separate `.S` file:

```cpp
void simd_memcpy_128(void* dst, const void* src, size_t bytes) {
    // bytes must be multiple of 16, pointers must be 16-byte aligned
    size_t chunks = bytes >> 4;  // Divide by 16
    
    asm volatile (
        "loopnez %[n], 1f\n"            // Zero-overhead loop
        "ee.vld.128.ip q0, %[src], 16\n" // Load 16 bytes
        "ee.vst.128.ip q0, %[dst], 16\n" // Store 16 bytes
        "1:\n"
        : [dst] "+r" (dst), [src] "+r" (src)  // Outputs (modified)
        : [n] "r" (chunks)                     // Inputs
        : "memory"                             // Clobbers
    );
}
```

### Pipeline Optimization: When It Matters

**The issue:** PIE instructions have latency. If instruction N+1 depends on the result of instruction N, the CPU stalls.

**Naive (data dependencies every instruction):**
```asm
ee.vld.128.ip  q0, a2, 16    // Load
ee.vadds.s16   q1, q0, q2    // STALL: waiting for q0
ee.vst.128.ip  q1, a3, 16    // STALL: waiting for q1
```

**Interleaved (hide latency with independent work):**
```asm
ee.vld.128.ip  q0, a2, 16    // Load first batch
ee.vld.128.ip  q1, a2, 16    // Load second batch (independent)
ee.vadds.s16   q2, q0, q4    // Process first (q0 now ready)
ee.vadds.s16   q3, q1, q5    // Process second (q1 now ready)
ee.vst.128.ip  q2, a3, 16    // Store first
ee.vst.128.ip  q3, a3, 16    // Store second
```

**Practical guidance:**
- **Naive SIMD still wins:** Even without interleaving, you get 3x+ speedup over scalar
- **Interleaving adds ~50% more:** Going from naive to interleaved adds another 1.5x
- **When to bother:** Only optimize the hottest inner loops. Profile first.
- **How to verify:** Use `esp_timer_get_time()` before/after to measure microseconds

### Common Pitfalls

| Pitfall | Symptom | Solution |
|---------|---------|----------|
| Unaligned data | Crash or wrong results | Use `__attribute__((aligned(16)))` on arrays |
| PSRAM data | No speedup observed | Copy to IRAM first, or accept memory-bound limit |
| Count not multiple of vector size | Processes wrong amount | Handle remainder with scalar fallback |
| Missing `#include "dsps_fft2r_platform.h"` | Assembler errors | Include this header to get PIE macros |
| Forgetting `"memory"` clobber | Compiler reorders incorrectly | Always include in inline asm |
| Using LOOPNEZ with function calls | LOOPNEZ not used | GCC won't use zero-overhead loops if loop body has calls |

### What Operations Are Available?

Quick reference of what PIE can do:

| Category | Operations | Element Sizes |
|----------|------------|---------------|
| Arithmetic | Add, subtract, multiply, MAC | 8, 16, 32-bit |
| Comparison | Equal, less-than, greater-than | 8, 16, 32-bit |
| Min/Max | Element-wise min, max | 8, 16, 32-bit |
| Bitwise | AND, OR, XOR, NOT | Full 128-bit |
| Shift | Left, right (by SAR register) | 32-bit elements |
| Data movement | Load, store, broadcast, zip/unzip | Various |
| Special | FFT butterfly, ReLU, complex multiply | 16-bit |
| GPIO | Fast GPIO read/write | Single-cycle |

**Not available:** Floating-point, division, arbitrary shifts per element, scatter/gather

### When NOT to Use SIMD

- **Data in PSRAM:** Memory bandwidth is the limit, not compute
- **Small data (<64 bytes):** Setup overhead exceeds benefit  
- **Irregular access patterns:** SIMD needs contiguous, aligned data
- **Floating-point math:** PIE is integer-only
- **Code portability needed:** PIE is ESP32-S3 specific (won't work on ESP32, ESP32-C3, etc.)
- **One-off operations:** SIMD shines in loops, not single operations

---

## Technical Overview

The ESP32-S3 includes PIE (Processor Instruction Extensions), a SIMD extension to the Xtensa LX7 processor. These are custom Tensilica/Cadence instructions—not standard ARM NEON or x86 SSE.

**Key characteristics:**
- 128-bit vector registers (8 total)
- Operations on 8-bit, 16-bit, or 32-bit elements within vectors
- Single-cycle load/store for aligned 128-bit data
- Multiply-accumulate with extended precision accumulators
- FFT-specific instructions
- Fast GPIO interface

---

## Compiler Support Status

| Feature | GCC (Xtensa) | Status |
|---------|--------------|--------|
| Auto-vectorization | ❌ | Not supported |
| Intrinsics | ❌ | Not available |
| Inline assembly | ✅ | Required method |
| Zero-overhead loops (LOOPNEZ) | ⚠️ | Compiler uses conservatively; not with inline asm or function calls in loop body |

**Bottom line:** You must use inline assembly or `.S` assembly files. The compiler will never automatically generate PIE instructions.

---

## Register Architecture

### Vector Registers (QR)
| Register | Size | Description |
|----------|------|-------------|
| q0-q7 | 128 bits each | General-purpose vector registers |

Each QR can be viewed as:
- 16 × 8-bit elements
- 8 × 16-bit elements  
- 4 × 32-bit elements

### Accumulator Registers

| Register | Size | Purpose |
|----------|------|---------|
| QACC_H | 160 bits | High part of multiply-accumulate results |
| QACC_L | 160 bits | Low part of multiply-accumulate results |
| ACCX | 40 bits | Scalar accumulator for reduction operations |

**QACC structure:**
- For 8-bit operations: 16 × 20-bit accumulators
- For 16-bit operations: 8 × 40-bit accumulators

### Special Registers

| Register | Purpose |
|----------|---------|
| SAR | Shift Amount Register (5 bits for shift, 4 bits for unaligned access) |
| SAR_BYTE | Lower 4 bits of SAR for byte alignment in unaligned loads |
| UA_STATE | Stores state for unaligned access operations |

---

## Memory Alignment Requirements

- **Optimal performance:** 16-byte (128-bit) aligned addresses
- **Unaligned access:** Supported via `EE.LD.128.USAR.*` + `EE.SRC.Q*` instruction pairs, but slower
- **Address register update:** Many instructions auto-increment the address register by 16

---

## Instruction Categories

### 1. Read (Load) Instructions

| Instruction | Description | Address Update |
|-------------|-------------|----------------|
| `EE.VLD.128.XP qu, as, ad` | Load 128 bits to QR from [as], add ad to as | as += ad |
| `EE.VLD.128.IP qu, as, imm` | Load 128 bits to QR from [as], add imm to as | as += imm |
| `EE.VLD.H.64.XP qu, as, ad` | Load 64 bits to high half of QR | as += ad |
| `EE.VLD.L.64.XP qu, as, ad` | Load 64 bits to low half of QR | as += ad |
| `EE.VLD.H.64.IP qu, as, imm` | Load 64 bits to high half of QR | as += imm |
| `EE.VLD.L.64.IP qu, as, imm` | Load 64 bits to low half of QR | as += imm |
| `EE.VLDBC.8 qu, as` | Load 8 bits, broadcast to all 16 bytes of QR | none |
| `EE.VLDBC.16 qu, as` | Load 16 bits, broadcast to all 8 halfwords of QR | none |
| `EE.VLDBC.32 qu, as` | Load 32 bits, broadcast to all 4 words of QR | none |
| `EE.VLDBC.8.XP qu, as, ad` | Load+broadcast 8 bits | as += ad |
| `EE.VLDBC.16.XP qu, as, ad` | Load+broadcast 16 bits | as += ad |
| `EE.VLDBC.32.XP qu, as, ad` | Load+broadcast 32 bits | as += ad |
| `EE.VLDBC.8.IP qu, as, imm` | Load+broadcast 8 bits | as += imm |
| `EE.VLDBC.16.IP qu, as, imm` | Load+broadcast 16 bits | as += imm |
| `EE.VLDBC.32.IP qu, as, imm` | Load+broadcast 32 bits | as += imm |
| `EE.VLDHBC.16.INCP qv, qu, as` | Load 16 bits into two QRs with broadcast | as += 2 |
| `EE.LDF.64.XP fu, as, ad` | Load 64 bits to FP register | as += ad |
| `EE.LDF.128.XP fu, as, ad` | Load 128 bits to FP registers | as += ad |
| `EE.LDF.64.IP fu, as, imm` | Load 64 bits to FP register | as += imm |
| `EE.LDF.128.IP fu, as, imm` | Load 128 bits to FP registers | as += imm |
| `EE.LD.128.USAR.XP qu, as, ad` | Load 128 bits unaligned, set SAR_BYTE | as += ad |
| `EE.LD.128.USAR.IP qu, as, imm` | Load 128 bits unaligned, set SAR_BYTE | as += imm |
| `EE.LDQA.U8.128.XP as, ad` | Load 128 bits, zero-extend 8→20 bit, store to QACC | as += ad |
| `EE.LDQA.U8.128.IP as, imm` | Load 128 bits, zero-extend 8→20 bit, store to QACC | as += imm |
| `EE.LDQA.U16.128.XP as, ad` | Load 128 bits, zero-extend 16→40 bit, store to QACC | as += ad |
| `EE.LDQA.S8.128.XP as, ad` | Load 128 bits, sign-extend 8→20 bit, store to QACC | as += ad |
| `EE.LDQA.S16.128.XP as, ad` | Load 128 bits, sign-extend 16→40 bit, store to QACC | as += ad |
| `EE.LD.QACC_H.H.32.IP as, imm` | Load 32 bits to high part of QACC_H | as += imm |
| `EE.LD.QACC_H.L.128.IP as, imm` | Load 128 bits to low part of QACC_H | as += imm |
| `EE.LD.QACC_L.H.32.IP as, imm` | Load 32 bits to high part of QACC_L | as += imm |
| `EE.LD.QACC_L.L.128.IP as, imm` | Load 128 bits to low part of QACC_L | as += imm |
| `EE.LD.ACCX.IP as, imm` | Load 64 bits to ACCX | as += imm |
| `EE.LD.UA_STATE.IP as, imm` | Load 128 bits to UA_STATE | as += imm |
| `EE.LDXQ.32 qu, as, qv, sel` | Select 16-bit from qv, add to as, load 32 bits | computed |

### 2. Write (Store) Instructions

| Instruction | Description | Address Update |
|-------------|-------------|----------------|
| `EE.VST.128.XP qu, as, ad` | Store 128 bits from QR to [as] | as += ad |
| `EE.VST.128.IP qu, as, imm` | Store 128 bits from QR to [as] | as += imm |
| `EE.VST.H.64.XP qu, as, ad` | Store high 64 bits of QR | as += ad |
| `EE.VST.L.64.XP qu, as, ad` | Store low 64 bits of QR | as += ad |
| `EE.VST.H.64.IP qu, as, imm` | Store high 64 bits of QR | as += imm |
| `EE.VST.L.64.IP qu, as, imm` | Store low 64 bits of QR | as += imm |
| `EE.STF.64.XP fu, as, ad` | Store 64 bits from FP register | as += ad |
| `EE.STF.128.XP fu, as, ad` | Store 128 bits from FP registers | as += ad |
| `EE.STF.64.IP fu, as, imm` | Store 64 bits from FP register | as += imm |
| `EE.STF.128.IP fu, as, imm` | Store 128 bits from FP registers | as += imm |
| `EE.ST.QACC_H.H.32.IP as, imm` | Store 32 bits from high of QACC_H | as += imm |
| `EE.ST.QACC_H.L.128.IP as, imm` | Store 128 bits from low of QACC_H | as += imm |
| `EE.ST.QACC_L.H.32.IP as, imm` | Store 32 bits from high of QACC_L | as += imm |
| `EE.ST.QACC_L.L.128.IP as, imm` | Store 128 bits from low of QACC_L | as += imm |
| `EE.ST.ACCX.IP as, imm` | Store 64 bits from ACCX | as += imm |
| `EE.ST.UA_STATE.IP as, imm` | Store 128 bits from UA_STATE | as += imm |
| `EE.STXQ.32 qu, as, qv, sel` | Select 16-bit from qv, add to as, store 32 bits | computed |
| `ST.QR qu, as, imm` | Store QR to memory (basic store) | none |

### 3. Data Exchange Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.MOVI.32.A qu, as, sel` | Move 32-bit element from QR to AR (general-purpose register) |
| `EE.MOVI.32.Q qv, as, qu, sel` | Move 32 bits from AR to specified position in QR |
| `EE.VZIP.8 qv, qu0, qu1` | Interleave bytes from two QRs |
| `EE.VZIP.16 qv, qu0, qu1` | Interleave halfwords from two QRs |
| `EE.VZIP.32 qv, qu0, qu1` | Interleave words from two QRs |
| `EE.VUNZIP.8 qv, qu0, qu1` | De-interleave bytes to two QRs |
| `EE.VUNZIP.16 qv, qu0, qu1` | De-interleave halfwords to two QRs |
| `EE.VUNZIP.32 qv, qu0, qu1` | De-interleave words to two QRs |
| `EE.ZERO.Q qu` | Zero entire QR register |
| `EE.ZERO.QACC` | Zero both QACC_H and QACC_L |
| `EE.ZERO.ACCX` | Zero ACCX accumulator |
| `EE.MOV.S8.QACC qu` | Move QACC to QR with signed 8-bit saturation |
| `EE.MOV.S16.QACC qu` | Move QACC to QR with signed 16-bit saturation |
| `EE.MOV.U8.QACC qu` | Move QACC to QR with unsigned 8-bit saturation |
| `EE.MOV.U16.QACC qu` | Move QACC to QR with unsigned 16-bit saturation |

### 4. Arithmetic Instructions

#### Vector Addition/Subtraction (with saturation)

| Instruction | Element Size | Description |
|-------------|--------------|-------------|
| `EE.VADDS.S8 qv, qu0, qu1` | 8-bit signed | qv = saturate(qu0 + qu1) |
| `EE.VADDS.S16 qv, qu0, qu1` | 16-bit signed | qv = saturate(qu0 + qu1) |
| `EE.VADDS.S32 qv, qu0, qu1` | 32-bit signed | qv = saturate(qu0 + qu1) |
| `EE.VSUBS.S8 qv, qu0, qu1` | 8-bit signed | qv = saturate(qu0 - qu1) |
| `EE.VSUBS.S16 qv, qu0, qu1` | 16-bit signed | qv = saturate(qu0 - qu1) |
| `EE.VSUBS.S32 qv, qu0, qu1` | 32-bit signed | qv = saturate(qu0 - qu1) |

**Fused load variants** (add/sub + load in single instruction):
- `EE.VADDS.S8.LD.INCP qv, qu0, qu1, as, qd` — Add and load next 128 bits
- `EE.VADDS.S8.ST.INCP qv, qu0, qu1, as, qd` — Add and store previous result
- Same pattern for S16, S32 and VSUBS variants

#### Vector Multiplication

| Instruction | Element Size | Description |
|-------------|--------------|-------------|
| `EE.VMUL.U8 qv, qu0, qu1` | 8-bit unsigned | Element-wise multiply, low 8 bits of result |
| `EE.VMUL.U16 qv, qu0, qu1` | 16-bit unsigned | Element-wise multiply, low 16 bits of result |
| `EE.VMUL.S8 qv, qu0, qu1` | 8-bit signed | Element-wise multiply, low 8 bits of result |
| `EE.VMUL.S16 qv, qu0, qu1` | 16-bit signed | Element-wise multiply, low 16 bits of result |

**Fused load/store variants:**
- `EE.VMUL.*.LD.INCP` — Multiply + load next vector
- `EE.VMUL.*.ST.INCP` — Multiply + store previous result

#### Complex Multiplication

| Instruction | Description |
|-------------|-------------|
| `EE.CMUL.S16 qv, qu0, qu1, sel` | Complex multiply on 16-bit pairs, sel chooses real/imag part |
| `EE.CMUL.S16.LD.INCP qv, qu0, qu1, as, sel, qd` | Complex multiply + load |
| `EE.CMUL.S16.ST.INCP qv, qu0, qu1, as, sel, qd` | Complex multiply + store |

#### Multiply-Accumulate to ACCX

| Instruction | Description |
|-------------|-------------|
| `EE.VMULAS.U8.ACCX qv, qu` | Multiply and accumulate all products to ACCX (unsigned 8-bit) |
| `EE.VMULAS.U16.ACCX qv, qu` | Multiply and accumulate all products to ACCX (unsigned 16-bit) |
| `EE.VMULAS.S8.ACCX qv, qu` | Multiply and accumulate all products to ACCX (signed 8-bit) |
| `EE.VMULAS.S16.ACCX qv, qu` | Multiply and accumulate all products to ACCX (signed 16-bit) |

**With fused load:**
- `EE.VMULAS.*.ACCX.LD.IP` — MAC + load with immediate offset
- `EE.VMULAS.*.ACCX.LD.XP` — MAC + load with register offset
- `EE.VMULAS.*.ACCX.LD.IP.QUP` — MAC + load + QR update for unaligned

#### Multiply-Accumulate to QACC (vector accumulator)

| Instruction | Description |
|-------------|-------------|
| `EE.VMULAS.U8.QACC qv, qu` | Element-wise MAC to QACC (unsigned 8-bit) |
| `EE.VMULAS.U16.QACC qv, qu` | Element-wise MAC to QACC (unsigned 16-bit) |
| `EE.VMULAS.S8.QACC qv, qu` | Element-wise MAC to QACC (signed 8-bit) |
| `EE.VMULAS.S16.QACC qv, qu` | Element-wise MAC to QACC (signed 16-bit) |

**With fused load:**
- `EE.VMULAS.*.QACC.LD.IP` — MAC + load
- `EE.VMULAS.*.QACC.LD.XP` — MAC + load
- `EE.VMULAS.*.QACC.LDBC.INCP` — MAC + load broadcast

#### Scalar × Vector Multiply-Accumulate

| Instruction | Description |
|-------------|-------------|
| `EE.VSMULAS.S8.QACC qv, qu, sel` | Multiply vector by scalar (selected from qu), accumulate |
| `EE.VSMULAS.S16.QACC qv, qu, sel` | Multiply vector by scalar (selected from qu), accumulate |
| `EE.VSMULAS.S8.QACC.LD.INCP` | Same + load |
| `EE.VSMULAS.S16.QACC.LD.INCP` | Same + load |

#### Accumulator Operations

| Instruction | Description |
|-------------|-------------|
| `EE.SRCMB.S8.QACC qv, qu, sel` | Shift-right QACC, saturate to 8-bit, combine with selector |
| `EE.SRCMB.S16.QACC qv, qu, sel` | Shift-right QACC, saturate to 16-bit, combine with selector |
| `EE.SRS.ACCX ar, as, sel` | Shift-right ACCX and store to AR register |

#### ReLU (Rectified Linear Unit)

| Instruction | Description |
|-------------|-------------|
| `EE.VRELU.S8 qv, qu, ax` | ReLU: max(qu, 0) for 8-bit elements, ax is threshold |
| `EE.VRELU.S16 qv, qu, ax` | ReLU: max(qu, 0) for 16-bit elements |
| `EE.VPRELU.S8 qv, qu0, qu1, ax` | Parametric ReLU for 8-bit |
| `EE.VPRELU.S16 qv, qu0, qu1, ax` | Parametric ReLU for 16-bit |

#### Min/Max

| Instruction | Description |
|-------------|-------------|
| `EE.VMAX.S8 qv, qu0, qu1` | Element-wise max, signed 8-bit |
| `EE.VMAX.S16 qv, qu0, qu1` | Element-wise max, signed 16-bit |
| `EE.VMAX.S32 qv, qu0, qu1` | Element-wise max, signed 32-bit |
| `EE.VMIN.S8 qv, qu0, qu1` | Element-wise min, signed 8-bit |
| `EE.VMIN.S16 qv, qu0, qu1` | Element-wise min, signed 16-bit |
| `EE.VMIN.S32 qv, qu0, qu1` | Element-wise min, signed 32-bit |

**With fused load/store:**
- `EE.VMAX.*.LD.INCP`, `EE.VMAX.*.ST.INCP`
- `EE.VMIN.*.LD.INCP`, `EE.VMIN.*.ST.INCP`

### 5. Comparison Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.VCMP.EQ.S8 qv, qu0, qu1` | Compare equal, 8-bit elements (result: 0xFF if true, 0x00 if false) |
| `EE.VCMP.EQ.S16 qv, qu0, qu1` | Compare equal, 16-bit elements |
| `EE.VCMP.EQ.S32 qv, qu0, qu1` | Compare equal, 32-bit elements |
| `EE.VCMP.LT.S8 qv, qu0, qu1` | Compare less-than, 8-bit |
| `EE.VCMP.LT.S16 qv, qu0, qu1` | Compare less-than, 16-bit |
| `EE.VCMP.LT.S32 qv, qu0, qu1` | Compare less-than, 32-bit |
| `EE.VCMP.GT.S8 qv, qu0, qu1` | Compare greater-than, 8-bit |
| `EE.VCMP.GT.S16 qv, qu0, qu1` | Compare greater-than, 16-bit |
| `EE.VCMP.GT.S32 qv, qu0, qu1` | Compare greater-than, 32-bit |

### 6. Bitwise Logical Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.ORQ qv, qu0, qu1` | Bitwise OR |
| `EE.XORQ qv, qu0, qu1` | Bitwise XOR |
| `EE.ANDQ qv, qu0, qu1` | Bitwise AND |
| `EE.NOTQ qv, qu` | Bitwise NOT |

### 7. Shift Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.VSR.32 qv, qu` | Logical right shift by SAR[4:0] bits, 32-bit elements |
| `EE.VSL.32 qv, qu` | Logical left shift by SAR[4:0] bits, 32-bit elements |
| `EE.SRC.Q qv, qu0, qu1` | Concatenate qu0:qu1 and shift right by SAR_BYTE bytes |
| `EE.SRC.Q.QUP qv, qu0, qu1, qx, qy` | SRC.Q + update qx, qy for unaligned pipeline |
| `EE.SRC.Q.LD.XP qv, qu0, qu1, as, ad` | SRC.Q + load |
| `EE.SRC.Q.LD.IP qv, qu0, qu1, as, imm` | SRC.Q + load |
| `EE.SLCI.2Q qv0, qv1, qu0, qu1, imm` | Shift-left-combine-immediate on 2 QR pairs |
| `EE.SLCXXP.2Q qv0, qv1, qu0, qu1, as, ad` | Shift-left-combine with XP addressing |
| `EE.SRCI.2Q qv0, qv1, qu0, qu1, imm` | Shift-right-combine-immediate on 2 QR pairs |
| `EE.SRCXXP.2Q qv0, qv1, qu0, qu1, as, ad` | Shift-right-combine with XP addressing |
| `EE.SRCQ.128.ST.INCP qv, qu0, qu1, as` | SRC + store |

### 8. FFT Dedicated Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.FFT.R2BF.S16 qv0, qv1, qu0, qu1` | Radix-2 butterfly for 16-bit complex FFT |
| `EE.FFT.R2BF.S16.ST.INCP qv0, qv1, qu0, qu1, as` | Radix-2 butterfly + store |
| `EE.FFT.CMUL.S16.LD.XP qz, qv, qx, qy, as, ad, sel` | Complex multiply + load (for twiddle factors) |
| `EE.FFT.CMUL.S16.ST.XP qz, qv, as, ad, qx, qy, sel` | Complex multiply + store |
| `EE.FFT.AMS.S16.LD.INCP qz, qv, qx, qy, as, sel` | Add-multiply-subtract + load |
| `EE.FFT.AMS.S16.LD.INCP.UAUP qz, qv, qx, qy, as, sel` | AMS + load + unaligned update |
| `EE.FFT.AMS.S16.LD.R32.DECP qz, qv, qx, qy, as, sel` | AMS + load 32-bit + decrement pointer |
| `EE.FFT.AMS.S16.ST.INCP qz, qv, qx, qy, as, qw, sel` | AMS + store |
| `EE.FFT.VST.R32.DECP qv, as, sel` | Store 32 bits with pointer decrement (for FFT reordering) |
| `EE.BITREV qu, as` | Bit-reverse index calculation for FFT |

### 9. GPIO Control Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.WR_MASK_GPIO_OUT value, mask` | Write to GPIO_OUT with mask |
| `EE.SET_BIT_GPIO_OUT sel` | Set single bit in GPIO_OUT |
| `EE.CLR_BIT_GPIO_OUT sel` | Clear single bit in GPIO_OUT |
| `EE.GET_GPIO_IN ar` | Read GPIO_IN to AR register |

**Note:** These provide single-cycle GPIO access, faster than memory-mapped GPIO.

### 10. Processor Control Instructions

| Instruction | Description |
|-------------|-------------|
| `EE.WUR.<regname> ar` | Write User Register |
| `EE.RUR.<regname> ar` | Read User Register |

---

## Pipeline and Data Hazards

The ESP32-S3 uses a 5-stage pipeline: I (fetch), R (decode), E (execute), M (memory), W (writeback).

### Key Latencies

Instructions may have latency > 1 cycle. When the next instruction depends on the result, the pipeline stalls.

**General rules from TRM Section 1.7:**
- Most PIE instructions have detailed timing in TRM tables
- Avoid data dependencies between consecutive instructions
- Interleave independent operations to hide latency

### Pipeline Optimization Pattern

**Bad:** Sequential dependencies
```c
// Each instruction waits for the previous
q0 = load(addr);
q1 = add(q0, q2);
store(q1);
```

**Better:** Interleaved operations
```c
// Overlap load latency with independent work
q0 = load(addr1);
q1 = load(addr2);   // Independent, runs during q0 load
q2 = add(q0, q3);   // q0 now ready
q4 = add(q1, q5);   // q1 now ready
```

---

## Zero-Overhead Loops

The Xtensa `LOOPNEZ` instruction provides hardware loop support that eliminates branch overhead.

**GCC behavior:**
- Compiler uses LOOPNEZ conservatively
- Will NOT use with inline assembly in loop body
- Will NOT use with function calls in loop body

**Implication:** If you need SIMD in a tight loop, you must also implement the loop in assembly.

```asm
// Zero-overhead loop example
loopnez a3, loop_end    // a3 = iteration count
// loop body here
loop_end:
```

---

## Inline Assembly Syntax

### Basic Pattern

```c
void simd_example(uint8_t* dst, uint8_t* src, int len) {
    asm volatile (
        "EE.VLD.128.IP q0, %[src], 16\n"    // Load 16 bytes
        "EE.VST.128.IP q0, %[dst], 16\n"    // Store 16 bytes
        : [dst] "+r" (dst), [src] "+r" (src)  // Output operands (modified)
        :                                      // Input operands
        : "memory"                             // Clobbers
    );
}
```

### Using QR Registers

QR registers are typically referenced as `q0` through `q7` in assembly. They are not part of GCC's register allocator, so they can be used freely but must be saved/restored if calling other code.

### Full Loop Example

```c
void memcpy_simd(uint8_t* dst, const uint8_t* src, size_t len) {
    size_t chunks = len / 16;
    
    asm volatile (
        "loopnez %[chunks], 1f\n"           // Zero-overhead loop
        "EE.VLD.128.IP q0, %[src], 16\n"    // Load
        "EE.VST.128.IP q0, %[dst], 16\n"    // Store
        "1:\n"                               // Loop end label
        : [dst] "+r" (dst), [src] "+r" (src)
        : [chunks] "r" (chunks)
        : "memory"
    );
}
```

### Two-Stage Pipeline for Throughput

```c
// Interleaved loads and stores for better throughput
asm volatile (
    "EE.VLD.128.IP q0, %[src], 16\n"    // Load first
    "loopnez %[n], 1f\n"
    "EE.VLD.128.IP q1, %[src], 16\n"    // Load next
    "EE.VST.128.IP q0, %[dst], 16\n"    // Store previous
    "EE.VLD.128.IP q0, %[src], 16\n"    // Load next
    "EE.VST.128.IP q1, %[dst], 16\n"    // Store previous
    "1:\n"
    "EE.VST.128.IP q0, %[dst], 16\n"    // Store final
    : [dst] "+r" (dst), [src] "+r" (src)
    : [n] "r" (n)
    : "memory"
);
```

---

## Clobber Lists

When using inline assembly with PIE, declare what you modify:

| Clobber | When to use |
|---------|-------------|
| `"memory"` | Always if accessing memory |
| `"q0"`-`"q7"` | If modifying specific QR registers (if your toolchain supports it) |

**Note:** GCC for Xtensa may not recognize QR registers in clobber lists. Common practice is to use all QR registers as scratch and save/restore around function boundaries if needed.

---

## Key Resources (Validated)

### Primary Documentation
1. **ESP32-S3 Technical Reference Manual** — Section 1 (PIE)
   - Section 1.3: Structure Overview (registers)
   - Section 1.6: Extended Instruction List
   - Section 1.7: Instruction Performance (cycle-accurate latency tables)
   - Section 1.8: Extended Instruction Functional Description
   - URL: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf

### Working Code Examples
2. **Larry Bank's Minimal SIMD Example** — The canonical "hello world" for ESP32-S3 SIMD
   - Blog post: https://bitbanksoftware.blogspot.com/2024/01/esp32-s3-simd-minimal-example.html
   - Shows `.S` file structure, C declarations, and alignment requirements

3. **project-x51/esp32-s3-memorycopy** — Benchmark suite with real performance numbers
   - Repository: https://github.com/project-x51/esp32-s3-memorycopy
   - Results spreadsheet: https://docs.google.com/spreadsheets/d/1A9UKdOb0QqLGQVSIru1gydPLEyCpcejhV0q_KI-OJMs
   - Demonstrates IRAM vs PSRAM performance differences

4. **vroland/epdiy** — Production SIMD usage in e-paper driver
   - Repository: https://github.com/vroland/epdiy
   - Discussion with optimization techniques: https://github.com/vroland/epdiy/discussions/289

5. **bitbank2/JPEGDEC** — JPEG decoder with ESP32-S3 SIMD color conversion
   - Repository: https://github.com/bitbank2/JPEGDEC

### Community Knowledge
6. **ESP32 Forum Threads**
   - PIE documentation request and release: https://esp32.com/viewtopic.php?t=23062
   - DSP documentation: https://esp32.com/viewtopic.php?t=26193

7. **Espressif Developer Portal** — Official PIE introduction (covers both S3 and P4)
   - https://developer.espressif.com/blog/2024/12/pie-introduction/

### Base ISA Reference
8. **Xtensa Instruction Set Architecture Summary** — For `LOOPNEZ` and other base instructions
   - Cadence documentation (page 477 for zero-overhead loops)

---

## Quick Reference Card for LLMs

```
┌────────────────────────────────────────────────────────────────────┐
│                    ESP32-S3 PIE SIMD QUICK FACTS                   │
├────────────────────────────────────────────────────────────────────┤
│ Registers:        8 × 128-bit vector (q0-q7)                       │
│ Element sizes:    8, 16, or 32-bit integers (NO floating point)    │
│ Alignment:        16-byte required for full performance            │
│ Compiler support: NONE — must use inline asm or .S files           │
│ Instruction prefix: EE.  (e.g., EE.VLD.128.IP, EE.VADDS.S16)       │
├────────────────────────────────────────────────────────────────────┤
│ SPEEDUP EXPECTATIONS (IRAM data only):                             │
│   Naive SIMD vs memcpy:       ~3x                                  │
│   Optimized SIMD vs memcpy:   ~5x                                  │
│   SIMD on PSRAM data:         NO BENEFIT (memory-bound)            │
├────────────────────────────────────────────────────────────────────┤
│ MINIMUM VIABLE PATTERN:                                            │
│   1. Create .S file with #include "dsps_fft2r_platform.h"          │
│   2. Use entry/retw.n for function prologue/epilogue               │
│   3. Args arrive in a2, a3, a4, a5 (Xtensa windowed ABI)           │
│   4. Use loopnez for zero-overhead loops                           │
│   5. Declare extern "C" in C++ to call assembly functions          │
│   6. Use __attribute__((aligned(16))) on all data arrays           │
├────────────────────────────────────────────────────────────────────┤
│ PORTABILITY: ESP32-S3 ONLY                                         │
│   Does NOT work on: ESP32, ESP32-S2, ESP32-C3, ESP32-C6            │
│   Different syntax on: ESP32-P4 (uses esp. prefix, not ee.)        │
└────────────────────────────────────────────────────────────────────┘
```
