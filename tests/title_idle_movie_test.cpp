#include "../loader/title_idle_movie.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
using title_idle_movie::Result;
struct TitleCode {
    std::array<uint32_t, 0x500 / 4> code{};
    TitleCode() {
        code[0x000 / 4] = 0xe92d4800;
        code[0x42c / 4] = 0xe301004c;
        code[0x43c / 4] = 0xe3500f96;
        code[0x440 / 4] = 0xba000039;
        code[0x4c8 / 4] = 0xeb07afa2;
    }
    uintptr_t address() { return (uintptr_t)code.data(); }
};
static int writes;
static int Write(void *dst, const void *src, size_t size) {
    ++writes; memcpy(dst, src, size); return 0;
}
static int Fail(void *, const void *, size_t) { return -1; }
int main() {
    TitleCode title, original;
    assert(title_idle_movie::Install(title.address(), false, Write) == Result::Applied);
    assert(writes == 1);
    for (unsigned i = 0; i < title.code.size(); ++i)
        assert(title.code[i] == (i == 0x440 / 4 ? 0xea000039 : original.code[i]));
    // ARM branch retains its native input-handling destination; fade routines
    // and the touch path are not patched. Tick 600 cannot enter the movie fade.
    assert((original.code[0x440 / 4] & 0x00ffffff) == (title.code[0x440 / 4] & 0x00ffffff));
    assert(0x440 + 8 + (title.code[0x440 / 4] & 0x00ffffff) * 4 == 0x52c);
    for (unsigned offset : {0x000, 0x42c, 0x43c, 0x440, 0x4c8}) {
        TitleCode wrong; wrong.code[offset / 4] ^= 1;
        auto before = wrong.code; writes = 0;
        assert(title_idle_movie::Install(wrong.address(), false, Write) == Result::Unsupported);
        assert(writes == 0 && wrong.code == before);
    }
    writes = 0;
    assert(title_idle_movie::Install(0, true, Write) == Result::PlaybackAvailable);
    assert(title_idle_movie::Install(original.address(), true, Write) == Result::PlaybackAvailable);
    assert(writes == 0 && original.code[0x440 / 4] == 0xba000039);
    assert(title_idle_movie::Install(0, false, Write) == Result::Unsupported);
    assert(title_idle_movie::Install(original.address() | 1, false, Write) == Result::Unsupported);
    assert(title_idle_movie::Install(original.address(), false, nullptr) == Result::Unsupported);
    assert(title_idle_movie::Install(original.address(), false, Fail) == Result::WriteFailed);
    puts("Title idle: guarded branch, native touch destination, available backend and rejection without writes OK");
}
