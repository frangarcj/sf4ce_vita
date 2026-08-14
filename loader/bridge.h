#ifndef __BRIDGE_H__
#define __BRIDGE_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// JNI Type stubs — the .so uses these through the JNI env
// ============================================================

typedef struct
{
    unsigned char *elements;
    int size;
} jni_bytearray;

typedef struct
{
    int *elements;
    int size;
} jni_intarray;

// ============================================================
// File I/O — the .so calls Java for file operations via JNI
// ============================================================

jni_bytearray *loadFile(char *path);
jni_bytearray *loadRawFile(char *path);
jni_bytearray *getSaveFileName();
void createSaveFile(size_t size);

// ============================================================
// Asset loading — SF4CE loads .obm files from assets/obb
// ============================================================

jni_bytearray *loadAsset(char *path);
jni_bytearray *loadAssetPack(char *path);

// ============================================================
// UI / Font rendering — the .so may request text rendering
// ============================================================

jni_intarray *drawFont(char *str, int x, int y, int color);
jni_intarray *loadTexture(jni_bytearray *data);

// ============================================================
// System queries
// ============================================================

int getCurrentLanguage();
int isDeviceAndroidTV();
uint64_t getCurrentFrame(uint64_t timestamp);

// ============================================================
// Network — SF4CE uses HTTP for achievements/leaderboards
// ============================================================

void startHttpConnection(int code, int content_length);
void sendHttpData(jni_bytearray *data);
void finishHttpConnection();
void failHttpConnection(char *tag, char *msg);

// ============================================================
// Purchase / IAP — stubs, not needed for Vita
// ============================================================

void purchaseResponse(char *response);

// ============================================================
// Bluetooth — P2P on Android, stub on Vita
// ============================================================

int bluetoothP2PUpdate();
void bluetoothDisconnect();

// ============================================================
// WebView — stub on Vita
// ============================================================

void openWebView(char *url, char *title);
void closeWebView();

// ============================================================
// Debug
// ============================================================

void dPrint(char *msg);
void traceReset(void);
void trace(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
