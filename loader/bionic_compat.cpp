/* Bionic ABI compatibility helpers for the SF4CE Android ARMv7 module.
 *
 * Copyright (C) 2026 SF4CE Vita contributors
 *
 * This software may be modified and distributed under the terms of the MIT
 * license. See the LICENSE file for details.
 *
 * The stat layout follows Android bionic's public ARM stat64 ABI. The pthread
 * and directory-handle strategy follows the MIT-licensed Vita .so loader
 * family originally written by Andy Nguyen.
 */

#include "bionic_compat.h"

extern "C" {
#include <psp2/kernel/threadmgr.h>
#include <vitaGL.h>
}

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bridge.h"
#include "config.h"

extern "C" int ftruncate(int fd, off_t length);
extern "C" int truncate(const char *path, off_t length);
extern "C" char *realpath(const char *path, char *resolved);

typedef struct __attribute__((aligned(8))) BionicStat {
    uint64_t st_dev;
    uint8_t pad0[4];
    uint32_t old_st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint8_t pad3[4];
    int64_t st_size;
    uint32_t st_blksize;
    uint32_t align_blocks;
    uint64_t st_blocks;
    int32_t st_atime_sec;
    int32_t st_atime_nsec;
    int32_t st_mtime_sec;
    int32_t st_mtime_nsec;
    int32_t st_ctime_sec;
    int32_t st_ctime_nsec;
    uint64_t st_ino;
} BionicStat;

static_assert(offsetof(BionicStat, st_size) == 0x30,
              "Android ARM st_size ABI offset changed");
static_assert(sizeof(BionicStat) == 0x68,
              "Android ARM stat ABI size changed");

typedef struct BionicDirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} BionicDirent;

typedef struct BionicDir {
    DIR *native;
    int64_t offset;
    BionicDirent entry;
} BionicDir;

typedef struct VitaAsset {
    FILE *file;
    long length;
} VitaAsset;

static void translate_path(const char *path, char *translated, size_t size)
{
    if (!path || !translated || size == 0)
        return;

    if (!strncmp(path, "ux0:", 4) || !strncmp(path, "ur0:", 4) ||
        !strncmp(path, "app0:", 5)) {
        snprintf(translated, size, "%s", path);
        return;
    }

    while (*path == '/')
        ++path;
    snprintf(translated, size, "%s/%s", DATA_PATH, path);
}

static void convert_stat(const struct stat *source, BionicStat *target)
{
    memset(target, 0, sizeof(*target));
    target->st_dev = source->st_dev;
    target->old_st_ino = (uint32_t)source->st_ino;
    target->st_mode = source->st_mode;
    target->st_nlink = source->st_nlink;
    target->st_uid = source->st_uid;
    target->st_gid = source->st_gid;
    target->st_rdev = source->st_rdev;
    target->st_size = source->st_size;
    target->st_blksize = 4096;
    target->st_blocks = (source->st_size + 511) / 512;
    target->st_atime_sec = source->st_atime;
    target->st_mtime_sec = source->st_mtime;
    target->st_ctime_sec = source->st_ctime;
    target->st_ino = source->st_ino;
}

void bionic_compat_init(void)
{
    trace("[Bionic] compatibility bridge initialized\n");
}

int bionic_stat(const char *path, void *out)
{
    char translated[512];
    struct stat native;
    translate_path(path, translated, sizeof(translated));
    int result = stat(translated, &native);
    if (result == 0 && out)
        convert_stat(&native, (BionicStat *)out);
    return result;
}

int bionic_lstat(const char *path, void *out)
{
    return bionic_stat(path, out);
}

int bionic_fstat(int fd, void *out)
{
    struct stat native;
    int result = fstat(fd, &native);
    if (result == 0 && out)
        convert_stat(&native, (BionicStat *)out);
    return result;
}

