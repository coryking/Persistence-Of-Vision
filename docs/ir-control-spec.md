# IR Control Plan

I would like to use an old sagetv remote (which is what i have on hand) to change the effects of the pov display.  it should map the buttons 1-9 (and 0) to the vairous effects.  I would like the IR remote to be on the motor controller not the display.  This is because the motor controller is not moving and I don't have to do any crazy mounting of the receiver.  It will communicate bidirectionanally with the display using ESP-Now.

When complete the display will periodically send several messages to the motor controller containing telemetry such as hall effect timing (timestamp, min, max, avg and number of rejected outliers) and what effect is being displayed and it's brightness.

The motor controller will command the display to show an effect and its brightness. (two messages).

Remember, this is an art project. You are my main coding dude responsible for keeping our house in order.  But that doesn't mean you are an architecture astronaut.  YAGNI and KISS are guiding principles.

To do this, I need to first boil the ocean.  Here is how to boil the ocean:

[TOC]

## Phase 0 - Update motor controller to use esp32-s3-zero

We need to update the motor controller to use my esp32-s3-zero.  Simply rip the platformio shit out of the ir_remote_test project as it is verified to work.

## Phase 0.5 - Get MAC addresses

We need to get the MAC addresses on both devices. Upload this simple sketch to each device:

```cpp
#include "WiFi.h"
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
}
void loop() {}
```

Note the MAC addresses and update the constants in the shared header (`shared/espnow_config.h`).

## Phase 1 - Establish ESP-Now Communication

The first phase of this operation is establishing bidirectional ESP-Now communication between the motor controller and the display.  

### Basic Hardware Config

Lets keep this super simple and just bake the mac addresses into the code.  Both can use channel 4 and since these literally sit about a meter apart when on the test bench and probalby less than 100mm apart when assembled we can use pretty low power, say WIFI_POWER_5dBm.

### Overall Message Format

Before we dive too far, we need to standardize our messages so they can be parsed by either end.  We'll use plain c-structs for each message.  The first field will define the message type.

**Shared Header Location:** Both projects need access to a common `messages.h` header file containing all message structures and configuration constants. Implementation agent should determine optimal directory structure (e.g., `POV_Project/shared/` or similar) and configure both PlatformIO projects to include this directory.

**Hardware Configuration Constants:**

```cpp
// MAC Addresses (see shared/espnow_config.h for authoritative values)
const uint8_t MOTOR_CONTROLLER_MAC[] = {0x34, 0xB7, 0xDA, 0x53, 0x00, 0xB4};  // ESP32-S3-Zero
const uint8_t DISPLAY_MAC[] = {0x30, 0x30, 0xF9, 0x33, 0xE4, 0x60};           // Seeed XIAO ESP32S3

// ESP-NOW Configuration
const uint8_t ESPNOW_CHANNEL = 4;
const wifi_power_t ESPNOW_POWER = WIFI_POWER_5dBm;  // Low power for <100mm separation
```

### First Message - Display to Motor Controller

The display sends telemetry every `ROLLING_AVERAGE_SIZE` revolutions (the same constant used for hall sensor smoothing - 20 revolutions). This keeps telemetry rate proportional to rotation speed. The message contains:

* the rolling average hall sensor value
* current timestamp
* number of revolutions since last message

Upon receiving this message, the motor controller should immediately write this to its serial console.

**Message Structure:**

```cpp
enum MessageType : uint8_t {
    MSG_TELEMETRY = 1,       // Display → Motor Controller
    MSG_BRIGHTNESS_UP = 2,   // Motor Controller → Display
    MSG_BRIGHTNESS_DOWN = 3, // Motor Controller → Display
    MSG_SET_EFFECT = 4,      // Motor Controller → Display
};

// Display → Motor Controller: Telemetry
struct TelemetryMsg {
    uint8_t type = MSG_TELEMETRY;
    uint32_t timestamp_ms;
    uint16_t hall_avg_us;          // Rolling average hall sensor period (microseconds)
    uint16_t revolutions;
} __attribute__((packed));
```

### Second Message - Motor Controller to Display

