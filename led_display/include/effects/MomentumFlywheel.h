#ifndef MOMENTUM_FLYWHEEL_H
#define MOMENTUM_FLYWHEEL_H

#include "Effect.h"
#include "types.h"

/**
 * Momentum Flywheel - Hand-spin effect with smooth energy decay
 *
 * Tracks "stored energy" that pumps up with each revolution and
 * decays continuously. Creates smooth wind-down even with slow
 * revolution rates.
 *
 * Visual behavior:
 * - Spin fast: immediate warm/bright glow
 * - Let go: glow persists briefly, then gracefully fades to cool
 * - Smooth color transition from orange (high energy) to blue (low energy)
 */
class MomentumFlywheel : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;

private:
    // Half-life of energy decay in microseconds (~1 second)
    static constexpr uint32_t DECAY_HALF_LIFE_US = 1000000;

    uint16_t storedEnergy;          // 0-65535 energy level
    timestamp_t lastDecayTime;      // For continuous decay calculation

    uint16_t speedToEnergy(interval_t microsPerRev) const;
    CHSV energyToColor(uint16_t energy) const;
};

#endif // MOMENTUM_FLYWHEEL_H
