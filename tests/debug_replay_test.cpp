#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
static void trace(const char *, ...) {}
#include "../loader/debug_replay.h"
int main() {
    char path[] = "/tmp/sf4ce-replay-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    DebugReplay replay; DebugInput input;
    assert(replay.Step(path, input) && !input.touch && !input.buttons);
    FILE *f = fopen(path, "a"); assert(f); fputs("tap 400 200 2\nwait 1\npad 8 1\n",f); fclose(f);
    assert(replay.Step(path,input) && input.touch && input.x == 400);
    assert(replay.Step(path,input) && input.touch && input.y == 200);
    assert(replay.Step(path,input) && !input.touch && !input.buttons);
    assert(replay.Step(path,input) && input.buttons == 8);
    assert(replay.Step(path,input) && !input.buttons);
    f=fopen(path,"a"); assert(f); fputs("tap -1 0 2\n",f); fclose(f);
    assert(replay.Step(path,input) && !input.touch && !input.buttons);
    unlink(path);
    puts("Guest replay: append after EOF, touch/pad durations, idle reset and invalid input rejection OK");
}
