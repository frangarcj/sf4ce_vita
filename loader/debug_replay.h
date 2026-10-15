#ifndef SF4CE_DEBUG_REPLAY_H
#define SF4CE_DEBUG_REPLAY_H

#include <cstdio>
#include <cstring>
#include <stdint.h>

struct DebugInput {
    uint32_t buttons = 0;
    int x = 0, y = 0;
    bool touch = false;
};

// Opt-in integration test input, consumed by the guest at its normal update
// cadence. This never sends events to the host desktop or other applications.
class DebugReplay {
    FILE *file = nullptr;
    bool opened = false, active = false, waiting = false;
    unsigned remaining = 0, line = 0;
    DebugInput current;
public:
    ~DebugReplay() { if (file) fclose(file); }
    bool Step(const char *path, DebugInput &input)
    {
        if (!opened) {
            opened = true;
            file = fopen(path, "r");
            active = file != nullptr;
        }
        input = {};
        if (!active) return false;
        if (!remaining && file) {
            char text[160], command[16];
            while (fgets(text, sizeof(text), file)) {
                ++line;
                if (text[0] == '#' || sscanf(text, "%15s", command) != 1) continue;
                unsigned frames = 0, buttons = 0;
                int x = 0, y = 0;
                current = {};
                bool valid = false;
                if (!strcmp(command, "wait")) {
                    valid = sscanf(text, "%*s %u", &frames) == 1;
                } else if (!strcmp(command, "tap")) {
                    valid = sscanf(text, "%*s %d %d %u", &x, &y, &frames) == 3 &&
                            x >= 0 && x < 960 && y >= 0 && y < 544;
                    current.touch = true; current.x = x; current.y = y;
                } else if (!strcmp(command, "pad")) {
                    valid = sscanf(text, "%*s %x %u", &buttons, &frames) == 2;
                    current.buttons = buttons;
                }
                if (!valid || frames == 0 || frames > 72000) {
                    trace("[Replay] rejected line %u\n", line);
                    fclose(file); file = nullptr; return true;
                }
                remaining = frames;
                waiting = false;
                trace("[Replay] line %u: %s", line, text);
                break;
            }
            if (!remaining && file) {
                if (!waiting) trace("[Replay] completed; waiting for appended commands\n");
                waiting = true;
                // Permit interactive guest-only QA by appending commands.
                // Seeking clears EOF in both newlib and the emulator's file.
                fseek(file, 0, SEEK_CUR);
            }
        }
        if (remaining) { input = current; --remaining; }
        return true;
    }
};
#endif
