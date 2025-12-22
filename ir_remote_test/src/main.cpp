/**
 * IR Remote Button Mapper
 *
 * Interactive utility to map IR remote buttons to their codes.
 * Prompts for each button on the remote, waits for press, records the IR code.
 * Outputs a CSV mapping at the end.
 *
 * Hardware:
 * - Board: ESP32-S3-Zero (programmed as esp32-s3-devkitc-1)
 * - IR Receiver: VS1838B (HX1838) module
 * - Wiring:
 *   - Yellow wire -> GND
 *   - Green wire  -> 3.3V
 *   - Orange wire -> GPIO2 (signal)
 *
 * This code uses the ESP32-S3's RMT (Remote Control) peripheral for efficient IR reception
 * with minimal CPU overhead.
 */

#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <vector>

// Pin Definitions
#define IR_RECV_PIN 2  // GPIO2 - Orange wire from VS1838B signal pin

// Button Definition Structure
struct Button {
    const char* label;
    const char* note;    // Optional note (can be nullptr)
    const char* color;   // Optional color/symbol (can be nullptr)
};

// Button Mapping Result
struct ButtonMapping {
    const char* label;
    uint64_t code;
    decode_type_t protocol;
    bool mapped;  // false if skipped
};

// SageTV Remote Button List (from buttons.json)
const Button BUTTONS[] = {
    {"POWER", nullptr, "red"},
    {"TV", nullptr, nullptr},
    {"Guide", nullptr, nullptr},
    {"Search", nullptr, nullptr},
    {"Home", nullptr, nullptr},
    {"Music", nullptr, nullptr},
    {"Photos", nullptr, nullptr},
    {"Videos", nullptr, nullptr},
    {"Online", nullptr, nullptr},
    {"1", nullptr, nullptr},
    {"2", "ABC", nullptr},
    {"3", "DEF", nullptr},
    {"4", "GHI", nullptr},
    {"5", "JKL", nullptr},
    {"6", "MNO", nullptr},
    {"7", "PQRS", nullptr},
    {"8", "TUV", nullptr},
    {"9", "WXYZ", nullptr},
    {"0", nullptr, nullptr},
    {"-", "dash/hyphen", nullptr},
    {"ABC 123", "text input toggle", nullptr},
    {"PREV CH", nullptr, nullptr},
    {"AUDIO", nullptr, nullptr},
    {"MUTE", nullptr, nullptr},
    {"CH UP", nullptr, nullptr},
    {"CH DOWN", nullptr, nullptr},
    {"VOL +", nullptr, nullptr},
    {"VOL -", nullptr, nullptr},
    {"Options", nullptr, nullptr},
    {"INFO", nullptr, "!"},
    {"BACK", nullptr, "arrow"},
    {"UP", nullptr, nullptr},
    {"DOWN", nullptr, nullptr},
    {"LEFT", nullptr, nullptr},
    {"RIGHT", nullptr, nullptr},
    {"ENTER", nullptr, nullptr},
    {"Favorite", nullptr, "F"},
    {"Watched", nullptr, "M"},
    {"PLAY", nullptr, "triangle"},
    {"PAUSE", nullptr, "||"},
    {"STOP", nullptr, "square"},
    {"RECORD", nullptr, "red dot"},
    {"Skip BK", "skip back", nullptr},
    {"REW", "rewind", nullptr},
    {"FF", "fast forward", nullptr},
    {"Skip FW", "skip forward", nullptr},
    {"|<<", "previous chapter", nullptr},
    {"Skip BK2", nullptr, nullptr},
    {"Skip FW2", nullptr, nullptr},
    {"Dot/Title", nullptr, nullptr},
    {">>|", "next chapter", nullptr},
    {"Delete", nullptr, nullptr},
    {"ASPECT", nullptr, nullptr},
    {"Video Out", nullptr, nullptr},
    {"DVD Menu", nullptr, nullptr},
    {"DVD Return", nullptr, nullptr}
};

const int BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

// IR Receiver Configuration
// Buffer size for storing IR timing data (1024 is default, plenty for most remotes)
const uint16_t kRecvBufferSize = 1024;

// Timeout in milliseconds for end of IR signal detection
// 15ms is standard for most protocols (NEC, Sony, RC5, etc.)
const uint8_t kTimeout = 15;

// Create IR receiver object
// This automatically uses the ESP32-S3's RMT peripheral for hardware-based IR decoding
IRrecv irrecv(IR_RECV_PIN, kRecvBufferSize, kTimeout, true);

// Decode results structure
decode_results results;

// Mapping state
std::vector<ButtonMapping> mappings;
int currentButtonIndex = 0;
bool mappingComplete = false;

// Debounce timing - ignore repeated codes within this window
const unsigned long DEBOUNCE_MS = 500;
unsigned long lastCodeTime = 0;