For the second message, let's focus on brightness control from the motor controller to the display. Once every 5 seconds the motor controller should send a **random brightness UP or DOWN message** to the display (we will later have this controlled by an IR remote in Phase 2).

The display should track brightness internally on a scale of **0 to 10** (0 being off, 10 being max brightness). Upon receiving `MSG_BRIGHTNESS_UP`, increment the internal brightness value (clamping at 10). Upon receiving `MSG_BRIGHTNESS_DOWN`, decrement (clamping at 0).

When rendering, map the 0-10 brightness to 0-255 for `nscale8()` using **perceptual (gamma-corrected) mapping**. Human vision perceives brightness logarithmically, so a linear 50% feels much brighter than halfway. Use gamma 2.2:

```cpp
// Perceptual brightness mapping (gamma 2.2)
// brightness: 0-10 input, returns 0-255 for nscale8()
uint8_t brightnessToScale(uint8_t brightness) {
    if (brightness == 0) return 0;
    if (brightness >= 10) return 255;
    // gamma 2.2: output = 255 * (input/10)^2.2
    float normalized = brightness / 10.0f;
    return (uint8_t)(255.0f * powf(normalized, 2.2f));
}
// Results: 0→0, 1→1, 2→5, 3→13, 4→25, 5→42, 6→65, 7→93, 8→128, 9→169, 10→255
```

**Confirmation:** Does the display brightness change (up or down) every 5 seconds?

**Message Structures:**

```cpp
// Motor Controller → Display: Increment brightness
struct BrightnessUpMsg {
    uint8_t type = MSG_BRIGHTNESS_UP;
} __attribute__((packed));

// Motor Controller → Display: Decrement brightness
struct BrightnessDownMsg {
    uint8_t type = MSG_BRIGHTNESS_DOWN;
} __attribute__((packed));
```

... once we get the basics set up and can get message from the display to the controller and back its time to do the fun stuff.... control effects and brightness using IR.

## Phase 2 - Control Effects

The second phase of this operation  assumes we have working bidirectional communication between the motor controller and display controller.  It assumes we have a standardized message format.  It also assumes that the IR hardware is wired into the motor controller and is ready to rock.  

### Configuration

* IR receiver code from `/Users/coryking/projects/POV_Project/test_projects/ir_remote_test`
* Same hardware/pins as ir_remote_test (GPIO2 for IR signal)
* SageTV remote button codes: `/Users/coryking/projects/POV_Project/docs/led_display/sagetv_remote_mapping.json`

### RC5 Toggle Bit

The SageTV remote uses RC5 protocol. RC5 has a **toggle bit** that alternates with each button press to distinguish "pressed again" from "still holding". This affects our codes:

- First press of VOL+: `0xF10`
- Second press of VOL+: `0x710`
- Third press of VOL+: `0xF10` (alternates)

The `0xF` vs `0x7` prefix is the toggle bit. **Mask it out** when comparing codes:

```cpp
// Strip RC5 toggle bit (bit 11) for comparison
uint16_t stripToggleBit(uint16_t code) {
    return code & 0x07FF;  // Keep lower 11 bits
}

// Example: Both 0xF10 and 0x710 become 0x310
// VOL+ base code: 0x310
// VOL- base code: 0x311
```

The button codes in `sagetv_remote_mapping.json` show the raw codes with toggle bit set. Use the masked values for switch statements.

### IR Button Code Reference

After masking the toggle bit (`code & 0x07FF`), these are the codes to match:

| Button | Raw (example) | Masked | Effect # |
|--------|---------------|--------|----------|
| 1 | `0x701` | `0x301` | 1 |
| 2 | `0xF02` | `0x302` | 2 |
| 3 | `0x703` | `0x303` | 3 |
| 4 | `0xF04` | `0x304` | 4 |
| 5 | `0x705` | `0x305` | 5 |
| 6 | `0xF06` | `0x306` | 6 |
| 7 | `0x707` | `0x307` | 7 |
| 8 | `0xF08` | `0x308` | 8 |
| 9 | `0x709` | `0x309` | 9 |
| 0 | `0xF00` | `0x300` | 10 |
| VOL+ | `0xF10` | `0x310` | Brightness ↑ |
| VOL- | `0x711` | `0x311` | Brightness ↓ |

