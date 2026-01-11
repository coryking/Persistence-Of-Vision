# Telemetry Commands

| File | Commands | Purpose |
|------|----------|---------|
| `status.py` | `status` | Get capture state and file inventory from device |
| `capture.py` | `start`, `stop` | Start/stop telemetry recording on device |
| `files.py` | `list`, `dump`, `delete`, `view` | Manage telemetry files on device |
| `rxstats.py` | `rxstats` | ESP-NOW receive statistics for debugging packet delivery |
| `analyze_cmd.py` | `analyze` | Run analysis on captured data and generate HTML report |

## Adding a New Command

Create a function with typer decorators in a new or existing file, then register it in `__init__.py` using `app.command()(your_function)`. Use `utils.py` for shared helpers like `get_connection()` and console output.
