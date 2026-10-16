#ifndef SF4CE_SHARED_FILE_BUFFER_H
#define SF4CE_SHARED_FILE_BUFFER_H

#include <string.h>

// GlTest2Renderer.FileLoad/LocalFileLoad share one resident data ByteBuffer.
// This is metadata only: the bytes remain in the existing 32 MiB JNI buffer.
namespace shared_file_buffer {
class Cache {
    char name[512] = {};
    int size = 0;
public:
    void Reset() { name[0] = 0; size = 0; }
    int Find(const char *filename) const {
        return filename && name[0] && !strcmp(name, filename) ? size : -1;
    }
    void Remember(const char *filename, int amount, long expected) {
        Reset();
        if (!filename || !*filename || amount <= 0 || amount != expected ||
            strlen(filename) >= sizeof(name)) return;
        strcpy(name, filename);
        size = amount;
    }
};
}
#endif
