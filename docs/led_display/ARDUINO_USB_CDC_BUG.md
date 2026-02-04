# Arduino USB CDC Multi-Core Bug

## Summary

Serial output from Core 0 is garbled/truncated when using Arduino-ESP32's HWCDC (Hardware USB CDC) implementation on ESP32-S3. Core 1 output works correctly.

**This is a known, unfixed bug in the Arduino-ESP32 framework.**

## Symptoms

- Serial output from tasks on Core 0 shows truncated messages (beginning cut off)
- Messages like `"[ESPNOW] No ACK from peer"` appear as `"om peer"` or `"K from peer"`
- Core 1 output (e.g., RenderTask logs) appears correctly
- When both cores log simultaneously, messages interleave (separate issue, "won't fix" per maintainers)

## Root Cause

The HWCDC ISR (interrupt service routine) is pinned to the core where `Serial.begin()` was called. In Arduino, `setup()` runs on Core 1, so the ISR is on Core 1.

When a task on Core 0 writes to Serial, there's a race condition between:
1. Core 0 writing to the ring buffer
2. Core 1's ISR reading from the ring buffer to send via USB

The `flushTXBuffer()` function in HWCDC.cpp discards old data when the buffer is full, and this races with the ISR. The ring buffer operations are individually thread-safe, but the overall logic flow isn't coordinated between cores.

**Key code in HWCDC.cpp (lines 217-227):**
```cpp
// When buffer full, DISCARDS old data without sending it
uint8_t *queued_buff = (uint8_t *)xRingbufferReceiveUpTo(tx_ring_buf, &queued_size, 0, to_flush);
if (queued_size && queued_buff != NULL) {
    vRingbufferReturnItem(tx_ring_buf, (void *)queued_buff);  // Data thrown away!
}
```

## Our Configuration

| Component | Core | Result |
|-----------|------|--------|
| Arduino setup()/loop() | Core 1 | CDC ISR pinned here |
| RenderTask | Core 1 | ✅ Logs work correctly |
| OutputTask | Core 0 | ❌ Logs garbled |
| ESP-NOW callbacks | Core 0 (WiFi stack) | ❌ Logs garbled |
| HallProcessingTask | Core 1 | ✅ Logs work correctly |

## ESP-IDF Comparison

ESP-IDF's USB console implementation (`esp-idf/components/esp_usb_cdc_rom_console/usb_console.c`) uses proper multi-core synchronization:

```c
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static inline void write_lock_acquire(void) {
    portENTER_CRITICAL_SAFE(&s_lock);  // Disables interrupts, uses spinlock
}
```

This properly coordinates writes across cores. **Moving to pure ESP-IDF would likely fix this issue.**

## Relevant GitHub Issues

- **[#11959](https://github.com/espressif/arduino-esp32/issues/11959)** - HWCDC Missing data on S3 (OPEN, Nov 2025)
  - Confirms bug exists in Arduino Core 3.0.x through 3.3.2+
  - Maintainer (SuGlider) identified ISR/core affinity as root cause
  - Proposed fix: add semaphores to synchronize ISR with cross-core writes
  - No fix merged yet

- **[#9378](https://github.com/espressif/arduino-esp32/issues/9378)** - HWCDC Missing data (partially addressed in 3.0.0)

- **[#9980](https://github.com/espressif/arduino-esp32/issues/9980)** - Multi-core ESP_LOG interleaving
  - Closed as "won't fix" - maintainers won't add mutex to logging for performance reasons

## Workarounds

### What Works

1. **Accept garbled Core 0 output** - Current approach. Core 1 logs are fine.

2. **Call `Serial.begin()` from Core 0 task** - Pins ISR to Core 0, but then Core 1 writes will be garbled instead.

3. **Queue all logs to Core 1** - Create a logging task on Core 1 that processes a queue. All ESP_LOG output goes through the queue.

4. **Use UART instead of USB CDC** - Hardware UART doesn't have this issue, but requires USB-UART adapter.

5. **Migrate to pure ESP-IDF** - ESP-IDF's implementation has proper synchronization.

### What Doesn't Work

- `Serial.flush()` after writes - Reduces throughput significantly, doesn't fully fix the issue
- Polling USB interrupts manually - The `esp_usb_console_poll_interrupts()` function is for the ROM-based driver, not HWCDC
- Lowering task priority - This is a synchronization bug, not a scheduling issue

## Future Considerations

If/when we migrate from Arduino to pure ESP-IDF:
- USB CDC output should work correctly from all cores
- ESP_LOG uses proper locking in ESP-IDF
- This would be one benefit among others (better control, smaller binary, etc.)

## References

- [ESP-IDF USB Serial/JTAG Console Docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html)
- [Arduino-ESP32 HWCDC.cpp source](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/HWCDC.cpp)
- [ESP-IDF usb_console.c source](https://github.com/espressif/esp-idf/blob/master/components/esp_usb_cdc_rom_console/usb_console.c)