### Display Prep Work

The display has two classes for effect management:

- **EffectRegistry** (`include/EffectRegistry.h`): Manages effect registration and switching. Stores up to 8 effects in an array, provides `setEffect(index)`, `next()`, `previous()`, and `current()` methods. This class is clean and stays as-is.

- **EffectScheduler** (`include/EffectScheduler.h`): Wraps EffectRegistry and adds "goofy shit" - auto-advancing effects on motor start, persisting to NVS on every change, cycling at boot. **DELETE THIS CLASS ENTIRELY.**

After gutting EffectScheduler:
- Call `EffectRegistry` directly from main.cpp
- Remove all NVS persistence code (no saving/loading effect index)
- Remove the `onMotorStart()` effect-cycling behavior
- Effect only changes when commanded via ESP-NOW

The render loop already calls `effectRegistry.current()->render(ctx)` - that pattern stays.

### Effect Control

**Everything is 1-based except the underlying array.**

The remote buttons map directly to effect numbers:

| Button | Effect Number | Array Index |
|--------|---------------|-------------|
| 1 | 1 | 0 |
| 2 | 2 | 1 |
| ... | ... | ... |
| 9 | 9 | 8 |
| 0 | 10 | 9 |

The motor controller transmits effect numbers 1-10. The display's `setEffect()` method accepts 1-based effect numbers and handles the array conversion internally:

```cpp
// EffectRegistry::setEffect takes 1-based effect number
void setEffect(uint8_t effectNumber) {
    if (effectNumber >= 1 && effectNumber <= effectCount) {
        currentIndex = effectNumber - 1;  // Convert to 0-based internally
        // ... call begin()/end() as needed
    }
    // Out of range → ignore silently
}

// Usage: setEffect(1) activates the first effect (array[0])
```

If the display receives an out-of-bounds effect number, it ignores the command silently. This keeps the motor controller dumb - it doesn't need to know how many effects exist.

**Message Structure** (see full enum in Phase 1):

```cpp
// Motor Controller → Display: Set effect by number
struct SetEffectMsg {
    uint8_t type = MSG_SET_EFFECT;
    uint8_t effect_number;  // 1-10 (1-based, matches remote buttons)
} __attribute__((packed));
```

### Brightness Control

I'd rather not have the motor controller care what the current brightness value is for now.  Therefore lets keep brightness control super simple and send a "brightness up" / "brightness down" message... upon being received the display can increment it's brightness on a scale of 0-10 as before in the first phase.  meaning the display controller should track brightness as 0-10 and only do the mapping upon render.  We will use the volume keys for this.  Vol + increments brigthness, Vol - decrements.  Each button press increments and decrements...  holding simply sends multiple messages.

Thats it!  If we do thisl, we'll have very nice control over our display far beyond what it currently does.

# Implementation Details

## No Backwards Compatibility Needed

We will *always* have to keep the firmwares in sync between the motor controller and display controller. Therefore do not burn needless tokens freting about versioning, backwards compatibility in structs, etc.... assume we update both firmwares at the same time.  If we completely change a struct, who gives a single flying fuck.  This isn't some public API here. This is a tightly coupled set of devices.

## Only Motor Controller and Display Controller

It will *always* be the case that a *single* motor controller talks to a *single* display controller and visa versa.  Do not waste precious tokens worrying about "what if we have a single motor controller with multiple display controllers" or noise like that. One motor, One Display Controller.... everything else is YAGNI.

## Shared Directory Structure

See **docs/PROJECT_STRUCTURE.md** for complete project layout.

The `shared/` directory contains headers for ESP-NOW communication:
- `messages.h` - MessageType enum and message structs
- `espnow_config.h` - MAC addresses, channel, TX power

Both platformio.ini files include `-I ../shared` in build_flags:
```cpp
#include "messages.h"
#include "espnow_config.h"
```

## Documentation

