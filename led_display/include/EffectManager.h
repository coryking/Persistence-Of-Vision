#ifndef EFFECT_MANAGER_H
#define EFFECT_MANAGER_H

#include "Effect.h"
#include "RotorDiagnosticStats.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <atomic>

#define EFFECT_MGR_TAG "EFFECT"

/**
 * Command types for cross-core communication
 */
enum class EffectCommandType : uint8_t {
    SET_EFFECT = 1,
    BRIGHTNESS_UP = 2,
    BRIGHTNESS_DOWN = 3,
    EFFECT_RIGHT = 4,
    EFFECT_LEFT = 5,
    EFFECT_UP = 6,
    EFFECT_DOWN = 7,
    DISPLAY_POWER = 8,
    EFFECT_ENTER = 9,
    STATS_TOGGLE = 10,
};

/**
 * Command sent from ESP-NOW callback (Core 0) to main loop (Core 1)
 */
struct EffectCommand {
    EffectCommandType type;
    uint8_t value;  // effect number for SET_EFFECT, unused for brightness
};

/**
 * Manages effect lifecycle, brightness, and cross-core command processing
 *
 * Responsibilities:
 * - Effect registration and switching
 * - Brightness state (0-10 scale)
 * - Command queue for cross-core communication
 * - Revolution event forwarding to current effect
 *
 * Usage:
 *   EffectManager manager;
 *   manager.registerEffect(&effect1);
 *   manager.registerEffect(&effect2);
 *   manager.begin();  // Creates queue, starts first effect
 *
 *   // ESP-NOW callback sends commands:
 *   EffectCommand cmd = {EffectCommandType::SET_EFFECT, 5};
 *   xQueueSend(manager.getCommandQueue(), &cmd, 0);
 *
 *   // Main loop processes commands:
 *   manager.processCommands();
 *   Effect* effect = manager.current();
 *   effect->render(ctx);
 */
class EffectManager {
public:
    static constexpr uint8_t MAX_EFFECTS = 12;
    static constexpr uint8_t DEFAULT_BRIGHTNESS = 5;  // 0-10 scale

    EffectManager() : effectCount(0), currentIndex(0), brightness(DEFAULT_BRIGHTNESS), commandQueue(nullptr) {}

    /**
     * Register an effect (call during setup)
     *
     * @param effect Pointer to effect instance (must outlive manager)
     * @return Index of registered effect, or 255 if registry is full
     */
    uint8_t registerEffect(Effect* effect) {
        if (effectCount >= MAX_EFFECTS) return 255;
        effects[effectCount] = effect;
        return effectCount++;
    }

