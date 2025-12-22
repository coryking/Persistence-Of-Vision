#include "FrameProfiler.h"
#include <Arduino.h>
#include "esp_timer.h"

#if ENABLE_TIMING_INSTRUMENTATION
namespace {
    bool s_headerPrinted = false;
}
#endif

namespace FrameProfiler {

void frameEnd(int64_t startHandle,
              uint32_t frameCount,
              uint8_t effectIndex,
              int slot,
              uint16_t angleUnits,
              uint64_t microsPerRev,
              uint16_t slotSize,
              uint32_t revCount,
              float angularRes,
              uint64_t lastInterval) {
#if ENABLE_TIMING_INSTRUMENTATION
    // Print CSV header on first call
    if (!s_headerPrinted) {
        Serial.println("frame,effect,total_us,slot,angle_units,usec_per_rev,"
                       "resolution_units,rev_count,angular_res,last_interval_us");
        s_headerPrinted = true;
    }

    int64_t totalTime = esp_timer_get_time() - startHandle;
    Serial.printf("%u,%u,%lld,%d,%u,%llu,%u,%u,%f,%llu\n",
                  frameCount,
                  effectIndex,
                  totalTime,
                  slot,
                  angleUnits,
                  microsPerRev,
                  slotSize,
                  revCount,
                  angularRes,
                  lastInterval);
#else
    // Suppress unused parameter warnings when disabled
    (void)startHandle;
    (void)frameCount;
    (void)effectIndex;
    (void)slot;
    (void)angleUnits;
    (void)microsPerRev;
    (void)slotSize;
    (void)revCount;
    (void)angularRes;
    (void)lastInterval;
#endif
}

} // namespace FrameProfiler
