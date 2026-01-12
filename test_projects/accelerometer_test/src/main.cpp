#include <Arduino.h>
#include <Wire.h>
#include <MPU9250_WE.h>

// Pin assignments from led_display/include/hardware_config.h
// MPU-9250 IMU (9-axis: gyro + accel + magnetometer)
constexpr uint8_t IMU_SCL_PIN = D1;    // Blue wire - I2C Clock
constexpr uint8_t IMU_SDA_PIN = D0;    // Green wire - I2C Data
constexpr uint8_t IMU_ADO_PIN = D3;    // Purple wire - I2C addr LSB (0=0x68, 1=0x69)
constexpr uint8_t IMU_NCS_PIN = D2;    // Orange wire - SPI chip select (HIGH for I2C mode)

// MPU-9250 I2C address (depends on ADO pin state)
constexpr uint8_t MPU9250_ADDR = 0x68;  // ADO = LOW

MPU9250_WE mpu = MPU9250_WE(MPU9250_ADDR);

// Sample rate tracking
uint32_t sampleCount = 0;
uint32_t lastRateCalc = 0;
float samplesPerSec = 0.0f;

// =============================================================================
// ANSI Terminal Helpers
// =============================================================================

#define ANSI_CLEAR_SCREEN "\033[2J"
#define ANSI_HOME         "\033[H"
#define ANSI_HIDE_CURSOR  "\033[?25l"
#define ANSI_SHOW_CURSOR  "\033[?25h"
#define ANSI_CLEAR_LINE   "\033[2K"

// Move cursor to row, col (1-indexed)
void moveTo(int row, int col) {
    Serial.printf("\033[%d;%dH", row, col);
}

// Print a float at position with format
void printAt(int row, int col, const char* fmt, float val) {
    moveTo(row, col);
    Serial.printf(fmt, val);
}

// Print XYZ values on a single row at specified columns
void printXYZ(int row, int colX, int colY, int colZ, const char* fmt, xyzFloat& v) {
    printAt(row, colX, fmt, v.x);
    printAt(row, colY, fmt, v.y);
    printAt(row, colZ, fmt, v.z);
}

// =============================================================================
// Display Layout Constants
// =============================================================================

// All row numbers are 1-indexed (ANSI standard)
namespace Layout {
    // Header
    constexpr int TITLE = 1;

    // Calibrated values section
    constexpr int CAL_HEADER = 3;
    constexpr int CAL_X = 4;
    constexpr int CAL_Y = 5;
    constexpr int CAL_Z = 6;
    constexpr int CAL_MAG = 7;
    constexpr int CAL_ACCEL_COL = 10;
    constexpr int CAL_GYRO_COL = 38;
    constexpr int CAL_MAG_COL = 8;

    // Raw values section (NO calibration)
    constexpr int RAW_HEADER = 9;
    constexpr int RAW_ACCEL = 10;
    constexpr int RAW_GYRO = 11;

    // Corrected raw section (WITH calibration)
    constexpr int CORR_HEADER = 13;
    constexpr int CORR_ACCEL = 14;
    constexpr int CORR_GYRO = 15;

    // XYZ column positions for raw/corrected sections
    constexpr int XYZ_X_COL = 12;
    constexpr int XYZ_Y_COL = 23;
    constexpr int XYZ_Z_COL = 34;

    // Sample rate
    constexpr int RATE_ROW = 17;
    constexpr int RATE_COL = 13;
}

// =============================================================================
// Setup Helpers
// =============================================================================

void waitForSerial() {
    while (!Serial) {
        delay(100);
    }
    delay(100);
}

void waitForKeypress(const char* prompt) {
    while (Serial.available()) Serial.read();

    while (!Serial.available()) {
        Serial.printf("\r%s%s", ANSI_CLEAR_LINE, prompt);
        for (int i = 0; i < 100 && !Serial.available(); i++) {
            delay(10);
        }
    }

    while (Serial.available()) Serial.read();
    Serial.println();
}