void *bionic_opendir(const char *path)
{
    char translated[512];
    translate_path(path, translated, sizeof(translated));
    DIR *native = opendir(translated);
    if (!native)
        return NULL;
    BionicDir *dir = (BionicDir *)calloc(1, sizeof(*dir));
    if (!dir) {
        closedir(native);
        return NULL;
    }
    dir->native = native;
    return dir;
}

void *bionic_readdir(void *opaque)
{
    BionicDir *dir = (BionicDir *)opaque;
    if (!dir || !dir->native) {
        errno = EBADF;
        return NULL;
    }
    struct dirent *native = readdir(dir->native);
    if (!native)
        return NULL;
    memset(&dir->entry, 0, sizeof(dir->entry));
    dir->entry.d_ino = ++dir->offset;
    dir->entry.d_off = dir->offset;
    dir->entry.d_reclen = sizeof(dir->entry);
    strncpy(dir->entry.d_name, native->d_name,
            sizeof(dir->entry.d_name) - 1);
    return &dir->entry;
}

int bionic_closedir(void *opaque)
{
    BionicDir *dir = (BionicDir *)opaque;
    if (!dir) {
        errno = EBADF;
        return -1;
    }
    int result = dir->native ? closedir(dir->native) : 0;
    free(dir);
    return result;
}

int bionic_chdir(const char *path)
{
    (void)path;
    return 0;
}

char *bionic_getcwd(char *buffer, size_t size)
{
    if (!buffer || size <= strlen(DATA_PATH)) {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buffer, DATA_PATH);
    return buffer;
}

int bionic_fchmod(int fd, mode_t mode)
{
    (void)fd;
    (void)mode;
    return 0;
}

int bionic_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    (void)dirfd;
    (void)path;
    (void)mode;
    (void)flags;
    return 0;
}

int bionic_ftruncate(int fd, int64_t length)
{
    return ftruncate(fd, (off_t)length);
}

static int unsupported_fs(void)
{
    errno = ENOSYS;
    return -1;
}

int bionic_link(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    return unsupported_fs();
}

ssize_t bionic_readlink(const char *path, char *buffer, size_t size)
{
    (void)path;
    (void)buffer;
    (void)size;
    return unsupported_fs();
}

int bionic_statfs(const char *path, void *out)
{
    (void)path;
    if (out)
        memset(out, 0, 64);
    return 0;
}

int bionic_truncate(const char *path, int64_t length)
{
    char translated[512];
    translate_path(path, translated, sizeof(translated));
    return truncate(translated, (off_t)length);
}

int bionic_symlink(const char *target, const char *linkpath)
{
    (void)target;
    (void)linkpath;
    return unsupported_fs();
}

long bionic_syscall(long number, ...)
{
    (void)number;
    errno = ENOSYS;
    return -1;
}

int bionic_utimensat(int dirfd, const char *path, const void *times, int flags)
{
    (void)dirfd;
    (void)path;
    (void)times;
    (void)flags;
    return 0;
}

ssize_t bionic_sendfile(int out_fd, int in_fd, void *offset, size_t count)
{
    (void)out_fd;
    (void)in_fd;
    (void)offset;
    (void)count;
    errno = ENOSYS;
    return -1;
}

char *bionic_realpath(const char *path, char *resolved)
{
    char translated[512];
    translate_path(path, translated, sizeof(translated));
    return realpath(translated, resolved);
}

int bionic_strerror_r(int error, char *buffer, size_t size)
{
    const char *message = strerror(error);
    if (!buffer || size == 0)
        return ERANGE;
    snprintf(buffer, size, "%s", message ? message : "Unknown error");
    return 0;
}

int bionic_vasprintf(char **output, const char *format, va_list args)
{
    if (!output || !format) {
        errno = EINVAL;
        return -1;
    }
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0)
        return -1;
    *output = (char *)malloc((size_t)length + 1);
    if (!*output)
        return -1;
    return vsnprintf(*output, (size_t)length + 1, format, args);
}

int bionic_fseeko(FILE *stream, int64_t offset, int whence)
{
    return fseek(stream, (long)offset, whence);
}

