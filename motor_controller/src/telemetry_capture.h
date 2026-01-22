#pragma once
#include <cstddef>
#include "types.h"

enum class CaptureState : uint8_t {
    IDLE,       // Ready for RECORD (also used after STOP)
    RECORDING,  // Actively writing to files
    FULL        // Filesystem limit reached (still recording, just can't write more)
};

void captureInit();       // Call from setup() - mounts LittleFS
void captureStart();      // RECORD: delete all, start recording (prints OK/ERR)
void captureStop();       // STOP: close files, print summary (prints OK/ERR)
void capturePlay();       // PLAY: dump all files as CSV (interactive, for IR remote)
void captureDelete();     // DELETE: delete all telemetry files (prints OK)

// Serial command interface (script-friendly output)
void captureStatus();     // Print state: IDLE, RECORDING, or FULL
void captureList();       // Print files: filename<TAB>records<TAB>bytes per line
void captureDump();       // Dump all files with >>> markers, no prompts

// Serial command wrappers (print OK/ERR instead of verbose output)
void captureStartSerial();   // START command - prints OK or ERR:
void captureStopSerial();    // STOP command - prints OK or ERR:
void captureDeleteSerial();  // DELETE command - prints OK

// Write telemetry data - handles lazy file creation
// msgType: the MessageType enum value (determines file)
// data: raw ESP-NOW message bytes (including type byte)
// len: number of bytes
// Note: For MSG_ACCEL_SAMPLES, unpacks batch and writes each sample
void captureWrite(uint8_t msgType, const uint8_t* data, size_t len);

CaptureState getCaptureState();
bool isCapturing();       // Convenience: returns true if RECORDING
bool isDumpInProgress();  // True during DUMP command (suppresses debug output)
