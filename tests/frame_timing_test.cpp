#include "../loader/frame_timing.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
using namespace frame_timing;
int main() {
    Clock render(60);
    assert(render.Due(0));
    unsigned frames = 0;
    for (uint64_t us = 0; us < 60000000; ++us) if (render.Due(us)) { ++frames; render.Advance(us); }
    assert(frames == 3600);
    assert(render.Deadline() == 60000000);
    render.Advance(90000000); // a load drops presentation slots, never bursts
    assert(render.Deadline() > 90000000 && render.Deadline() <= 90016667);
    assert(NativeInterval(500, true) == 50000);
    assert(NativeInterval(166, true) == 16000);
    assert(NativeInterval(50, false) == 50000);
    assert(!NativeInterval(-1, false));
    assert(!NativeInterval(0, true));
    assert(!NativeInterval(10010, true));
    assert(NextSimulation(1000000, 1003000, 50000) == 1050000);
    assert(NextSimulation(1000000, 2000000, 50000) == 2050000);
    // 60 render slots, 20 simulation ticks. The 16/17 ms render cadence does
    // not cause 60 updates, and scheduler jitter cannot accumulate slowdown.
    Clock jittered(60); unsigned draws = 0, updates = 0; uint64_t next = 0;
    for (uint64_t us = 0; us < 10000000; us += 1000) {
        if (!jittered.Due(us)) continue;
        jittered.Advance(us); ++draws;
        if (!next || us >= next) { ++updates; next = NextSimulation(next, us, 50000); }
    }
    assert(draws == 600 && updates == 200);
    assert(Alpha(1000000, 1050000, 50000) == 0);
    assert(Alpha(1025000, 1050000, 50000) == .5f);
    assert(Alpha(1050000, 1050000, 50000) == 1);
    assert(Alpha(1100000, 1050000, 50000) == 1);
    assert(fabsf(Blend(-10, 20, 1.0f / 3)) < 1e-5f);
    puts("Frame timing: native units, exact 60 Hz, 20 Hz simulation, jitter and stalls OK");
}
