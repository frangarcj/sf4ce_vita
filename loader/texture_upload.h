#ifndef SF4CE_TEXTURE_UPLOAD_H
#define SF4CE_TEXTURE_UPLOAD_H

#include <stdint.h>
#include <stddef.h>

namespace texture_upload {

// GLES rows include UNPACK_ALIGNMENT padding, including for packed pixels.
inline bool Layout5551(int width, int height, int alignment,
                       size_t &source_stride, size_t &output_bytes)
{
    if (width <= 0 || height <= 0 ||
        (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8))
        return false;
    const size_t w = static_cast<size_t>(width), h = static_cast<size_t>(height);
    if (w > (SIZE_MAX - 7) / 4)
        return false;
    source_stride = (w * 2 + alignment - 1) & ~(size_t)(alignment - 1);
    if (h > SIZE_MAX / source_stride || h > SIZE_MAX / (w * 4))
        return false;
    output_bytes = w * h * 4;
    return true;
}

inline uint8_t Expand5(uint16_t value)
{
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

inline void Convert5551(const void *source, uint8_t *destination,
                        int width, int height, size_t source_stride)
{
    const uint8_t *row = static_cast<const uint8_t *>(source);
    for (int y = 0; y < height; ++y, row += source_stride) {
        for (int x = 0; x < width; ++x) {
            const uint16_t pixel = row[x * 2] | (uint16_t(row[x * 2 + 1]) << 8);
            *destination++ = Expand5((pixel >> 11) & 31);
            *destination++ = Expand5((pixel >> 6) & 31);
            *destination++ = Expand5((pixel >> 1) & 31);
            *destination++ = (pixel & 1) ? 255 : 0;
        }
    }
}

} // namespace texture_upload
#endif
