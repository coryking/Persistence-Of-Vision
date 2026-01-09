#pragma once
#include <cstddef>
#include "types.h"

enum class CaptureState : uint8_t {
    IDLE,       // Ready for RECORD
    RECORDING,  // Actively writing to files
    FULL,       // Filesystem limit reached
    STOPPED     // Capture complete, files closed
};

void captureInit();       // Call from setup() - mounts LittleFS
void captureStart();      // RECORD: delete all, start recording
void captureStop();       // STOP: close files, print summary
void capturePlay();       // PLAY: dump all files as CSV
void captureDelete();     // DELETE: delete all telemetry files

// Write telemetry data - handles lazy file creation
// msgType: the MessageType enum value (determines file)
// data: raw ESP-NOW message bytes (including type byte)
// len: number of bytes
// Note: For MSG_ACCEL_SAMPLES, unpacks batch and writes each sample
void captureWrite(uint8_t msgType, const uint8_t* data, size_t len);

CaptureState getCaptureState();
bool isCapturing();  // Convenience: returns true if RECORDING