int64_t bionic_ftello(FILE *stream)
{
    return ftell(stream);
}

void *bionic_memmem(const void *haystack, size_t haystack_len,
                    const void *needle, size_t needle_len)
{
    if (needle_len == 0)
        return const_cast<void *>(haystack);
    if (!haystack || !needle || needle_len > haystack_len)
        return NULL;
    const uint8_t *h = (const uint8_t *)haystack;
    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (h[i] == *(const uint8_t *)needle &&
            !memcmp(h + i, needle, needle_len))
            return (void *)(h + i);
    }
    return NULL;
}

int bionic_usleep(unsigned int usec)
{
    return sceKernelDelayThread(usec);
}

void bionic_assert2(const char *file, int line, const char *function,
                    const char *expression)
{
    trace("[Bionic] assertion failed: %s:%d %s: %s\n",
          file ? file : "?", line, function ? function : "?",
          expression ? expression : "?");
    abort();
}

extern "C" int __cxa_atexit(void (*)(void *), void *, void *);

int bionic_cxa_thread_atexit_impl(void (*destructor)(void *), void *object,
                                  void *dso_handle)
{
    return __cxa_atexit(destructor, object, dso_handle);
}

void bionic_aeabi_memset(void *dst, size_t len, int value)
{
    memset(dst, value, len);
}

void bionic_aeabi_memset8(void *dst, size_t len, int value)
{
    memset(dst, value, len);
}

static FILE *open_asset_file(const char *filename, char *resolved,
                             size_t resolved_size)
{
    if (!filename || !*filename)
        return NULL;

    const char *relative = filename;
    while (*relative == '/')
        ++relative;

    const char *patterns[] = {
        DATA_PATH "/assets/%s",
        DATA_PATH "/demand_assets/%s",
        DATA_PATH "/%s",
    };

    if (!strncmp(filename, "ux0:", 4) || !strncmp(filename, "app0:", 5)) {
        snprintf(resolved, resolved_size, "%s", filename);
        return fopen(resolved, "rb");
    }

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        snprintf(resolved, resolved_size, patterns[i], relative);
        FILE *file = fopen(resolved, "rb");
        if (file)
            return file;
    }
    return NULL;
}

void *vita_asset_open(const char *filename, int mode, void *opaque)
{
    (void)mode;
    (void)opaque;
    char path[512];
    FILE *file = open_asset_file(filename, path, sizeof(path));
    if (!file) {
        trace("[Asset] open failed: %s\n", filename ? filename : "(null)");
        return NULL;
    }

    VitaAsset *asset = (VitaAsset *)calloc(1, sizeof(*asset));
    if (!asset) {
        fclose(file);
        return NULL;
    }
    asset->file = file;
    fseek(file, 0, SEEK_END);
    asset->length = ftell(file);
    fseek(file, 0, SEEK_SET);
    trace("[Asset] open %s (%ld bytes)\n", path, asset->length);
    return asset;
}

int vita_asset_read(void *opaque, void *buffer, unsigned int size)
{
    VitaAsset *asset = (VitaAsset *)opaque;
    if (!asset || !asset->file)
        return -1;
    return (int)fread(buffer, 1, size, asset->file);
}

long vita_asset_seek(void *opaque, long offset)
{
    VitaAsset *asset = (VitaAsset *)opaque;
    if (!asset || !asset->file || fseek(asset->file, offset, SEEK_SET) != 0)
        return -1;
    return ftell(asset->file);
}

long vita_asset_get_length(void *opaque)
{
    VitaAsset *asset = (VitaAsset *)opaque;
    return asset ? asset->length : -1;
}

int vita_asset_open_fd(void *opaque, long *start, long *length)
{
    VitaAsset *asset = (VitaAsset *)opaque;
    if (!asset || !asset->file)
        return -1;
    if (start)
        *start = 0;
    if (length)
        *length = asset->length;
    /* Assets originating in an APK are commonly compressed, for which
     * AAsset_openFileDescriptor also returns -1. Callers must use read(). */
    return -1;
}

