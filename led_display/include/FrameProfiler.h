#pragma once

#include <cstdint>
#include "esp_timer.h"

/**
 * Frame timing profiler - outputs CSV data when ENABLE_TIMING_INSTRUMENTATION is defined.
 *
 * When disabled, all functions become no-ops and the compiler eliminates dead code.
 * Usage pattern (like logging frameworks):
 *
 *   auto handle = FrameProfiler::frameStart();
 *   // ... render frame ...
 *   FrameProfiler::frameEnd(handle, frameCount, effectIndex, ...);
 */
namespace FrameProfiler {

/**
 * Compile-time check if profiling is active
 */
constexpr bool isActive() {
#if ENABLE_TIMING_INSTRUMENTATION
    return true;
#else
    return false;
#endif
}

/**
 * Start frame timing measurement.
 * @return Opaque handle (0 if profiling disabled)
 */
inline int64_t frameStart() {
#if ENABLE_TIMING_INSTRUMENTATION
    return esp_timer_get_time();
#else
    return 0;
#endif
}

/**
 * End frame timing and emit CSV data.
 * On first call, prints CSV header. No-op if profiling disabled.
 *
 * @param startHandle Handle from frameStart()
 * @param frameCount Current frame number
 * @param effectIndex Current effect index
 * @param slot Slot number rendered
 * @param angleUnits Angular position in units
 * @param microsPerRev Microseconds per revolution
 * @param slotSize Slot size in angular units
 * @param revCount Revolution count
 * @param angularRes Angular resolution
 * @param lastInterval Last actual interval in microseconds
 */
void frameEnd(int64_t startHandle,
              uint32_t frameCount,
              uint8_t effectIndex,
              int slot,
              uint16_t angleUnits,
              uint64_t microsPerRev,
              uint16_t slotSize,
              uint32_t revCount,
              float angularRes,
              uint64_t lastInterval);

} // namespace FrameProfiler
