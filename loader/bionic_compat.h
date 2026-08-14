#ifndef SF4CE_BIONIC_COMPAT_H
#define SF4CE_BIONIC_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void bionic_compat_init(void);

int bionic_stat(const char *path, void *out);
int bionic_lstat(const char *path, void *out);
int bionic_fstat(int fd, void *out);
void *bionic_opendir(const char *path);
void *bionic_readdir(void *dir);
int bionic_closedir(void *dir);

int bionic_chdir(const char *path);
char *bionic_getcwd(char *buffer, size_t size);
int bionic_fchmod(int fd, mode_t mode);
int bionic_fchmodat(int dirfd, const char *path, mode_t mode, int flags);
int bionic_ftruncate(int fd, int64_t length);
int bionic_link(const char *oldpath, const char *newpath);
ssize_t bionic_readlink(const char *path, char *buffer, size_t size);
int bionic_statfs(const char *path, void *out);
int bionic_truncate(const char *path, int64_t length);
int bionic_symlink(const char *target, const char *linkpath);
long bionic_syscall(long number, ...);
int bionic_utimensat(int dirfd, const char *path, const void *times, int flags);
ssize_t bionic_sendfile(int out_fd, int in_fd, void *offset, size_t count);
char *bionic_realpath(const char *path, char *resolved);
int bionic_strerror_r(int error, char *buffer, size_t size);
int bionic_vasprintf(char **output, const char *format, va_list args);

int bionic_fseeko(FILE *stream, int64_t offset, int whence);
int64_t bionic_ftello(FILE *stream);
void *bionic_memmem(const void *haystack, size_t haystack_len,
                    const void *needle, size_t needle_len);
int bionic_usleep(unsigned int usec);

void bionic_assert2(const char *file, int line, const char *function,
                    const char *expression);
int bionic_cxa_thread_atexit_impl(void (*destructor)(void *), void *object,
                                  void *dso_handle);
void bionic_aeabi_memset(void *dst, size_t len, int value);
void bionic_aeabi_memset8(void *dst, size_t len, int value);

/* Vita-backed replacement for Cki::AssetManager.  The Android implementation
 * obtains its callbacks from a Java AssetManager, which is not present here. */
void *vita_asset_open(const char *filename, int mode, void *opaque);
int vita_asset_read(void *asset, void *buffer, unsigned int size);
long vita_asset_seek(void *asset, long offset);
long vita_asset_get_length(void *asset);
int vita_asset_open_fd(void *asset, long *start, long *length);
void vita_asset_close(void *asset);

void *vita_ck_alloc(int size, int alignment);
void *vita_ck_realloc(void *memory, unsigned int size);
void vita_ck_free(void *memory);
void vita_ck_mem_init(void *allocator);
void vita_ck_mem_shutdown(void);
void vita_ck_audio_noop(void);
__attribute__((pcs("aapcs"))) float vita_ck_audio_zero(void *sound);

void glCompressedTexSubImage2D_compat(unsigned int target, int level,
                                      int xoffset, int yoffset, int width,
                                      int height, unsigned int format,
                                      int image_size, const void *data);
void glTexImage2D_compat(unsigned int target, int level, int internal_format,
                         int width, int height, int border,
                         unsigned int format, unsigned int type,
                         const void *pixels);
void glTexSubImage2D_compat(unsigned int target, int level, int xoffset,
                            int yoffset, int width, int height,
                            unsigned int format, unsigned int type,
                            const void *pixels);
__attribute__((pcs("aapcs"))) void glColor4f_compat(float red, float green,
                                                     float blue, float alpha);
__attribute__((pcs("aapcs"))) void glTexParameterf_compat(unsigned int target,
                                                           unsigned int pname,
                                                           float param);
void glEnable_compat(unsigned int cap);
void glDisable_compat(unsigned int cap);
void glBindBuffer_compat(unsigned int target, unsigned int buffer);
void glBindTexture_compat(unsigned int target, unsigned int texture);
void glVertexPointer_compat(int size, unsigned int type, int stride,
                            const void *pointer);
void glTexCoordPointer_compat(int size, unsigned int type, int stride,
                              const void *pointer);
void glDrawArrays_compat(unsigned int mode, int first, int count);
void glDrawElements_compat(unsigned int mode, int count, unsigned int type,
                           const void *indices);
void glBindFramebuffer_compat(unsigned int target, unsigned int framebuffer);
void glBindRenderbuffer_compat(unsigned int target, unsigned int renderbuffer);
unsigned int glCheckFramebufferStatus_compat(unsigned int target);
void glFramebufferTexture2D_compat(unsigned int target, unsigned int attachment,
                                   unsigned int textarget, unsigned int texture,
                                   int level);
void glFramebufferRenderbuffer_compat(unsigned int target,
                                      unsigned int attachment,
                                      unsigned int renderbuffer_target,
                                      unsigned int renderbuffer);
void glRenderbufferStorage_compat(unsigned int target,
                                  unsigned int internal_format,
                                  int width, int height);
void glLightModelf_compat(unsigned int pname, float param);
void glLightf_compat(unsigned int light, unsigned int pname, float param);
void glLogicOp_compat(unsigned int opcode);

#ifdef __cplusplus
}
#endif

#endif