    /**
     * Initialize manager: create command queue, start first effect
     * Call after all effects are registered
     */
    void begin() {
        // Create command queue (size 10 - handles burst button presses)
        commandQueue = xQueueCreate(10, sizeof(EffectCommand));
        if (!commandQueue) {
            ESP_LOGE(EFFECT_MGR_TAG, "Failed to create command queue!");
        }

        // Start first effect
        if (effectCount > 0 && effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Get command queue handle (for ESP-NOW callback to send commands)
     */
    QueueHandle_t getCommandQueue() { return commandQueue; }

    /**
     * Process pending commands from queue (call from main loop)
     * Polls queue non-blocking, processes all pending commands
     */
    void processCommands() {
        if (!commandQueue) return;

        EffectCommand cmd;
        while (xQueueReceive(commandQueue, &cmd, 0) == pdPASS) {
            switch (cmd.type) {
                case EffectCommandType::SET_EFFECT:
                    changeEffect(cmd.value);
                    break;
                case EffectCommandType::BRIGHTNESS_UP:
                    incrementBrightness();
                    break;
                case EffectCommandType::BRIGHTNESS_DOWN:
                    decrementBrightness();
                    break;
                case EffectCommandType::EFFECT_RIGHT:
                    if (currentIndex < effectCount && effects[currentIndex]) {
                        effects[currentIndex]->right();
                        ESP_LOGI(EFFECT_MGR_TAG, "Button -> RIGHT");
                    }
                    break;
                case EffectCommandType::EFFECT_LEFT:
                    if (currentIndex < effectCount && effects[currentIndex]) {
                        effects[currentIndex]->left();
                        ESP_LOGI(EFFECT_MGR_TAG, "Button -> LEFT");
                    }
                    break;
                case EffectCommandType::EFFECT_UP:
                    if (currentIndex < effectCount && effects[currentIndex]) {
                        effects[currentIndex]->up();
                        ESP_LOGI(EFFECT_MGR_TAG, "Button -> UP");
                    }
                    break;
                case EffectCommandType::EFFECT_DOWN:
                    if (currentIndex < effectCount && effects[currentIndex]) {
                        effects[currentIndex]->down();
                        ESP_LOGI(EFFECT_MGR_TAG, "Button -> DOWN");
                    }
                    break;
                case EffectCommandType::EFFECT_ENTER:
                    if (currentIndex < effectCount && effects[currentIndex]) {
                        effects[currentIndex]->enter();
                        ESP_LOGI(EFFECT_MGR_TAG, "Button -> ENTER");
                    }
                    break;
                case EffectCommandType::DISPLAY_POWER:
                    setDisplayEnabled(cmd.value != 0);
                    if (currentIndex < effectCount && effects[currentIndex]) {
                        effects[currentIndex]->onDisplayPower(cmd.value != 0);
                    }
                    break;
                case EffectCommandType::STATS_TOGGLE:
                    _statsEnabled = !_statsEnabled;
                    ESP_LOGI(EFFECT_MGR_TAG, "Stats overlay -> %s", _statsEnabled ? "ON" : "OFF");
                    break;
            }
        }
    }

    /**
     * Get currently active effect
     * @return Pointer to current effect, or nullptr if none registered
     */
    Effect* current() {
        return (currentIndex < effectCount) ? effects[currentIndex] : nullptr;
    }

    /**
     * Get total number of registered effects
     */
    uint8_t getEffectCount() const { return effectCount; }

    /**
     * Get current effect index (0-based)
     */
    uint8_t getCurrentEffectIndex() const { return currentIndex; }

    /**
     * Get current brightness (0-10 scale)
     * Returns max brightness (10) if current effect requires full brightness
     */
    uint8_t getBrightness() const {
        if (currentIndex < effectCount && effects[currentIndex] &&
            effects[currentIndex]->requiresFullBrightness()) {
            return 10;
        }
        return brightness;
    }

    /**
     * Switch to specific effect by number (1-based, matches remote buttons)
     * No-op if number invalid or already current
     *
     * @param effectNumber 1-based effect number (1-10)
     */
    void changeEffect(uint8_t effectNumber) {
        if (effectNumber < 1 || effectNumber > effectCount) {
            ESP_LOGW(EFFECT_MGR_TAG, "Invalid effect %d (have %d effects)", effectNumber, effectCount);
            return;
        }

        uint8_t newIndex = effectNumber - 1;  // Convert to 0-based
        if (newIndex == currentIndex) return;

        // End current effect
        if (effects[currentIndex]) {
            effects[currentIndex]->end();
        }

        // Switch to new effect
        currentIndex = newIndex;

        // Begin new effect
        if (effects[currentIndex]) {
            effects[currentIndex]->begin();
        }

        // Update diagnostic stats with new effect number
        RotorDiagnosticStats::instance().setEffectNumber(effectNumber);

        ESP_LOGI(EFFECT_MGR_TAG, "Effect -> %d", effectNumber);
    }

    /**
     * Set brightness (0-10 scale, clamped internally)
     */
    void setBrightness(uint8_t level) {
        if (level > 10) level = 10;
        brightness = level;
        RotorDiagnosticStats::instance().setBrightness(brightness);
        ESP_LOGI(EFFECT_MGR_TAG, "Brightness -> %d", brightness);
    }

    /**
     * Increment brightness (clamps at 10)
     */
    void incrementBrightness() {
        if (brightness < 10) {
            brightness++;
            RotorDiagnosticStats::instance().setBrightness(brightness);
            ESP_LOGI(EFFECT_MGR_TAG, "Brightness UP -> %d", brightness);
        }
    }

    /**
     * Decrement brightness (clamps at 0)
     */
    void decrementBrightness() {
        if (brightness > 0) {
            brightness--;
            RotorDiagnosticStats::instance().setBrightness(brightness);
            ESP_LOGI(EFFECT_MGR_TAG, "Brightness DOWN -> %d", brightness);
        }
    }

    /**
     * Notify current effect of revolution boundary
     * Call from hall sensor processing task
     *
     * @param usPerRev Microseconds per revolution
     * @param timestamp Revolution timestamp
     * @param revolutionCount Total revolution count
     */
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
        if (currentIndex < effectCount && effects[currentIndex]) {
            effects[currentIndex]->onRevolution(usPerRev, timestamp, revolutionCount);
        }
    }

    /**
     * Set display power state (controls whether LEDs render or go black)
     * @param enabled true = render effects, false = LEDs off
     */
    void setDisplayEnabled(bool enabled) {
        displayEnabled.store(enabled);
        ESP_LOGI(EFFECT_MGR_TAG, "Display power -> %s", enabled ? "ON" : "OFF");
    }

    /**
     * Check if display rendering is enabled
     */
    bool isDisplayEnabled() const { return displayEnabled.load(); }

    /**
     * Check if stats overlay is enabled
     */
    bool isStatsEnabled() const { return _statsEnabled; }

private:
    Effect* effects[MAX_EFFECTS];
    uint8_t effectCount;
    uint8_t currentIndex;     // 0-based internally
    uint8_t brightness;       // 0-10 scale
    QueueHandle_t commandQueue;
    std::atomic<bool> displayEnabled{true};  // Power state (true = render, false = off)
    bool _statsEnabled{false};  // Stats overlay toggle
};

#endif // EFFECT_MANAGER_H
