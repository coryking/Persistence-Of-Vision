# Quick Reference: Float Precision Issues - What to Fix First

## The 288° Boundary Bug (Most Visible)

**Symptom:** Pattern flickers at 288° (every rotation, very noticeable)

**Root cause:** Double-precision math loses precision, causing pattern index to jump between 15 and 16

**Quick fix options (easiest to hardest):**

### Option A: Add Epsilon Buffer (5 minutes)
```cpp
// File: src/effects/SolidArms.cpp line 37
// BEFORE:
uint8_t pattern = static_cast<uint8_t>(normAngle / 18.0f);

// AFTER: Add 0.5° safety margin
uint8_t pattern = static_cast<uint8_t>((normAngle + 0.5f) / 18.0f);
```

**Why it works:** The extra 0.5° prevents sub-18.0 values from truncating incorrectly  
**Cost:** Shifts pattern boundaries by 0.5° (human eye won't notice)  
**Complexity:** Trivial (1 line change)

### Option B: Replace with Integer Math (1 hour)
```cpp
// File: src/main.cpp lines 264-266
// BEFORE:
double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;
double angleMiddle = fmod(static_cast<double>(elapsed) / microsecondsPerDegree, 360.0);

// AFTER:
// 720 angle units = 360° (0.5° precision)
uint16_t angleUnits = (elapsed * 720) / microsecondsPerRev;
if (angleUnits >= 720) angleUnits %= 720;  // Wrap at 360°

// Then in SolidArms: pattern = (angleUnits / 36) - since 36 units = 18°
```

**Why it works:** Integer division is exact, no accumulated error  
**Cost:** Need to change angle representation in RenderContext  
**Complexity:** Medium (affects multiple files)

---

## The Slot Gating Problem (Frame Skipping)

**Symptom:** Some frames don't render (display appears to jump/stutter)

**Root cause:** Float division for slot calculation rounds unpredictably near boundaries

**Quick fix:**
```cpp
// File: src/main.cpp lines 299-302
// BEFORE:
int currentSlot = static_cast<int>(angleMiddle / slotSize);

// AFTER: Add safety margin
float safeAngle = angleMiddle + 0.001f;  // Push slightly forward
int currentSlot = static_cast<int>(safeAngle / slotSize);
```

**Why it works:** Prevents rounding errors near slot boundaries  
**Cost:** Minimal (might skip one frame every ~2 seconds)  
**Complexity:** Trivial

---

## The Double-Precision Killer (Performance)

**Symptom:** Everything else works but performance is poor (low FPS)

**Root cause:** `double` operations are 40x slower than integer on ESP32-S3

**Quick fix (no visual change):**
```cpp
// File: src/main.cpp line 264
// BEFORE:
double microsecondsPerDegree = static_cast<double>(microsecondsPerRev) / 360.0;

// AFTER:
// Just use float instead of double (smaller ULP error is fine)
float microsecondsPerDegree = static_cast<float>(microsecondsPerRev) / 360.0f;
```

**Why it works:** Float has enough precision for this calculation, runs 8x faster  
**Cost:** Tiny precision loss (imperceptible)  
**Complexity:** Trivial (1 line)  
**Benefit:** Free up ~2µs per frame

---

## Symptom-to-Fix Matrix

| What You See | Most Likely Cause | Quick Fix |
|--------------|-------------------|-----------|
| Flicker at 288° | Pattern boundary precision | Add 0.5° epsilon (Option A) |
| Flicker at any 18° boundary | Same as above | Add epsilon or use integer math |
| Random frame skips | Slot gating float rounding | Add safeAngle margin |
| Sluggish performance | Double precision overhead | Use float, not double |
| All effects sluggish | Multiple float divisions | Reduce division chains |
| Blob animations drifting | Cumulative float error | Use integer phase |

---

## Validation Checklist

After applying fixes, verify:

```bash
# Run SolidArms effect for 10 minutes at 2800 RPM
# Check serial output for FLICKER messages (should be 0)
grep "FLICKER@288" /dev/cu.usbmodem2101

# Should see output like:
# BOUNDARY_STATS@10000: hits=[20,22,18] changes=[0,0,0]
#                                             ↑ Should be 0!

# Measure frame rate
# Should be consistent 40-50 FPS without drops
```

---

## Testing Integer Conversion (Before You Code)

To verify integer math will work, simulate it:

```python
# Test integer angle math
microsPerRev = 21428  # At 2800 RPM
elapsedTime = 0

for frame in range(100):
    # Current float way (WRONG)
    microsPerDeg_float = microsPerRev / 360.0
    angle_float = (elapsedTime / microsPerDeg_float) % 360.0
    pattern_float = int(angle_float / 18.0)
    
    # New integer way (RIGHT)
    angleUnits = (elapsedTime * 720) // microsPerRev
    angleUnits %= 720
    pattern_int = angleUnits // 36  # 36 units = 18°
    
    # Should match
    if pattern_float != pattern_int:
        print(f"MISMATCH at {frame}: float={pattern_float} int={pattern_int}")
        print(f"  angle_float={angle_float:.4f}")
        print(f"  angleUnits={angleUnits}")
    
    elapsedTime += 50000  # 50ms per frame at 2800 RPM
```

If this shows 0 mismatches, integer math is safe to use!

---

## Don't Fall Into These Traps

❌ **Don't:** Use `double` for anything timing-critical  
✓ **Do:** Use `float` or integer  

❌ **Don't:** Chain divisions (a / b / c)  
✓ **Do:** Combined division ((a * c) / b)

❌ **Don't:** Compare floats with `==` or `!=`  
✓ **Do:** Compare integers or use epsilon

❌ **Don't:** Use `fmod()` with floats for 360° wrapping  
✓ **Do:** Use modulo with integers (angle % 720)

❌ **Don't:** Divide every frame if you can divide once per revolution  
✓ **Do:** Pre-calculate in `onRevolution()` callback

---

## Files to Modify (Priority Order)

1. **src/main.cpp** - Lines 264-266 (double → float)
2. **src/effects/SolidArms.cpp** - Line 37 (add epsilon)
3. **src/main.cpp** - Lines 299-302 (slot gating epsilon)
4. **src/blob_types.h** - Lines 70-98 (if you want to optimize further)

---

## Estimated Time to Fix

- **Quick patches (epsilon buffers):** 15 minutes
- **Convert to float:** 20 minutes
- **Full integer conversion:** 2-3 hours (but much cleaner)

Recommendation: Start with Option A (epsilon), verify it works, then migrate to integer math at your own pace.