void vita_asset_close(void *opaque)
{
    VitaAsset *asset = (VitaAsset *)opaque;
    if (!asset)
        return;
    if (asset->file)
        fclose(asset->file);
    free(asset);
}

void *vita_ck_alloc(int size, int alignment)
{
    if (size < 0)
        return NULL;
    if (alignment < 4)
        alignment = 4;

    /* Cki stores two words before every returned allocation.  The first is
     * the distance from that header to the allocator's base, in words; the
     * second is the requested size.  Keep that ABI because getSize() and some
     * inline/free paths in the original module inspect it directly. */
    size_t total = (size_t)size + (size_t)alignment + 8;
    uint8_t *base = (uint8_t *)malloc(total);
    if (!base)
        return NULL;

    uintptr_t candidate = (uintptr_t)base + 8;
    uintptr_t aligned = candidate;
    uintptr_t remainder = candidate % (uintptr_t)alignment;
    if (remainder)
        aligned += (uintptr_t)alignment - remainder;
    aligned = (aligned + 3) & ~(uintptr_t)3;

    uint32_t *header = (uint32_t *)(aligned - 8);
    header[0] = (uint32_t)(((uintptr_t)header - (uintptr_t)base) / 4);
    header[1] = (uint32_t)size;
    return (void *)aligned;
}

void vita_ck_free(void *memory)
{
    if (!memory)
        return;
    uint32_t *header = (uint32_t *)memory - 2;
    uint8_t *base = (uint8_t *)header - header[0] * 4;
    free(base);
}

void *vita_ck_realloc(void *memory, unsigned int size)
{
    if (!memory)
        return vita_ck_alloc((int)size, 4);
    if (!size) {
        vita_ck_free(memory);
        return NULL;
    }

    uint32_t old_size = ((uint32_t *)memory)[-1];
    void *replacement = vita_ck_alloc((int)size, 4);
    if (!replacement)
        return NULL;
    memcpy(replacement, memory, old_size < size ? old_size : size);
    vita_ck_free(memory);
    return replacement;
}

void vita_ck_mem_init(void *allocator)
{
    (void)allocator;
}

void vita_ck_mem_shutdown(void)
{
}

void vita_ck_audio_noop(void)
{
}

__attribute__((pcs("aapcs"))) float vita_ck_audio_zero(void *sound)
{
    (void)sound;
    return 0.0f;
}

void glCompressedTexSubImage2D_compat(unsigned int target, int level,
                                      int xoffset, int yoffset, int width,
                                      int height, unsigned int format,
                                      int image_size, const void *data)
{
    static int calls;
    if (calls++ < 24)
        trace("[GL] CompressedTexSubImage2D target=%X level=%d offset=%d,%d %dx%d format=%X size=%d data=%p (unsupported)\n",
              target, level, xoffset, yoffset, width, height, format,
              image_size, data);
}

/* Android/OpenGL GL_UNSIGNED_SHORT_5_5_5_1 (0x8034) packs a pixel as
 * R5 G5 B5 A1 with R in the high bits and A in bit 0. Two empirical facts
 * from testing in Vita3K during combat pin down what the installed vitaGL
 * fast path actually expects:
 *
 *  1. Moving the alpha bit from bit 0 to bit 15 (standard
 *     GL_UNSIGNED_SHORT_1_5_5_5_REV placement) fixed the extreme/incorrect
 *     transparency seen with no conversion at all. Alpha placement alone
 *     was correct in that test — sprites rendered with normal opacity.
 *  2. With alpha only (R,G,B left in original order under it), color was
 *     still wrong: E. Honda's tan skin (R high, B low) rendered solid
 *     blue, while Blanka's green skin (R approx equals B, both low) was
 *     unaffected. Since a pure R/B swap is a no-op exactly when R == B,
 *     this pins the remaining bug down to R and B being swapped.
 *
 * Combining both: alpha at bit 15, and R/G/B in REV order but with R and B
 * swapped relative to the standard RGBA REV layout (i.e. BGRA ordered
 * under the alpha bit). This is a bounded 16-bit-per-pixel repack scoped
 * strictly to this type, done just before the real GL call; it does not
 * touch any other format and does not change VRAM usage. See
 * PORTING_NOTES.md "Problemas conocidos" for the texture corruption this
 * addresses and prior conversions ruled out. */
