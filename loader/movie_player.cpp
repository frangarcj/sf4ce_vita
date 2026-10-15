/* Play the user's original H.264/AAC intro with Vita's hardware AV player.
 * The MP4 is optional and is never bundled in the VPK. */
#include "movie_player.h"
#include "audio_bridge.h"
#include "movie_geometry.h"
extern "C" {
#include "bridge.h"
#include "config.h"
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <vitaGL.h>
}
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <pthread.h>

namespace {
constexpr int kBuffers = 5;
static char path[512];
static bool available;
static movie_geometry::Size display_size;
static std::atomic<bool> playing{false};
static SceAvPlayerHandle player;
// Also serialize calls for Vita3K's shared audio/video demuxer.
static pthread_mutex_t decoder_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool audio_enabled;

static void *Allocate(void *, uint32_t alignment, uint32_t size)
{
    if (!size || (alignment && (alignment & (alignment - 1)))) return nullptr;
    return memalign(alignment < 8 ? 8 : alignment, size);
}
static void Deallocate(void *, void *memory) { free(memory); }
static void *AllocateFrame(void *, uint32_t alignment, uint32_t size)
{
    constexpr uint32_t block_alignment = 1024 * 1024;
    if (!size || size > 32 * block_alignment || alignment > block_alignment) return nullptr;
    uint32_t bytes = (size + block_alignment - 1) & ~(block_alignment - 1);
    SceUID block = sceKernelAllocMemBlock("sf4ce_movie", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, bytes, nullptr);
    if (block < 0) return nullptr;
    void *memory = nullptr;
    if (sceKernelGetMemBlockBase(block, &memory) < 0 ||
        sceGxmMapMemory(memory, bytes, SCE_GXM_MEMORY_ATTRIB_RW) < 0) {
        sceKernelFreeMemBlock(block);
        return nullptr;
    }
    return memory;
}
static void DeallocateFrame(void *, void *memory)
{
    if (!memory) return;
    // All submitted video draws have completed before Stop/Close.
    SceUID block = sceKernelFindMemBlockByAddr(memory, 0);
    sceGxmUnmapMemory(memory);
    if (block >= 0) sceKernelFreeMemBlock(block);
}

static int AudioThread(SceSize, void *)
{
    int port = -1, last_rate = 0, last_channels = 0, last_frames = 0;
    uint64_t deadline = 0, blocks = 0, audible = 0;
    while (playing.load()) {
        SceAvPlayerFrameInfo frame = {};
        pthread_mutex_lock(&decoder_mutex);
        bool got = sceAvPlayerIsActive(player) && sceAvPlayerGetAudioData(player, &frame);
        pthread_mutex_unlock(&decoder_mutex);
        if (!got) { sceKernelDelayThread(1000); continue; }
        const auto &audio = frame.details.audio;
        if (!frame.pData || (audio.channelCount != 1 && audio.channelCount != 2) ||
            audio.sampleRate < 8000 || audio.sampleRate > 48000 || !audio.size ||
            audio.size % (2 * audio.channelCount)) continue;
        int frames = audio.size / (2 * audio.channelCount);
        if (frames > 65472) continue;
        if (port < 0 && audio_enabled && frames % 64 == 0) {
            port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, frames,
                audio.sampleRate, audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
            // A missing audio port must still drain and pace the audio stream.
            if (port < 0) audio_enabled = false;
            else {
                int volume[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
                sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), volume);
                trace("[Movie] audio: %u Hz, %u channels, %d frames\n", audio.sampleRate, audio.channelCount, frames);
                last_rate = audio.sampleRate; last_channels = audio.channelCount; last_frames = frames;
            }
        }
        const int16_t *pcm = (const int16_t *)frame.pData;
        bool nonzero = false;
        for (uint32_t i = 0; i < audio.size / 2; ++i) nonzero |= pcm[i] != 0;
        ++blocks; audible += nonzero;
        if (port >= 0) {
            bool configured = true;
            if (last_rate != (int)audio.sampleRate || last_channels != audio.channelCount || last_frames != frames) {
                configured = frames % 64 == 0 && sceAudioOutSetConfig(port, frames, audio.sampleRate,
                    audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO) >= 0;
                last_rate = audio.sampleRate; last_channels = audio.channelCount; last_frames = frames;
            }
            if (configured && sceAudioOutOutput(port, frame.pData) >= 0) continue;
            sceAudioOutReleasePort(port); port = -1; audio_enabled = false;
        }
        uint64_t now = sceKernelGetProcessTimeWide();
        if (!deadline || now > deadline + 100000) deadline = now;
        deadline += uint64_t(frames) * 1000000 / audio.sampleRate;
        if (deadline > now) sceKernelDelayThread(deadline - now);
    }
    if (port >= 0) { sceAudioOutOutput(port, nullptr); sceAudioOutReleasePort(port); }
    trace("[Movie] audio decoded blocks=%llu audible=%llu\n", blocks, audible);
    return 0;
}

