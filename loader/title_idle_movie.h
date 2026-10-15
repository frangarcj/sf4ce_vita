#ifndef SF4CE_TITLE_IDLE_MOVIE_H
#define SF4CE_TITLE_IDLE_MOVIE_H
#include <stddef.h>
#include <stdint.h>

namespace title_idle_movie {
using Writer = int (*)(void *, const void *, size_t);
enum class Result { Applied, PlaybackAvailable, Unsupported, WriteFailed };

// ST_TitleSystem starts a fade at idle tick 600, *before* checking whether
// sf4_online.mp4 can be played. Its missing-movie path only resets the timer:
// it neither fades in nor recreates the title labels deleted at tick 600.
// If the optional movie/backend is unavailable, stay on the title/input path.
inline Result Install(uintptr_t title_system, bool movie_playback_available,
                      Writer write)
{
    if (movie_playback_available) return Result::PlaybackAvailable;
    if (!title_system || (title_system & 3) || !write)
        return Result::Unsupported;
    struct Signature { size_t offset; uint32_t instruction; };
    const Signature signatures[] = {
        {0x000, 0xe92d4800}, // push {fp,lr}
        {0x42c, 0xe301004c}, // movw r0,#0x104c (idle counter)
        {0x43c, 0xe3500f96}, // cmp r0,#600
        {0x440, 0xba000039}, // blt normal title/input handling
        {0x4c8, 0xeb07afa2}, // bl AppMain::SetFadeOut(int,int)@plt
    };
    for (const auto &signature : signatures)
        if (*(const uint32_t *)(title_system + signature.offset) != signature.instruction)
            return Result::Unsupported;

    // Keep the original branch target; only remove its idle-counter condition.
    const uint32_t normal_title = 0xea000039;
    return write((void *)(title_system + 0x440), &normal_title, sizeof(normal_title)) < 0
        ? Result::WriteFailed : Result::Applied;
}
}
#endif
