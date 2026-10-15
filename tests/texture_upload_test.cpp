#include "../loader/texture_upload.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

int main()
{
    // Every possible packed value, with unaligned input, preserves all channels.
    std::vector<uint8_t> input(65536 * 2 + 1), output(65536 * 4);
    for (unsigned p = 0; p < 65536; ++p) {
        input[p * 2 + 1] = p & 255;
        input[p * 2 + 2] = p >> 8;
    }
    texture_upload::Convert5551(input.data() + 1, output.data(), 65536, 1, 131072);
    for (unsigned p = 0; p < 65536; ++p) {
        const unsigned shifts[] = {11, 6, 1};
        for (int c = 0; c < 3; ++c) {
            unsigned v = (p >> shifts[c]) & 31;
            assert((output[p * 4 + c] >> 3) == v);
            const int error = int(output[p * 4 + c]) - int((v * 255 + 15) / 31);
            assert(error >= -1 && error <= 1);
        }
        assert(output[p * 4 + 3] == ((p & 1) ? 255 : 0));
    }
    // Two odd-width rows with padding: do not treat padding as image data.
    for (int alignment : {1, 2, 4, 8}) {
        size_t stride, bytes;
        assert(texture_upload::Layout5551(3, 2, alignment, stride, bytes));
        assert(bytes == 24);
        std::vector<uint8_t> padded(stride * 2, 0xa5);
        const uint16_t colors[] = {0xf801, 0x07c1, 0x003f};
        for (int y = 0; y < 2; ++y)
            memcpy(padded.data() + y * stride, colors, sizeof(colors));
        std::array<uint8_t, 24> result{};
        texture_upload::Convert5551(padded.data(), result.data(), 3, 2, stride);
        const uint8_t expected[] = {255,0,0,255, 0,255,0,255, 0,0,255,255};
        assert(memcmp(result.data(), expected, 12) == 0);
        assert(memcmp(result.data() + 12, expected, 12) == 0);
    }
    size_t stride, bytes;
    assert(!texture_upload::Layout5551(0, 10, 4, stride, bytes));
    assert(!texture_upload::Layout5551(-1, 10, 4, stride, bytes));
    assert(!texture_upload::Layout5551(3, 2, 3, stride, bytes));
    puts("5551 uploads: all colors/alpha, unaligned sources and padded rows OK");
}
