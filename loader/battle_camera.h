#ifndef SF4CE_BATTLE_CAMERA_H
#define SF4CE_BATTLE_CAMERA_H

#include <stdint.h>
#include <stddef.h>

namespace battle_camera {

// The Android camera pulls back further as the fighters separate. Keep its
// original distance and tracking, and use a narrower lens for console framing.
constexpr uint32_t kFovBam = 466; // 40.96 degrees, 4096 BAM per revolution
constexpr uint32_t kTargetHeight = 124 * 4096; // Q12 world coordinates
constexpr int kAirborneFovBam = 600; // 52.73 degrees at high jumps

// Character task coordinates use Q16; widen gradually as either fighter rises.
inline int FovForHeights(int32_t first_y, int32_t second_y)
{
    int32_t height = first_y > second_y ? first_y : second_y;
    if (height <= 0) return kFovBam;
    if (height >= 128 * 65536) return kAirborneFovBam;
    return kFovBam + int(int64_t(height) * (kAirborneFovBam - kFovBam) / (128 * 65536));
}

inline int FovForBounds(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int64_t separation = int64_t(x1) - x2;
    if (separation < 0) separation = -separation;
    int distance_fov = kFovBam;
    constexpr int64_t start = 340 * 65536, end = 680 * 65536;
    if (separation >= end) distance_fov = 512;
    else if (separation > start)
        distance_fov += int((separation - start) * (512 - kFovBam) / (end - start));
    int jump_fov = FovForHeights(y1, y2);
    return jump_fov > distance_fov ? jump_fov : distance_fov;
}

inline int ApproachFov(int previous, int desired)
{
    // Open faster than we close: a rising fighter needs room immediately.
    if (desired > previous + 24) return previous + 24;
    if (desired < previous - 12) return previous - 12;
    return desired;
}


constexpr uint32_t MovwR1(uint32_t value)
{
    return 0xe3001000 | ((value & 0xf000) << 4) | (value & 0x0fff);
}

// Write through kubridge on Vita; a normal writer also permits host tests.
using Writer = int (*)(void *, const void *, size_t);

inline bool Install(uintptr_t init, uintptr_t direction, uintptr_t view,
                    Writer write)
{
    if (!init || !direction || !view || !write ||
        ((init | direction | view) & 3))
        return false;

    struct Patch {
        uintptr_t address;
        uint32_t expected;
        uint32_t replacement;
    };
    const Patch patches[] = {
        {init + 0x254, 170 * 4096, 170 * 4096},
        {init + 0x258, 80 * 4096, kTargetHeight},
        {init + 0xbc, 0xe3a01c02, MovwR1(kFovBam)},
        {direction + 0x74, 0xe3001200, MovwR1(kFovBam)},
        // CameraViewSet restores Y=80 in mode 10; keep that path consistent.
        {view + 0x180, 80 * 4096, kTargetHeight},
    };

    // Reject another game build before changing any camera code or literals.
    for (const Patch &patch : patches)
        if (*(const uint32_t *)patch.address != patch.expected)
            return false;

    for (const Patch &patch : patches)
        if (patch.expected != patch.replacement &&
            write((void *)patch.address, &patch.replacement,
                  sizeof(patch.replacement)) < 0)
            return false;
    return true;
}

} // namespace battle_camera

#endif
