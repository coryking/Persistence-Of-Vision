# Telemetry Capture

Captures high-rate telemetry from the LED display to dedicated flash partitions for offline analysis. Uses raw partition writes (bypassing filesystem overhead) to achieve the write speeds needed for 8kHz IMU data.

## Control Methods

### Python CLI (Recommended)

The `pov` CLI tool provides the easiest interface for capturing and downloading telemetry:

```bash
# From project root
uv sync                           # Install CLI (first time only)

# Automated per-step capture (recommended for analysis)
pov telemetry test                # Full test: ramp through all speeds, capture each
pov telemetry test -s 3 -r 5      # Settle 3s, record 5s per speed (defaults)

# Manual capture (for custom workflows)
pov telemetry status              # Check device state
pov telemetry start               # Start recording
# ... spin motor, collect data ...
pov telemetry stop                # Stop recording
pov telemetry dump                # Download CSVs to telemetry/
```

**All commands support `--json` for LLM-friendly structured output.**

The `test` command automates the full capture workflow:
1. Powers on motor, sets calibration effect
2. For each speed (1-10): settle → record → dump files with `_step_XX_NNNrpm` suffix
3. Powers off motor, writes `manifest.json` with metadata

Output files are named like `MSG_ACCEL_SAMPLES_step_01_245rpm.csv` for easy identification.

See `POV_TELEMETRY_ANALYSIS_GUIDE.md` for analysis methodology.

### IR Remote

| Button | Action |
|--------|--------|
| RECORD | Start capture (erases partitions first) |
| STOP | Stop capture, print summary |
| DELETE | Erase all telemetry partitions |

Note: PLAY was removed from IR remote - use `pov telemetry dump` instead.

### Serial Commands

Line-based commands at 921600 baud (case-insensitive):

| Command | Response | Description |
|---------|----------|-------------|
| `STATUS` | `IDLE` / `RECORDING` / `FULL` | Current capture state |
| `DELETE_ALL_CAPTURES` | `OK` | Erase all partitions (~5s, must call before START_CAPTURE) |
| `START_CAPTURE` | `OK` / `ERR: Already recording` | Start recording (instant, no erase) |
| `STOP_CAPTURE` | `OK` / `ERR: Not recording` | Stop recording, write header |
| `LIST_CAPTURES` | Tab-separated file list | List captured data |
| `DUMP_CAPTURES` | CSV data with `>>>` markers | Download all data |

**Important**: DELETE_ALL_CAPTURES must be called before START_CAPTURE. The header is written on STOP_CAPTURE, so if interrupted before stop, data is incomplete.

**LIST_CAPTURES output format:**
```
MSG_ACCEL_SAMPLES.bin<TAB>1234<TAB>5678
MSG_HALL_EVENT.bin<TAB>456<TAB>789
<blank line = end of list>
```

**DUMP_CAPTURES output format:**
```
>>> MSG_ACCEL_SAMPLES.bin
timestamp_us,sequence_num,x,y,z,gx,gy,gz
1234567890,1,12,-34,981,5,-10,32000
1234569140,2,11,-35,980,4,-11,32001
>>> MSG_HALL_EVENT.bin
timestamp_us,period_us,rotation_num
1234567890,25000,42
1234592890,25000,43
>>>
```

Note: `rotation_num` and `micros_since_hall` are computed in Python post-processing by correlating accel timestamps with hall events. The `pov telemetry dump` command automatically adds these columns to the CSV output.

## Architecture

- Main loop owns state machine (IDLE, RECORDING, FULL)
- FreeRTOS task owns flash write operations
- Queue carries both DATA and STOP messages
- Task wakes on RECORD, buffers data to 4KB sectors, writes to flash, closes on STOP, sleeps

## Data Flow

```
ESP-NOW callback → captureWrite() → queue → captureTask → flash partition
```

## Storage Design

Uses raw flash partitions instead of a filesystem for maximum write throughput:
- **LittleFS**: ~100-200 KB/s (journaling, wear leveling, metadata overhead)
- **Raw partition**: ~400-800 KB/s (direct esp_partition_write)

This matters because 8kHz IMU data at 16 bytes/sample = 128 KB/s sustained.

### Partition Layout

Defined in `partitions.csv`:

| Partition | Size | Record Size | Max Records | Duration |
|-----------|------|-------------|-------------|----------|
| accel | 1.875 MB | 16 bytes | ~122,000 | ~15 sec @ 8kHz |
| hall | 128 KB | 12 bytes | ~10,900 | ~218 sec @ 50/sec |
| stats | 128 KB | 52 bytes | ~2,500 | ~21 min @ 2/sec |

### Binary Format

Each partition starts with a 32-byte header:

```cpp
struct TelemetryHeader {
    uint32_t magic;           // 0x54454C4D "TELM"
    uint32_t version;         // 1
    uint64_t base_timestamp;  // First sample absolute time (microseconds)
    uint32_t start_sequence;  // First sample sequence number
    uint32_t sample_count;    // Total samples written
    uint32_t reserved[2];     // Padding for future use
};
```

**Record structures:**

| Partition | Record Size | Fields |
|-----------|-------------|--------|
| accel | 16 bytes | delta_us (u32), x/y/z (i16 each), gx/gy/gz (i16 each) |
| hall | 12 bytes | delta_us (u32), period_us (u32), rotation_num (u32) |
| stats | 52 bytes | Full RotorStatsRecord (see messages.h) |

Delta timestamps are offsets from the header's base_timestamp, converted to absolute timestamps during dump.

## Concurrency Model

No locks needed - task is sole owner of flash writes during recording. Main loop only touches partitions when task is asleep (after STOP acknowledged).

```
Main loop                         captureTask
─────────                         ───────────
captureStart()
  erase partitions (safe)
  wake task ──────────────────→  wakes up
  state = RECORDING               buffers DATA messages
                                  writes 4KB sectors to flash
                                  ...
captureStop()
  send STOP via queue ────────→  receives STOP
  wait for ack...                 flushes buffer
                                  writes headers
                          ←────  sets s_taskStopped = true
  print summary                   sleeps (ulTaskNotifyTake)
  state = IDLE
```

## Queue Message Format

```cpp
struct CaptureMessage {
    CaptureMessageType type;  // DATA or STOP
    uint8_t msgType;          // For DATA: partition identifier
    uint16_t len;
    uint8_t data[MAX_CAPTURE_PAYLOAD];
};
```

## State Machine

```
         captureStart()           captureStop()
  IDLE ─────────────────→ RECORDING ────────────→ IDLE
                              │
                              │ (partition full)
                              ↓
                            FULL ─── captureStop() ──→ IDLE
```

## Files

**Firmware:**
- `src/telemetry_capture.{h,cpp}` - Capture implementation
- `src/serial_command.{h,cpp}` - Serial command parser
- `partitions.csv` - Flash partition layout

**Python CLI:**
- `tools/pov_tools/` - CLI package
- `tools/pov_tools/serial_comm.py` - Serial protocol
- `tools/pov_tools/telemetry.py` - Telemetry commands

**Data:**
- Flash partitions on device (accel, hall, stats)
- `telemetry/*.csv` in project root (downloaded, gitignored)
