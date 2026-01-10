# Telemetry Capture

Captures high-rate telemetry from the LED display to LittleFS for offline analysis.

## IR Remote Control

| Button | Action |
|--------|--------|
| RECORD | Start capture (deletes previous data) |
| STOP | Stop capture, print summary |
| PLAY | Dump captured data as CSV to serial |
| DELETE | Delete all captured files |

## Architecture

- Main loop owns state machine (IDLE, RECORDING, FULL)
- FreeRTOS task owns file write operations
- Queue carries both DATA and STOP messages
- Task wakes on RECORD, writes data, closes files on STOP, sleeps

## Data Flow

```
ESP-NOW callback → captureWrite() → queue → captureTask → LittleFS
```

## Concurrency Model

No locks needed - task is sole owner of file handles during recording. Main loop only touches files when task is asleep (after STOP acknowledged).

```
Main loop                         captureTask
─────────                         ───────────
captureStart()
  delete old files (safe)
  wake task ──────────────────→  wakes up
  state = RECORDING               writes DATA messages
                                  ...
captureStop()
  send STOP via queue ────────→  receives STOP
  wait for ack...                 closes files
                          ←────  sets s_taskStopped = true
  print summary                   sleeps (ulTaskNotifyTake)
  state = IDLE
```

## Queue Message Format

```cpp
struct CaptureMessage {
    CaptureMessageType type;  // DATA or STOP
    uint8_t msgType;          // For DATA: file identifier
    uint8_t len;
    uint8_t data[MAX_CAPTURE_PAYLOAD];
};
```

## State Machine

```
         captureStart()           captureStop()
  IDLE ─────────────────→ RECORDING ────────────→ IDLE
                              │
                              │ (filesystem full)
                              ↓
                            FULL ─── captureStop() ──→ IDLE
```

## Files

- `src/telemetry_capture.{h,cpp}` - Implementation
- Data stored in `/telemetry/*.bin` on LittleFS
