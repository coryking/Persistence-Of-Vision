/**
 * IR Remote Test - Code Dump Utility
 *
 * Simple test project to receive and decode IR remote control signals.
 * This will become the basis for IR-controlled effect switching in the main POV display.
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
 * with minimal CPU overhead. This is critical for eventual integration into the POV display
 * where timing-sensitive LED rendering occurs.
 */

#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

// Pin Definitions
#define IR_RECV_PIN 2  // GPIO2 - Orange wire from VS1838B signal pin

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

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    delay(2000);  // Wait for serial connection to stabilize

    Serial.println("========================================");
    Serial.println("IR Remote Test - Code Dump Utility");
    Serial.println("========================================");
    Serial.println();

    Serial.println("Hardware Configuration:");
    Serial.printf("  IR Receiver: VS1838B/HX1838 on GPIO%d\n", IR_RECV_PIN);
    Serial.printf("  Buffer Size: %d bytes\n", kRecvBufferSize);
    Serial.printf("  Timeout: %d ms\n", kTimeout);
    Serial.println();

    // Start the IR receiver
    irrecv.enableIRIn();

    Serial.println("IR receiver initialized and ready!");
    Serial.println("Point your remote at the receiver and press buttons.");
    Serial.println("Codes will be displayed below:");
    Serial.println("----------------------------------------");
}

void loop() {
    // Check if IR signal has been received
    if (irrecv.decode(&results)) {
        // Print timestamp
        Serial.printf("[%lu ms] ", millis());

        // Print protocol name and raw code
        Serial.print(typeToString(results.decode_type));
        Serial.print(" - ");

        // Print the full decoded result in a human-readable format
        // This includes protocol, value, address, command, etc.
        Serial.println(resultToHumanReadableBasic(&results));

        // Print raw timing data (useful for debugging unknown protocols)
        // Uncomment the line below if you want to see the raw IR timings
        // Serial.println(resultToTimingInfo(&results));

        // Print a separator line for readability
        Serial.println();

        // Resume receiving the next IR signal
        irrecv.resume();
    }

    // Small delay to prevent overwhelming the serial output
    // This doesn't affect IR reception since the RMT peripheral handles it in hardware
    delay(100);
}
