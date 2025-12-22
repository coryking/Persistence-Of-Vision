// MAC Address Test - prints WiFi MAC address every 3 seconds
// For ESP-NOW setup (Phase 0.5 of ir-control-spec.md)

#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);  // Give serial time to connect

    WiFi.mode(WIFI_STA);

    Serial.println();
    Serial.println("=== MAC Address Test ===");
    Serial.println("Copy this MAC address to shared/espnow_config.h");
    Serial.println();
}

void loop() {
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    delay(3000);
}
