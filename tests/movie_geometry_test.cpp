#include "../loader/movie_geometry.h"
#include <cassert>
#include <cmath>
#include <vector>
using Bytes = std::vector<unsigned char>;
static void Put(Bytes &v, unsigned at, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) v[at + i] = x >> (24 - i * 8);
}
static Bytes Box(const char *type, Bytes body, bool extended = false) {
    Bytes out(extended ? 16 : 8); Put(out, 0, extended ? 1 : out.size() + body.size());
    memcpy(out.data() + 4, type, 4); if (extended) Put(out, 12, 16 + body.size());
    out.insert(out.end(), body.begin(), body.end()); return out;
}
static Bytes Track(unsigned version, unsigned w, unsigned h) {
    Bytes tkhd(version ? 96 : 84); tkhd[0] = version;
    Put(tkhd, tkhd.size() - 8, w << 16); Put(tkhd, tkhd.size() - 4, h << 16);
    return Box("trak", Box("tkhd", tkhd));
}
static movie_geometry::Size Read(const Bytes &data) {
    FILE *f = tmpfile(); assert(f); assert(fwrite(data.data(), 1, data.size(), f) == data.size());
    auto size = movie_geometry::Read(f); fclose(f); return size;
}
int main(int argc, char **argv) {
    for (unsigned version = 0; version < 2; ++version) {
        auto tracks = Track(version, 0, 0); // audio tkhd must be skipped
        auto video = Track(version, 960, 540); tracks.insert(tracks.end(), video.begin(), video.end());
        auto mp4 = Box("mdat", Bytes(1024)); auto moov = Box("moov", tracks, true);
        mp4.insert(mp4.end(), moov.begin(), moov.end());
        auto size = Read(mp4); assert(size.width == 960 && size.height == 540);
        auto visible = movie_geometry::Visible(size, {960, 544});
        assert(fabsf(visible.aspect - 16.0f / 9) < 1e-6f && visible.u == 1);
        assert(fabsf(visible.v - 540.0f / 544) < 1e-6f);
        // Every truncation is rejected, including 64-bit headers and tkhd tails.
        for (unsigned i = 0; i < mp4.size(); ++i) assert(!Read(Bytes(mp4.begin(), mp4.begin() + i)).width);
        Put(mp4, 0, 4); assert(!Read(mp4).width); // atom smaller than header
    }
    assert(!Read(Box("moov", Box("trak", Box("tkhd", Bytes(85))))).width);
    Bytes overflow(16); Put(overflow, 0, 1); Put(overflow, 8, 0xffffffff); Put(overflow, 12, 0xffffffff);
    assert(!Read(overflow).width);
    auto fallback = movie_geometry::Visible({1920, 1080}, {960, 544});
    assert(fallback.u == 1 && fallback.v == 1); // never read outside a decoded surface
    fallback = movie_geometry::Visible({960, 500}, {960, 544});
    assert(fallback.v == 1); // do not guess a large crop
    assert(!movie_geometry::Read(nullptr).width);
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb"); assert(file);
        auto size = movie_geometry::Read(file); fclose(file);
        assert(size.width == 960 && size.height == 540);
        printf("Original MP4 visible dimensions: %ux%u\n", size.width, size.height);
    }
    puts("Movie geometry: MP4 v0/v1, audio-track skip, extended boxes, truncation/overflow and padded-surface crop OK");
}