We need to update project documentation such that it includes an overview of this new fangled IR control thing.  It should orient LLM's and humans alike with filesystem locations and details that cannot easily be inferred from code.  A subagent should be tasked with identifying obsolete information that needs to be purged / changed or deduplicated.

## Message Structure

See message definitions in Phase 1 and Phase 2 sections above.

**Key Design Decisions:**

- **No version fields:** Both firmwares must be synchronized anyway (YAGNI principle)
- **No sequence numbers:** For this simple two-device system, dropped packets are acceptable. Display continues with last known state.
- **Type-first design:** First byte is always message type, enabling simple switch-based parsing.  Speaking of message numbers, don't worry about backwards compatibility. You can recycle message number id's all you want. Remember, the two systems are tightly coupled.
- **Brightness is stateful on display:** Motor controller sends increment/decrement commands; display maintains current brightness state (0-10 scale)

**Error Handling Philosophy:**

- Invalid effect index → Display ignores command silently
- Brightness at limits → Display clamps (no wraparound)
- Unknown message type → Ignore and log to serial
- Corrupted message (wrong length) → Ignore and log to serial

## ESP-NOW Implementation Notes

**Getting Device MAC Addresses:**

```cpp
#include "WiFi.h"

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
}
```

Run this on both devices to get their MAC addresses, then update the constants in the shared header.

**Basic ESP-NOW Setup:**

ESP-NOW uses the raw ESP-IDF API (`esp_now.h`). Setup is straightforward:

1. Initialize WiFi in STA mode
2. Initialize ESP-NOW
3. Register receive callback
4. Add peer device by MAC address
5. Send/receive using callbacks

**Example initialization:**

Note: Please be kind and put this setup code into its own function.  We can't keep spewing shit into setup() .... there should be a setupESPNow() that setup calls....  consider if this counts as "shared code" that can go in our shared code directory....

```cpp
#include <esp_now.h>
#include <WiFi.h>

void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    // Handle received message
    uint8_t msg_type = data[0];
    // ... parse based on type
}

void setup() {
    WiFi.mode(WIFI_STA);
    
    // Optional: Set channel and power
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    WiFi.setTxPower(ESPNOW_POWER);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    
    // Register receive callback
    esp_now_register_recv_cb(on_data_recv);
    
    // Add peer
    esp_now_peer_info_t peer;
    memcpy(peer.peer_addr, OTHER_DEVICE_MAC, 6);
    peer.channel = 0;  // Use current channel
    peer.encrypt = false;
    
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
}
```

**Sending messages:**

use the esp's timestamp not mills()

```cpp
TelemetryMsg msg;
msg.timestamp_ms = millis();
msg.hall_avg_us = 1234;
msg.revolutions = 5;

esp_now_send(MOTOR_CONTROLLER_MAC, (uint8_t*)&msg, sizeof(msg));
```

**Important notes:**

- Receive callback runs on WiFi task (Core 0 by default). Keep it fast - just copy data to a queue and process in your main task.
- You only receive messages from peers you've explicitly added via `esp_now_add_peer()`
- ESP-NOW v2.0 supports up to 1470 bytes per message on ESP32-S3
- Setting `peer.channel = 0` means "use current WiFi channel"
- Both devices must be on the same WiFi channel to communicate

## Communication Architecture

### Motor Controller (Simple)

Motor controller only receives telemetry from display → log it to serial. No state management needed.

### Display Controller (Needs Care)

ESP-NOW receive callback runs on WiFi task (Core 0). Main render loop runs on Core 1 with microsecond-precision timing. These cannot directly share variables safely.

**Solution: DisplayState Struct with Atomics**

```cpp
// Explicit single source of truth for display settings
// Lives in a header both ESP-NOW handler and main loop can see
struct DisplayState {
    std::atomic<uint8_t> brightness{5};       // 0-10 scale, default mid
    std::atomic<uint8_t> effectNumber{1};     // 1-based effect number (1-10)
};

extern DisplayState g_displayState;  // Global instance
```

**Why atomics for settings:**
- Brightness and effect number are single values
- Atomics are lock-free and fast for single-value reads/writes
- Main loop reads, ESP-NOW callback writes - no race conditions