static void *convert_5551_for_vitagl(const void *pixels, int width, int height)
{
    if (!pixels || width <= 0 || height <= 0)
        return NULL;
    size_t count = (size_t)width * (size_t)height;
    uint16_t *converted = (uint16_t *)malloc(count * sizeof(uint16_t));
    if (!converted) {
        trace("[GL] 5551 convert: malloc failed for %dx%d\n", width, height);
        return NULL;
    }
    const uint16_t *src = (const uint16_t *)pixels;
    for (size_t i = 0; i < count; i++) {
        uint16_t px = src[i];
        uint16_t r = (px >> 11) & 0x1F;
        uint16_t g = (px >> 6) & 0x1F;
        uint16_t b = (px >> 1) & 0x1F;
        uint16_t a = px & 0x1;
        converted[i] = (uint16_t)((a << 15) | (r << 10) | (g << 5) | b);
    }
    return converted;
}

void glTexImage2D_compat(unsigned int target, int level, int internal_format,
                         int width, int height, int border,
                         unsigned int format, unsigned int type,
                         const void *pixels)
{
    static int calls;
    int log_call = calls++ < 48;
    unsigned int sample = 0;
    if (pixels)
        memcpy(&sample, pixels, sizeof(sample));
    int binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    if (log_call)
        trace("[GL] TexImage2D tex=%d target=%X level=%d internal=%X %dx%d border=%d format=%X type=%X pixels=%p first=%08X\n",
              binding,
              target, level, internal_format, width, height, border, format,
              type, pixels, sample);
    void *converted = NULL;
    if (type == GL_UNSIGNED_SHORT_5_5_5_1)
        converted = convert_5551_for_vitagl(pixels, width, height);
    glTexImage2D(target, level, internal_format, width, height, border, format,
                 type, converted ? converted : pixels);
    if (converted)
        free(converted);
    unsigned int error = glGetError();
    if (log_call && error)
        trace("[GL] TexImage2D error=%X\n", error);
}

void glTexSubImage2D_compat(unsigned int target, int level, int xoffset,
                            int yoffset, int width, int height,
                            unsigned int format, unsigned int type,
                            const void *pixels)
{
    static int calls;
    if (calls++ < 48)
        trace("[GL] TexSubImage2D target=%X level=%d offset=%d,%d %dx%d format=%X type=%X pixels=%p\n",
              target, level, xoffset, yoffset, width, height, format, type,
              pixels);
    void *converted = NULL;
    if (type == GL_UNSIGNED_SHORT_5_5_5_1)
        converted = convert_5551_for_vitagl(pixels, width, height);
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type,
                    converted ? converted : pixels);
    if (converted)
        free(converted);
}

__attribute__((pcs("aapcs")))
void glColor4f_compat(float red, float green, float blue, float alpha)
{
    static int calls;
    if (calls++ < 64)
        trace("[GL] Color4f %.3f %.3f %.3f %.3f\n", red, green, blue, alpha);
    glColor4f(red, green, blue, alpha);
}

__attribute__((pcs("aapcs")))
void glTexParameterf_compat(unsigned int target, unsigned int pname, float param)
{
    static int calls;
    if (calls++ < 64)
        trace("[GL] TexParameterf target=%X pname=%X param=%.3f\n",
              target, pname, param);
    glTexParameterf(target, pname, param);
}

void glEnable_compat(unsigned int cap)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] Enable %X\n", cap);
    glEnable(cap);
}

