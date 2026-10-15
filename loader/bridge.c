/* bridge.c — JNI callback implementations for SF4CE Vita port
 *
 * The .so calls back into Java through JNI for file I/O, network, etc.
 * We provide Vita-native implementations here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "bridge.h"
#include "config.h"

void traceReset(void)
{
    sceIoRemove(DATA_PATH "/boot.log");
}

void trace(const char *fmt, ...)
{
    char message[768];
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (length < 0)
        return;
    if (length >= (int)sizeof(message))
        length = sizeof(message) - 1;

    if (!options.debug_log && !strstr(message, "ERROR") &&
        !strstr(message, "error") && !strstr(message, "WARNING") &&
        !strstr(message, "warning") && !strstr(message, "FATAL") &&
        !strstr(message, "failed") && !strstr(message, "missing")) {
        static const char *verbose[] = {
            "[Asset]", "[JNI]", "[Format]", "[GL]", "[Text]", "[Game]",
            "[Touch]", "[TouchXform]", "[Input] buttons=", "[TouchCursor]"
        };
        for (size_t i = 0; i < sizeof(verbose) / sizeof(verbose[0]); ++i)
            if (strncmp(message, verbose[i], strlen(verbose[i])) == 0)
                return;
    }
    char line[816];
    int line_length = snprintf(line, sizeof(line), "[t%08X] %s",
                               (unsigned)sceKernelGetThreadId(), message);
    if (line_length < 0)
        return;
    if (line_length >= (int)sizeof(line))
        line_length = sizeof(line) - 1;

    sceClibPrintf("%s", line);
    SceUID fd = sceIoOpen(DATA_PATH "/boot.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0)
    {
        sceIoWrite(fd, line, line_length);
        sceIoClose(fd);
    }
}

// ============================================================
// File I/O
// ============================================================

jni_bytearray *loadFile(char *path)
{
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", DATA_PATH, path);

    FILE *f = fopen(full_path, "rb");
    if (!f)
    {
        // Try assets path
        snprintf(full_path, sizeof(full_path), "%s/assets/%s", DATA_PATH, path);
        f = fopen(full_path, "rb");
    }
    if (!f)
    {
        printf("[Bridge] loadFile failed: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    jni_bytearray *result = (jni_bytearray *)malloc(sizeof(jni_bytearray));
    result->elements = (unsigned char *)malloc(size);
    result->size = (int)size;

    if (fread(result->elements, 1, size, f) != (size_t)size)
    {
        printf("[Bridge] loadFile read error: %s\n", path);
        free(result->elements);
        free(result);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return result;
}

jni_bytearray *loadRawFile(char *path)
{
    // Same as loadFile for now
    return loadFile(path);
}

jni_bytearray *loadAsset(char *path)
{
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/assets/%s", DATA_PATH, path);

    FILE *f = fopen(full_path, "rb");
    if (!f)
    {
        printf("[Bridge] loadAsset failed: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    jni_bytearray *result = (jni_bytearray *)malloc(sizeof(jni_bytearray));
    result->elements = (unsigned char *)malloc(size);
    result->size = (int)size;

    if (fread(result->elements, 1, size, f) != (size_t)size)
    {
        printf("[Bridge] loadAsset read error: %s\n", path);
        free(result->elements);
        free(result);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return result;
}

jni_bytearray *loadAssetPack(char *path)
{
    // For demand-loaded asset packs
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/demand_assets/%s", DATA_PATH, path);

    FILE *f = fopen(full_path, "rb");
    if (!f)
    {
        printf("[Bridge] loadAssetPack failed: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    jni_bytearray *result = (jni_bytearray *)malloc(sizeof(jni_bytearray));
    result->elements = (unsigned char *)malloc(size);
    result->size = (int)size;

    if (fread(result->elements, 1, size, f) != (size_t)size)
    {
        printf("[Bridge] loadAssetPack read error: %s\n", path);
        free(result->elements);
        free(result);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return result;
}

// ============================================================
// Save files
// ============================================================

jni_bytearray *getSaveFileName()
{
    const char *save_path = DATA_PATH "/save.dat";
    jni_bytearray *result = (jni_bytearray *)malloc(sizeof(jni_bytearray));
    result->size = (int)strlen(save_path) + 1;
    result->elements = (unsigned char *)malloc(result->size);
    memcpy(result->elements, save_path, result->size);
    return result;
}

void createSaveFile(size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/save.dat", DATA_PATH);
    FILE *f = fopen(path, "wb");
    if (f)
    {
        unsigned char *buf = (unsigned char *)calloc(1, size);
        fwrite(buf, 1, size, f);
        fclose(f);
        free(buf);
    }
}

// ============================================================
// System queries
// ============================================================

int getCurrentLanguage()
{
    return 0; // English default
}

int isDeviceAndroidTV()
{
    return 0; // Not Android TV
}

uint64_t getCurrentFrame(uint64_t timestamp)
{
    (void)timestamp;
    return 0;
}

// ============================================================
// UI / Font
// ============================================================

jni_intarray *drawFont(char *str, int x, int y, int color)
{
    (void)str;
    (void)x;
    (void)y;
    (void)color;
    // TODO: Implement font rendering
    return NULL;
}

jni_intarray *loadTexture(jni_bytearray *data)
{
    (void)data;
    // TODO: Implement texture loading
    return NULL;
}

// ============================================================
// Network stubs — SF4CE uses HTTP for achievements/leaderboards
// ============================================================

void startHttpConnection(int code, int content_length)
{
    (void)code;
    (void)content_length;
    printf("[Bridge] HTTP connection started (stub)\n");
}

void sendHttpData(jni_bytearray *data)
{
    (void)data;
    printf("[Bridge] HTTP data sent (stub)\n");
}

void finishHttpConnection()
{
    printf("[Bridge] HTTP connection finished (stub)\n");
}

void failHttpConnection(char *tag, char *msg)
{
    (void)tag;
    (void)msg;
    printf("[Bridge] HTTP connection failed (stub)\n");
}

// ============================================================
// Purchase / IAP stubs
// ============================================================

void purchaseResponse(char *response)
{
    (void)response;
    printf("[Bridge] Purchase response (stub): %s\n", response);
}

// ============================================================
// Bluetooth P2P stubs — not available on Vita
// ============================================================

int bluetoothP2PUpdate()
{
    return 0; // No P2P data
}

void bluetoothDisconnect()
{
    // No-op
}

// ============================================================
// WebView stubs
// ============================================================

void openWebView(char *url, char *title)
{
    (void)url;
    (void)title;
    printf("[Bridge] WebView open (stub)\n");
}

void closeWebView()
{
    // No-op
}

// ============================================================
// Debug
// ============================================================

void dPrint(char *msg)
{
    printf("[SF4CE] %s\n", msg);
}
