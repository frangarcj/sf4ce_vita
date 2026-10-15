/* Vita output for the Cricket mixer embedded in the user's Android library.
 * No decoder or audio assets are redistributed by this bridge. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
extern "C" {
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include "so_util.h"
#include "bridge.h"
#include "config.h"
}
#include "audio_bridge.h"
#include <atomic>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

namespace {
constexpr int kFrames = 1024;
constexpr int kRate = 44100;
using Constructor = void (*)(void *, const void *);
using Render = bool (*)(void *, float *, int);
static Constructor system_constructor;
static void (*system_destructor)(void *);
static void *(*dummy_constructor)(void *, bool);
static void (*config_init)(void *, void *, void *, void *);
static int (*ck_init)(void *);
static Render render;
static int (*sound_file_load)(void *, const char *, int);
static void **system_instance;
static void *system_storage;
static uintptr_t system_vtable;
static void *output;
static void *java_vm;
static SceUID audio_thread = -1;
static std::atomic<bool> running{false};

static void SystemInit(const void *config)
{
    if (*system_instance)
        return;
    // Size and field offsets belong to the guarded ARMv7 library build.
    memset(system_storage, 0, 892);
    system_constructor(system_storage, config);
    *(uintptr_t *)system_storage = system_vtable + 8;
    ((uint8_t *)system_storage)[0x49] = 1; // Cortex-A9 supports NEON.
    *(int *)((uint8_t *)system_storage + 0x50) = 21;
    *(void **)((uint8_t *)system_storage + 0x54) = java_vm;
    *system_instance = system_storage;
}
static void SystemShutdown()
{
    if (*system_instance) {
        system_destructor(*system_instance);
        *system_instance = NULL;
    }
}
static int SoundFileLoad(void *sound, const char *filename, int id)
{
    void *core = sound ? *(void **)sound : NULL;
    if (!core) return 0;
    int result = sound_file_load(core, filename, id);
    if (id >= 0 && id < 1516) {
        void *sample = *(void **)((uint8_t *)core + 0x1934 + id * 4);
        if (!sample || options.debug_audio_capture)
            trace("[Audio] sample %s id=%d name=%s\n", sample ? "ready" : "unavailable", id, filename);
    }
    return result;
}
static const char *FilesDir(void *) { return DATA_PATH; }
static int NativeSampleRate() { return kRate; }
static bool PlatformInit() { return true; }
static void Noop() {}

static void WaveHeader(FILE *file, uint32_t samples)
{
    uint8_t h[44] = {};
    memcpy(h, "RIFF", 4); memcpy(h + 8, "WAVEfmt ", 8);
    memcpy(h + 36, "data", 4);
    auto le32 = [&](int at, uint32_t v) {
        for (int i = 0; i < 4; ++i) h[at + i] = (v >> (8 * i)) & 255;
    };
    le32(4, samples * 2 + 36); le32(16, 16);
    h[20] = 1; h[22] = 2;
    le32(24, kRate); le32(28, kRate * 4);
    h[32] = 4; h[34] = 16; le32(40, samples * 2);
    fseek(file, 0, SEEK_SET); fwrite(h, 1, sizeof(h), file);
    fseek(file, 0, SEEK_END);
}

static int OutputThread(SceSize, void *)
{
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kFrames,
                                  kRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        trace("[Audio] failed to open Vita output: 0x%08X\n", port);
        running.store(false);
        return 0;
    }
    int volume[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), volume);
    alignas(16) float mixed[kFrames * 2];
    alignas(16) int16_t pcm[kFrames * 2];
    FILE *capture = options.debug_audio_capture ? fopen(DATA_PATH "/audio-debug.wav", "wb") : NULL;
    uint32_t captured_samples = 0;
    if (capture) WaveHeader(capture, 0);
    unsigned blocks = 0, audible_blocks = 0;
    trace("[Audio] Vita stereo output running: %d Hz, %d frames\n", kRate, kFrames);
    while (running.load()) {
        memset(mixed, 0, sizeof(mixed));
        render(output, mixed, kFrames);
        bool audible = false;
        for (int i = 0; i < kFrames * 2; ++i) {
            float v = mixed[i];
            if (!isfinite(v)) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm[i] = (int16_t)(v * (v < 0 ? 32768.0f : 32767.0f));
            audible |= pcm[i] != 0;
        }
        audible_blocks += audible;
        if (capture) {
            fwrite(pcm, sizeof(pcm), 1, capture);
            captured_samples += kFrames * 2;
            if ((blocks % 128) == 0) { WaveHeader(capture, captured_samples); fflush(capture); }
            if (captured_samples >= kRate * 2 * 180) {
                WaveHeader(capture, captured_samples); fclose(capture); capture = NULL;
            }
        }
        if ((++blocks % 256) == 0 && options.debug_audio_capture)
            trace("[Audio] output blocks=%u audible=%u\n", blocks, audible_blocks);
        if (sceAudioOutOutput(port, pcm) < 0) break;
    }
    if (capture) { WaveHeader(capture, captured_samples); fclose(capture); }
    sceAudioOutOutput(port, NULL);
    sceAudioOutReleasePort(port);
    running.store(false);
    return 0;
}
static void Start(void *self)
{
    if (running.load()) return;
    output = self;
    running.store(true);
    audio_thread = sceKernelCreateThread("sf4ce_audio", OutputThread, 0x40, 64 * 1024, 0, 0, NULL);
    if (audio_thread < 0 || sceKernelStartThread(audio_thread, 0, NULL) < 0) {
        if (audio_thread >= 0) sceKernelDeleteThread(audio_thread);
        audio_thread = -1; running.store(false);
        trace("[Audio] could not start output thread\n");
    }
}
static void Stop(void *)
{
    running.store(false);
    if (audio_thread >= 0) {
        sceKernelWaitThreadEnd(audio_thread, NULL, NULL);
        sceKernelDeleteThread(audio_thread);
        audio_thread = -1;
    }
}
static uintptr_t output_vtable[6];
static void *CreateOutput()
{
    void *memory = malloc(0x210);
    // true selects polling mode, avoiding the original Android worker thread.
    if (!memory) return NULL;
    void *result = dummy_constructor(memory, true);
    memcpy(output_vtable, *(uintptr_t **)result - 2, sizeof(output_vtable));
    output_vtable[4] = (uintptr_t)Start;
    output_vtable[5] = (uintptr_t)Stop;
    *(uintptr_t **)result = output_vtable + 2;
    return result;
}
} // namespace

bool InstallAudioBridge(so_module *module, void *vm)
{
    static const uint8_t expected_build_id[20] = {
        0x52,0x0f,0xc2,0xea,0x2d,0xf8,0x98,0xa6,0x82,0x74,
        0x2a,0x3f,0x9b,0xa7,0xa2,0x2f,0x18,0x5f,0xa9,0xe7
    };
    if (module->build_id_size != sizeof(expected_build_id) ||
        memcmp(module->build_id, expected_build_id, sizeof(expected_build_id)) != 0)
        return false;
    java_vm = vm;
    auto symbol = [&](const char *name) { return so_symbol(module, name); };
    system_constructor = (Constructor)symbol("_ZN3Cki6SystemC2ERK9_CkConfig");
    system_destructor = (void (*)(void *))symbol("_ZN3Cki6SystemD2Ev");
    dummy_constructor = (void *(*)(void *, bool))symbol("_ZN3Cki16GraphOutputDummyC2Eb");
    config_init = (decltype(config_init))symbol("CkConfigInit");
    ck_init = (decltype(ck_init))symbol("CkInit");
    sound_file_load = (decltype(sound_file_load))symbol("_ZN6CSound8FileLoadEPKci");
    render = (Render)symbol("_ZN3Cki11GraphOutput6renderIfEEbPT_i");
    system_instance = (void **)symbol("_ZN3Cki13SystemAndroid10s_instanceE");
    system_storage = (void *)symbol("_ZN3Cki13SystemAndroid5s_memE");
    system_vtable = symbol("_ZTVN3Cki13SystemAndroidE");
    const struct { const char *name; uintptr_t hook; } hooks[] = {
        {"_ZN8SoundObj8FileLoadEPKci", (uintptr_t)SoundFileLoad},
        {"_ZN3Cki12AssetManager4initEP8_jobject", (uintptr_t)Noop},
        {"_ZN3Cki13SystemAndroid4initERK9_CkConfig", (uintptr_t)SystemInit},
        {"_ZN3Cki13SystemAndroid8shutdownEv", (uintptr_t)SystemShutdown},
        {"_ZN3Cki5Audio12platformInitEv", (uintptr_t)PlatformInit},
        {"_ZN3Cki5Audio19getNativeSampleRateEv", (uintptr_t)NativeSampleRate},
        {"_ZN3Cki5Audio16platformShutdownEv", (uintptr_t)Noop},
        {"_ZN3Cki11GraphOutput6createEv", (uintptr_t)CreateOutput},
        {"_ZN3Cki16GraphOutputDummy4pollEv", (uintptr_t)Noop},
    };
    if (!system_constructor || !system_destructor || !dummy_constructor ||
        !config_init || !ck_init || !render || !sound_file_load || !system_instance ||
        !system_storage || !system_vtable)
        return false;
    // Guard field layouts before installing any hooks.
    if ((uintptr_t)system_storage - (uintptr_t)system_instance != 4 ||
        *(uint16_t *)((uintptr_t)dummy_constructor & ~1u) != 0xb5b0)
        return false;
    for (const auto &hook : hooks) if (!symbol(hook.name)) return false;
    for (const auto &hook : hooks) hook_addr(symbol(hook.name), hook.hook);
    // These getters are smaller than a branch trampoline. Replace their GOT
    // entries instead of overwriting adjacent Thumb functions.
    const char *directories[] = {"_ZNK3Cki13SystemAndroid11getFilesDirEv",
                                "_ZNK3Cki13SystemAndroid21getExternalStorageDirEv"};
    for (const char *name : directories)
        so_redirect_plt(module, name, (uintptr_t)FilesDir);
    return true;
}

bool InitAudioBridge()
{
    alignas(8) uint8_t config[0x38] = {};
    config_init(config, NULL, NULL, NULL);
    *(float *)(config + 0x20) = 1000.0f * kFrames / kRate;
    *(int *)(config + 0x34) = 0; // Floating point mixer, rendered by the Vita worker.
    int result = ck_init(config);
    trace("[Audio] Cricket initialization: %s (result=%d)\n", result ? "ready" : "failed", result);
    return result != 0;
}

bool PauseAudioBridgeForMovie()
{
    bool was_running = output && running.load();
    if (output && audio_thread >= 0) Stop(output);
    return was_running;
}

void ResumeAudioBridgeAfterMovie(bool was_running)
{
    if (was_running && output) Start(output);
}