// glBegin/glEnd leave the game's client-array pointers untouched. vitaGL's
// attrib stack does not include textures/program/framebuffer/current color.
struct GraphicsState {
    GLint framebuffer, program, matrix, active_texture, texture, environment;
    GLfloat color[4], clear[4];
    GLboolean textured;
    GraphicsState(int width, int height) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_MATRIX_MODE, &matrix);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        glGetFloatv(GL_CURRENT_COLOR, color); glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &environment);
        textured = glIsEnabled(GL_TEXTURE_2D);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glUseProgram(0);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING); glDisable(GL_FOG);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glColor4f(1, 1, 1, 1); glClearColor(0, 0, 0, 1);
        glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrthof(0, width, height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    }
    ~GraphicsState() {
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_TEXTURE); glPopMatrix();
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, environment);
        if (textured) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
        glActiveTexture(active_texture);
        glColor4fv(color); glClearColor(clear[0], clear[1], clear[2], clear[3]);
        glPopAttrib(); glMatrixMode(matrix);
        glUseProgram(program); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    }
};
}

bool InitIntroMovie(bool enabled)
{
    available = false;
    if (!enabled) { trace("[Movie] disabled by configuration\n"); return false; }
    const char *locations[] = {DATA_PATH "/demand_assets/sf4_online.mp4",
        DATA_PATH "/assets/sf4_online.mp4", DATA_PATH "/sf4_online.mp4"};
    for (const char *candidate : locations) {
        FILE *file = fopen(candidate, "rb");
        if (!file) continue;
        unsigned char header[12] = {};
        bool valid = fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header + 4, "ftyp", 4);
        if (valid) display_size = movie_geometry::Read(file);
        fclose(file);
        if (valid) { snprintf(path, sizeof(path), "%s", candidate); available = true; break; }
    }
    if (!available) { trace("[Movie] optional sf4_online.mp4 not found\n"); return false; }
    int result = sceSysmoduleIsLoaded(SCE_SYSMODULE_AVPLAYER);
    if (result < 0) result = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    available = result >= 0;
    trace("[Movie] %s: %s (module=0x%08X)\n", path, available ? "available" : "unavailable", result);
    if (display_size.width) trace("[Movie] visible dimensions: %ux%u\n", display_size.width, display_size.height);
    return available;
}
bool IntroMovieAvailable() { return available; }
bool IntroMoviePlaying() { return playing.load(); }

