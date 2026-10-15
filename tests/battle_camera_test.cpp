#include "../loader/battle_camera.h"

#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>

struct CameraCode {
    std::array<uint32_t, 0x260 / 4> init{};
    std::array<uint32_t, 0xb8 / 4> direction{};
    std::array<uint32_t, 0x18c / 4> view{};

    CameraCode()
    {
        init[0x254 / 4] = 170 * 4096;
        init[0x258 / 4] = 80 * 4096;
        init[0xbc / 4] = 0xe3a01c02;
        direction[0x74 / 4] = 0xe3001200;
        view[0x180 / 4] = 80 * 4096;
    }

    bool install(battle_camera::Writer writer) {
        return battle_camera::Install((uintptr_t)init.data(),
                                      (uintptr_t)direction.data(),
                                      (uintptr_t)view.data(), writer);
    }
};

static int writes;
static int Write(void *dst, const void *src, size_t size)
{
    ++writes;
    memcpy(dst, src, size);
    return 0;
}

int main()
{
    static_assert(battle_camera::MovwR1(398) == 0xe300118e);
    static_assert(battle_camera::MovwR1(0xabcd) == 0xe30a1bcd);
    assert(battle_camera::FovForHeights(0, 0) == 466);
    assert(battle_camera::FovForHeights(-100000, 0) == 466);
    assert(battle_camera::FovForHeights(INT32_MAX, 0) == 600);
    int previous = 466;
    for (int height = 0; height <= 140 * 65536; height += 10000) {
        int desired = battle_camera::FovForHeights(height, 0);
        assert(desired >= previous && desired <= 600);
        assert(desired == battle_camera::FovForHeights(0, height));
        previous = desired;
    }
    assert(battle_camera::FovForBounds(0, 0, 0, 0) == 466);
    assert(battle_camera::FovForBounds(INT32_MIN, 0, INT32_MAX, 0) == 512);
    assert(battle_camera::FovForBounds(INT32_MIN, INT32_MAX, INT32_MAX, 0) == 600);
    int fov = 466;
    for (int i = 0; i < 12; ++i) {
        int next = battle_camera::ApproachFov(fov, 600);
        assert(next >= fov && next - fov <= 24);
        fov = next;
    }
    assert(fov == 600);
    for (int i = 0; i < 12; ++i) fov = battle_camera::ApproachFov(fov, 466);
    assert(fov == 466);
    CameraCode code;
    CameraCode expected = code;
    expected.init[0xbc / 4] = 0xe30011d2;
    expected.direction[0x74 / 4] = 0xe30011d2;
    expected.init[0x258 / 4] = 124 * 4096;
    expected.view[0x180 / 4] = 124 * 4096;
    assert(code.install(Write));
    assert(writes == 4);
    assert(code.init == expected.init);
    assert(code.direction == expected.direction);
    assert(code.view == expected.view);

    // A mismatch at any site, including the last one, must make zero writes.
    for (int site = 0; site < 5; ++site) {
        CameraCode bad;
        uint32_t *sites[] = {&bad.init[0x254 / 4], &bad.init[0x258 / 4],
                            &bad.init[0xbc / 4], &bad.direction[0x74 / 4],
                            &bad.view[0x180 / 4]};
        *sites[site] ^= 1;
        const CameraCode before = bad;
        writes = 0;
        assert(!bad.install(Write));
        assert(writes == 0);
        assert(bad.init == before.init);
        assert(bad.direction == before.direction);
        assert(bad.view == before.view);
    }
    CameraCode original;
    assert(!original.install(nullptr));
    assert(!battle_camera::Install(0, 0, 0, Write));
    assert(!battle_camera::Install((uintptr_t)original.init.data() | 1,
                                   (uintptr_t)original.direction.data(),
                                   (uintptr_t)original.view.data(), Write));
    puts("Battle camera: framing, unchanged tracking, and signature guards OK");
}
