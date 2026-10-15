#ifndef SF4CE_MOVIE_GEOMETRY_H
#define SF4CE_MOVIE_GEOMETRY_H
#include <cstdio>
#include <cstdint>
#include <climits>
#include <cstring>
namespace movie_geometry {
struct Size { uint32_t width = 0, height = 0; };
inline uint32_t Big32(const unsigned char *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
// Read only bounded moov/trak/tkhd boxes. tkhd retains the visible dimensions
// when AVPlayer reports macroblock-padded surfaces (540 -> 544 rows).
inline Size Boxes(FILE *file, uint64_t begin, uint64_t end, unsigned depth) {
    if (depth > 2 || end > LONG_MAX) return {};
    for (uint64_t at = begin; at < end;) {
        unsigned char box[16];
        if (end - at < 8 || fseek(file, long(at), SEEK_SET) || fread(box, 1, 8, file) != 8) return {};
        uint64_t bytes = Big32(box), header = 8;
        if (bytes == 1) {
            if (end - at < 16 || fread(box + 8, 1, 8, file) != 8) return {};
            bytes = uint64_t(Big32(box + 8)) << 32 | Big32(box + 12); header = 16;
        } else if (!bytes) bytes = end - at;
        if (bytes < header || bytes > end - at) return {};
        if ((!memcmp(box + 4, "moov", 4) && depth == 0) ||
            (!memcmp(box + 4, "trak", 4) && depth == 1)) {
            Size found = Boxes(file, at + header, at + bytes, depth + 1);
            if (found.width) return found;
        } else if (!memcmp(box + 4, "tkhd", 4) && depth == 2) {
            unsigned char data[96];
            size_t length = bytes - header;
            if ((length == 84 || length == 96) && !fseek(file, long(at + header), SEEK_SET) &&
                fread(data, 1, length, file) == length &&
                ((data[0] == 0 && length == 84) || (data[0] == 1 && length == 96))) {
                uint32_t w = Big32(data + length - 8), h = Big32(data + length - 4);
                if (!(w & 65535) && !(h & 65535) && (w >> 16) >= 2 && (w >> 16) <= 1920 &&
                    (h >> 16) >= 2 && (h >> 16) <= 1088)
                    return {w >> 16, h >> 16};
            }
        }
        at += bytes;
    }
    return {};
}
inline Size Read(FILE *file) {
    if (!file || fseek(file, 0, SEEK_END)) return {};
    long length = ftell(file);
    return length > 0 ? Boxes(file, 0, uint64_t(length), 0) : Size{};
}
struct View { float aspect, u, v; };
inline View Visible(Size display, Size surface) {
    if (display.width && display.height && display.width <= surface.width && display.height <= surface.height &&
        surface.width - display.width < 16 && surface.height - display.height < 16)
        return {float(display.width) / display.height, float(display.width) / surface.width, float(display.height) / surface.height};
    return {surface.height ? float(surface.width) / surface.height : 1.0f, 1, 1};
}
}
#endif
