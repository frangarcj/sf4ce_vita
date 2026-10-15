#ifndef SF4CE_FRAME_TIMING_H
#define SF4CE_FRAME_TIMING_H
#include <stdint.h>

namespace frame_timing {
constexpr uint32_t kInitialSimulationUs = 50000;
constexpr uint32_t kRenderHz = 60;
// A clock in integer ticks avoids accumulating the 1/60 second rounding error.
class Clock {
    uint64_t origin = 0, tick = 0;
    uint32_t hz;
    bool started = false;
public:
    explicit Clock(uint32_t frequency) : hz(frequency) {}
    uint64_t Deadline() const { return origin + tick * 1000000 / hz; }
    bool Due(uint64_t now) const { return !started || now >= Deadline(); }
    void Advance(uint64_t now) {
        if (!started || now > Deadline() + 250000) {
            origin = now; tick = 0; started = true;
        }
        ++tick;
        // Drop missed presentation slots, without running the game faster.
        if (Deadline() <= now) tick = (now - origin) * hz / 1000000 + 1;
    }
};
inline uint32_t NativeInterval(int value, bool animation) {
    // Match Java's integer milliseconds: setAnimationInterval receives seconds
    // scaled by 10000, whereas FrameIntervalSet receives milliseconds.
    int64_t milliseconds = animation ? value / 10 : value;
    if (milliseconds < 1 || milliseconds > 1000) return 0;
    return static_cast<uint32_t>(milliseconds * 1000);
}
inline uint64_t NextSimulation(uint64_t previous, uint64_t now, uint32_t interval) {
    // A blocking load must not produce seconds of catch-up updates.
    if (!previous || now > previous + 250000) return now + interval;
    return previous + interval;
}
inline float Alpha(uint64_t now, uint64_t next, uint32_t interval) {
    if (now >= next) return 1.0f;
    if (next - now >= interval) return 0.0f;
    return 1.0f - static_cast<float>(next - now) / interval;
}
inline float Blend(float a, float b, float alpha) { return a + (b - a) * alpha; }
}
#endif
