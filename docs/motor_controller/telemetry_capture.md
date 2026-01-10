# Telemetry Capture

Captures high-rate telemetry from the LED display to LittleFS for offline analysis.

## Control Methods

### Python CLI (Recommended)

The `pov` CLI tool provides the easiest interface for capturing and downloading telemetry:

```bash
# From project root
uv sync                           # Install CLI (first time only)

pov telemetry status              # Check device state
pov telemetry start               # Start recording
# ... spin motor, collect data ...
pov telemetry stop                # Stop recording
pov telemetry dump                # Download CSVs to telemetry/
```

**All commands support `--json` for LLM-friendly structured output.**

### IR Remote

| Button | Action |
|--------|--------|
| RECORD | Start capture (deletes previous data) |
| STOP | Stop capture, print summary |
| DELETE | Delete all captured files |

Note: PLAY was removed from IR remote - use `pov telemetry dump` instead.

### Serial Commands

Line-based commands at 921600 baud (case-insensitive):

| Command | Response | Description |
|---------|----------|-------------|
| `STATUS` | `IDLE` / `RECORDING` / `FULL` | Current capture state |
| `START` | `OK` / `ERR: Already recording` | Start recording |
| `STOP` | `OK` / `ERR: Not recording` | Stop recording |
| `DELETE` | `OK` | Delete all files |
| `LIST` | Tab-separated file list | List files on device |
| `DUMP` | CSV data with `>>>` markers | Download all data |

**LIST output format:**
```
MSG_ACCEL_SAMPLES.bin<TAB>1234<TAB>5678
MSG_HALL_EVENT.bin<TAB>456<TAB>789
<blank line = end of list>
```

**DUMP output format:**
```
>>> MSG_ACCEL_SAMPLES.bin
timestamp_us,x,y,z
1234567890,0.12,-0.34,9.81
>>> MSG_HALL_EVENT.bin
timestamp_us,period_us
1234567890,25000
>>>
```

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

## Binary File Format

Files on LittleFS are self-describing binary format:

```
[1 byte: msgType] [records...]
```

The first byte is the message type (from `messages.h`), followed by packed binary records.

**File naming**: `/telemetry/MSG_ACCEL_SAMPLES.bin`, `/telemetry/MSG_HALL_EVENT.bin`, etc.

**Record structures** (from `telemetry_capture.cpp`):

| Message Type | Record Size | Fields |
|--------------|-------------|--------|
| MSG_ACCEL_SAMPLES (10) | 20 bytes | timestamp_us (u64), x/y/z (float each) |
| MSG_HALL_EVENT (11) | 12 bytes | timestamp_us (u64), period_us (u32) |
| MSG_TELEMETRY (1) | 20 bytes | timestamp_us (u64), hall_avg_us (u32), revolutions (u16), counters... |

## Files

**Firmware:**
- `src/telemetry_capture.{h,cpp}` - Capture implementation
- `src/serial_command.{h,cpp}` - Serial command parser

**Python CLI:**
- `tools/pov_tools/` - CLI package
- `tools/pov_tools/serial_comm.py` - Serial protocol
- `tools/pov_tools/telemetry.py` - Telemetry commands

**Data:**
- `/telemetry/*.bin` on LittleFS (device)
- `telemetry/*.csv` in project root (downloaded, gitignored)