void printStaticLayout() {
    Serial.print(ANSI_CLEAR_SCREEN ANSI_HOME ANSI_HIDE_CURSOR);

    moveTo(Layout::TITLE, 1);
    Serial.println("=== MPU-9250 Live Data ===");

    moveTo(Layout::CAL_HEADER, 1);
    Serial.println("ACCELEROMETER              GYROSCOPE");
    Serial.println("  X (g):                     X (dps):");
    Serial.println("  Y (g):                     Y (dps):");
    Serial.println("  Z (g):                     Z (dps):");
    Serial.println("  Mag:");

    moveTo(Layout::RAW_HEADER, 1);
    Serial.println("RAW VALUES (int16, NO calibration)");
    Serial.println("  Accel X:         Y:         Z:");
    Serial.println("  Gyro  X:         Y:         Z:");

    moveTo(Layout::CORR_HEADER, 1);
    Serial.println("CORRECTED RAW (int16, WITH calibration offsets)");
    Serial.println("  Accel X:         Y:         Z:");
    Serial.println("  Gyro  X:         Y:         Z:");

    moveTo(Layout::RATE_ROW, 1);
    Serial.println("Sample rate:      Hz");
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
    Serial.begin(115200);

    // Configure pins for I2C mode BEFORE waiting for serial
    pinMode(IMU_NCS_PIN, OUTPUT);
    digitalWrite(IMU_NCS_PIN, HIGH);  // HIGH = I2C mode
    pinMode(IMU_ADO_PIN, OUTPUT);
    digitalWrite(IMU_ADO_PIN, LOW);   // LOW = address 0x68

    waitForSerial();

    Serial.print(ANSI_CLEAR_SCREEN ANSI_HOME);
    Serial.println("=== MPU-9250 IMU Test ===\n");

    waitForKeypress("Press any key to initialize IMU...");

    Serial.printf("Pins: SDA=%d, SCL=%d, NCS=%d, ADO=%d\n",
                  IMU_SDA_PIN, IMU_SCL_PIN, IMU_NCS_PIN, IMU_ADO_PIN);

    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
    Wire.setClock(400000);

    Serial.println("Initializing MPU-9250...");

    if (!mpu.init()) {
        Serial.println("ERROR: MPU-9250 not responding!");
        Serial.println("Check wiring and connections.");
        while (true) delay(1000);
    }

    Serial.println("MPU-9250 found!\n");

    // Configure ranges before calibration
    mpu.setAccRange(MPU9250_ACC_RANGE_16G);
    mpu.setGyrRange(MPU9250_GYRO_RANGE_2000);
    mpu.enableAccDLPF(true);
    mpu.setAccDLPF(MPU9250_DLPF_6);
    mpu.setGyrDLPF(MPU9250_DLPF_6);
    mpu.setSampleRateDivider(0);

    Serial.println("Config: Accel +/-16g, Gyro +/-2000dps\n");

    // Calibration
    Serial.println("CALIBRATION: Keep the sensor STILL and LEVEL!");
    waitForKeypress("Press any key to start calibration...");

    Serial.println("Calibrating... (takes a few seconds)");
    mpu.autoOffsets();

    xyzFloat accOffsets = mpu.getAccOffsets();
    xyzFloat gyrOffsets = mpu.getGyrOffsets();

    Serial.println("Calibration complete!");
    Serial.printf("  Accel offsets: X=%.3f Y=%.3f Z=%.3f\n",
                  accOffsets.x, accOffsets.y, accOffsets.z);
    Serial.printf("  Gyro offsets:  X=%.2f Y=%.2f Z=%.2f\n\n",
                  gyrOffsets.x, gyrOffsets.y, gyrOffsets.z);

    waitForKeypress("Press any key for live display...");

    printStaticLayout();
    lastRateCalc = millis();
}

void loop() {
    // Read all value types
    xyzFloat accel = mpu.getGValues();
    xyzFloat gyro = mpu.getGyrValues();
    xyzFloat accelRaw = mpu.getAccRawValues();
    xyzFloat gyroRaw = mpu.getGyrRawValues();
    xyzFloat accelCorrected = mpu.getCorrectedAccRawValues();
    xyzFloat gyroCorrected = mpu.getCorrectedGyrRawValues();

    sampleCount++;

    // Calculate sample rate every second
    uint32_t now = millis();
    if (now - lastRateCalc >= 1000) {
        samplesPerSec = sampleCount * 1000.0f / (now - lastRateCalc);
        sampleCount = 0;
        lastRateCalc = now;
    }

    // Update calibrated accel (g values)
    printAt(Layout::CAL_X, Layout::CAL_ACCEL_COL, "%+8.3f", accel.x);
    printAt(Layout::CAL_Y, Layout::CAL_ACCEL_COL, "%+8.3f", accel.y);
    printAt(Layout::CAL_Z, Layout::CAL_ACCEL_COL, "%+8.3f", accel.z);

    // Magnitude
    float mag = sqrt(accel.x*accel.x + accel.y*accel.y + accel.z*accel.z);
    printAt(Layout::CAL_MAG, Layout::CAL_MAG_COL, "%7.3f", mag);

    // Update calibrated gyro (dps values)
    printAt(Layout::CAL_X, Layout::CAL_GYRO_COL, "%+8.1f", gyro.x);
    printAt(Layout::CAL_Y, Layout::CAL_GYRO_COL, "%+8.1f", gyro.y);
    printAt(Layout::CAL_Z, Layout::CAL_GYRO_COL, "%+8.1f", gyro.z);

    // Update raw values (no calibration)
    printXYZ(Layout::RAW_ACCEL, Layout::XYZ_X_COL, Layout::XYZ_Y_COL, Layout::XYZ_Z_COL, "%+6.0f", accelRaw);
    printXYZ(Layout::RAW_GYRO, Layout::XYZ_X_COL, Layout::XYZ_Y_COL, Layout::XYZ_Z_COL, "%+6.0f", gyroRaw);

    // Update corrected raw values (with calibration offsets)
    printXYZ(Layout::CORR_ACCEL, Layout::XYZ_X_COL, Layout::XYZ_Y_COL, Layout::XYZ_Z_COL, "%+6.0f", accelCorrected);
    printXYZ(Layout::CORR_GYRO, Layout::XYZ_X_COL, Layout::XYZ_Y_COL, Layout::XYZ_Z_COL, "%+6.0f", gyroCorrected);

    // Update sample rate
    printAt(Layout::RATE_ROW, Layout::RATE_COL, "%6.0f", samplesPerSec);

    delay(10);
}
