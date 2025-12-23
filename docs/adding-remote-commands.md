# Adding Remote Commands

Quick reference for adding new IR remote button commands to the POV system.

## Motor Controller Side

### 1. Define Message Type (shared/)
Edit `shared/messages.h`:
- Add `MSG_*` enum value in `MessageType`
- Add corresponding message struct with `__attribute__((packed))`

### 2. Add Command Enum
Edit `motor_controller/src/commands.h`:
- Add command to `Command` enum

### 3. Map Button to Command
Edit `motor_controller/src/remote_input.cpp`:
- Add case in switch statement mapping `SAGETV_BTN_*` to `Command::*`
- Button codes defined in `shared/sagetv_buttons.h`

### 4. Route Command to ESP-NOW
Edit `motor_controller/src/command_processor.cpp`:
- Add case calling `send*()` function

### 5. Declare Sender Function
Edit `motor_controller/src/espnow_comm.h`:
- Add function declaration: `void send*();`

### 6. Implement Sender Function
Edit `motor_controller/src/espnow_comm.cpp`:
- Create function that instantiates message struct and calls `esp_now_send()`
- Add debug Serial.println() for confirmation

## LED Display Side

### 1. Add Command Type
Edit `led_display/include/EffectManager.h`:
- Add command to `EffectCommandType` enum

### 2. Handle ESP-NOW Message
Edit `led_display/src/ESPNowComm.cpp`:
- Add case in `onDataRecv()` switch for `MSG_*`
- Create `EffectCommand` and queue it via `xQueueSend()`

### 3. Process Command
Edit `led_display/include/EffectManager.h`:
- Add case in `processCommands()` switch
- For global commands: call EffectManager method directly
- For effect commands: call `effects[currentIndex]->methodName()`

### 4. (If effect-specific) Add Virtual Method
Edit `led_display/include/Effect.h`:
- Add `virtual void methodName() {}` with empty default

### 5. (If effect-specific) Implement in Effects
Edit effect header/source files:
- Override method in effects that support this command
- Effects that don't care inherit the empty default

## File Summary

| Layer | Motor Controller | LED Display |
|-------|------------------|-------------|
| Message definition | `shared/messages.h` | (same file) |
| Command enum | `src/commands.h` | `include/EffectManager.h` |
| Button mapping | `src/remote_input.cpp` | N/A |
| Command routing | `src/command_processor.cpp` | `src/ESPNowComm.cpp` |
| Send/receive | `src/espnow_comm.cpp` | `src/ESPNowComm.cpp` |
| Effect interface | N/A | `include/Effect.h` |