bool PlayIntroMovie(int width, int height, bool audio, bool (*debug_skip)())
{
    if (!available || playing.load()) return false;
    SceAvPlayerInitData init = {};
    init.memoryReplacement.allocate = Allocate;
    init.memoryReplacement.deallocate = Deallocate;
    init.memoryReplacement.allocateTexture = AllocateFrame;
    init.memoryReplacement.deallocateTexture = DeallocateFrame;
    init.basePriority = 0xA0; init.numOutputVideoFrameBuffers = kBuffers;
    init.autoStart = SCE_TRUE;
    player = sceAvPlayerInit(&init);
    // Firmware handles are opaque pointers and may have the high bit set.
    // A signed-negative test incorrectly rejects valid 0x8xxxxxxx handles.
    if (!player || (uint32_t(player) & 0xffff0000u) == 0x806a0000u) {
        available = false; trace("[Movie] init failed: 0x%08X\n", player); return false;
    }
    bool resume_audio = PauseAudioBridgeForMovie();
    bool decoded = false, skipped = false, failed = false;
    uint64_t started = sceKernelGetProcessTimeWide(), last_frame = started, draws = 0;
    trace("[Movie] playing %s\n", path);
    {
        GraphicsState graphics(width, height);
        GLuint textures[kBuffers] = {};
        SceGxmTexture *descriptors[kBuffers] = {};
        void *owned_pixels[kBuffers] = {};
        glGenTextures(kBuffers, textures);
        for (int i = 0; i < kBuffers; ++i) {
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            descriptors[i] = vglGetGxmTexture(GL_TEXTURE_2D);
            owned_pixels[i] = vglGetTexDataPointer(GL_TEXTURE_2D);
            if (!descriptors[i] || !owned_pixels[i]) failed = true;
        }
        int result = failed ? -1 : sceAvPlayerAddSource(player, path);
        SceUID audio_thread = -1;
        if (result < 0) { trace("[Movie] add source failed: 0x%08X\n", result); failed = true; }
        else {
            playing.store(true); audio_enabled = audio;
            audio_thread = sceKernelCreateThread("sf4ce_movie_audio", AudioThread, 0x40, 64 * 1024, 0, 0, nullptr);
            if (audio_thread < 0 || sceKernelStartThread(audio_thread, 0, nullptr) < 0) {
                if (audio_thread >= 0) sceKernelDeleteThread(audio_thread);
                audio_thread = -1; failed = true;
                trace("[Movie] could not start audio drain thread\n");
            }
        }
        int index = 0;
        float x = 0, y = 0, w = width, h = height, u = 1, v = 1;
        while (!failed) {
            SceCtrlData pad = {}; SceTouchData touch = {};
            bool skip = debug_skip && debug_skip();
            if (sceCtrlPeekBufferPositiveExt2(0, &pad, 1) > 0)
                skip |= (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) != 0;
            if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) skip |= touch.reportNum > 0;
            if (skip) { skipped = true; break; }
            SceAvPlayerFrameInfo frame = {};
            pthread_mutex_lock(&decoder_mutex);
            bool active = sceAvPlayerIsActive(player);
            bool got = active && sceAvPlayerGetVideoData(player, &frame);
            pthread_mutex_unlock(&decoder_mutex);
            uint64_t now = sceKernelGetProcessTimeWide();
            if (!active && decoded) break;
            if (got && frame.pData) {
                unsigned vw = frame.details.video.width, vh = frame.details.video.height;
                if (!vw || !vh || vw > 1920 || vh > 1088 || (vw & 1) || (vh & 1)) { failed = true; break; }
                index = (index + 1) % kBuffers;
                if (sceGxmTextureInitLinear(descriptors[index], frame.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1, vw, vh, 0) < 0) { failed = true; break; }
                sceGxmTextureSetMinFilter(descriptors[index], SCE_GXM_TEXTURE_FILTER_LINEAR);
                sceGxmTextureSetMagFilter(descriptors[index], SCE_GXM_TEXTURE_FILTER_LINEAR);
                auto visible = movie_geometry::Visible(display_size, {vw, vh});
                float aspect = visible.aspect;
                u = visible.u; v = visible.v;
                w = width; h = w / aspect;
                if (h > height) { h = height; w = h * aspect; }
                x = (width - w) * 0.5f; y = (height - h) * 0.5f;
                if (!decoded) trace("[Movie] first frame: %ux%u, aspect=%.4f\n", vw, vh, aspect);
                decoded = true; last_frame = now;
            }
            if (now - last_frame > 10000000) { trace("[Movie] video decode timeout\n"); failed = true; break; }
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
            glClear(GL_COLOR_BUFFER_BIT);
            if (decoded) {
                glBindTexture(GL_TEXTURE_2D, textures[index]);
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex3f(x, y, 0);
                glTexCoord2f(u, 0); glVertex3f(x + w, y, 0);
                glTexCoord2f(0, v); glVertex3f(x, y + h, 0);
                glTexCoord2f(u, v); glVertex3f(x + w, y + h, 0);
                glEnd(); ++draws;
            }
            vglSwapBuffers(GL_FALSE);
            // Also bound polling if swaps are configured without v-sync.
            uint64_t elapsed = sceKernelGetProcessTimeWide() - now;
            if (elapsed < 16000) sceKernelDelayThread(16000 - elapsed);
        }
        playing.store(false);
        if (audio_thread >= 0) { sceKernelWaitThreadEnd(audio_thread, nullptr, nullptr); sceKernelDeleteThread(audio_thread); }
        glFinish();
        // vitaGL owns only the tiny original allocations, never AVPlayer's
        // decoded frames. Restore descriptors before deleting GL textures.
        for (int i = 0; i < kBuffers; ++i)
            if (descriptors[i] && owned_pixels[i])
                sceGxmTextureInitLinear(descriptors[i], owned_pixels[i], SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 8, 8, 0);
        glDeleteTextures(kBuffers, textures);
        sceAvPlayerStop(player); sceAvPlayerClose(player);
    }
    ResumeAudioBridgeAfterMovie(resume_audio);
    available = !failed && (decoded || skipped);
    trace("[Movie] %s after %.3f s, draws=%llu; returning to native title\n",
        failed ? "failed" : skipped ? "skipped" : "completed", (sceKernelGetProcessTimeWide() - started) / 1000000.0, draws);
    return available;
}
