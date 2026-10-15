#include "../loader/menu_background.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
static unsigned writes;
static int Write(void *dst, const void *src, size_t n) { ++writes; memcpy(dst, src, n); return 0; }
static int Fail(void *, const void *, size_t) { return -1; }
int main() {
    std::array<uint32_t, 428 / 4> original{};
    original[0] = 0xe92d4800; original[0x14/4] = 0xe3021c70;
    original[0x28/4] = 0x1a000000; original[0xf4/4] = 0xeb09af01; original[0x194/4] = 0xe1a0d00b;
    auto code = original;
    assert(menu_background::Install((uintptr_t)code.data(), Write));
    assert(writes == 1 && code[0x28/4] == 0xea000059);
    assert(0x28 + 8 + (code[0x28/4] & 0xffffff) * 4 == 0x194);
    code[0x28/4] = original[0x28/4]; assert(code == original);
    for (unsigned at : {0, 0x14, 0x28, 0xf4, 0x194}) {
        code = original; code[at/4] ^= 1; auto before = code; writes = 0;
        assert(!menu_background::Install((uintptr_t)code.data(), Write));
        assert(code == before && writes == 0);
    }
    assert(!menu_background::Install(0, Write));
    assert(!menu_background::Install((uintptr_t)original.data() | 1, Write));
    assert(!menu_background::Install((uintptr_t)original.data(), Fail));
    puts("Menu background: guarded optional replay bypass, same native epilogue, incompatible versions rejected OK");
}
