#ifndef SF4CE_MENU_BACKGROUND_H
#define SF4CE_MENU_BACKGROUND_H
#include <stdint.h>
#include <stddef.h>
namespace menu_background {
// Optional downloaded replay behind the main menu currently drives a broken
// incremental texture load. Keep the native static menu and all local modes.
inline bool Install(uintptr_t update, int (*write)(void *, const void *, size_t)) {
    if (!update || (update & 3) || !write) return false;
    struct Check { unsigned at; uint32_t word; };
    const Check checks[] = {{0, 0xe92d4800}, {0x14, 0xe3021c70},
        {0x28, 0x1a000000}, {0xf4, 0xeb09af01}, {0x194, 0xe1a0d00b}};
    for (auto check : checks)
        if (*(const uint32_t *)(update + check.at) != check.word) return false;
    const uint32_t exit = 0xea000059; // same function epilogue at +0x194
    return write((void *)(update + 0x28), &exit, sizeof(exit)) >= 0;
}
}
#endif