**In ESP-NOW callback (Core 0):**
```cpp
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    uint8_t msgType = data[0];
    switch (msgType) {
        case MSG_BRIGHTNESS_UP:
            if (g_displayState.brightness < 10) g_displayState.brightness++;
            break;
        case MSG_BRIGHTNESS_DOWN:
            if (g_displayState.brightness > 0) g_displayState.brightness--;
            break;
        case MSG_SET_EFFECT: {
            uint8_t num = data[1];  // 1-based effect number from remote
            if (num >= 1 && num <= 10) {
                g_displayState.effectNumber = num;  // Store as 1-based
            }
            break;
        }
    }
}
```

**In main render loop (Core 1):**
```cpp
// Read current settings (atomic, no locking needed)
uint8_t brightness = g_displayState.brightness;
uint8_t effectNum = g_displayState.effectNumber;  // 1-based

// Apply effect change if needed
if (effectNum != lastEffectNum) {
    effectRegistry.setEffect(effectNum);  // setEffect takes 1-based number
    lastEffectNum = effectNum;
}

// Use brightness in render...
```

This keeps state management explicit and visible rather than buried in callbacks. The 1-based effect number flows through the entire system - only `EffectRegistry::setEffect()` converts to 0-based array index internally.

## Hardware Wiring

Here are the pin assignments on the esp32-s3-zero controller.  Please ensure this information is encapsulated in exactly one location (the hardware.h file for the motor controller) and all other documentation refers to it without duplication.

GPIO3 - Rotary CLK - Green
GPIO4 - Rotatry DT - Blue
GPIO5 - Rotatry SW - Yellow

GPIO7 - Motor IN1 - Blue
GPIO8 - Motor IN2 - Yellow
GPIO9 - Motor ENA - Green

GPIO2 - IR Signal - Orange

---

# Bootstrap Reference (for implementation agents)

This section provides implementation-specific context. See **docs/PROJECT_STRUCTURE.md** for complete project layout and file locations.

## Key Constants

| Constant | Location | Value | Notes |
|----------|----------|-------|-------|
| `ROLLING_AVERAGE_SIZE` | `led_display/include/types.h` | 20 | Telemetry interval |
| `MAX_EFFECTS` | `led_display/include/EffectRegistry.h` | 8 | Max registered effects |
| `GLOBAL_BRIGHTNESS` | `led_display/include/hardware_config.h` | 255 | Make runtime variable |

## Files to Modify

**Motor Controller:**
- `src/hardware_config.h` - Update pin definitions for ESP32-S3-Zero (use GPIO numbers from Hardware Wiring section)
- `src/main.cpp` - Add ESP-NOW init, IR receiver, message handling

**LED Display:**
- `include/EffectRegistry.h` - Modify `setEffect()` to accept 1-based effect numbers
- `include/SlotTiming.h` - Modify `copyPixelsToStrip()` to use runtime brightness from DisplayState
- `src/main.cpp` - Remove EffectScheduler usage, add ESP-NOW init, add DisplayState, integrate with render loop

## Files to Delete

- `led_display/include/EffectScheduler.h`
- `led_display/src/EffectScheduler.cpp` (if exists)
- Remove all `#include "EffectScheduler.h"` and `effectScheduler.*` references from main.cpp

## Display Architecture Context

**FreeRTOS Tasks:**
- Hall processing task (priority 3) - processes hall sensor events via `g_hallEventQueue`
- Main loop (priority ~1) - tight render loop on Core 1

**ESP-NOW callback** runs on WiFi task (Core 0) - use atomics for cross-core communication.

**Render loop pattern:**
```cpp
// In loop() - runs tight when rotating
Effect* effect = effectRegistry.current();
effect->render(ctx);
copyPixelsToStrip(ctx);  // Apply brightness here
strip.Show();
```

## Build Commands

See project-specific CLAUDE.md files for build instructions. General pattern:
```bash
cd <project_dir>
uv run pio run              # Build
uv run pio run -t upload    # Upload
```