void glDisable_compat(unsigned int cap)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] Disable %X\n", cap);
    glDisable(cap);
}

void glBindBuffer_compat(unsigned int target, unsigned int buffer)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] BindBuffer target=%X buffer=%u\n", target, buffer);
    glBindBuffer(target, buffer);
}

void glBindTexture_compat(unsigned int target, unsigned int texture)
{
    static int calls;
    static int late_calls;
    if (calls++ < 80 || (texture >= 11 && late_calls++ < 120))
        trace("[GL] BindTexture target=%X texture=%u\n", target, texture);
    glBindTexture(target, texture);
}

void glVertexPointer_compat(int size, unsigned int type, int stride,
                            const void *pointer)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] VertexPointer size=%d type=%X stride=%d ptr=%p\n",
              size, type, stride, pointer);
    glVertexPointer(size, type, stride, pointer);
}

void glTexCoordPointer_compat(int size, unsigned int type, int stride,
                              const void *pointer)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] TexCoordPointer size=%d type=%X stride=%d ptr=%p\n",
              size, type, stride, pointer);
    glTexCoordPointer(size, type, stride, pointer);
}

void glDrawArrays_compat(unsigned int mode, int first, int count)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] DrawArrays mode=%X first=%d count=%d\n", mode, first, count);
    glDrawArrays(mode, first, count);
}

void glDrawElements_compat(unsigned int mode, int count, unsigned int type,
                           const void *indices)
{
    static int calls;
    if (calls++ < 80)
        trace("[GL] DrawElements mode=%X count=%d type=%X indices=%p\n",
              mode, count, type, indices);
    glDrawElements(mode, count, type, indices);
}

void glBindFramebuffer_compat(unsigned int target, unsigned int framebuffer)
{
    static int calls;
    if (calls++ < 120)
        trace("[GL] BindFramebuffer target=%X framebuffer=%u\n", target,
              framebuffer);
    glBindFramebuffer(target, framebuffer);
}

void glBindRenderbuffer_compat(unsigned int target, unsigned int renderbuffer)
{
    trace("[GL] BindRenderbuffer target=%X renderbuffer=%u\n", target,
          renderbuffer);
    glBindRenderbuffer(target, renderbuffer);
}

unsigned int glCheckFramebufferStatus_compat(unsigned int target)
{
    unsigned int status = glCheckFramebufferStatus(target);
    trace("[GL] CheckFramebufferStatus target=%X status=%X\n", target, status);
    return status;
}

void glFramebufferTexture2D_compat(unsigned int target, unsigned int attachment,
                                   unsigned int textarget, unsigned int texture,
                                   int level)
{
    trace("[GL] FramebufferTexture2D target=%X attachment=%X texture=%u level=%d\n",
          target, attachment, texture, level);
    glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

void glFramebufferRenderbuffer_compat(unsigned int target,
                                      unsigned int attachment,
                                      unsigned int renderbuffer_target,
                                      unsigned int renderbuffer)
{
    trace("[GL] FramebufferRenderbuffer target=%X attachment=%X renderbuffer=%u\n",
          target, attachment, renderbuffer);
    glFramebufferRenderbuffer(target, attachment, renderbuffer_target,
                              renderbuffer);
}

void glRenderbufferStorage_compat(unsigned int target,
                                  unsigned int internal_format,
                                  int width, int height)
{
    trace("[GL] RenderbufferStorage target=%X internal=%X %dx%d\n", target,
          internal_format, width, height);
    glRenderbufferStorage(target, internal_format, width, height);
}

__attribute__((pcs("aapcs")))
void glLightModelf_compat(unsigned int pname, float param)
{
    glLightModelfv(pname, &param);
}

__attribute__((pcs("aapcs")))
void glLightf_compat(unsigned int light, unsigned int pname, float param)
{
    glLightfv(light, pname, &param);
}

void glLogicOp_compat(unsigned int opcode)
{
    (void)opcode;
}