// Helper function to prompt for the next button
void promptNextButton() {
    if (currentButtonIndex >= BUTTON_COUNT) {
        mappingComplete = true;
        return;
    }

    const Button& btn = BUTTONS[currentButtonIndex];

    Serial.println("\n========================================");
    Serial.printf("Button %d of %d\n", currentButtonIndex + 1, BUTTON_COUNT);
    Serial.println("========================================");
    Serial.printf("Label: %s\n", btn.label);

    if (btn.note) {
        Serial.printf("Note:  %s\n", btn.note);
    }
    if (btn.color) {
        Serial.printf("Mark:  %s\n", btn.color);
    }

    Serial.println("\nPress the button on your remote now...");
    Serial.println("(or type 's' + Enter to skip this button)");
    Serial.println("----------------------------------------");
}

// Helper function to print final CSV mapping
void printMapping() {
    Serial.println("\n\n========================================");
    Serial.println("BUTTON MAPPING COMPLETE");
    Serial.println("========================================\n");

    Serial.println("CSV Format:");
    Serial.println("Label,Protocol,Code,Mapped");
    Serial.println("----------------------------------------");

    for (const auto& mapping : mappings) {
        Serial.printf("%s,%s,0x%llX,%s\n",
            mapping.label,
            mapping.mapped ? typeToString(mapping.protocol) : "SKIPPED",
            mapping.code,
            mapping.mapped ? "YES" : "NO"
        );
    }

    Serial.println("\n========================================");
    Serial.printf("Total: %d buttons (%d mapped, %d skipped)\n",
        mappings.size(),
        std::count_if(mappings.begin(), mappings.end(), [](const ButtonMapping& m) { return m.mapped; }),
        std::count_if(mappings.begin(), mappings.end(), [](const ButtonMapping& m) { return !m.mapped; })
    );
    Serial.println("========================================\n");
}

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    delay(2000);  // Wait for serial connection to stabilize

    Serial.println("========================================");
    Serial.println("IR Remote Button Mapper");
    Serial.println("========================================");
    Serial.println();

    Serial.println("This tool will help you map all buttons");
    Serial.println("on your remote to their IR codes.");
    Serial.println();

    Serial.println("Hardware Configuration:");
    Serial.printf("  IR Receiver: VS1838B/HX1838 on GPIO%d\n", IR_RECV_PIN);
    Serial.printf("  Total Buttons: %d\n", BUTTON_COUNT);
    Serial.println();

    // Start the IR receiver
    irrecv.enableIRIn();

    Serial.println("IR receiver initialized and ready!");
    Serial.println();

    // Prompt for first button
    promptNextButton();
}

void loop() {
    // If mapping is complete, print results and stop
    if (mappingComplete) {
        printMapping();
        while(true) {
            delay(1000);  // Infinite loop - mapping done
        }
    }

    // Check for serial input (skip command)
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 's' || cmd == 'S') {
            // Skip this button
            Serial.println("\n>>> Skipping this button\n");

            ButtonMapping skipped;
            skipped.label = BUTTONS[currentButtonIndex].label;
            skipped.code = 0;
            skipped.protocol = UNKNOWN;
            skipped.mapped = false;
            mappings.push_back(skipped);

            currentButtonIndex++;
            promptNextButton();

            // Clear any remaining serial input
            while(Serial.available()) Serial.read();
        }
    }

    // Check if IR signal has been received
    if (irrecv.decode(&results)) {
        unsigned long now = millis();

        // Debounce - ignore codes that come too quickly (button held down)
        if (now - lastCodeTime < DEBOUNCE_MS) {
            irrecv.resume();
            return;
        }

        // Ignore UNKNOWN protocols (noise)
        if (results.decode_type == UNKNOWN) {
            Serial.println(">>> Noise detected (UNKNOWN protocol), ignoring...");
            irrecv.resume();
            return;
        }

        lastCodeTime = now;

        // Display what we received
        Serial.println("\n>>> IR Code Received:");
        Serial.printf("    Protocol: %s\n", typeToString(results.decode_type));
        Serial.printf("    Code: 0x%llX\n", results.value);
        Serial.printf("    Bits: %d\n\n", results.bits);

        // Store the mapping
        ButtonMapping mapping;
        mapping.label = BUTTONS[currentButtonIndex].label;
        mapping.code = results.value;
        mapping.protocol = results.decode_type;
        mapping.mapped = true;
        mappings.push_back(mapping);

        Serial.println(">>> Mapped successfully!\n");

        // Move to next button
        currentButtonIndex++;
        delay(500);  // Brief pause before next prompt
        promptNextButton();

        // Resume receiving the next IR signal
        irrecv.resume();
    }

    delay(50);  // Small delay for responsiveness
}
