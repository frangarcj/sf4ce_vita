/* main.cpp — Street Fighter IV CE .so loader for PS Vita
 *
 * Loads libGltest2jni.so (ARMv7) and provides a fake JNI environment
 * so the Android native library can run on Vita via vitaGL.
 *
 * Based on the FF3 Vita port architecture.
 */

extern "C"
{
#include <kubridge.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/power.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <vitaGL.h>
#include <vitashark.h>

#include <OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <psp2/kernel/processmgr.h>
#include <zlib.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <inttypes.h>
#include <malloc.h>
#include <sched.h>
#include <wchar.h>
#include <wctype.h>
#include <iconv.h>

extern "C"
{
#include "bridge.h"
#include "bionic_compat.h"
#include "config.h"
#include "so_util.h"
}

/* Vita3K buffers guest stdout aggressively; keep the boot diagnostics in a
 * file that survives a guest crash. */
#define printf trace

#include "softfp_wrappers.hpp"
#include "battle_camera.h"
#include "menu_pointer.h"
#include "shared_file_buffer.h"
#include "audio_bridge.h"
#include "movie_player.h"
#include "debug_replay.h"
#include "frame_timing.h"
#include "title_idle_movie.h"
#include "menu_background.h"
#include "render_interpolation.h"
#include "asset_pack.h"

// ============================================================
// Globals
// ============================================================

int SCREEN_W = DEF_SCREEN_W;
int SCREEN_H = DEF_SCREEN_H;
config_opts options;

static void set_default_options(void)
{
    options.res = 0;
    options.bilinear = 0;
    options.msaa = 2;
    options.postfx = 0;
    options.battle_camera = 1;
    options.debug_touch_cursor = 0;
    options.debug_input_replay = 0;
    options.audio = 1;
    options.debug_log = 0;
    options.debug_audio_capture = 0;
    options.smooth_rendering = 1;
    options.debug_timing = 0;
    options.intro_movie = 1;
}

void loadOptions()
{
    char line[128];
    char key[30];
    int value = 0;

    set_default_options();

    FILE *f = fopen(CONFIG_FILE_PATH, "rb");
    if (f)
    {
        while (fgets(line, sizeof(line), f))
        {
            if (sscanf(line, "%29[^=]=%d", key, &value) != 2)
                continue;

            if (strcmp("resolution", key) == 0)
                options.res = value;
            else if (strcmp("bilinear", key) == 0)
                options.bilinear = value;
            else if (strcmp("antialiasing", key) == 0)
                options.msaa = value;
            else if (strcmp("postfx", key) == 0)
                options.postfx = value;
            else if (strcmp("battle_camera", key) == 0)
                options.battle_camera = value == 0 ? 0 : 1;
            else if (strcmp("debug_log", key) == 0)
                options.debug_log = value == 1;
            else if (strcmp("debug_input_replay", key) == 0)
                options.debug_input_replay = value == 1;
            else if (strcmp("audio", key) == 0)
                options.audio = value == 1;
            else if (strcmp("debug_audio_capture", key) == 0)
                options.debug_audio_capture = value == 1;
            else if (strcmp("smooth_rendering", key) == 0)
                options.smooth_rendering = value == 1;
            else if (strcmp("debug_timing", key) == 0)
                options.debug_timing = value == 1;
            else if (strcmp("intro_movie", key) == 0)
                options.intro_movie = value == 1;
            else if (strcmp("debug_touch_cursor", key) == 0)
                options.debug_touch_cursor = value == 1 ? 1 : 0;
        }
        fclose(f);
    }

    switch (options.res)
    {
    case 0:
        SCREEN_W = 960;
        SCREEN_H = 544;
        break;
    case 1:
        SCREEN_W = 1280;
        SCREEN_H = 725;
        break;
    case 2:
        SCREEN_W = 1920;
        SCREEN_H = 1088;
        break;
    }
}

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;
int _opensles_user_freq = 32000;

static so_module sf4ce_mod;
static so_module cxx_mod;

// ============================================================
// JNI Buffers — allocated as direct byte buffers like Android
// ============================================================

static uint8_t *g_data_buf = NULL;    // 32 MB
static shared_file_buffer::Cache g_file_buffer_cache;
static uint8_t *g_send_buf = NULL;    // 4800 bytes
static uint8_t *g_receive_buf = NULL; // 4800 bytes
static uint8_t *g_text_pixels = NULL;
static int g_text_width = 0;
static int g_text_height = 0;
static FT_Library g_ft_library = NULL;
static FT_Face g_ft_face = NULL;

static bool InitTextFont(void)
{
    if (g_ft_face)
        return true;
    if (!g_ft_library && FT_Init_FreeType(&g_ft_library))
        return false;
    static const char *font_paths[] = {
        "vs0:data/external/font/pvf/n023055ms.ttf",
        "vs0:data/external/font/pvf/n023055ts.ttf",
    };
    for (unsigned int i = 0; i < sizeof(font_paths) / sizeof(font_paths[0]); ++i)
    {
        if (!FT_New_Face(g_ft_library, font_paths[i], 0, &g_ft_face))
        {
            trace("[Font] loaded %s\n", font_paths[i]);
            return true;
        }
    }
    trace("[Font] could not open the Vita system font\n");
    return false;
}

static uint32_t NextUtf8Codepoint(const char **cursor)
{
    const unsigned char *s = (const unsigned char *)*cursor;
    if (!*s)
        return 0;
    uint32_t codepoint;
    int extra;
    if (s[0] < 0x80) {
        codepoint = s[0];
        extra = 0;
    } else if ((s[0] & 0xe0) == 0xc0) {
        codepoint = s[0] & 0x1f;
        extra = 1;
    } else if ((s[0] & 0xf0) == 0xe0) {
        codepoint = s[0] & 0x0f;
        extra = 2;
    } else if ((s[0] & 0xf8) == 0xf0) {
        codepoint = s[0] & 0x07;
        extra = 3;
    } else {
        *cursor += 1;
        return 0xfffd;
    }
    for (int i = 1; i <= extra; ++i) {
        if ((s[i] & 0xc0) != 0x80) {
            *cursor += 1;
            return 0xfffd;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3f);
    }
    *cursor += extra + 1;
    return codepoint;
}

// Android's KeyBord/KeyBordBase activities exchange UTF-16 code units with
// the native game through CharGet/CharNumGet/CharFinishGet.  Keep the same
// asynchronous contract while using Vita's system IME.
static SceWChar16 g_ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static SceWChar16 g_ime_initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static SceWChar16 g_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint16_t g_char_table[48];
static int g_char_count = 0;
static int g_char_finished = 0;
static bool g_ime_active = false;
static bool g_ime_is_name = false;
static bool g_initial_name_expected = false;
static int g_ime_max_chars = 12;
static int g_google_play_state = 0;

static int Utf8ToUtf16(const char *source, SceWChar16 *destination,
                       int capacity)
{
    if (!destination || capacity <= 0)
        return 0;
    int written = 0;
    const char *cursor = source ? source : "";
    while (*cursor && written + 1 < capacity)
    {
        uint32_t codepoint = NextUtf8Codepoint(&cursor);
        if (codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            codepoint = 0xfffd;
        if (codepoint <= 0xffff)
        {
            destination[written++] = (SceWChar16)codepoint;
        }
        else if (written + 2 < capacity)
        {
            codepoint -= 0x10000;
            destination[written++] = (SceWChar16)(0xd800 + (codepoint >> 10));
            destination[written++] = (SceWChar16)(0xdc00 + (codepoint & 0x3ff));
        }
        else
        {
            break;
        }
    }
    destination[written] = 0;
    return written;
}

static void CompleteIme(const SceWChar16 *text)
{
    memset(g_char_table, 0, sizeof(g_char_table));
    int output = 0;
    for (int input = 0; text && text[input] && input < g_ime_max_chars &&
                        output < (int)(sizeof(g_char_table) /
                                     sizeof(g_char_table[0]));
         ++input)
    {
        uint16_t character = text[input];
        if (character == '\n' || character == '\t')
            continue;
        g_char_table[output++] = character;
        if (output == 25 && output < (int)(sizeof(g_char_table) /
                                           sizeof(g_char_table[0])))
            g_char_table[output++] = '\n';
    }
    g_char_count = output;
    g_char_finished = 1;

    /* Closing the system IME only returns the text to KeyBoardObj.  The game
     * still has its own OK button and, on Android, validates the name through
     * an asynchronous network request.  Do not mark first-run setup complete
     * here: doing so made Cancel/Return look like a successful confirmation. */
}

static void OpenIme(const char *initial_text, int max_chars, bool is_name,
                    bool numeric)
{
    g_char_count = 0;
    g_char_finished = 0;
    memset(g_char_table, 0, sizeof(g_char_table));
    if (g_ime_active)
        return;

    if (max_chars <= 0)
        max_chars = 1;
    if (max_chars > 40)
        max_chars = 40;
    g_ime_max_chars = max_chars;
    g_ime_is_name = is_name;
    memset(g_ime_title, 0, sizeof(g_ime_title));
    memset(g_ime_initial, 0, sizeof(g_ime_initial));
    memset(g_ime_input, 0, sizeof(g_ime_input));
    Utf8ToUtf16(is_name ? "Player name" : "Text", g_ime_title,
                SCE_IME_DIALOG_MAX_TITLE_LENGTH);
    Utf8ToUtf16(initial_text, g_ime_initial, max_chars + 1);

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001ffff;
    param.languagesForced = SCE_TRUE;
    param.type = numeric ? SCE_IME_TYPE_NUMBER : SCE_IME_TYPE_DEFAULT;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = g_ime_title;
    param.maxTextLength = (SceUInt32)max_chars;
    param.initialText = g_ime_initial;
    param.inputTextBuffer = g_ime_input;
    int result = sceImeDialogInit(&param);
    if (result < 0)
    {
        trace("[IME] sceImeDialogInit failed: 0x%08X\n", result);
        CompleteIme(g_ime_initial);
        return;
    }
    g_ime_active = true;
    trace("[IME] opened %s entry (max=%d)\n",
          is_name ? "name" : "text", max_chars);
}

static void UpdateIme(void)
{
    if (!g_ime_active ||
        sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return;

    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    const SceWChar16 *text = result.button == SCE_IME_DIALOG_BUTTON_ENTER
                                 ? g_ime_input
                                 : g_ime_initial;
    CompleteIme(text);
    sceImeDialogTerm();
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    g_ime_active = false;
    trace("[IME] %s (%d UTF-16 units)\n",
          result.button == SCE_IME_DIALOG_BUTTON_ENTER ? "accepted" : "cancelled",
          g_char_count);
}

static void DrawTextToCanvas(const char *string, int x, int y, int red,
                             int green, int blue, int alpha, int size)
{
    if (!string || !g_text_pixels || size <= 0 || !InitTextFont())
        return;
    FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)size);
    int pen_x = x;
    int baseline = y + (int)(g_ft_face->size->metrics.ascender >> 6);
    const char *cursor = string;
    while (*cursor)
    {
        uint32_t codepoint = NextUtf8Codepoint(&cursor);
        if (FT_Load_Char(g_ft_face, codepoint, FT_LOAD_RENDER))
            continue;
        FT_GlyphSlot glyph = g_ft_face->glyph;
        FT_Bitmap *bitmap = &glyph->bitmap;
        int origin_x = pen_x + glyph->bitmap_left;
        int origin_y = baseline - glyph->bitmap_top;
        for (unsigned int row = 0; row < bitmap->rows; ++row)
        {
            int dst_y = origin_y + (int)row;
            if (dst_y < 0 || dst_y >= g_text_height)
                continue;
            const unsigned char *source = bitmap->buffer + row * bitmap->pitch;
            for (unsigned int column = 0; column < bitmap->width; ++column)
            {
                int dst_x = origin_x + (int)column;
                if (dst_x < 0 || dst_x >= g_text_width)
                    continue;
                unsigned int coverage = source[column];
                unsigned int source_alpha = coverage * (unsigned int)alpha / 255;
                uint8_t *pixel = g_text_pixels +
                    ((size_t)dst_y * g_text_width + dst_x) * 4;
                unsigned int inverse = 255 - source_alpha;
                pixel[0] = (uint8_t)((red * source_alpha + pixel[0] * inverse) / 255);
                pixel[1] = (uint8_t)((green * source_alpha + pixel[1] * inverse) / 255);
                pixel[2] = (uint8_t)((blue * source_alpha + pixel[2] * inverse) / 255);
                pixel[3] = (uint8_t)(source_alpha + pixel[3] * inverse / 255);
            }
        }
        pen_x += (int)(glyph->advance.x >> 6);
    }
}

static void MeasureText(const char *string, int size, int *width, int *height)
{
    *width = 0;
    *height = size;
    if (!string || size <= 0 || !InitTextFont())
        return;
    FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)size);
    const char *cursor = string;
    while (*cursor)
    {
        uint32_t codepoint = NextUtf8Codepoint(&cursor);
        if (!FT_Load_Char(g_ft_face, codepoint, FT_LOAD_DEFAULT))
            *width += (int)(g_ft_face->glyph->advance.x >> 6);
    }
    *height = (int)(g_ft_face->size->metrics.height >> 6);
}

// Touch input buffer — 5 touches × 8 shorts = 40 shorts
static short g_touch_tbl[TOUCH_TBL_SIZE];
static DebugInput g_debug_input;
static bool g_debug_input_active;
static DebugReplay *g_movie_replay;
static uintptr_t g_title_system;

static bool MovieReplaySkip()
{
    if (!options.debug_input_replay || !g_movie_replay) return false;
    static uint64_t next = 0;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (now < next) return false;
    next = now + 50000;
    DebugInput input;
    g_movie_replay->Step(DATA_PATH "/debug-input.txt", input);
    return input.touch || (input.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START));
}

static void GuardUnavailableIdleMovie()
{
    auto result = title_idle_movie::Install(g_title_system, IntroMovieAvailable(), &kuKernelCpuUnrestrictedMemcpy);
    if (result == title_idle_movie::Result::Applied) {
        trace("[Title] Idle intro video disabled: movie playback unavailable\n");
        so_flush_caches(&sf4ce_mod);
    } else if (result != title_idle_movie::Result::PlaybackAvailable) {
        trace("[Title] WARNING: idle movie guard was not installed (%d)\n", (int)result);
    }
}
static uint32_t g_controller_buttons = 0;
static float g_left_stick_x = 0.0f;
static float g_left_stick_y = 0.0f;
static float g_right_stick_x = 0.0f;
static float g_right_stick_y = 0.0f;
static bool g_full_asset_pack_mounted = false;

static bool HasCompleteLocalAssetPack(void)
{
    static const char *required[] = {
        "_filelist.txt",
        "guy_sel_chara.obm",
        "jri_sel_chara.obm",
        "dan_sel_chara.obm",
        "eln_sel_chara.obm",
        "psn_sel_chara.obm",
    };
    for (unsigned int i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
    {
        char path[512];
        struct stat info;
        snprintf(path, sizeof(path), "%s/demand_assets/%s", DATA_PATH,
                 required[i]);
        if (stat(path, &info) != 0 || info.st_size <= 0)
            return false;
        if (i == 0 && info.st_size < 100000)
            return false;
    }
    FILE *manifest = fopen(DATA_PATH "/demand_assets/_filelist.txt", "r");
    if (!manifest) return false;
    asset_pack::Audit audit;
    char line[512];
    while (fgets(line, sizeof(line), manifest)) {
        char *end = strchr(line, ',');
        if (!end) continue;
        *end = 0;
        if (asset_pack::RosterFile(line) < 0) continue;
        struct stat info;
        char path[640];
        snprintf(path, sizeof(path), "%s/demand_assets/%s", DATA_PATH, line);
        bool exists = stat(path, &info) == 0 && info.st_size > 0;
        if (!exists) {
            snprintf(path, sizeof(path), "%s/assets/%s", DATA_PATH, line);
            exists = stat(path, &info) == 0 && info.st_size > 0;
        }
        audit.Add(line, exists);
        if (!exists && audit.missing <= 16)
            trace("[Assets] missing combat file: %s\n", line);
    }
    fclose(manifest);
    trace("[Assets] roster verification: files=%u missing=%u primary=0x%08X\n",
          audit.checked, audit.missing, audit.primary_characters);
    return audit.Complete();
}

static float NormalizeStick(uint8_t value)
{
    float result = ((int)value - 128) / 127.0f;
    if (fabsf(result) < 0.15f)
        return 0.0f;
    if (result < -1.0f)
        return -1.0f;
    if (result > 1.0f)
        return 1.0f;
    return result;
}

static void UpdateControllerInput(const SceCtrlData *pad)
{
    enum {
        GC_LEFT = 1,
        GC_RIGHT = 2,
        GC_UP = 4,
        GC_DOWN = 8,
        GC_BTN01 = 32,
        GC_BTN02 = 64,
        GC_BTN03 = 128,
        GC_BTN04 = 256,
        GC_L1 = 512,
        GC_R1 = 1024,
        GC_START = 65536,
        GC_SELECT = 131072,
    };
    uint32_t buttons = 0;
    if (pad->buttons & SCE_CTRL_LEFT) buttons |= GC_LEFT;
    if (pad->buttons & SCE_CTRL_RIGHT) buttons |= GC_RIGHT;
    if (pad->buttons & SCE_CTRL_UP) buttons |= GC_UP;
    if (pad->buttons & SCE_CTRL_DOWN) buttons |= GC_DOWN;
    /* Android's standard gamepad order is A/B/X/Y. */
    if (pad->buttons & SCE_CTRL_CROSS) buttons |= GC_BTN01;
    if (pad->buttons & SCE_CTRL_CIRCLE) buttons |= GC_BTN02;
    if (pad->buttons & SCE_CTRL_SQUARE) buttons |= GC_BTN03;
    if (pad->buttons & SCE_CTRL_TRIANGLE) buttons |= GC_BTN04;
    if (pad->buttons & SCE_CTRL_LTRIGGER) buttons |= GC_L1;
    if (pad->buttons & SCE_CTRL_RTRIGGER) buttons |= GC_R1;
    if (pad->buttons & SCE_CTRL_START) buttons |= GC_START;
    if (pad->buttons & SCE_CTRL_SELECT) buttons |= GC_SELECT;
    g_controller_buttons = buttons;
    g_left_stick_x = NormalizeStick(pad->lx);
    g_left_stick_y = NormalizeStick(pad->ly);
    g_right_stick_x = NormalizeStick(pad->rx);
    g_right_stick_y = NormalizeStick(pad->ry);
    if (options.debug_touch_cursor && (pad->buttons & SCE_CTRL_SELECT))
    {
        g_controller_buttons = 0;
        g_left_stick_x = g_left_stick_y = 0.0f;
        g_right_stick_x = g_right_stick_y = 0.0f;
    }

    /* Keep this bounded: it is useful for confirming Vita3K/hardware button
     * delivery without turning the per-frame log into another bottleneck. */
    static uint32_t previous_buttons = UINT32_MAX;
    static unsigned int trace_count;
    if (buttons != previous_buttons && trace_count < 32)
    {
        trace("[Input] buttons=0x%08" PRIx32
              " left=(%.2f,%.2f) right=(%.2f,%.2f)\n",
              buttons, g_left_stick_x, g_left_stick_y,
              g_right_stick_x, g_right_stick_y);
        previous_buttons = buttons;
        ++trace_count;
    }
}

// ============================================================
// JNI Function Pointers — resolved from the .so
// ============================================================

typedef void (*NativeInitFunc)(void *, void *, void *, void *, void *);
typedef void (*NativeUpdateFunc)(void *, void *, void *);
typedef void (*NativeDrawFunc)(void *, void *);
typedef void (*NativePauseFunc)(void *, void *);
typedef void (*NativeResumeFunc)(void *, void *);
typedef void (*NativeTermFunc)(void *, void *);
typedef void (*NativeCommonFunc)(void *, void *, int);
typedef int (*NativeP2PUpdateFunc)(void *, void *);
typedef void (*NativePurchaseResponseFunc)(void *, void *, char *);
typedef int (*NativeisConnectEnvFamilyFunc)(void *, void *, char *);
typedef void (*NativeHttpResponseFunc)(void *, void *, int, int);
typedef void (*NativeHttpFailureFunc)(void *, void *, void *, void *);
typedef void **(*GetAppMainInstanceFunc)(void);
typedef void (*SetSceneTitleFunc)(void *);
typedef SOFTFP_FN(float, TouchPosFunc, void *, int);
typedef SOFTFP_FN(float, CheckDisplayModeAxisFunc, void *, int, float, int);
typedef SOFTFP_FN(float, AppFloatFunc, void *);
typedef SOFTFP_FN(float, ScreenScaleFunc);
typedef int (*AppIntFunc)(void *);

static NativeInitFunc fp_NativeInit = NULL;
static NativeUpdateFunc fp_NativeUpdate = NULL;
static NativeDrawFunc fp_NativeDraw = NULL;
static NativePauseFunc fp_NativePause = NULL;
static NativeResumeFunc fp_NativeResume = NULL;
static NativeTermFunc fp_NativeTerm = NULL;
static NativeCommonFunc fp_NativeCommon = NULL;
static NativeP2PUpdateFunc fp_NativeP2PUpdate = NULL;
static NativePurchaseResponseFunc fp_NativePurchaseResponse = NULL;
static NativeisConnectEnvFamilyFunc fp_NativeisConnectEnvFamily = NULL;
static NativeHttpResponseFunc fp_NativeHttpResponse = NULL;
static NativeHttpFailureFunc fp_NativeHttpFailure = NULL;
static GetAppMainInstanceFunc fp_GetAppMainInstance = NULL;
static SetSceneTitleFunc fp_SetSceneTitle = NULL;
static uintptr_t fp_ST_LogoSystem = 0;
static TouchPosFunc fp_GetTouchPosX = NULL;
static TouchPosFunc fp_GetTouchPosY = NULL;
static CheckDisplayModeAxisFunc fp_CheckDisplayModeX = NULL;
static CheckDisplayModeAxisFunc fp_CheckDisplayModeY = NULL;
static AppFloatFunc fp_GetZoomUIW = NULL;
static AppFloatFunc fp_GetZoomUIH = NULL;
static ScreenScaleFunc fp_GetScreenScale = NULL;
static AppIntFunc fp_GetDeviceWidth = NULL;
static AppIntFunc fp_GetDeviceHeight = NULL;
static volatile bool g_http_failure_pending = false;
static void *g_app_main = NULL;
static uintptr_t fp_PadSettingSystem, fp_PadSettingToPauseSystem;
static void (*fp_TouchControlDraw[4])(void *);
static bool TouchControlSetup() {
    if (!g_app_main) return false;
    uintptr_t scene = *(const uintptr_t *)((const uint8_t *)g_app_main + 0xdec);
    return (fp_PadSettingSystem && scene == fp_PadSettingSystem) ||
           (fp_PadSettingToPauseSystem && scene == fp_PadSettingToPauseSystem);
}
template<unsigned Index> static void DrawTouchControlsInSetup(void *pad) {
    if (TouchControlSetup() && fp_TouchControlDraw[Index]) fp_TouchControlDraw[Index](pad);
}
typedef void *(*PerspectiveFunc)(void *, int, int, int, int);
static PerspectiveFunc fp_Perspective;
static uintptr_t fp_GameMainSystem, fp_CameraOffsetNone;
static menu_pointer::Pointer g_menu_pointer;
static bool g_menu_back;
static int g_common_dialog_result;
static bool MenuPointerEnabled() {
    if (!g_app_main || !fp_GameMainSystem || g_ime_active) return false;
    const uint8_t *app = static_cast<const uint8_t *>(g_app_main);
    uintptr_t scene = *(const uintptr_t *)(app + 0xdec);
    uintptr_t next = *(const uintptr_t *)(app + 0xdf4);
    return scene && scene != fp_GameMainSystem && next != fp_GameMainSystem;
}

static void DrawMenuPointer() {
    if (!g_menu_pointer.visible || !MenuPointerEnabled()) return;
    // Immediate mode preserves the game's client array pointers.
    GLint framebuffer, program, matrix, active_texture;
    GLboolean textured[2];
    GLfloat color[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_MATRIX_MODE, &matrix);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glGetFloatv(GL_CURRENT_COLOR, color);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glUseProgram(0);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    // Disable both units: a cinematic/menu may leave multitexturing active.
    for (int unit = 1; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        textured[unit] = glIsEnabled(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_2D);
    }
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrthof(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    const float x = g_menu_pointer.x, y = g_menu_pointer.y;
    glColor4f(0, 0, 0, 1);
    glBegin(GL_TRIANGLES);
    glVertex2f(x - 2, y - 2); glVertex2f(x + 18, y + 12); glVertex2f(x + 5, y + 24);
    glEnd();
    glColor4f(1, g_menu_pointer.down ? 0.6f : 1, 0.2f, 1);
    glBegin(GL_TRIANGLES);
    glVertex2f(x, y); glVertex2f(x + 14, y + 12); glVertex2f(x + 6, y + 19);
    glEnd();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopAttrib(); glMatrixMode(matrix);
    for (int unit = 1; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        if (textured[unit]) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(active_texture); glColor4fv(color);
    glUseProgram(program); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}
static uint32_t g_simulation_interval_us = frame_timing::kInitialSimulationUs;
static void (*fp_AppDrawLoop)(void *);
static bool g_smooth_render_available = false;
static bool NormalBattleRendering() {
    if (!g_smooth_render_available || !g_app_main || !fp_AppDrawLoop || !fp_GameMainSystem || !fp_CameraOffsetNone) return false;
    const uint8_t *app = static_cast<const uint8_t *>(g_app_main);
    return *(const uintptr_t *)(app + 0xdec) == fp_GameMainSystem &&
           *(const uintptr_t *)(app + 0x1134) == fp_CameraOffsetNone &&
           !*(const uintptr_t *)(app + 0xe1c) && !*(const uintptr_t *)(app + 0xe20);
}


static bool g_simulating = true;
static void (*fp_SystemLoop)(void *), (*fp_FadeLoop)(void *);
static void (*fp_TouchSet)(void *, short *), (*fp_TouchClear)(void *);
static void (*fp_ControllerUpdate)(void *);
static void (*fp_TextUpdate)(), (*fp_CricketUpdate)(), (*fp_NetworkUpdate)();
static void TimedSystemLoop(void *app) { if (g_simulating) fp_SystemLoop(app); }
static void TimedFadeLoop(void *app) { if (g_simulating) fp_FadeLoop(app); }
static void TimedTouchSet(void *touch, short *input) { if (g_simulating) fp_TouchSet(touch, input); }
static void TimedTouchClear(void *touch) { if (g_simulating) fp_TouchClear(touch); }
static void TimedControllerUpdate(void *controller) { if (g_simulating) fp_ControllerUpdate(controller); }
static void TimedTextUpdate() { if (g_simulating) fp_TextUpdate(); }
static void TimedCricketUpdate() { if (g_simulating) fp_CricketUpdate(); }
static void TimedNetworkUpdate() { if (g_simulating) fp_NetworkUpdate(); }

static void DrawLoopWithInterpolationGuard(void *app) {
    // SystemLoop can enter a cinematic or pause on this very tick. Recheck
    // after it has run, before capturing any geometry or camera history.
    if (!NormalBattleRendering()) RenderInterpolationBegin(false, true, 1);
    fp_AppDrawLoop(app);
}

static void TraceSlowFrame(uint64_t start, uint64_t update_end,
                           uint64_t draw_end, uint64_t end, bool simulation) {
    if (!options.debug_timing || end - start < 150000) return;
    trace("[Timing] slow frame at %llu us: simulation=%d update=%llu draw=%llu present=%llu us scene=%p camera=%p interval=%u\n",
          start, simulation, update_end - start, draw_end - update_end,
          end - draw_end,
          g_app_main ? (void *)*(uintptr_t *)((uint8_t *)g_app_main + 0xdec) : nullptr,
          g_app_main ? (void *)*(uintptr_t *)((uint8_t *)g_app_main + 0x1134) : nullptr,
          g_simulation_interval_us);
}

static void *ConsolePerspective(void *matrix, int fov, int aspect, int near_z, int far_z)
{
    static int current_fov = battle_camera::kFovBam;
    if (g_app_main && matrix == (uint8_t *)g_app_main + 0xe34) {
        uint8_t *app = (uint8_t *)g_app_main;
        bool normal_battle = *(uintptr_t *)(app + 0xdec) == fp_GameMainSystem &&
                             *(uintptr_t *)(app + 0x1134) == fp_CameraOffsetNone;
        void *first = *(void **)(app + 0x1174);
        void *second = *(void **)(app + 0x1178);
        int desired = battle_camera::kFovBam;
        if (normal_battle && first && second) {
            int y1 = *(int32_t *)((uint8_t *)first + 0x48);
            int y2 = *(int32_t *)((uint8_t *)second + 0x48);
            int x1 = *(int32_t *)((uint8_t *)first + 0x44);
            int x2 = *(int32_t *)((uint8_t *)second + 0x44);
            desired = battle_camera::FovForBounds(x1, y1, x2, y2);
            if (options.debug_input_replay && desired != current_fov)
                trace("[Camera] bounds x=%d,%d y=%d,%d fov=%d target=%d\n", x1, x2, y1, y2, current_fov, desired);
            current_fov = battle_camera::ApproachFov(current_fov, desired);
            fov = current_fov;
        } else {
            // Preserve the projection requested by native cinematic cameras.
            current_fov = battle_camera::kFovBam;
        }
    }
    return fp_Perspective(matrix, fov, aspect, near_z, far_z);
}


typedef void (*NetworkCompletionFunc)(void);
static volatile NetworkCompletionFunc g_network_completion_pending = NULL;
static uintptr_t g_name_validation_success = 0;
static volatile bool g_mark_name_after_update = false;

struct ModeSelectEntry
{
    int32_t resource;
    int32_t task_id;
    int32_t position;
    int32_t depth;
};

static bool HideCockpitPauseButton(uintptr_t cockpit_rects)
{
    /* ciImgRectCockpit is an array of eight-short texture rectangles.  Entry
     * 96 is the 88x26 PAUSE label in cockpit.obm.  Keep the texture and Start
     * handling intact; a zero-sized source rectangle only removes the mobile
     * touch affordance from the HUD. */
    static const int16_t expected[] = {
        372, 378, 88, 26, 44, 12, 96, 0,
    };
    static const int16_t hidden[] = {
        372, 378, 0, 0, 0, 0, 96, 0,
    };
    if (!cockpit_rects)
        return false;
    int16_t *pause_rect = (int16_t *)cockpit_rects + 96 * 8;
    if (memcmp(pause_rect, expected, sizeof(expected)) != 0)
    {
        trace("[Input] WARNING: cockpit PAUSE rectangle does not match; "
              "it was not patched\n");
        return false;
    }
    kuKernelCpuUnrestrictedMemcpy(pause_rect, hidden, sizeof(hidden));
    return true;
}

static bool InstallOfflineMainMenu(uintptr_t create_main_panel,
                                   uintptr_t create_sub_menu)
{
    /* This table belongs to ST_CreateModeSelectMainPanel in the Android
     * supported ARMv7 binary.  IDs 12 and 13 dispatch to the Store and Battle
     * panels.  Compacting those two entries out removes the unavailable
     * online categories while keeping Solo Play, Other and all local items.
     *
     * Check both code and data before writing: a different game build must
     * fail closed instead of receiving offsets intended for this one. */
    static const ModeSelectEntry expected[] = {
        {37, 7, 0, 5}, {12, 1, 0, 5}, {11, 2, 1, 4},
        {11, 3, 2, 3}, {11, 12, 2, 3}, {12, 6, 2, 3},
        {12, 13, 3, 2}, {12, 10, 4, 1}, {12, 5, 4, 1},
        {12, 11, 4, 1}, {12, 4, 5, 0},
    };
    static const ModeSelectEntry offline[] = {
        {37, 7, 0, 5}, {12, 1, 0, 5}, {11, 2, 1, 4},
        {11, 3, 2, 3}, {12, 6, 2, 3}, {12, 10, 4, 1},
        {12, 5, 4, 1}, {12, 11, 4, 1}, {12, 4, 5, 0},
    };
    static const ModeSelectEntry expected_navbar[] = {
        {47, 0, 3, 11}, {48, 0, 3, 11}, {49, 0, 3, 11},
        {50, 0, 3, 11}, {51, 0, 3, 11}, {52, 0, 3, 11},
    };
    static const ModeSelectEntry offline_navbar[] = {
        {47, 0, 3, 11}, {48, 0, 3, 11},
        {49, 0, 3, 11}, {52, 0, 3, 11},
    };
    static const uint32_t expected_count = 0xe300000b;        // movw r0, #11
    static const uint32_t offline_count = 0xe3000009;         // movw r0, #9
    static const uint32_t expected_navbar_count = 0xe3000006; // movw r0, #6
    static const uint32_t offline_navbar_count = 0xe3000004;  // movw r0, #4

    if (!create_main_panel || !create_sub_menu)
        return false;

    uintptr_t panel = create_main_panel & ~(uintptr_t)1;
    ModeSelectEntry *panel_table =
        (ModeSelectEntry *)(panel - (uintptr_t)0xe5500);
    uint32_t *panel_count = (uint32_t *)(panel + 0x5c);
    uintptr_t sub_menu = create_sub_menu & ~(uintptr_t)1;
    ModeSelectEntry *navbar_table =
        (ModeSelectEntry *)(sub_menu - (uintptr_t)0xe58e8);
    uint32_t *navbar_count = (uint32_t *)(sub_menu + 0x78);

    /* Validate both structures before modifying either one.  This makes the
     * two related patches atomic from the point of view of version checks. */
    if (*panel_count != expected_count ||
        memcmp(panel_table, expected, sizeof(expected)) != 0 ||
        *navbar_count != expected_navbar_count ||
        memcmp(navbar_table, expected_navbar, sizeof(expected_navbar)) != 0)
    {
        trace("[Menu] WARNING: main-menu/navbar layout does not match; "
              "online entries were not patched\n");
        return false;
    }

    kuKernelCpuUnrestrictedMemcpy(panel_table, offline, sizeof(offline));
    kuKernelCpuUnrestrictedMemcpy(panel_count, &offline_count,
                                  sizeof(offline_count));
    kuKernelCpuUnrestrictedMemcpy(navbar_table, offline_navbar,
                                  sizeof(offline_navbar));
    kuKernelCpuUnrestrictedMemcpy(navbar_count, &offline_navbar_count,
                                  sizeof(offline_navbar_count));
    trace("[Menu] offline main menu/navbar installed: Store/Battle removed\n");
    return true;
}

static void MarkNameEntryConfirmed(void)
{
    char marker_path[512];
    snprintf(marker_path, sizeof(marker_path),
             "%s/.vita_name_entry_confirmed", DATA_PATH);
    FILE *marker = fopen(marker_path, "wb");
    if (marker)
    {
        fputs("1\n", marker);
        fclose(marker);
    }
    g_initial_name_expected = false;
}

extern "C" void vita_network_connect_start_base(
    int type, NetworkCompletionFunc success, NetworkCompletionFunc failure)
{
    /* NET_CONNECT_TYPE 11 is the server-side uniqueness check made by the
     * name-entry dialog.  There is no SF4 server on Vita, so accept that one
     * locally.  Every other online operation follows its original failure
     * callback instead of entering ST_NetworkConnecting and hanging. */
    NetworkCompletionFunc completion = failure;
    bool name_validation = type == 11 && success &&
                           (uintptr_t)success == g_name_validation_success;
    if (name_validation)
    {
        completion = success;
        g_mark_name_after_update = true;
    }
    trace("[Network] request type=%d -> %s callback\n", type,
          name_validation ? "local name success" : "offline failure");
    g_network_completion_pending = completion;
}

extern "C" void vita_cancel_network_task(void *app_main, int state)
{
    (void)state;
    static bool logged;
    if (!app_main)
        return;
    uint8_t *network = *(uint8_t **)((uint8_t *)app_main + 0x102c);
    if (!network)
        return;

    /* ST_NetworkConnecting dispatches +0x124/+0x12c when the Java HTTP
     * activity completes.  Vita has no corresponding online service: cancel
     * the request flags and indicator, but leave AppMain's scene callbacks
     * entirely untouched. */
    network[0xe0] = 0;
    *(int16_t *)(network + 0x10c) = 0;
    network[0x120] = 0;
    network[0x128] = 0;
    if (!logged)
    {
        trace("[Network] online task cancelled; local scene preserved\n");
        logged = true;
    }
}

extern "C" int vita_sound_file_load(void *sound, const char *filename, int bank)
{
    (void)sound;
    (void)bank;
    trace("[Audio] muted bank accepted: %s\n", filename ? filename : "(null)");
    return 1;
}

extern "C" int vita_sound_check(void *sound, int port)
{
    (void)sound;
    (void)port;
    return 0;
}

static void InstallMutedAudio(so_module *module)
{
    hook_addr(so_symbol(module, "CkUpdate"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "CkSuspend"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "CkResume"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "CkShutdown"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN8SoundObj9SoundInitEv"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN8SoundObj8FileLoadEPKci"),
              (uintptr_t)&vita_sound_file_load);
    hook_addr(so_symbol(module, "_ZN8SoundObj10SoundCheckEi"),
              (uintptr_t)&vita_sound_check);
    hook_addr(so_symbol(module, "_ZN8SoundObj14RequestPlayBGMEi"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN8SoundObj14RequestPlayBGMEii"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN8SoundObj16RequestPlayBGMExEii"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN8SoundObj12SoundPlayBGMEv"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN8SoundObj12SoundStopBGMEb"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN7AppMain10RequestBGMEib"),
              (uintptr_t)&vita_ck_audio_noop);
    static const char *muted_sound_methods[] = {
        "_ZN8SoundObj10SoundPitchEif", "_ZN8SoundObj10SoundResetEv",
        "_ZN8SoundObj11SoundPlaySEEv", "_ZN8SoundObj11SoundResumeEv",
        "_ZN8SoundObj11SoundStopSEEb", "_ZN8SoundObj11SoundStopSEEib",
        "_ZN8SoundObj11SoundVolumeEif", "_ZN8SoundObj11bPortSearchEb",
        "_ZN8SoundObj12ClearAllPortEv", "_ZN8SoundObj12DeleteBufferEi",
        "_ZN8SoundObj12DeleteSourceEi", "_ZN8SoundObj12SoundPauseSEEb",
        "_ZN8SoundObj12SoundReleaseEv", "_ZN8SoundObj12SoundStopAllEv",
        "_ZN8SoundObj12SoundSuspendEv", "_ZN8SoundObj13RequestCancelEi",
        "_ZN8SoundObj13RequestCancelEii", "_ZN8SoundObj13RequestPlaySEEi",
        "_ZN8SoundObj13RequestPlaySEEii", "_ZN8SoundObj13RequestPlaySEEiib",
        "_ZN8SoundObj13RequestSearchEi", "_ZN8SoundObj13SoundNoSearchEi",
        "_ZN8SoundObj13SoundPauseBGMEb", "_ZN8SoundObj13SoundVolumeSEEf",
        "_ZN8SoundObj13setCharCkBankEPKcb",
        "_ZN8SoundObj14SoundVolumeBGMEf", "_ZN8SoundObj15DeleteAllSourceEv",
        "_ZN8SoundObj15SoundPlaySearchEi", "_ZN8SoundObj15SoundPortPlayNoEi",
        "_ZN8SoundObj15SoundStopSearchEi", "_ZN8SoundObj15SoundStopSearchEib",
        "_ZN8SoundObj17CreateSoundBufferEv", "_ZN8SoundObj17DeleteSoundBufferEv",
        "_ZN8SoundObj17RequestCancelPortEi", "_ZN8SoundObj17SoundMasterVolumeEif",
        "_ZN8SoundObj18SoundSuspendStopSEEv", "_ZN8SoundObj19SoundMasterVolumeSEEf",
        "_ZN8SoundObj19SoundSuspendStopBGMEv",
        "_ZN8SoundObj20RequestPortFixPlaySEEii",
        "_ZN8SoundObj20SoundMasterVolumeBGMEf", "_ZN8SoundObj9ClearPortEi",
        "_ZN8SoundObj9SoundSeekEif", "_ZN8SoundObj9SoundStopEi",
        "_ZN8SoundObj9SoundStopEib",
    };
    for (const char *symbol : muted_sound_methods)
    {
        uintptr_t address = so_symbol(module, symbol);
        if (address)
            hook_addr(address, (uintptr_t)&vita_sound_check);
    }
    hook_addr(so_symbol(module, "_ZN8SoundObj15GetMasterVolumeEi"),
              (uintptr_t)&vita_ck_audio_zero);
    hook_addr(so_symbol(module, "_ZN8SoundObj18GetMasterVolumeBGMEv"),
              (uintptr_t)&vita_ck_audio_zero);
    hook_addr(so_symbol(module, "_ZN3Cki5Sound9updateAllEv"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZN3Cki5Sound6updateEv"),
              (uintptr_t)&vita_ck_audio_noop);
    hook_addr(so_symbol(module, "_ZNK3Cki5Sound14getMixedVolumeEv"),
              (uintptr_t)&vita_ck_audio_zero);
    hook_addr(so_symbol(module, "_ZNK3Cki5Sound14getFinalVolumeEv"),
              (uintptr_t)&vita_ck_audio_zero);
}

extern "C" int vita_controller_connected(void *controller)
{
    (void)controller;
    return !TouchControlSetup();
}

extern "C" void vita_gamepad_draw_noop(void *gamepad)
{
    (void)gamepad;
}

// ============================================================
// Fake JNI Environment
// ============================================================

// JNI types (matching Android DEX signatures)
typedef void *jobject;
typedef void *jclass;
typedef void *jstring;
typedef void *jarray;
typedef void *jbyteArray;
typedef void *jshortArray;
typedef void *jintArray;
typedef void *jmethodID;

// JNI scalar types
typedef signed char jbyte;
typedef signed short jshort;
typedef signed int jint;
typedef signed long long jlong;
typedef float jfloat;
typedef double jdouble;
typedef unsigned char jboolean;
typedef signed int jsize;
typedef enum
{
    JNI_COMMIT = 1,
    JNI_ABORT = 2
} jmode;

// Fake JNIEnv vtable — the .so calls JNI functions through this
static char fake_env[0x2000];
static char fake_vm[0x100];

enum LocalRefKind
{
    LOCAL_REF_STRING,
    LOCAL_REF_BYTE_ARRAY
};

struct LocalRefEntry
{
    void *ref;
    LocalRefKind kind;
};

struct GlobalRefEntry
{
    void *ref;
    LocalRefKind kind;
    unsigned int references;
};

/* Android releases the temporary String/byte[] objects created by
 * CFile::GetJString through DeleteLocalRef.  Keep just enough ownership
 * information to mirror that behaviour without treating static bridge
 * strings (device name, locale, etc.) as heap allocations. */
static LocalRefEntry local_refs[256];
static GlobalRefEntry global_refs[64];

static void FreeOwnedJniRef(void *ref, LocalRefKind kind)
{
    if (!ref)
        return;
    if (kind == LOCAL_REF_BYTE_ARRAY)
    {
        jni_bytearray *array = (jni_bytearray *)ref;
        free(array->elements);
    }
    free(ref);
}

static void ClearLocalRefs(void)
{
    for (unsigned int i = 0; i < sizeof(local_refs) / sizeof(local_refs[0]); ++i)
    {
        if (!local_refs[i].ref)
            continue;
        FreeOwnedJniRef(local_refs[i].ref, local_refs[i].kind);
        local_refs[i].ref = NULL;
    }
}

static void RegisterLocalRef(void *ref, LocalRefKind kind)
{
    if (!ref)
        return;
    for (unsigned int i = 0; i < sizeof(local_refs) / sizeof(local_refs[0]); ++i)
    {
        if (!local_refs[i].ref)
        {
            local_refs[i].ref = ref;
            local_refs[i].kind = kind;
            return;
        }
    }
    trace("[JNI] local reference table exhausted\n");
}

static jint JavaVMGetEnv(void *vm, void **env, jint version)
{
    (void)vm;
    (void)version;
    if (!env)
        return -1;
    *env = fake_env;
    return 0;
}

static jint JavaVMDetach(void *) { return 0; }

// JNI function implementations
static jclass FindClass(void *env, const char *name)
{
    (void)env;
    (void)name;
    return (jclass)0x41414141; // Dummy class reference
}

static jmethodID GetMethodID(void *env, jclass clazz, const char *name, const char *sig)
{
    (void)env;
    (void)clazz;
    static int calls;
    if (calls++ < 128)
        trace("[JNI] GetMethodID %s %s\n", name ? name : "(null)",
              sig ? sig : "(null)");
    return (jmethodID)name;
}

static jmethodID GetStaticMethodID(void *env, jclass clazz, const char *name, const char *sig)
{
    (void)env;
    (void)clazz;
    static int calls;
    if (calls++ < 256)
        trace("[JNI] GetStaticMethodID %s %s\n", name ? name : "(null)",
              sig ? sig : "(null)");
    return (jmethodID)name;
}

static jstring NewStringUTF(void *env, const char *bytes)
{
    (void)env;
    if (!bytes)
        return NULL;
    size_t len = strlen(bytes) + 1;
    char *copy = (char *)malloc(len);
    if (!copy)
        return NULL;
    memcpy(copy, bytes, len);
    RegisterLocalRef(copy, LOCAL_REF_STRING);
    return (jstring)copy;
}

static char *GetStringUTFChars(void *env, jstring string, jboolean *isCopy)
{
    (void)env;
    if (isCopy)
        *isCopy = 0;
    static int string_trace_count;
    if (string_trace_count++ < 48)
        trace("[JNI] GetStringUTFChars object=%p value=%s\n", string,
              string ? (char *)string : "(null)");
    return (char *)string;
}

static int GetStringUTFLength(void *env, jstring string)
{
    (void)env;
    return string ? (int)strlen((char *)string) : 0;
}

static void ReleaseStringUTFChars(void *env, jstring string, const char *str)
{
    (void)env;
    (void)string;
    (void)str;
    // No-op: strings are managed elsewhere
}

static jbyteArray NewByteArray(void *env, jint size)
{
    (void)env;
    if (size < 0)
        return NULL;
    jni_bytearray *array = (jni_bytearray *)malloc(sizeof(jni_bytearray));
    if (!array)
        return NULL;
    array->elements = size ? (unsigned char *)malloc(size) : NULL;
    if (size && !array->elements)
    {
        free(array);
        return NULL;
    }
    array->size = (int)size;
    RegisterLocalRef(array, LOCAL_REF_BYTE_ARRAY);
    return (jbyteArray)array;
}

static void DeleteLocalRef(void *env, jobject ref);

/* Mirror GlTest2Renderer.convertString* without depending on Android's Java
 * charset implementation.  The game mainly uses these two paths to cross
 * between its UTF-16LE message tables and the UTF-8 strings consumed by JNI.
 * Newlib's iconv handles the less common encodings requested by the game. */
static jbyteArray ConvertBytes(const unsigned char *input, size_t input_size,
                               const char *from, const char *to)
{
    if (!input || !from || !to)
        return NULL;

    size_t capacity = input_size * 4 + 16;
    jbyteArray result = NewByteArray(NULL, (jint)capacity);
    if (!result)
        return NULL;
    jni_bytearray *output = (jni_bytearray *)result;

    const bool from_utf8 = !strcasecmp(from, "UTF-8") ||
                           !strcasecmp(from, "UTF8");
    const bool to_utf8 = !strcasecmp(to, "UTF-8") ||
                         !strcasecmp(to, "UTF8");
    const bool from_utf16le = !strcasecmp(from, "UTF-16LE") ||
                              !strcasecmp(from, "UTF16LE");
    const bool to_utf16le = !strcasecmp(to, "UTF-16LE") ||
                            !strcasecmp(to, "UTF16LE");

    if ((from_utf8 && to_utf8) || (from_utf16le && to_utf16le) ||
        !strcasecmp(from, to))
    {
        memcpy(output->elements, input, input_size);
        output->size = (int)input_size;
        return result;
    }

    if (from_utf16le && to_utf8)
    {
        size_t out = 0;
        for (size_t in = 0; in + 1 < input_size; in += 2)
        {
            uint32_t cp = (uint32_t)input[in] | ((uint32_t)input[in + 1] << 8);
            if (cp >= 0xd800 && cp <= 0xdbff && in + 3 < input_size)
            {
                uint32_t low = (uint32_t)input[in + 2] |
                               ((uint32_t)input[in + 3] << 8);
                if (low >= 0xdc00 && low <= 0xdfff)
                {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    in += 2;
                }
                else
                    cp = 0xfffd;
            }
            else if (cp >= 0xdc00 && cp <= 0xdfff)
                cp = 0xfffd;

            if (cp < 0x80)
                output->elements[out++] = (unsigned char)cp;
            else if (cp < 0x800)
            {
                output->elements[out++] = 0xc0 | (cp >> 6);
                output->elements[out++] = 0x80 | (cp & 0x3f);
            }
            else if (cp < 0x10000)
            {
                output->elements[out++] = 0xe0 | (cp >> 12);
                output->elements[out++] = 0x80 | ((cp >> 6) & 0x3f);
                output->elements[out++] = 0x80 | (cp & 0x3f);
            }
            else
            {
                output->elements[out++] = 0xf0 | (cp >> 18);
                output->elements[out++] = 0x80 | ((cp >> 12) & 0x3f);
                output->elements[out++] = 0x80 | ((cp >> 6) & 0x3f);
                output->elements[out++] = 0x80 | (cp & 0x3f);
            }
        }
        output->size = (int)out;
        return result;
    }

    if (from_utf8 && to_utf16le)
    {
        size_t in = 0;
        size_t out = 0;
        while (in < input_size)
        {
            uint32_t cp = input[in++];
            unsigned int extra = 0;
            if (cp < 0x80) {}
            else if ((cp & 0xe0) == 0xc0) { cp &= 0x1f; extra = 1; }
            else if ((cp & 0xf0) == 0xe0) { cp &= 0x0f; extra = 2; }
            else if ((cp & 0xf8) == 0xf0) { cp &= 0x07; extra = 3; }
            else { cp = 0xfffd; }
            for (unsigned int i = 0; i < extra; ++i)
            {
                if (in >= input_size || (input[in] & 0xc0) != 0x80)
                {
                    cp = 0xfffd;
                    break;
                }
                cp = (cp << 6) | (input[in++] & 0x3f);
            }
            if (cp > 0xffff)
            {
                cp -= 0x10000;
                uint16_t high = (uint16_t)(0xd800 | (cp >> 10));
                uint16_t low = (uint16_t)(0xdc00 | (cp & 0x3ff));
                output->elements[out++] = high & 0xff;
                output->elements[out++] = high >> 8;
                output->elements[out++] = low & 0xff;
                output->elements[out++] = low >> 8;
            }
            else
            {
                output->elements[out++] = cp & 0xff;
                output->elements[out++] = cp >> 8;
            }
        }
        output->size = (int)out;
        return result;
    }

    iconv_t converter = iconv_open(to, from);
    if (converter == (iconv_t)-1)
    {
        trace("[JNI] iconv_open %s <- %s failed: %d\n", to, from, errno);
        DeleteLocalRef(NULL, result);
        return NULL;
    }

    char *in_cursor = (char *)input;
    size_t in_left = input_size;
    char *out_cursor = (char *)output->elements;
    size_t out_left = capacity;
    size_t status = iconv(converter, &in_cursor, &in_left, &out_cursor, &out_left);
    iconv_close(converter);
    if (status == (size_t)-1)
    {
        trace("[JNI] iconv %s <- %s failed at %u/%u: %d\n", to, from,
              (unsigned int)(input_size - in_left), (unsigned int)input_size,
              errno);
        DeleteLocalRef(NULL, result);
        return NULL;
    }
    output->size = (int)(capacity - out_left);
    return result;
}

static jsize GetArrayLength(void *env, jarray array)
{
    (void)env;
    jni_bytearray *ba = (jni_bytearray *)array;
    return ba ? ba->size : 0;
}

static unsigned char *GetByteArrayElements(void *env, jbyteArray array, jboolean *isCopy)
{
    (void)env;
    if (isCopy)
        *isCopy = 0;
    jni_bytearray *ba = (jni_bytearray *)array;
    return ba ? ba->elements : NULL;
}

static void SetByteArrayRegion(void *env, jbyteArray array, jsize start, jsize len, const jbyte *buf)
{
    (void)env;
    jni_bytearray *ba = (jni_bytearray *)array;
    if (ba && buf)
    {
        static int region_trace_count;
        if (region_trace_count++ < 64) {
            char hex[3 * 24 + 1];
            int amount = len < 24 ? len : 24;
            int i = 0;
            for (; i < amount; ++i)
                snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ",
                         (unsigned char)buf[i]);
            hex[i * 3] = '\0';
            trace("[JNI] SetByteArrayRegion size=%d start=%d len=%d src=%p bytes=%s\n",
                  ba->size, start, len, buf, hex);
        }
        memcpy(ba->elements + start, buf, len);
    }
}

static void GetByteArrayRegion(void *env, jbyteArray array, jsize start, jsize len, jbyte *buf)
{
    (void)env;
    jni_bytearray *ba = (jni_bytearray *)array;
    if (ba && buf)
    {
        memcpy(buf, ba->elements + start, len);
    }
}

static short *GetShortArrayElements(void *env, jshortArray array, jboolean *isCopy)
{
    (void)env;
    if (isCopy)
        *isCopy = 0;
    return (short *)array;
}

static void ReleaseByteArrayElements(void *env, jbyteArray array, jbyte *elems, jmode mode)
{
    (void)env;
    (void)array;
    (void)elems;
    (void)mode;
}

static void ReleaseShortArrayElements(void *env, jshortArray array, jshort *elems, jmode mode)
{
    (void)env;
    (void)array;
    (void)elems;
    (void)mode;
}

static jobject NewGlobalRef(void *env, jobject obj)
{
    (void)env;
    if (!obj)
        return NULL;

    for (unsigned int i = 0; i < sizeof(global_refs) / sizeof(global_refs[0]); ++i)
    {
        if (global_refs[i].ref == obj)
        {
            ++global_refs[i].references;
            return obj;
        }
    }

    for (unsigned int i = 0; i < sizeof(local_refs) / sizeof(local_refs[0]); ++i)
    {
        if (local_refs[i].ref != obj)
            continue;
        for (unsigned int j = 0;
             j < sizeof(global_refs) / sizeof(global_refs[0]); ++j)
        {
            if (global_refs[j].ref)
                continue;
            global_refs[j].ref = obj;
            global_refs[j].kind = local_refs[i].kind;
            global_refs[j].references = 1;
            local_refs[i].ref = NULL;
            return obj;
        }
        trace("[JNI] global reference table exhausted\n");
        return NULL;
    }

    /* Static bridge objects and the dummy class are not heap-owned. */
    return obj;
}

static void DeleteLocalRef(void *env, jobject ref)
{
    (void)env;
    if (!ref)
        return;
    for (unsigned int i = 0; i < sizeof(local_refs) / sizeof(local_refs[0]); ++i)
    {
        if (local_refs[i].ref != ref)
            continue;
        FreeOwnedJniRef(ref, local_refs[i].kind);
        local_refs[i].ref = NULL;
        return;
    }
}

static void DeleteGlobalRef(void *env, jobject ref)
{
    (void)env;
    if (!ref)
        return;
    for (unsigned int i = 0; i < sizeof(global_refs) / sizeof(global_refs[0]); ++i)
    {
        if (global_refs[i].ref != ref)
            continue;
        if (--global_refs[i].references == 0)
        {
            FreeOwnedJniRef(ref, global_refs[i].kind);
            global_refs[i].ref = NULL;
        }
        return;
    }
}

static jobject NewObjectV(void *env, jclass clazz, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    (void)methodID;

    /* The only object constructor used during boot is
     * new String(byte[], charset).  On ARM32, the V-entry point receives a
     * pointer to the packed argument words; args[0] is the byte array and
     * args[1] is the encoding String. */
    jni_bytearray *bytes = args ? (jni_bytearray *)args[0] : NULL;
    const char *encoding = args ? (const char *)args[1] : NULL;
    if (!bytes || bytes->size < 0 || (bytes->size && !bytes->elements))
        return NULL;

    static int constructor_trace_count;
    if (constructor_trace_count++ < 48)
        trace("[JNI] new String(%d bytes, %s)\n", bytes->size,
              encoding ? encoding : "(null)");

    jni_bytearray *utf8 = NULL;
    if (encoding)
        utf8 = (jni_bytearray *)ConvertBytes(bytes->elements,
                                             (size_t)bytes->size,
                                             encoding, "UTF-8");
    const unsigned char *source = utf8 ? utf8->elements : bytes->elements;
    int source_size = utf8 ? utf8->size : bytes->size;
    char *string = (char *)malloc((size_t)source_size + 1);
    if (!string)
    {
        if (utf8)
            DeleteLocalRef(NULL, utf8);
        return NULL;
    }
    if (source_size)
        memcpy(string, source, (size_t)source_size);
    string[source_size] = '\0';
    if (utf8)
        DeleteLocalRef(NULL, utf8);
    RegisterLocalRef(string, LOCAL_REF_STRING);
    return (jobject)string;
}

// JNI Call methods — the .so calls back into Java through these
static void CallVoidMethodV(void *env, jobject obj, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)obj;
    (void)methodID;
    (void)args;
}

static void CallStaticVoidMethodV(void *env, jclass clazz, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    const char *name = (const char *)methodID;
    if (!name)
        return;
    if (!strcmp(name, "MoviePlay")) {
        // MovieObj stops game sounds and owns the return-to-title transition.
        if (!PlayIntroMovie(SCREEN_W, SCREEN_H, options.audio, MovieReplaySkip))
            GuardUnavailableIdleMovie();
    } else if (!strcmp(name, "resetTouchBackKey")) {
        g_menu_back = false;
    } else if (!strcmp(name, "resetOldDataFileInfo")) {
        g_file_buffer_cache.Reset();
    } else if (!strcmp(name, "openCommonDialog")) {
        // CommonDialogFragment.onCancel: NO for YES/NO, OK for notices.
        // In particular, Back on Main must not wait forever for Android's
        // absent quit confirmation. Vita exits through its system UI.
        g_common_dialog_result = args && args[0] == 1 ? 2 : 1;
        trace("[Menu] dismissed Android common dialog (result=%d)\n", g_common_dialog_result);
    } else if (!strcmp(name, "setAnimationInterval") || !strcmp(name, "FrameIntervalSet")) {
        uint32_t interval = args ? frame_timing::NativeInterval(static_cast<int>(args[0]), !strcmp(name, "setAnimationInterval")) : 0;
        if (interval) {
            g_simulation_interval_us = interval;
            trace("[Timing] native %s(%d): %u us/update\n", name, static_cast<int>(args[0]), interval);
        }
    } else if (!strcmp(name, "DPrint")) {
        trace("[Game] %s\n", args && args[0] ? (const char *)args[0] : "(null)");
    } else if (!strcmp(name, "TextInit")) {
        int width = args ? (int)args[0] : 0;
        int height = args ? (int)args[1] : 0;
        if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
            return;
        free(g_text_pixels);
        g_text_pixels = (uint8_t *)calloc((size_t)width * height, 4);
        g_text_width = g_text_pixels ? width : 0;
        g_text_height = g_text_pixels ? height : 0;
        trace("[JNI] TextInit %dx%d pixels=%p\n", width, height, g_text_pixels);
    } else if (!strcmp(name, "TextBegin")) {
        if (g_text_pixels)
            memset(g_text_pixels, 0, (size_t)g_text_width * g_text_height * 4);
    } else if (!strcmp(name, "TextEnd")) {
        if (!g_text_pixels)
            return;
        glBindTexture(GL_TEXTURE_2D, 2234);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_text_width, g_text_height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, g_text_pixels);
    } else if (!strcmp(name, "TextDraw")) {
        if (!args)
            return;
        static int text_trace_count;
        if (text_trace_count++ < 48) {
            const unsigned char *s = (const unsigned char *)args[0];
            char hex[3 * 48 + 1];
            unsigned int i = 0;
            if (s) {
                for (; i < 48 && s[i]; ++i)
                    snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", s[i]);
            }
            hex[i * 3] = '\0';
            trace("[Text] draw size=%d at %d,%d bytes=%s\n",
                  (int)args[7], (int)args[1], (int)args[2], s ? hex : "(null)");
        }
        DrawTextToCanvas((const char *)args[0], (int)args[1], (int)args[2],
                         (int)args[3], (int)args[4], (int)args[5],
                         (int)args[6], (int)args[7]);
    } else if (!strcmp(name, "KeyBord")) {
        const char *initial = args && args[0] ? (const char *)args[0] : "";
        int id = args ? (int)args[1] : 1;
        int type = id / 10;
        int max_chars = type == 1 ? 10 : (type == 2 ? 40 : 12);
        OpenIme(initial, max_chars, type == 0, false);
    } else if (!strcmp(name, "KeyBordBase")) {
        const char *initial = args && args[7] ? (const char *)args[7] : "";
        int max_chars = args ? (int)args[5] : 12;
        bool numeric = args && (int)args[0] == 2;
        bool is_initial_name = g_initial_name_expected && max_chars == 12 &&
                               !numeric;
        OpenIme(initial, max_chars, is_initial_name, numeric);
    } else if (!strcmp(name, "GooglePlayGamesSignIn")) {
        /* Match the Android callback's terminal failure state.  There is no
         * Play Games service on Vita, but native code must not wait forever. */
        g_google_play_state = 3;
    } else if (!strcmp(name, "GooglePlayGamesSignOut")) {
        g_google_play_state = 0;
    } else if (!strcmp(name, "connectHTTP")) {
        const char *url = args ? (const char *)args[0] : NULL;
        const char *method = args ? (const char *)args[1] : NULL;
        trace("[HTTP] %s %s (offline Vita bridge)\n",
              method ? method : "(null)", url ? url : "(null)");
        g_http_failure_pending = true;
    } else if (!strcmp(name, "DownLoad") ||
               !strcmp(name, "setObbStartDownLoad")) {
        trace("[JNI] %s requested; local asset pack is already mounted\n", name);
    } else if (!strcmp(name, "cancelHTTP") ||
               !strcmp(name, "stopAnimIndicator")) {
        /* Android UI cleanup only.  Vita has no Java HTTP request or Java
         * progress overlay to cancel here. */
    } else {
        static const char *last_unhandled;
        if (name != last_unhandled) {
            trace("[JNI] unhandled static void callback: %s\n", name);
            last_unhandled = name;
        }
    }
}

static jobject CallObjectMethodV(void *env, jobject obj, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    const char *name = (const char *)methodID;
    if (name && !strcmp(name, "getBytes"))
    {
        const char *input = (const char *)obj;
        const char *encoding = args ? (const char *)args[0] : NULL;
        trace("[JNI] String.getBytes(%s) (%u UTF-8 bytes)\n",
              encoding ? encoding : "(null)",
              input ? (unsigned int)strlen(input) : 0);
        return input && encoding
                   ? ConvertBytes((const unsigned char *)input, strlen(input),
                                  "UTF-8", encoding)
                   : NULL;
    }
    return NULL;
}

static jobject CallStaticObjectMethodV(void *env, jclass clazz, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    const char *name = (const char *)methodID;
    if (!name)
        return NULL;
    if (!strcmp(name, "getExternalPath"))
        return (jobject)DATA_PATH;
    if (!strcmp(name, "getExternalFullPath"))
    {
        const char *filename = args ? (const char *)args[0] : NULL;
        if (!filename || strstr(filename, "..") || strchr(filename, '/'))
            return NULL;
        char path[512];
        snprintf(path, sizeof(path), "%s/demand_assets/%s", DATA_PATH, filename);
        if (access(path, F_OK) != 0)
            snprintf(path, sizeof(path), "%s/assets/%s", DATA_PATH, filename);
        return NewStringUTF(NULL, path);
    }
    if (!strcmp(name, "getDeviceModelName"))
        return (jobject)"PlayStation Vita";
    if (!strcmp(name, "getDeviceOSVersion"))
        return (jobject)"3.60";
    if (!strcmp(name, "getLocaleCountry"))
        return (jobject)"US";
    if (!strcmp(name, "getLocaleCurrencyCode"))
        return (jobject)"USD";
    if (!strcmp(name, "getVersionName"))
        return (jobject)"1.05.00";
    if (!strcmp(name, "getDeviceToken"))
        return (jobject)"";
    if (!strcmp(name, "getGUID") || !strcmp(name, "getDeviceAndroidId") ||
        !strcmp(name, "getDeviceSerial"))
        return (jobject)"SF4CE-VITA";
    if (!strcmp(name, "convertString"))
    {
        const char *input = args ? (const char *)args[0] : NULL;
        const char *encoding = args ? (const char *)args[1] : NULL;
        trace("[JNI] convertString UTF-8 -> %s (%u bytes)\n",
              encoding ? encoding : "(null)",
              input ? (unsigned int)strlen(input) : 0);
        return input && encoding
                   ? ConvertBytes((const unsigned char *)input, strlen(input),
                                  "UTF-8", encoding)
                   : NULL;
    }
    if (!strcmp(name, "convertStringFromBytes"))
    {
        jni_bytearray *input = args ? (jni_bytearray *)args[0] : NULL;
        const char *encoding = args ? (const char *)args[1] : NULL;
        trace("[JNI] convertStringFromBytes UTF-16LE -> %s (%d bytes)\n",
              encoding ? encoding : "(null)", input ? input->size : 0);
        return input && encoding
                   ? ConvertBytes(input->elements, (size_t)input->size,
                                  "UTF-16LE", encoding)
                   : NULL;
    }
    return NULL;
}

static jint CallIntMethodV(void *env, jobject obj, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)obj;
    (void)methodID;
    (void)args;
    return 0;
}

static jint CallStaticIntMethodV(void *env, jclass clazz, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    const char *name = (const char *)methodID;
    if (!name)
        return 0;
    if (!strcmp(name, "getRealDisplaySize") ||
        !strcmp(name, "getApplicationDisplaySize"))
        return (SCREEN_W << 16) | (SCREEN_H & 0xffff);
    if (!strcmp(name, "getDeviceApiLevel"))
        return 30;
    if (!strcmp(name, "LanguageGet"))
        return 1;
    if (!strcmp(name, "getBatteryState"))
        return 2;
    if (!strcmp(name, "isMountedSD"))
        return 1;
    if (!strcmp(name, "getSdCardFreeBlocks"))
        return 1024 * 1024;
    if (!strcmp(name, "getVersionCode"))
        return 143;
    if (!strcmp(name, "getKeyDownData_GamceController"))
        return (jint)g_controller_buttons;
    if (!strcmp(name, "getCommonDialogResId"))
        return g_common_dialog_result;
    if (!strcmp(name, "getCharLength")) {
        const char *string = args ? (const char *)args[0] : NULL;
        return string ? (jint)strlen(string) : 0;
    }
    if (!strcmp(name, "getCharacter")) {
        const unsigned char *string = args
                                          ? (const unsigned char *)args[0]
                                          : NULL;
        int index = args ? (int)args[1] : -1;
        return string && index >= 0 && index < (int)strlen((const char *)string)
                   ? string[index]
                   : 0;
    }
    if (!strcmp(name, "CharGet")) {
        int index = args ? (int)args[0] : -1;
        return index >= 0 && index < (int)(sizeof(g_char_table) /
                                           sizeof(g_char_table[0]))
                   ? g_char_table[index]
                   : 0;
    }
    if (!strcmp(name, "CharNumGet"))
        return g_char_count;
    if (!strcmp(name, "CharFinishGet"))
        return g_char_finished;
    if (!strcmp(name, "isGooglePlayGameSignConnect"))
        return g_google_play_state;
    if (!strcmp(name, "getNetworkState"))
        return 0;
    if (!strcmp(name, "isNetworkEnable"))
        return 0;
    if (!strcmp(name, "TextSizeGet")) {
        const char *string = args ? (const char *)args[0] : NULL;
        int size = args ? (int)args[1] : 0;
        int width = 0;
        int height = 0;
        MeasureText(string, size, &width, &height);
        int width10 = width * 10;
        int height10 = height * 10;
        return (width10 << 16) | (height10 & 0xffff);
    }
    if (!strcmp(name, "LocalFileSave")) {
        // The engine has written new bytes into the shared buffer to save.
        g_file_buffer_cache.Reset();
        const char *filename = args ? (const char *)args[0] : NULL;
        int length = args ? (int)args[1] : 0;
        if (!filename || strstr(filename, "..") || strchr(filename, '/') ||
            length < 0 || length > DATA_BUF_SIZE)
            return 0;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", DATA_PATH, filename);
        FILE *file = fopen(path, "wb");
        if (!file)
            return 0;
        int written = (int)fwrite(g_data_buf, 1, (size_t)length, file);
        fclose(file);
        trace("[JNI] LocalFileSave %s (%d/%d bytes)\n", filename, written,
              length);
        return written;
    }
    if (!strcmp(name, "FileLoad") || !strcmp(name, "LocalFileLoad")) {
        const char *filename = args ? (const char *)args[0] : NULL;
        trace("[JNI] %s args=%p filename=%p %s\n", name, args, filename,
              filename ? filename : "(null)");
        int resident = g_file_buffer_cache.Find(filename);
        if (resident >= 0) {
            static unsigned logged_hits;
            if (options.debug_timing && logged_hits++ < 8)
                trace("[Timing] resident file buffer: %s (%d bytes)\n", filename, resident);
            return resident;
        }
        g_file_buffer_cache.Reset();
        void *asset = vita_asset_open(filename, 2, NULL);
        if (!asset)
            return 0;
        long length = vita_asset_get_length(asset);
        if (length < 0 || length > DATA_BUF_SIZE) {
            vita_asset_close(asset);
            return 0;
        }
        int amount = vita_asset_read(asset, g_data_buf, (unsigned int)length);
        vita_asset_close(asset);
        g_file_buffer_cache.Remember(filename, amount, length);
        return amount > 0 ? amount : 0;
    }
    if (!strcmp(name, "PngFileLoad") || !strcmp(name, "LocalPngFileLoad")) {
        const char *filename = args ? (const char *)args[0] : NULL;
        trace("[JNI] %s %s\n", name, filename ? filename : "(null)");
        void *asset = vita_asset_open(filename, 2, NULL);
        if (!asset)
            return 0;

        long encoded_size = vita_asset_get_length(asset);
        if (encoded_size <= 0 || encoded_size > DATA_BUF_SIZE) {
            vita_asset_close(asset);
            return 0;
        }
        unsigned char *encoded = (unsigned char *)malloc((size_t)encoded_size);
        if (!encoded) {
            vita_asset_close(asset);
            return 0;
        }
        int amount = vita_asset_read(asset, encoded, (unsigned int)encoded_size);
        vita_asset_close(asset);
        if (amount != encoded_size) {
            free(encoded);
            return 0;
        }

        png_image image;
        memset(&image, 0, sizeof(image));
        image.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_memory(&image, encoded,
                                               (png_alloc_size_t)encoded_size)) {
            trace("[PNG] decode header failed for %s: %s\n", filename,
                  image.message);
            free(encoded);
            return 0;
        }
        image.format = PNG_FORMAT_RGBA;
        png_alloc_size_t decoded_size = PNG_IMAGE_SIZE(image);
        unsigned char *pixels = (unsigned char *)malloc(decoded_size);
        if (!pixels || !png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
            trace("[PNG] decode failed for %s: %s\n", filename, image.message);
            free(pixels);
            png_image_free(&image);
            free(encoded);
            return 0;
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)image.width,
                     (int)image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned int gl_error = glGetError();
        trace("[PNG] uploaded %s (%ux%u), GL=%X\n", filename,
              image.width, image.height, gl_error);
        jint result = ((jint)image.width << 16) | ((jint)image.height & 0xffff);
        free(pixels);
        png_image_free(&image);
        free(encoded);
        return gl_error ? 0 : result;
    }
    if (!strcmp(name, "getDisplayCutoutLeft") ||
        !strcmp(name, "getDisplayCutoutRight"))
        return 0;
    static const char *last_unhandled;
    if (name != last_unhandled) {
        trace("[JNI] unhandled static int callback: %s\n", name);
        last_unhandled = name;
    }
    return 0;
}

static jlong CallStaticLongMethodV(void *env, jclass clazz, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    (void)args;
    const char *name = (const char *)methodID;
    if (name && !strcmp(name, "TimeGet")) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    if (name && (!strcmp(name, "getAssetPackDownloadedByte") ||
                 !strcmp(name, "getAssetPackDownloadByteMax") ||
                 !strcmp(name, "getObbDownByte") ||
                 !strcmp(name, "getObbDownByteMax")))
        return 494305698LL;
    return 0;
}

static jlong CallLongMethodV(void *env, jobject obj, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)obj;
    (void)methodID;
    (void)args;
    return 0;
}

static jboolean CallBooleanMethodV(void *env, jobject obj, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)obj;
    (void)methodID;
    (void)args;
    return 0;
}

static jboolean CallStaticBooleanMethodV(void *env, jclass clazz, jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    const char *name = (const char *)methodID;
    if (!name)
        return 0;
    if (!strcmp(name, "getTouchBackKey"))
        return g_menu_back;
    if (!strcmp(name, "isObbCheckExist") || !strcmp(name, "isObbMounted") ||
        !strcmp(name, "isDownLoadedAssetPack") ||
        !strncmp(name, "checkPermission", 15))
        return 1;
    if (!strcmp(name, "isMovieExist"))
        return IntroMovieAvailable();
    if (!strcmp(name, "isPlayMovie"))
        return IntroMoviePlaying();
    if (!strcmp(name, "isFileExist")) {
        const char *filename = args ? (const char *)args[0] : NULL;
        void *asset = vita_asset_open(filename, 2, NULL);
        if (!asset)
            return 0;
        vita_asset_close(asset);
        return 1;
    }
    return 0;
}

static SOFTFP_ABI jfloat CallFloatMethodV(void *env, jobject obj,
                                          jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)obj;
    (void)methodID;
    (void)args;
    return 0.0f;
}

static SOFTFP_ABI jfloat CallStaticFloatMethodV(void *env, jclass clazz,
                                                jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    (void)args;
    const char *name = (const char *)methodID;
    if (name && !strcmp(name, "getBatteryLevel"))
        return 1.0f;
    if (name && !strcmp(name, "getLeftStickValueX_GamceController"))
        return g_left_stick_x;
    if (name && !strcmp(name, "getLeftStickValueY_GamceController"))
        return g_left_stick_y;
    if (name && !strcmp(name, "getRightStickValueX_GamceController"))
        return g_right_stick_x;
    if (name && !strcmp(name, "getRightStickValueY_GamceController"))
        return g_right_stick_y;
    return 0.0f;
}

static SOFTFP_ABI jdouble CallStaticDoubleMethodV(void *env, jclass clazz,
                                                  jmethodID methodID, uintptr_t *args)
{
    (void)env;
    (void)clazz;
    (void)methodID;
    (void)args;
    return 0.0;
}

static void *GetDirectBufferAddress(void *env, jobject buf)
{
    (void)env;
    // For direct ByteBuffers, return the buffer pointer itself
    return (void *)buf;
}

static jlong GetDirectBufferCapacity(void *env, jobject buf)
{
    (void)env;
    if (buf == g_data_buf)
        return DATA_BUF_SIZE;
    if (buf == g_send_buf)
        return SEND_BUF_SIZE;
    if (buf == g_receive_buf)
        return RECEIVE_BUF_SIZE;
    return 0;
}

static int EnsureLocalCapacity(void *env, jint capacity)
{
    (void)env;
    (void)capacity;
    return 0;
}

// ============================================================
// Initialize fake JNIEnv vtable
// ============================================================

void InitJNIEnv(void)
{
    memset(fake_env, 0, sizeof(fake_env));
    *(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env;

    memset(fake_vm, 0, sizeof(fake_vm));
    *(uintptr_t *)(fake_vm + 0x00) = (uintptr_t)fake_vm;
    *(uintptr_t *)(fake_vm + 0x10) = (uintptr_t)JavaVMGetEnv;
    *(uintptr_t *)(fake_vm + 0x14) = (uintptr_t)JavaVMDetach;
    *(uintptr_t *)(fake_vm + 0x18) = (uintptr_t)JavaVMGetEnv;

    // JNI vtable offsets (based on Android ARM32 JNIEnv layout)
    // FindClass at offset 0x18
    *(uintptr_t *)(fake_env + 0x18) = (uintptr_t)FindClass;

    // NewGlobalRef at 0x54
    *(uintptr_t *)(fake_env + 0x54) = (uintptr_t)NewGlobalRef;

    // DeleteGlobalRef/DeleteLocalRef at 0x58/0x5C.
    *(uintptr_t *)(fake_env + 0x58) = (uintptr_t)DeleteGlobalRef;
    *(uintptr_t *)(fake_env + 0x5C) = (uintptr_t)DeleteLocalRef;

    // NewObjectV at 0x74
    *(uintptr_t *)(fake_env + 0x74) = (uintptr_t)NewObjectV;

    // GetMethodID at 0x84
    *(uintptr_t *)(fake_env + 0x84) = (uintptr_t)GetMethodID;

    // CallObjectMethodV at 0x8C
    *(uintptr_t *)(fake_env + 0x8C) = (uintptr_t)CallObjectMethodV;

    // CallBooleanMethodV at 0x98
    *(uintptr_t *)(fake_env + 0x98) = (uintptr_t)CallBooleanMethodV;

    // CallIntMethodV at 0xC8
    *(uintptr_t *)(fake_env + 0xC8) = (uintptr_t)CallIntMethodV;

    // CallLongMethodV at 0xD4
    *(uintptr_t *)(fake_env + 0xD4) = (uintptr_t)CallLongMethodV;

    // CallFloatMethodV at 0xE0
    *(uintptr_t *)(fake_env + 0xE0) = (uintptr_t)CallFloatMethodV;

    // CallVoidMethodV at 0xF8
    *(uintptr_t *)(fake_env + 0xF8) = (uintptr_t)CallVoidMethodV;

    // GetStaticMethodID at 0x1C4
    *(uintptr_t *)(fake_env + 0x1C4) = (uintptr_t)GetStaticMethodID;

    // CallStaticObjectMethodV at 0x1CC
    *(uintptr_t *)(fake_env + 0x1CC) = (uintptr_t)CallStaticObjectMethodV;

    // CallStaticBooleanMethodV at 0x1D8
    *(uintptr_t *)(fake_env + 0x1D8) = (uintptr_t)CallStaticBooleanMethodV;

    // CallStaticIntMethodV at 0x208
    *(uintptr_t *)(fake_env + 0x208) = (uintptr_t)CallStaticIntMethodV;

    // CallStaticLongMethodV at 0x214
    *(uintptr_t *)(fake_env + 0x214) = (uintptr_t)CallStaticLongMethodV;

    // CallStaticFloatMethodV/CallStaticDoubleMethodV at 0x220/0x22C
    *(uintptr_t *)(fake_env + 0x220) = (uintptr_t)CallStaticFloatMethodV;
    *(uintptr_t *)(fake_env + 0x22C) = (uintptr_t)CallStaticDoubleMethodV;

    // CallStaticVoidMethodV at 0x238
    *(uintptr_t *)(fake_env + 0x238) = (uintptr_t)CallStaticVoidMethodV;

    // NewStringUTF at 0x29C
    *(uintptr_t *)(fake_env + 0x29C) = (uintptr_t)NewStringUTF;

    // GetStringUTFLength at 0x2A0
    *(uintptr_t *)(fake_env + 0x2A0) = (uintptr_t)GetStringUTFLength;

    // GetStringUTFChars at 0x2A4
    *(uintptr_t *)(fake_env + 0x2A4) = (uintptr_t)GetStringUTFChars;

    // ReleaseStringUTFChars at 0x2A8
    *(uintptr_t *)(fake_env + 0x2A8) = (uintptr_t)ReleaseStringUTFChars;

    // GetArrayLength at 0x2AC
    *(uintptr_t *)(fake_env + 0x2AC) = (uintptr_t)GetArrayLength;

    // NewByteArray at 0x2C0
    *(uintptr_t *)(fake_env + 0x2C0) = (uintptr_t)NewByteArray;

    // GetByteArrayElements at 0x2E0
    *(uintptr_t *)(fake_env + 0x2E0) = (uintptr_t)GetByteArrayElements;

    // ReleaseByteArrayElements at 0x300
    *(uintptr_t *)(fake_env + 0x300) = (uintptr_t)ReleaseByteArrayElements;

    // SetByteArrayRegion at 0x340
    *(uintptr_t *)(fake_env + 0x340) = (uintptr_t)SetByteArrayRegion;

    // GetByteArrayRegion at 0x320
    *(uintptr_t *)(fake_env + 0x320) = (uintptr_t)GetByteArrayRegion;

    // GetShortArrayElements at 0x2E8
    *(uintptr_t *)(fake_env + 0x2E8) = (uintptr_t)GetShortArrayElements;

    // ReleaseShortArrayElements at 0x308
    *(uintptr_t *)(fake_env + 0x308) = (uintptr_t)ReleaseShortArrayElements;

    // GetDirectBufferAddress/GetDirectBufferCapacity at 0x398/0x39C.
    *(uintptr_t *)(fake_env + 0x398) = (uintptr_t)GetDirectBufferAddress;
    *(uintptr_t *)(fake_env + 0x39C) = (uintptr_t)GetDirectBufferCapacity;

    // EnsureLocalCapacity at 0x68
    *(uintptr_t *)(fake_env + 0x68) = (uintptr_t)EnsureLocalCapacity;
}

void *Android_JNI_GetEnv(void)
{
    return fake_env;
}

// ============================================================
// Android log stub
// ============================================================

extern "C"
{
    int __android_log_print(int prio, const char *tag, const char *fmt, ...)
    {
        va_list list;
        char string[512];
        va_start(list, fmt);
        vsprintf(string, fmt, list);
        va_end(list);
        printf("[%s] %s\n", tag ? tag : "SF4CE", string);
        return 0;
    }

    void __assert2(const char *file, int line, const char *func, const char *expr)
    {
        printf("ASSERTION FAILED:\n  %s:%d (%s): %s\n", file, line, func, expr);
    }
}

// ============================================================
// Memory function wrappers (use SCE optimized versions)
// ============================================================

extern "C"
{
    void *__wrap_memcpy(void *dest, const void *src, size_t n)
    {
        return sceClibMemcpy(dest, src, n);
    }
    void *__wrap_memmove(void *dest, const void *src, size_t n)
    {
        return sceClibMemmove(dest, src, n);
    }
    void *__wrap_memset(void *s, int c, size_t n)
    {
        return sceClibMemset(s, c, n);
    }
}

// ============================================================
// Standard library stubs
// ============================================================

int ret0(void) { return 0; }
int ret1(void) { return 1; }
int retminus1(void) { return -1; }
void *retNULL(void) { return NULL; }

static int stack_chk_guard_fake = 0x42424242;
static FILE android_sF_fake[3];
static FILE *android_stderr_fake = NULL;

extern "C" int *__errno_fake(void) { return &errno; }

extern "C" int __android_log_write(int prio, const char *tag, const char *text)
{
    (void)prio;
    printf("[%s] %s\n", tag ? tag : "SF4CE", text ? text : "");
    return 0;
}

extern "C" void *__memcpy_chk(void *dst, const void *src, size_t len, size_t dst_len)
{
    if (len > dst_len)
        abort();
    return memcpy(dst, src, len);
}

extern "C" void *__memmove_chk(void *dst, const void *src, size_t len, size_t dst_len)
{
    if (len > dst_len)
        abort();
    return memmove(dst, src, len);
}

extern "C" void *__memset_chk(void *dst, int value, size_t len, size_t dst_len)
{
    if (len > dst_len)
        abort();
    return memset(dst, value, len);
}

extern "C" char *__strcpy_chk(char *dst, const char *src, size_t dst_len)
{
    size_t len = strlen(src) + 1;
    if (len > dst_len)
        abort();
    return (char *)memcpy(dst, src, len);
}

extern "C" char *__strncpy_chk(char *dst, const char *src, size_t len, size_t dst_len)
{
    if (len > dst_len)
        abort();
    return strncpy(dst, src, len);
}

extern "C" size_t __strlen_chk(const char *str, size_t max_len)
{
    size_t len = 0;
    while (len < max_len && str[len])
        ++len;
    if (len == max_len)
        abort();
    return len;
}

extern "C" SOFTFP_ABI int __vsnprintf_chk(char *dst, size_t len, int flags,
                                             size_t dst_len, const char *fmt,
                                             va_list args)
{
    (void)flags;
    if (len > dst_len)
        abort();
    static int trace_count;
    uintptr_t cursor = 0;
    static_assert(sizeof(args) == sizeof(cursor), "unexpected Vita va_list");
    memcpy(&cursor, &args, sizeof(cursor));
    uintptr_t words[6] = {};
    if (cursor >= 0x80000000u && cursor <= 0xA0000000u)
        memcpy(words, (const void *)cursor, sizeof(words));
    int result = vsnprintf(dst, len, fmt, args);
    if (trace_count++ < 96)
        trace("[Format] vsnprintf dst=%p len=%u cap=%u fmt=%p \"%s\" "
              "args=%p words=%08X,%08X,%08X,%08X,%08X,%08X -> %d \"%s\"\n",
              dst, (unsigned int)len, (unsigned int)dst_len, fmt,
              fmt ? fmt : "(null)", (void *)cursor,
              (unsigned int)words[0], (unsigned int)words[1],
              (unsigned int)words[2], (unsigned int)words[3],
              (unsigned int)words[4], (unsigned int)words[5], result,
              dst ? dst : "(null)");
    return result;
}

extern "C" SOFTFP_ABI int __vsprintf_chk(char *dst, int flags,
                                            size_t dst_len, const char *fmt,
                                            va_list args)
{
    (void)flags;
    static int trace_count;
    uintptr_t cursor = 0;
    static_assert(sizeof(args) == sizeof(cursor), "unexpected Vita va_list");
    memcpy(&cursor, &args, sizeof(cursor));
    uintptr_t words[6] = {};
    if (cursor >= 0x80000000u && cursor <= 0xA0000000u)
        memcpy(words, (const void *)cursor, sizeof(words));
    /* Bionic uses SIZE_MAX when fortify cannot determine the destination
     * object's size.  Passing that sentinel to newlib's vsnprintf is not
     * equivalent: newlib rejects sizes larger than INT_MAX with EOVERFLOW,
     * leaving the destination untouched.  In the unknown-size case this is
     * the ordinary vsprintf operation Android requested. */
    int result = dst_len == SIZE_MAX ? vsprintf(dst, fmt, args)
                                     : vsnprintf(dst, dst_len, fmt, args);
    if (trace_count++ < 96)
        trace("[Format] vsprintf dst=%p cap=%u fmt=%p \"%s\" "
              "args=%p words=%08X,%08X,%08X,%08X,%08X,%08X -> %d \"%s\"\n",
              dst, (unsigned int)dst_len, fmt, fmt ? fmt : "(null)",
              (void *)cursor, (unsigned int)words[0],
              (unsigned int)words[1], (unsigned int)words[2],
              (unsigned int)words[3], (unsigned int)words[4],
              (unsigned int)words[5], result, dst ? dst : "(null)");
    return result;
}

extern "C" size_t __fread_chk(void *dst, size_t dst_len, size_t size,
                                size_t count, FILE *stream)
{
    if (size && count > dst_len / size)
        abort();
    return fread(dst, size, count, stream);
}

extern "C" size_t __fwrite_chk(const void *src, size_t src_len, size_t size,
                                 size_t count, FILE *stream)
{
    if (size && count > src_len / size)
        abort();
    return fwrite(src, size, count, stream);
}

extern "C" int __open_2(const char *path, int flags)
{
    return open(path, flags);
}

extern "C" ssize_t __read_chk(int fd, void *dst, size_t count, size_t dst_len)
{
    if (count > dst_len)
        abort();
    return read(fd, dst, count);
}

extern "C" void aeabi_memclr(void *dst, size_t len) { memset(dst, 0, len); }
extern "C" void aeabi_memcpy(void *dst, const void *src, size_t len) { memcpy(dst, src, len); }
extern "C" void aeabi_memmove(void *dst, const void *src, size_t len) { memmove(dst, src, len); }

extern "C" int register_atfork_fake(void *, void *, void *) { return 0; }
extern "C" void *dlopen_fake(const char *, int) { return NULL; }
extern "C" void *dlsym_fake(void *, const char *) { return NULL; }
extern "C" int dlclose_fake(void *) { return 0; }
extern "C" const char *dlerror_fake(void) { return "dynamic loading unavailable"; }

int gettid(void) { return sceKernelGetThreadId(); }

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    return malloc(length);
}

int munmap(void *addr, size_t length)
{
    (void)length;
    free(addr);
    return 0;
}

// ============================================================
// pthread stubs (Android uses different mutex semantics)
// ============================================================

int pthread_mutex_init_fake(pthread_mutex_t **uid, const pthread_mutexattr_t *mutexattr)
{
    pthread_mutex_t *m = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
    if (!m)
        return -1;
    *m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init(m, mutexattr);
    *uid = m;
    return 0;
}

int pthread_mutex_destroy_fake(pthread_mutex_t **uid)
{
    if (uid && *uid)
    {
        pthread_mutex_destroy(*uid);
        free(*uid);
        *uid = NULL;
    }
    return 0;
}

int pthread_mutex_lock_fake(pthread_mutex_t **uid)
{
    if (!*uid)
        pthread_mutex_init_fake(uid, NULL);
    return pthread_mutex_lock(*uid);
}

int pthread_mutex_unlock_fake(pthread_mutex_t **uid)
{
    if (!*uid)
        pthread_mutex_init_fake(uid, NULL);
    return pthread_mutex_unlock(*uid);
}

int pthread_mutex_trylock_fake(pthread_mutex_t **uid)
{
    if (!*uid)
        pthread_mutex_init_fake(uid, NULL);
    return pthread_mutex_trylock(*uid);
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *condattr)
{
    (void)condattr;
    pthread_cond_t *c = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
    if (!c)
        return -1;
    *c = PTHREAD_COND_INITIALIZER;
    pthread_cond_init(c, NULL);
    *cnd = c;
    return 0;
}

int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx)
{
    if (!*cnd)
        pthread_cond_init_fake(cnd, NULL);
    return pthread_cond_wait(*cnd, *mtx);
}

int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx,
                                const struct timespec *t)
{
    if (!*cnd)
        pthread_cond_init_fake(cnd, NULL);
    return pthread_cond_timedwait(*cnd, *mtx, t);
}

int pthread_cond_signal_fake(pthread_cond_t **cnd)
{
    if (!*cnd)
        pthread_cond_init_fake(cnd, NULL);
    return pthread_cond_signal(*cnd);
}

int pthread_cond_broadcast_fake(pthread_cond_t **cnd)
{
    if (!*cnd)
        pthread_cond_init_fake(cnd, NULL);
    return pthread_cond_broadcast(*cnd);
}

int pthread_cond_destroy_fake(pthread_cond_t **cnd)
{
    if (cnd && *cnd)
    {
        pthread_cond_destroy(*cnd);
        free(*cnd);
        *cnd = NULL;
    }
    return 0;
}

int pthread_create_fake(pthread_t *thread, const void *unused, void *entry, void *arg)
{
    (void)unused;
    return pthread_create(thread, NULL, (void *(*)(void *))entry, arg);
}

int pthread_once_fake(volatile int *once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine)
        return -1;
    if (__sync_lock_test_and_set(once_control, 1) == 0)
        (*init_routine)();
    return 0;
}

// ============================================================
// Input mapping — Vita controls → touch coordinates
// ============================================================

void mapVitaInputToTouch(SceCtrlData *pad)
{
    static bool was_down;
    static short last_x;
    static short last_y;
    SceTouchData touch;
    memset(&touch, 0, sizeof(touch));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (g_debug_input_active) {
        memset(&touch, 0, sizeof(touch));
        if (g_debug_input.touch) {
            touch.reportNum = 1;
            touch.report[0].x = g_debug_input.x * 2;
            touch.report[0].y = g_debug_input.y * 2;
        }
    }

    const bool diagnostic_cursor = options.debug_touch_cursor &&
                                   (pad->buttons & SCE_CTRL_SELECT);
    float dx = NormalizeStick(pad->lx), dy = NormalizeStick(pad->ly);
    if (pad->buttons & SCE_CTRL_LEFT) dx = -1;
    if (pad->buttons & SCE_CTRL_RIGHT) dx = 1;
    if (pad->buttons & SCE_CTRL_UP) dy = -1;
    if (pad->buttons & SCE_CTRL_DOWN) dy = 1;
    bool menu = MenuPointerEnabled() && !diagnostic_cursor;
    g_menu_pointer.Update(sceKernelGetProcessTimeWide(), menu, touch.reportNum > 0,
                          dx, dy, pad->buttons & SCE_CTRL_CROSS,
                          pad->buttons & SCE_CTRL_CIRCLE);
    g_menu_back = g_menu_pointer.back;
    if (menu) {
        // The native game still owns click handling, scrolling and dragging.
        // Do not deliver the same buttons again through its combat controller.
        g_controller_buttons = 0;
        g_left_stick_x = g_left_stick_y = 0;
        g_right_stick_x = g_right_stick_y = 0;
    }
    if (g_menu_pointer.down) {
        touch.reportNum = 1;
        touch.report[0].x = (int)(g_menu_pointer.x * 2);
        touch.report[0].y = (int)(g_menu_pointer.y * 2);
    }


    // Optional diagnostic pointer: hold Select, move with the D-pad and tap
    // Cross. This travels through the same Android touch bridge as the panel,
    // and makes touchscreen-only menus testable using a keyboard/controller.
    static int cursor_x = 480;
    static int cursor_y = 272;
    if (options.debug_touch_cursor && (pad->buttons & SCE_CTRL_SELECT))
    {
        if (pad->buttons & SCE_CTRL_LEFT) cursor_x -= 8;
        if (pad->buttons & SCE_CTRL_RIGHT) cursor_x += 8;
        if (pad->buttons & SCE_CTRL_UP) cursor_y -= 8;
        if (pad->buttons & SCE_CTRL_DOWN) cursor_y += 8;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x > 959) cursor_x = 959;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y > 543) cursor_y = 543;
        memset(&touch, 0, sizeof(touch));
        if (pad->buttons & SCE_CTRL_CROSS)
        {
            touch.reportNum = 1;
            touch.report[0].x = cursor_x * 2;
            touch.report[0].y = cursor_y * 2;
        }
        static int last_x = -1, last_y = -1;
        if (cursor_x != last_x || cursor_y != last_y)
        {
            trace("[TouchCursor] %d,%d\n", cursor_x, cursor_y);
            last_x = cursor_x;
            last_y = cursor_y;
        }
    }

    short *slot = &g_touch_tbl[0];
    if (touch.reportNum > 0)
    {
        short x;
        short y;
        x = (short)((uint32_t)touch.report[0].x * SCREEN_W * 10 / 1920);
        y = (short)((uint32_t)touch.report[0].y * SCREEN_H * 10 / 1088);
        if (!was_down)
        {
            slot[0] = slot[2] = slot[4] = x;
            slot[1] = slot[3] = slot[5] = y;
            slot[6] = 1; // ACTION_DOWN
            slot[7] = 1;
            trace("[Touch] down %d,%d\n", x / 10, y / 10);
        }
        else if (x != last_x || y != last_y)
        {
            slot[4] = slot[2];
            slot[5] = slot[3];
            slot[2] = x;
            slot[3] = y;
            slot[6] = 2; // ACTION_MOVE
            slot[7] = 1;
        }
        else
        {
            /* GlTest2Renderer.TouchSet only updates the table when Android
             * delivers an event.  Keep the last DOWN/MOVE action while a
             * finger is stationary so CTouch can advance its internal
             * pressed/held state exactly as it does on Android. */
            slot[7] = 1;
        }
        last_x = x;
        last_y = y;
        was_down = true;
    }
    else if (was_down)
    {
        slot[4] = slot[2];
        slot[5] = slot[3];
        slot[2] = last_x;
        slot[3] = last_y;
        slot[6] = 3; // ACTION_UP
        slot[7] = 0;
        was_down = false;
        trace("[Touch] up %d,%d\n", last_x / 10, last_y / 10);
    }
    else
    {
        /* Likewise, retain ACTION_UP until the next event. */
    }
}

// ============================================================
// Default dynamic library symbols
// ============================================================

extern "C" void vita_glShadeModel(GLenum mode)
{
    /* vitaGL already falls back from GL_FLAT to GL_SMOOTH, but emits a
     * warning for every call.  Select the effective mode directly. */
    glShadeModel(mode == GL_FLAT ? GL_SMOOTH : mode);
}

extern "C"
{
    extern void *_ZdaPv;
    extern void *_ZdlPv;
    extern void *_Znaj;
    extern void *_Znwj;
    extern void *__aeabi_atexit;
    extern void *__aeabi_d2ulz;
    extern void *__aeabi_dcmpgt;
    extern void *__aeabi_dmul;
    extern void *__aeabi_f2d;
    extern void *__aeabi_fadd;
    extern void *__aeabi_fsub;
    extern void *__aeabi_idiv;
    extern void *__aeabi_idivmod;
    extern void *__aeabi_l2d;
    extern void *__aeabi_l2f;
    extern void *__aeabi_ldivmod;
    extern void *__aeabi_uidiv;
    extern void *__aeabi_uidivmod;
    extern void *__aeabi_uldivmod;
    extern void *__cxa_atexit;
    extern void *__cxa_finalize;
    extern void *__stack_chk_fail;
}

static so_default_dynlib default_dynlib[] = {
    // ============================================================
    // JNI symbols — the .so expects these from libandroid.so
    // ============================================================
    {"Android_JNI_GetEnv", (uintptr_t)&Android_JNI_GetEnv},

    // ============================================================
    // pthread symbols — Android uses different semantics
    // ============================================================
    {"pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake},
    {"pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake},
    {"pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake},
    {"pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake},
    {"pthread_cond_init", (uintptr_t)&pthread_cond_init_fake},
    {"pthread_cond_wait", (uintptr_t)&pthread_cond_wait_fake},
    {"pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_fake},
    {"pthread_cond_signal", (uintptr_t)&pthread_cond_signal_fake},
    {"pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake},
    {"pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake},
    {"pthread_create", (uintptr_t)&pthread_create_fake},
    {"pthread_once", (uintptr_t)&pthread_once_fake},

    // ============================================================
    // System symbols
    // ============================================================
    {"gettid", (uintptr_t)&gettid},
    {"mmap", (uintptr_t)&mmap},
    {"munmap", (uintptr_t)&munmap},
    {"__android_log_print", (uintptr_t)&__android_log_print},

    // ============================================================
    // C++ symbols
    // ============================================================
    {"_ZdaPv", (uintptr_t)&_ZdaPv},
    {"_ZdlPv", (uintptr_t)&_ZdlPv},
    {"_Znaj", (uintptr_t)&_Znaj},
    {"_Znwj", (uintptr_t)&_Znwj},
    {"__aeabi_atexit", (uintptr_t)&__aeabi_atexit},
    {"__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz},
    {"__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt},
    {"__aeabi_dmul", (uintptr_t)&__aeabi_dmul},
    {"__aeabi_f2d", (uintptr_t)&__aeabi_f2d},
    {"__aeabi_fadd", (uintptr_t)&__aeabi_fadd},
    {"__aeabi_fsub", (uintptr_t)&__aeabi_fsub},
    {"__aeabi_idiv", (uintptr_t)&__aeabi_idiv},
    {"__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod},
    {"__aeabi_l2d", (uintptr_t)&__aeabi_l2d},
    {"__aeabi_l2f", (uintptr_t)&__aeabi_l2f},
    {"__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod},
    {"__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv},
    {"__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod},
    {"__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod},
    {"__cxa_atexit", (uintptr_t)&__cxa_atexit},
    {"__cxa_finalize", (uintptr_t)&__cxa_finalize},
    {"__stack_chk_fail", (uintptr_t)&__stack_chk_fail},
    {"__stack_chk_guard", (uintptr_t)&stack_chk_guard_fake},
    {"__errno", (uintptr_t)&__errno_fake},
    {"__sF", (uintptr_t)&android_sF_fake},
    {"stderr", (uintptr_t)&android_stderr_fake},
    {"__register_atfork", (uintptr_t)&register_atfork_fake},
    {"__memcpy_chk", (uintptr_t)&__memcpy_chk},
    {"__memmove_chk", (uintptr_t)&__memmove_chk},
    {"__memset_chk", (uintptr_t)&__memset_chk},
    {"__strcpy_chk", (uintptr_t)&__strcpy_chk},
    {"__strncpy_chk", (uintptr_t)&__strncpy_chk},
    {"__strncpy_chk2", (uintptr_t)&__strncpy_chk},
    {"__strlen_chk", (uintptr_t)&__strlen_chk},
    {"__strchr_chk", (uintptr_t)&strchr},
    {"__strrchr_chk", (uintptr_t)&strrchr},
    {"__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk},
    {"__vsprintf_chk", (uintptr_t)&__vsprintf_chk},
    {"__fread_chk", (uintptr_t)&__fread_chk},
    {"__fwrite_chk", (uintptr_t)&__fwrite_chk},
    {"__open_2", (uintptr_t)&__open_2},
    {"__read_chk", (uintptr_t)&__read_chk},
    {"__recvfrom_chk", (uintptr_t)&retminus1},
    {"__sendto_chk", (uintptr_t)&retminus1},
    {"__FD_SET_chk", (uintptr_t)&ret0},
    {"__FD_ISSET_chk", (uintptr_t)&ret0},
    {"__gnu_strerror_r", (uintptr_t)&ret0},
    {"__android_log_write", (uintptr_t)&__android_log_write},
    {"dl_unwind_find_exidx", (uintptr_t)&retNULL},
    {"__aeabi_memclr", (uintptr_t)&aeabi_memclr},
    {"__aeabi_memclr4", (uintptr_t)&aeabi_memclr},
    {"__aeabi_memclr8", (uintptr_t)&aeabi_memclr},
    {"__aeabi_memcpy", (uintptr_t)&aeabi_memcpy},
    {"__aeabi_memcpy4", (uintptr_t)&aeabi_memcpy},
    {"__aeabi_memmove", (uintptr_t)&aeabi_memmove},
    {"__aeabi_memmove4", (uintptr_t)&aeabi_memmove},
    {"__aeabi_memmove8", (uintptr_t)&aeabi_memmove},

    // ============================================================
    // libc / libm / zlib
    // ============================================================
    {"abort", (uintptr_t)&abort},
    {"abs", (uintptr_t)(int (*)(int))&abs},
    {"acosf", WRAP(acosf)},
    {"asinf", WRAP(asinf)},
    {"atan", WRAP((double (*)(double))atan)},
    {"atan2", WRAP((double (*)(double, double))atan2)},
    {"atan2f", WRAP(atan2f)},
    {"atof", (uintptr_t)&atof},
    {"atoi", (uintptr_t)&atoi},
    {"calloc", (uintptr_t)&calloc},
    {"ceil", WRAP((double (*)(double))ceil)},
    {"clock_gettime", (uintptr_t)&clock_gettime},
    {"close", (uintptr_t)&close},
    {"closedir", (uintptr_t)&bionic_closedir},
    {"cos", WRAP((double (*)(double))cos)},
    {"cosf", WRAP(cosf)},
    {"dlclose", (uintptr_t)&dlclose_fake},
    {"dlerror", (uintptr_t)&dlerror_fake},
    {"dlopen", (uintptr_t)&dlopen_fake},
    {"dlsym", (uintptr_t)&dlsym_fake},
    {"exp2f", WRAP(exp2f)},
    {"expf", WRAP(expf)},
    {"fclose", (uintptr_t)&fclose},
    {"ferror", (uintptr_t)&ferror},
    {"fflush", (uintptr_t)&fflush},
    {"fgetc", (uintptr_t)&fgetc},
    {"floor", WRAP((double (*)(double))floor)},
    {"floorf", WRAP(floorf)},
    {"fopen", (uintptr_t)&fopen},
    {"fprintf", (uintptr_t)&fprintf},
    {"fputc", (uintptr_t)&fputc},
    {"fputs", (uintptr_t)&fputs},
    {"fread", (uintptr_t)&fread},
    {"free", (uintptr_t)&free},
    {"fseek", (uintptr_t)&fseek},
    {"fseeko", (uintptr_t)&bionic_fseeko},
    {"ftell", (uintptr_t)&ftell},
    {"ftello", (uintptr_t)&bionic_ftello},
    {"fwrite", (uintptr_t)&fwrite},
    {"getcwd", (uintptr_t)&bionic_getcwd},
    {"gettimeofday", (uintptr_t)&gettimeofday},
    {"ldexpf", WRAP(ldexpf)},
    {"log10", WRAP((double (*)(double))log10)},
    {"log10f", WRAP(log10f)},
    {"logf", WRAP(logf)},
    {"malloc", (uintptr_t)&malloc},
    {"memchr", (uintptr_t)&memchr},
    {"memcmp", (uintptr_t)&memcmp},
    {"memcpy", (uintptr_t)&memcpy},
    {"memmem", (uintptr_t)&bionic_memmem},
    {"memmove", (uintptr_t)&memmove},
    {"memset", (uintptr_t)&memset},
    {"mkdir", (uintptr_t)&mkdir},
    {"nanosleep", (uintptr_t)&nanosleep},
    {"open", (uintptr_t)&open},
    {"opendir", (uintptr_t)&bionic_opendir},
    {"pow", WRAP((double (*)(double, double))pow)},
    {"powf", WRAP(powf)},
    {"qsort", (uintptr_t)&qsort},
    {"raise", (uintptr_t)&raise},
    {"rand", (uintptr_t)&rand},
    {"read", (uintptr_t)&read},
    {"readdir", (uintptr_t)&bionic_readdir},
    {"realloc", (uintptr_t)&realloc},
    {"rewind", (uintptr_t)&rewind},
    {"rmdir", (uintptr_t)&rmdir},
    {"signal", (uintptr_t)&signal},
    {"sin", WRAP((double (*)(double))sin)},
    {"sinf", WRAP(sinf)},
    {"sqrtf", WRAP(sqrtf)},
    {"srand", (uintptr_t)&srand},
    {"sscanf", (uintptr_t)&sscanf},
    {"stat", (uintptr_t)&bionic_stat},
    {"strcasecmp", (uintptr_t)&strcasecmp},
    {"strchr", (uintptr_t)&strchr},
    {"strcmp", (uintptr_t)&strcmp},
    {"strcpy", (uintptr_t)&strcpy},
    {"strerror", (uintptr_t)&strerror},
    {"strlen", (uintptr_t)&strlen},
    {"strncasecmp", (uintptr_t)&strncasecmp},
    {"strncmp", (uintptr_t)&strncmp},
    {"strncpy", (uintptr_t)&strncpy},
    {"strrchr", (uintptr_t)&strrchr},
    {"strstr", (uintptr_t)&strstr},
    {"strtod", WRAP(strtod)},
    {"strtol", (uintptr_t)&strtol},
    {"tanf", WRAP(tanf)},
    {"time", (uintptr_t)&time},
    {"tolower", (uintptr_t)&tolower},
    {"toupper", (uintptr_t)&toupper},
    {"unlink", (uintptr_t)&unlink},
    {"usleep", (uintptr_t)&bionic_usleep},
    {"vfprintf", (uintptr_t)&vfprintf},
    {"vsnprintf", (uintptr_t)&vsnprintf},
    {"compress", (uintptr_t)&compress},
    {"crc32", (uintptr_t)&crc32},
    {"uncompress", (uintptr_t)&uncompress},

    // Imports used by the APK's Android libc++_shared.so (NDK r22b).
    {"__aeabi_memset", (uintptr_t)&bionic_aeabi_memset},
    {"__aeabi_memset8", (uintptr_t)&bionic_aeabi_memset8},
    {"__assert2", (uintptr_t)&bionic_assert2},
    {"__cxa_thread_atexit_impl", (uintptr_t)&bionic_cxa_thread_atexit_impl},
    {"btowc", (uintptr_t)&btowc},
    {"chdir", (uintptr_t)&bionic_chdir},
    {"fchmod", (uintptr_t)&bionic_fchmod},
    {"fchmodat", (uintptr_t)&bionic_fchmodat},
    {"fstat", (uintptr_t)&bionic_fstat},
    {"ftruncate", (uintptr_t)&bionic_ftruncate},
    {"getc", (uintptr_t)&fgetc},
    {"getenv", (uintptr_t)&getenv},
    {"iswalpha", (uintptr_t)&iswalpha},
    {"iswcntrl", (uintptr_t)&iswcntrl},
    {"iswdigit", (uintptr_t)&iswdigit},
    {"iswlower", (uintptr_t)&iswlower},
    {"iswprint", (uintptr_t)&iswprint},
    {"iswpunct", (uintptr_t)&iswpunct},
    {"iswspace", (uintptr_t)&iswspace},
    {"iswupper", (uintptr_t)&iswupper},
    {"iswxdigit", (uintptr_t)&iswxdigit},
    {"link", (uintptr_t)&bionic_link},
    {"lstat", (uintptr_t)&bionic_lstat},
    {"mbrlen", (uintptr_t)&mbrlen},
    {"memalign", (uintptr_t)&memalign},
    {"pathconf", (uintptr_t)&pathconf},
    {"pthread_detach", (uintptr_t)&pthread_detach},
    {"pthread_equal", (uintptr_t)&pthread_equal},
    {"pthread_self", (uintptr_t)&pthread_self},
    {"readlink", (uintptr_t)&bionic_readlink},
    {"realpath", (uintptr_t)&bionic_realpath},
    {"remove", (uintptr_t)&remove},
    {"rename", (uintptr_t)&rename},
    {"sched_yield", (uintptr_t)&sched_yield},
    {"sendfile", (uintptr_t)&bionic_sendfile},
    {"snprintf", (uintptr_t)&snprintf},
    {"statfs", (uintptr_t)&bionic_statfs},
    {"strcoll", (uintptr_t)&strcoll},
    {"strerror_r", (uintptr_t)&bionic_strerror_r},
    {"strftime", (uintptr_t)&strftime},
    {"strtoimax", (uintptr_t)&strtoimax},
    {"strtoll", (uintptr_t)&strtoll},
    {"strtoul", (uintptr_t)&strtoul},
    {"strtoull", (uintptr_t)&strtoull},
    {"strtoumax", (uintptr_t)&strtoumax},
    {"strxfrm", (uintptr_t)&strxfrm},
    {"symlink", (uintptr_t)&bionic_symlink},
    {"syscall", (uintptr_t)&bionic_syscall},
    {"sysconf", (uintptr_t)&sysconf},
    {"towlower", (uintptr_t)&towlower},
    {"towupper", (uintptr_t)&towupper},
    {"truncate", (uintptr_t)&bionic_truncate},
    {"ungetc", (uintptr_t)&ungetc},
    {"utimensat", (uintptr_t)&bionic_utimensat},
    {"vasprintf", (uintptr_t)&bionic_vasprintf},
    {"vsscanf", (uintptr_t)&vsscanf},
    {"wcscoll", (uintptr_t)&wcscoll},
    {"wcsxfrm", (uintptr_t)&wcsxfrm},
    {"wctob", (uintptr_t)&wctob},

    // Network services are deliberately disabled for the offline port.
    {"bind", (uintptr_t)&retminus1},
    {"connect", (uintptr_t)&retminus1},
    {"freeaddrinfo", (uintptr_t)&ret0},
    {"getaddrinfo", (uintptr_t)&retminus1},
    {"getnameinfo", (uintptr_t)&retminus1},
    {"getpeername", (uintptr_t)&retminus1},
    {"getsockname", (uintptr_t)&retminus1},
    {"getsockopt", (uintptr_t)&retminus1},
    {"ioctl", (uintptr_t)&retminus1},
    {"recvfrom", (uintptr_t)&retminus1},
    {"recvmsg", (uintptr_t)&retminus1},
    {"select", (uintptr_t)&retminus1},
    {"sendmsg", (uintptr_t)&retminus1},
    {"sendto", (uintptr_t)&retminus1},
    {"setpriority", (uintptr_t)&ret0},
    {"setsockopt", (uintptr_t)&retminus1},
    {"socket", (uintptr_t)&retminus1},

    // Remaining pthread entry points use the Vita SDK ABI directly.
    {"pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy},
    {"pthread_attr_init", (uintptr_t)&pthread_attr_init},
    {"pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate},
    {"pthread_exit", (uintptr_t)&pthread_exit},
    {"pthread_getspecific", (uintptr_t)&pthread_getspecific},
    {"pthread_join", (uintptr_t)&pthread_join},
    {"pthread_key_create", (uintptr_t)&pthread_key_create},
    {"pthread_key_delete", (uintptr_t)&pthread_key_delete},
    {"pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_fake},
    {"pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy},
    {"pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init},
    {"pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype},
    {"pthread_setspecific", (uintptr_t)&pthread_setspecific},

    // ============================================================
    // OpenSLES symbols (audio)
    // ============================================================
    {"SL_IID_ANDROIDSIMPLEBUFFERQUEUE", (uintptr_t)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE},
    {"SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE},
    {"SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE},
    {"SL_IID_OBJECT", (uintptr_t)&SL_IID_OBJECT},
    {"SL_IID_OUTPUTMIX", (uintptr_t)&SL_IID_OUTPUTMIX},
    {"SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY},
    {"SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME},

    // ============================================================
    // OpenGL ES symbols — provided by vitaGL
    // ============================================================
    {"glAlphaFunc", WRAP(glAlphaFunc)},
    {"glBindBuffer", (uintptr_t)&glBindBuffer_compat},
    {"glBindFramebufferOES", (uintptr_t)&glBindFramebuffer_compat},
    {"glBindRenderbufferOES", (uintptr_t)&glBindRenderbuffer_compat},
    {"glBindTexture", (uintptr_t)&glBindTexture_compat},
    {"glBlendFunc", (uintptr_t)&glBlendFunc},
    {"glCheckFramebufferStatusOES", (uintptr_t)&glCheckFramebufferStatus_compat},
    {"glClear", (uintptr_t)&glClear},
    {"glClearColor", WRAP(glClearColor)},
    {"glClearDepthf", WRAP(glClearDepthf)},
    {"glColor4f", (uintptr_t)&glColor4f_compat},
    {"glColorMask", (uintptr_t)&glColorMask},
    {"glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D},
    {"glCompressedTexSubImage2D", (uintptr_t)&glCompressedTexSubImage2D_compat},
    {"glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D},
    {"glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D},
    {"glCullFace", (uintptr_t)&glCullFace},
    {"glDeleteFramebuffersOES", (uintptr_t)&glDeleteFramebuffers},
    {"glDeleteTextures", (uintptr_t)&glDeleteTextures},
    {"glDepthFunc", (uintptr_t)&glDepthFunc},
    {"glDepthMask", (uintptr_t)&glDepthMask},
    {"glDisable", (uintptr_t)&glDisable_compat},
    {"glDisableClientState", (uintptr_t)&glDisableClientState},
    {"glDrawArrays", (uintptr_t)&glDrawArrays_compat},
    {"glDrawElements", (uintptr_t)&glDrawElements_compat},
    {"glEnable", (uintptr_t)&glEnable_compat},
    {"glEnableClientState", (uintptr_t)&glEnableClientState},
    {"glFlush", (uintptr_t)&glFlush},
    {"glFogf", WRAP(glFogf)},
    {"glFogfv", (uintptr_t)&glFogfv},
    {"glFramebufferRenderbufferOES", (uintptr_t)&glFramebufferRenderbuffer_compat},
    {"glFramebufferTexture2DOES", (uintptr_t)&glFramebufferTexture2D_compat},
    {"glFrustumf", WRAP(glFrustumf)},
    {"glGenFramebuffersOES", (uintptr_t)&glGenFramebuffers},
    {"glGenRenderbuffersOES", (uintptr_t)&glGenRenderbuffers},
    {"glGenTextures", (uintptr_t)&glGenTextures},
    {"glGetError", (uintptr_t)&glGetError},
    {"glGetIntegerv", (uintptr_t)&glGetIntegerv},
    {"glHint", (uintptr_t)&glHint},
    {"glIsTexture", (uintptr_t)&glIsTexture},
    {"glLightModelf", (uintptr_t)&glLightModelf_compat},
    {"glLightModelfv", (uintptr_t)&glLightModelfv},
    {"glLightf", (uintptr_t)&glLightf_compat},
    {"glLightfv", (uintptr_t)&glLightfv},
    {"glLoadIdentity", (uintptr_t)&glLoadIdentity},
    {"glLoadMatrixf", (uintptr_t)&glLoadMatrixf},
    {"glLogicOp", (uintptr_t)&glLogicOp_compat},
    {"glMaterialf", WRAP(glMaterialf)},
    {"glMaterialfv", (uintptr_t)&glMaterialfv},
    {"glMatrixMode", (uintptr_t)&glMatrixMode},
    {"glMultMatrixf", (uintptr_t)&glMultMatrixf},
    {"glOrthof", WRAP(glOrthof)},
    {"glPixelStorei", (uintptr_t)&glPixelStorei},
    {"glPopMatrix", (uintptr_t)&glPopMatrix},
    {"glPushMatrix", (uintptr_t)&glPushMatrix},
    {"glReadPixels", (uintptr_t)&glReadPixels_compat},
    {"glRenderbufferStorageOES", (uintptr_t)&glRenderbufferStorage_compat},
    {"glRotatef", WRAP(glRotatef)},
    {"glScalef", WRAP(glScalef)},
    {"glScissor", (uintptr_t)&glScissor},
    {"glShadeModel", (uintptr_t)&vita_glShadeModel},
    {"glTexCoordPointer", (uintptr_t)&glTexCoordPointer_compat},
    {"glTexImage2D", (uintptr_t)&glTexImage2D_compat},
    {"glTexParameterf", (uintptr_t)&glTexParameterf_compat},
    {"glTexSubImage2D", (uintptr_t)&glTexSubImage2D_compat},
    {"glTranslatef", WRAP(glTranslatef)},
    {"glVertexPointer", (uintptr_t)&glVertexPointer_compat},
    {"glViewport", (uintptr_t)&glViewport},
};

// ============================================================
// Resolve JNI function pointers from loaded .so
// ============================================================

void resolveNativeFunctions()
{
    printf("[SF4CE] Resolving native function symbols...\n");

    // JNI function naming convention: Java_package_class_method
    fp_NativeInit = (NativeInitFunc)so_symbol(&sf4ce_mod,
                                              "Java_jp_gltest2_android_GlTest2Renderer_NativeInit");
    fp_NativeUpdate = (NativeUpdateFunc)so_symbol(&sf4ce_mod,
                                                  "Java_jp_gltest2_android_GlTest2Renderer_NativeUpdate");
    fp_NativeDraw = (NativeDrawFunc)so_symbol(&sf4ce_mod,
                                              "Java_jp_gltest2_android_GlTest2Renderer_NativeDraw");
    fp_NativePause = (NativePauseFunc)so_symbol(&sf4ce_mod,
                                                "Java_jp_gltest2_android_GlTest2Renderer_NativePause");
    fp_NativeResume = (NativeResumeFunc)so_symbol(&sf4ce_mod,
                                                  "Java_jp_gltest2_android_GlTest2Renderer_NativeResume");
    fp_NativeTerm = (NativeTermFunc)so_symbol(&sf4ce_mod,
                                              "Java_jp_gltest2_android_GlTest2Renderer_NativeTerm");
    fp_NativeCommon = (NativeCommonFunc)so_symbol(&sf4ce_mod,
                                                  "Java_jp_gltest2_android_GlTest2Renderer_NativeCommon");
    fp_NativeP2PUpdate = (NativeP2PUpdateFunc)so_symbol(&sf4ce_mod,
                                                        "Java_jp_gltest2_android_GlTest2Renderer_NativeP2PUpdate");
    fp_NativePurchaseResponse = (NativePurchaseResponseFunc)so_symbol(&sf4ce_mod,
                                                                      "Java_jp_gltest2_android_GlTest2Renderer_NativePurchaseResponse");
    fp_NativeisConnectEnvFamily = (NativeisConnectEnvFamilyFunc)so_symbol(&sf4ce_mod,
                                                                          "Java_jp_gltest2_android_GlTest2Renderer_NativeisConnectEnvFamily");
    fp_NativeHttpResponse = (NativeHttpResponseFunc)so_symbol(
        &sf4ce_mod,
        "Java_jp_gltest2_android_GlTest2Renderer_NativeHttpConnectionResponce");
    fp_NativeHttpFailure = (NativeHttpFailureFunc)so_symbol(
        &sf4ce_mod,
        "Java_jp_gltest2_android_GlTest2Renderer_NativeHttpConnectionDidFailWithError");

    printf("  NativeInit:              %p\n", (void *)fp_NativeInit);
    printf("  NativeUpdate:            %p\n", (void *)fp_NativeUpdate);
    printf("  NativeDraw:              %p\n", (void *)fp_NativeDraw);
    printf("  NativePause:             %p\n", (void *)fp_NativePause);
    printf("  NativeResume:            %p\n", (void *)fp_NativeResume);
    printf("  NativeTerm:              %p\n", (void *)fp_NativeTerm);
    printf("  NativeCommon:            %p\n", (void *)fp_NativeCommon);
    printf("  NativeP2PUpdate:         %p\n", (void *)fp_NativeP2PUpdate);
    printf("  NativePurchaseResponse:  %p\n", (void *)fp_NativePurchaseResponse);
    printf("  NativeisConnectEnvFamily:%p\n", (void *)fp_NativeisConnectEnvFamily);
    printf("  NativeHttpResponse:      %p\n", (void *)fp_NativeHttpResponse);
    printf("  NativeHttpFailure:       %p\n", (void *)fp_NativeHttpFailure);

    if (!fp_NativeInit || !fp_NativeUpdate || !fp_NativeDraw || !fp_NativeTerm)
    {
        printf("[SF4CE] ERROR: Critical native functions not found!\n");
    }
}

// ============================================================
// Main thread
// ============================================================

int main_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    // NativeInit creates a default save, so inspect first-run state before
    // loading the game. A leftover marker must not hide a deleted save.
    char name_marker_path[512];
    snprintf(name_marker_path, sizeof(name_marker_path),
             "%s/.vita_name_entry_confirmed", DATA_PATH);
    bool initial_name_route_pending = access(name_marker_path, F_OK) != 0 ||
        access(DATA_PATH "/savedata2.dat", F_OK) != 0;

    printf("=== Street Fighter IV CE Vita Port ===\n");
    printf("Loading Android libc++ from %s\n", CXX_SO_PATH);

    bionic_compat_init();
    g_full_asset_pack_mounted = HasCompleteLocalAssetPack();
    printf("[Assets] complete user asset pack: %s\n",
           g_full_asset_pack_mounted ? "yes" : "no");
    android_sF_fake[0] = *stdin;
    android_sF_fake[1] = *stdout;
    android_sF_fake[2] = *stderr;
    android_stderr_fake = stderr;

    int load_result = so_load(&cxx_mod, CXX_SO_PATH, CXX_LOAD_ADDRESS);
    if (load_result < 0)
    {
        printf("[SF4CE] FATAL: Could not load %s (0x%08X)\n", CXX_SO_PATH,
               load_result);
        return -1;
    }
    so_relocate(&cxx_mod);
    int unresolved_count = so_resolve(&cxx_mod, default_dynlib,
                                      sizeof(default_dynlib) / sizeof(default_dynlib[0]), 0);
    printf("[SF4CE] libc++ unresolved dynamic relocations: %d\n", unresolved_count);
    if (unresolved_count)
        return -1;
    so_flush_caches(&cxx_mod);
    so_initialize(&cxx_mod);
    printf("[SF4CE] Android libc++ initialized\n");

    printf("Loading libGltest2jni.so from %s\n", SO_PATH);

    // Initialize fake JNI environment
    InitJNIEnv();
    printf("[SF4CE] Fake JNIEnv initialized\n");

    // Load the native library
    load_result = so_load(&sf4ce_mod, SO_PATH, LOAD_ADDRESS);
    if (load_result < 0)
    {
        printf("[SF4CE] FATAL: Could not load %s (0x%08X)\n", SO_PATH,
               load_result);
        return -1;
    }
    printf("[SF4CE] Loaded .so at 0x%08X\n", (unsigned int)sf4ce_mod.text_base);

    // Relocate and resolve symbols
    so_relocate(&sf4ce_mod);
    unresolved_count = so_resolve(&sf4ce_mod, default_dynlib,
                                  sizeof(default_dynlib) / sizeof(default_dynlib[0]), 0);
    printf("[SF4CE] Unresolved dynamic relocations: %d\n", unresolved_count);
    if (unresolved_count)
    {
        printf("[SF4CE] FATAL: refusing to initialize an incompletely linked .so\n");
        return -1;
    }

    /* Cki normally fills this backend through android.content.res.AssetManager.
     * Redirect its public entry points to the extracted, user-supplied assets. */
    hook_addr(so_symbol(&sf4ce_mod,
                       "_ZN3Cki12AssetManager4openEPKciPv"),
              (uintptr_t)&vita_asset_open);
    hook_addr(so_symbol(&sf4ce_mod,
                       "_ZN3Cki12AssetManager4readEPNS0_6AAssetEPvj"),
              (uintptr_t)&vita_asset_read);
    hook_addr(so_symbol(&sf4ce_mod,
                       "_ZN3Cki12AssetManager4seekEPNS0_6AAssetEl"),
              (uintptr_t)&vita_asset_seek);
    hook_addr(so_symbol(&sf4ce_mod,
                       "_ZN3Cki12AssetManager9getLengthEPNS0_6AAssetE"),
              (uintptr_t)&vita_asset_get_length);
    hook_addr(so_symbol(&sf4ce_mod,
                       "_ZN3Cki12AssetManager18openFileDescriptorEPNS0_6AAssetEPlS3_"),
              (uintptr_t)&vita_asset_open_fd);
    hook_addr(so_symbol(&sf4ce_mod,
                       "_ZN3Cki12AssetManager5closeEPNS0_6AAssetE"),
              (uintptr_t)&vita_asset_close);

    hook_addr(so_symbol(&sf4ce_mod, "_ZN3Cki3Mem5allocEii"),
              (uintptr_t)&vita_ck_alloc);
    hook_addr(so_symbol(&sf4ce_mod, "_ZN3Cki3Mem4freeEPv"),
              (uintptr_t)&vita_ck_free);
    hook_addr(so_symbol(&sf4ce_mod, "CK_free"),
              (uintptr_t)&vita_ck_free);
    hook_addr(so_symbol(&sf4ce_mod, "CK_realloc"),
              (uintptr_t)&vita_ck_realloc);
    hook_addr(so_symbol(&sf4ce_mod, "_ZN3Cki3Mem4initEPNS_9AllocatorE"),
              (uintptr_t)&vita_ck_mem_init);
    hook_addr(so_symbol(&sf4ce_mod, "_ZN3Cki3Mem8shutdownEv"),
              (uintptr_t)&vita_ck_mem_shutdown);
    bool audio_installed = options.audio && InstallAudioBridge(&sf4ce_mod, fake_vm);
    printf("[Audio] backend: %s\n", audio_installed ? "Cricket/Vita PCM" :
           options.audio ? "unsupported library; muted fallback" : "disabled");
    if (!audio_installed)
        InstallMutedAudio(&sf4ce_mod);

    uintptr_t get_app_main = so_symbol(
        &sf4ce_mod, "_ZN7AppMain11getInstanceEv");
    uintptr_t title_system = so_symbol(
        &sf4ce_mod, "_ZN7AppMain14ST_TitleSystemEv");
    uintptr_t logo_system = so_symbol(
        &sf4ce_mod, "_ZN7AppMain13ST_logoSystemEv");
    uintptr_t set_scene_title = so_symbol(
        &sf4ce_mod, "_ZN7AppMain13SetSceneTitleEv");
    uintptr_t network_connecting = so_symbol(
        &sf4ce_mod, "_ZN7AppMain20ST_NetworkConnectingEi");
    uintptr_t network_connect_start_base = so_symbol(
        &sf4ce_mod,
        "_ZN7AppMain26ST_NetworkConnectStartBaseE16NET_CONNECT_TYPEPFvvES2_");
    uintptr_t name_entry_task = so_symbol(
        &sf4ce_mod, "_ZN7AppMain18GT_NameEntryDialogEP12GENERAL_TASK");
    uintptr_t is_network_enable = so_symbol(
        &sf4ce_mod, "_ZN13DeviceManager15IsNetworkEnableEb");
    uintptr_t is_game_center_enable = so_symbol(
        &sf4ce_mod, "_ZN13DeviceManager18IsGameCenterEnableEv");
    uintptr_t create_mode_select_main_panel = so_symbol(
        &sf4ce_mod, "_ZN7AppMain28ST_CreateModeSelectMainPanelEb");
    uintptr_t create_mode_select_sub_menu = so_symbol(
        &sf4ce_mod,
        "_ZN7AppMain26ST_CreateModeSelectSubMenuENS_16MODE_SELECT_MODEEb");
    uintptr_t controller_is_connected = so_symbol(
        &sf4ce_mod,
        "_ZN19AndroidGCController19isConnectControllerEv");
    uintptr_t gamepad_draw_button = so_symbol(
        &sf4ce_mod, "_ZN7GamePad10DrawButtonEv");
    uintptr_t gamepad_draw_guid = so_symbol(
        &sf4ce_mod, "_ZN7GamePad8DrawGuidEv");
    uintptr_t gamepad_draw_stick = so_symbol(
        &sf4ce_mod, "_ZN7GamePad9DrawStickEv");
    uintptr_t gamepad_draw_controller_icon = so_symbol(
        &sf4ce_mod, "_ZN7GamePad22DrawGameControllerIconEv");
    uintptr_t cockpit_image_rects = so_symbol(
        &sf4ce_mod, "ciImgRectCockpit");
    uintptr_t battle_camera_init = so_symbol(
        &sf4ce_mod, "_Z16BattleCameraInitv");
    uintptr_t camera_direction_set = so_symbol(
        &sf4ce_mod, "_Z18CameraDirectionSetv");
    uintptr_t camera_view_set = so_symbol(
        &sf4ce_mod, "_Z13CameraViewSetb");
    fp_GetTouchPosX = (TouchPosFunc)so_symbol(
        &sf4ce_mod, "_ZN8TouchObj12getTouchPosXEi");
    fp_GetTouchPosY = (TouchPosFunc)so_symbol(
        &sf4ce_mod, "_ZN8TouchObj12getTouchPosYEi");
    fp_CheckDisplayModeX = (CheckDisplayModeAxisFunc)so_symbol(
        &sf4ce_mod, "_ZN8TouchObj17checkDisplayModeXEifi");
    fp_CheckDisplayModeY = (CheckDisplayModeAxisFunc)so_symbol(
        &sf4ce_mod, "_ZN8TouchObj17checkDisplayModeYEifi");
    fp_GetZoomUIW = (AppFloatFunc)so_symbol(
        &sf4ce_mod, "_ZN7AppMain10GetZoomUIWEv");
    fp_GetZoomUIH = (AppFloatFunc)so_symbol(
        &sf4ce_mod, "_ZN7AppMain10GetZoomUIHEv");
    fp_GetScreenScale = (ScreenScaleFunc)so_symbol(
        &sf4ce_mod, "_ZN7AppMain14getScreenScaleEv");
    fp_GetDeviceWidth = (AppIntFunc)so_symbol(
        &sf4ce_mod, "_ZN7AppMain14GetDeviceWidthEv");
    fp_GetDeviceHeight = (AppIntFunc)so_symbol(
        &sf4ce_mod, "_ZN7AppMain15GetDeviceHeightEv");
    fp_GetAppMainInstance = (GetAppMainInstanceFunc)get_app_main;

    fp_ST_LogoSystem = logo_system;
    fp_SetSceneTitle = (SetSceneTitleFunc)set_scene_title;
    g_name_validation_success = name_entry_task
                                    ? name_entry_task + 0xe28
                                    : 0;
    if (network_connect_start_base)
        hook_addr(network_connect_start_base,
                  (uintptr_t)&vita_network_connect_start_base);
    if (network_connecting)
        hook_addr(network_connecting, (uintptr_t)&vita_cancel_network_task);
    if (is_network_enable)
        hook_addr(is_network_enable, (uintptr_t)&ret0);
    if (is_game_center_enable)
        hook_addr(is_game_center_enable, (uintptr_t)&ret0);
    if (controller_is_connected)
        hook_addr(controller_is_connected,
                  (uintptr_t)&vita_controller_connected);
    fp_PadSettingSystem = so_symbol(&sf4ce_mod, "_ZN7AppMain19ST_PadSettingSystemEv");
    fp_PadSettingToPauseSystem = so_symbol(&sf4ce_mod, "_ZN7AppMain26ST_PadSettingToPauseSystemEv");
    const char *draw_names[] = {"_ZN7GamePad10DrawButtonEv", "_ZN7GamePad8DrawGuidEv",
        "_ZN7GamePad9DrawStickEv", "_ZN7GamePad22DrawGameControllerIconEv"};
    uintptr_t draw_wrappers[] = {(uintptr_t)DrawTouchControlsInSetup<0>,
        (uintptr_t)DrawTouchControlsInSetup<1>, (uintptr_t)DrawTouchControlsInSetup<2>,
        (uintptr_t)DrawTouchControlsInSetup<3>};
    for (unsigned i = 0; i < 4; ++i) {
        fp_TouchControlDraw[i] = (void (*)(void *))so_symbol(&sf4ce_mod, draw_names[i]);
        if (fp_TouchControlDraw[i]) so_redirect_plt(&sf4ce_mod, draw_names[i], draw_wrappers[i]);
    }
    bool cockpit_pause_hidden = HideCockpitPauseButton(cockpit_image_rects);
    bool battle_camera_installed = options.battle_camera &&
        battle_camera::Install(battle_camera_init, camera_direction_set,
                               camera_view_set, &kuKernelCpuUnrestrictedMemcpy);
    fp_Perspective = (PerspectiveFunc)so_symbol(&sf4ce_mod, "_ZN10OGL_MATRIX14PerspectiveFovEiiii");
    fp_AppDrawLoop = (void (*)(void *))so_symbol(&sf4ce_mod, "_ZN7AppMain8DrawLoopEv");
    fp_GameMainSystem = so_symbol(&sf4ce_mod, "_ZN7AppMain17ST_GameMainSystemEv");
    fp_CameraOffsetNone = so_symbol(&sf4ce_mod, "_Z16CameraOffsetNonev");
    if (battle_camera_installed && fp_Perspective && fp_GameMainSystem && fp_CameraOffsetNone)
        printf("[Camera] adaptive projection imports: %d\n", so_redirect_plt(
            &sf4ce_mod, "_ZN10OGL_MATRIX14PerspectiveFovEiiii", (uintptr_t)ConsolePerspective));

    static const uint8_t rendering_build_id[20] = {
        0x52, 0x0f, 0xc2, 0xea, 0x2d, 0xf8, 0x98, 0xa6, 0x82, 0x74,
        0x2a, 0x3f, 0x9b, 0xa7, 0xa2, 0x2f, 0x18, 0x5f, 0xa9, 0xe7
    };
    if (options.smooth_rendering && fp_AppDrawLoop && fp_GameMainSystem &&
        fp_CameraOffsetNone && sf4ce_mod.build_id_size == sizeof(rendering_build_id) &&
        !memcmp(sf4ce_mod.build_id, rendering_build_id, sizeof(rendering_build_id))) {
        struct TimingImport { const char *name; uintptr_t wrapper; void *original; };
        TimingImport imports[] = {
            {"_ZN7AppMain10SystemLoopEv", (uintptr_t)TimedSystemLoop, &fp_SystemLoop},
            {"_ZN7AppMain8FadeLoopEv", (uintptr_t)TimedFadeLoop, &fp_FadeLoop},
            {"_ZN6CTouch8TouchSetEPs", (uintptr_t)TimedTouchSet, &fp_TouchSet},
            {"_ZN8TouchObj15TouchStateClearEv", (uintptr_t)TimedTouchClear, &fp_TouchClear},
            {"_ZN19AndroidGCController13updateKeyDataEv", (uintptr_t)TimedControllerUpdate, &fp_ControllerUpdate},
            {"_Z10TextUpdatev", (uintptr_t)TimedTextUpdate, &fp_TextUpdate},
            {"CkUpdate", (uintptr_t)TimedCricketUpdate, &fp_CricketUpdate},
            {"_Z20updateNetworkConnectv", (uintptr_t)TimedNetworkUpdate, &fp_NetworkUpdate}
        };
        bool complete = true;
        for (auto &entry : imports) {
            uintptr_t original = so_symbol(&sf4ce_mod, entry.name);
            memcpy(entry.original, &original, sizeof(original));
            complete = complete && original;
        }
        if (complete) {
            for (auto &entry : imports)
                complete = (so_redirect_plt(&sf4ce_mod, entry.name, entry.wrapper) > 0) && complete;
            complete = (so_redirect_plt(&sf4ce_mod, "_ZN7AppMain8DrawLoopEv",
                (uintptr_t)DrawLoopWithInterpolationGuard) > 0) && complete;
        }
        g_smooth_render_available = complete;
    }
    printf("[Timing] interpolated combat rendering: %s\n",
           g_smooth_render_available ? "60 Hz target" : "native cadence");

    g_title_system = title_system;
    InitIntroMovie(options.intro_movie);
    GuardUnavailableIdleMovie();

    bool menu_background_disabled = menu_background::Install(so_symbol(
        &sf4ce_mod, "_ZN7AppMain24ST_NewModeSelectVSUpdateEv"),
        &kuKernelCpuUnrestrictedMemcpy);
    printf("[Menu] background replay: %s\n", menu_background_disabled ? "disabled" : "WARNING: signature mismatch");

    bool offline_menu_installed =
        InstallOfflineMainMenu(create_mode_select_main_panel,
                               create_mode_select_sub_menu);
    if (get_app_main && title_system && logo_system && set_scene_title &&
        network_connecting && network_connect_start_base && name_entry_task &&
        is_network_enable && is_game_center_enable && offline_menu_installed)
    {
        printf("[SF4CE] Offline services isolated; original fade gates kept\n");
    }
    else
    {
        printf("[SF4CE] WARNING: incomplete offline scene bridge\n");
    }
    if (controller_is_connected && gamepad_draw_button &&
        gamepad_draw_guid && gamepad_draw_stick &&
        gamepad_draw_controller_icon && cockpit_pause_hidden)
    {
        printf("[Input] physical controller bridge enabled; "
               "touch combat overlay hidden\n");
    }
    else
    {
        printf("[Input] WARNING: incomplete controller/overlay bridge\n");
    }
    if (!options.battle_camera)
        printf("[Camera] original Android framing selected\n");
    else if (battle_camera_installed)
        printf("[Camera] console framing: FOV=%.2f height=%u distance=170\n",
               battle_camera::kFovBam * 360.0 / 4096.0,
               (unsigned)(battle_camera::kTargetHeight / 4096));
    else
        printf("[Camera] WARNING: console framing was not installed\n");
    printf("[SF4CE] Touch transform probes: pos=%p/%p mode=%p/%p "
           "zoom=%p/%p scale=%p device=%p/%p\n",
           (void *)fp_GetTouchPosX, (void *)fp_GetTouchPosY,
           (void *)fp_CheckDisplayModeX, (void *)fp_CheckDisplayModeY,
           (void *)fp_GetZoomUIW, (void *)fp_GetZoomUIH,
           (void *)fp_GetScreenScale, (void *)fp_GetDeviceWidth,
           (void *)fp_GetDeviceHeight);
    so_flush_caches(&sf4ce_mod);
    printf("[SF4CE] Vita asset and Cki memory bridges installed\n");

    // Initialize the .so
    so_initialize(&sf4ce_mod);
    printf("[SF4CE] .so initialized\n");

    typedef jint (*JNIOnLoadFunc)(void *, void *);
    JNIOnLoadFunc jni_on_load = (JNIOnLoadFunc)so_symbol(&sf4ce_mod, "JNI_OnLoad");
    if (jni_on_load)
    {
        jint version = jni_on_load(fake_vm, NULL);
        ClearLocalRefs();
        printf("[SF4CE] JNI_OnLoad -> 0x%08X\n", version);
    }

    if (audio_installed && !InitAudioBridge()) {
        printf("[Audio] initialization failed; continuing with muted fallback\n");
        InstallMutedAudio(&sf4ce_mod);
        so_flush_caches(&sf4ce_mod);
        audio_installed = false;
    }

    // Resolve JNI function pointers
    resolveNativeFunctions();

    // Allocate JNI buffers
    g_data_buf = (uint8_t *)aligned_alloc(4096, DATA_BUF_SIZE);
    g_send_buf = (uint8_t *)aligned_alloc(4096, SEND_BUF_SIZE);
    g_receive_buf = (uint8_t *)aligned_alloc(4096, RECEIVE_BUF_SIZE);

    if (!g_data_buf || !g_send_buf || !g_receive_buf)
    {
        printf("[SF4CE] FATAL: Could not allocate JNI buffers\n");
        return -1;
    }
    memset(g_data_buf, 0, DATA_BUF_SIZE);
    memset(g_send_buf, 0, SEND_BUF_SIZE);
    memset(g_receive_buf, 0, RECEIVE_BUF_SIZE);

    printf("[SF4CE] JNI buffers allocated:\n");
    printf("  data_buf:    0x%08X (%d MB)\n", (unsigned int)g_data_buf, DATA_BUF_SIZE / (1024 * 1024));
    printf("  send_buf:    0x%08X (%d bytes)\n", (unsigned int)g_send_buf, SEND_BUF_SIZE);
    printf("  receive_buf: 0x%08X (%d bytes)\n", (unsigned int)g_receive_buf, RECEIVE_BUF_SIZE);

    // Initialize vitaGL
    const SceGxmMultisampleMode aa_modes[] = {
        SCE_GXM_MULTISAMPLE_NONE, SCE_GXM_MULTISAMPLE_2X, SCE_GXM_MULTISAMPLE_4X
    };
    int aa_option = options.msaa >= 0 && options.msaa <= 2 ? options.msaa : 2;
    // Leave physically contiguous memory for the hardware video decoder.
    int has_low_res = vglInitWithCustomThreshold(64 * 1024, SCREEN_W, SCREEN_H,
        MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, 0,
        IntroMovieAvailable() ? 32 * 1024 * 1024 : 0,
        0x8C6000, aa_modes[aa_option]); // vitaGL's standard dialog reserve
    if (has_low_res)
    {
        SCREEN_W = DEF_SCREEN_W;
        SCREEN_H = DEF_SCREEN_H;
    }
    printf("[SF4CE] vitaGL initialized: %dx%d\n", SCREEN_W, SCREEN_H);

    // ============================================================
    // Call NativeInit — this starts the game engine
    // ============================================================
    printf("[SF4CE] Calling NativeInit...\n");
    if (fp_NativeInit)
    {
        fp_NativeInit(fake_env, NULL, g_data_buf, g_send_buf, g_receive_buf);
        ClearLocalRefs();
    }
    printf("[SF4CE] NativeInit complete\n");

    // Initialize touch buffer
    memset(g_touch_tbl, 0, sizeof(g_touch_tbl));
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    int ctrl_sampling_result =
        sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    printf("[Input] analog-wide sampling result: 0x%08X\n",
           ctrl_sampling_result);

    // ============================================================
    // Main loop
    // ============================================================
    uint64_t next_simulation = 0, stats_start = 0, simulated = 0, presented = 0;
    uint64_t stats_interpolated = 0, update_cost = 0, draw_cost = 0;
    frame_timing::Clock presentation(frame_timing::kRenderHz);
    bool title_scene_restored = false;

    printf("[SF4CE] Starting main loop...\n");

    typedef int (*RosterFunc)(void *, int *, bool);
    RosterFunc get_roster = (RosterFunc)so_symbol(&sf4ce_mod, "_ZN7AppMain18getCanUseCharaListEPib");
    bool roster_logged = false;
    DebugReplay replay;
    g_movie_replay = &replay;

    while (1)
    {
        uint64_t now_us = sceKernelGetProcessTimeWide();
        bool smooth = options.smooth_rendering && NormalBattleRendering();
        if (options.debug_timing && g_app_main) {
            static uintptr_t previous_scene, previous_camera;
            uintptr_t scene = *(uintptr_t *)((uint8_t *)g_app_main + 0xdec);
            uintptr_t camera = *(uintptr_t *)((uint8_t *)g_app_main + 0x1134);
            if (scene != previous_scene || camera != previous_camera) {
                trace("[Timing] transition at %llu us: scene=%p camera=%p smooth=%d\n",
                      now_us, (void *)scene, (void *)camera, smooth);
                previous_scene = scene; previous_camera = camera;
            }
        }
        uint64_t deadline = smooth ? presentation.Deadline() : next_simulation;
        if (deadline > now_us) {
            sceKernelDelayThread(static_cast<unsigned>(deadline - now_us));
            continue;
        }
        bool simulation = !next_simulation || now_us >= next_simulation;
        if (!simulation && !smooth) continue;
        presentation.Advance(now_us);
        if (!stats_start) stats_start = now_us;
        if (options.debug_timing && now_us - stats_start >= 5000000) {
            double seconds = (now_us - stats_start) / 1000000.0;
            uint64_t draws = RenderInterpolationDraws();
            trace("[Timing] %.3f s: updates=%llu (%.2f Hz), presents=%llu (%.2f Hz), update=%.2f ms draw-only=%.2f ms, interpolated=%llu cache=%u KiB normal=%d interval=%u\n",
                  seconds, simulated, simulated / seconds, presented, presented / seconds,
                  simulated ? update_cost / (1000.0 * simulated) : 0.0,
                  presented > simulated ? draw_cost / (1000.0 * (presented - simulated)) : 0.0,
                  draws - stats_interpolated, static_cast<unsigned>(RenderInterpolationMemory() / 1024), smooth, g_simulation_interval_us);
            stats_start = now_us; simulated = presented = update_cost = draw_cost = 0;
            stats_interpolated = draws;
        }
        g_simulating = simulation;
        RenderInterpolationBegin(smooth, simulation,
            simulation ? 0.0f : frame_timing::Alpha(now_us, next_simulation, g_simulation_interval_us));
        if (!simulation) {
            // Preserve NativeUpdate's framebuffer bind and final composition.
            // The PLT gates omit simulation/input/audio work on these frames.
            if (fp_NativeUpdate) fp_NativeUpdate(fake_env, nullptr, g_touch_tbl);
            uint64_t update_end = options.debug_timing ? sceKernelGetProcessTimeWide() : 0;
            if (fp_NativeDraw) fp_NativeDraw(fake_env, nullptr);
            ClearLocalRefs();
            uint64_t draw_end = options.debug_timing ? sceKernelGetProcessTimeWide() : 0;
            vglSwapBuffers(g_ime_active ? GL_TRUE : GL_FALSE);
            if (options.debug_timing)
                TraceSlowFrame(now_us, update_end, draw_end, sceKernelGetProcessTimeWide(), false);
            ++presented; draw_cost += sceKernelGetProcessTimeWide() - now_us;
            continue;
        }
        const uint32_t scheduled_interval = g_simulation_interval_us;
        next_simulation = frame_timing::NextSimulation(next_simulation, now_us, scheduled_interval);

        // Read Vita controls
        SceCtrlData pad = {};
        int ctrl_samples = sceCtrlPeekBufferPositiveExt2(0, &pad, 1);
        if (ctrl_samples <= 0) {
            memset(&pad, 0, sizeof(pad));
            pad.lx = pad.ly = pad.rx = pad.ry = 128;
        }
        g_debug_input_active = options.debug_input_replay &&
            replay.Step(DATA_PATH "/debug-input.txt", g_debug_input);
        if (g_debug_input_active) {
            pad.buttons = g_debug_input.buttons;
            pad.lx = pad.ly = pad.rx = pad.ry = 128;
        }
        UpdateControllerInput(&pad);

        // Map Vita input to touch coordinates
        mapVitaInputToTouch(&pad);

        if (!g_app_main && fp_GetAppMainInstance)
        {
            void **instance = fp_GetAppMainInstance();
            if (instance)
                g_app_main = *instance;
        }
        if (g_app_main)
        {
            uint8_t *app = (uint8_t *)g_app_main;
            uint8_t *content_data = *(uint8_t **)(app + 0x1020);
            if (g_full_asset_pack_mounted && content_data &&
                *(uint32_t *)(content_data + 8) != 1)
            {
                trace("[Assets] complete local pack detected; enabling "
                      "installed content state (%u/%u/%u)\n",
                      *(uint32_t *)content_data,
                      *(uint32_t *)(content_data + 4),
                      *(uint32_t *)(content_data + 8));
                /* ContentDataControl::UnlockContent(2) performs this exact
                 * runtime assignment.  Only mirror it when the user's full
                 * local asset pack has passed the checks above. */
                *(uint32_t *)(content_data + 8) = 1;
            }
            uintptr_t active_system = *(uintptr_t *)(app + 0xdec);
            if (!title_scene_restored && app[0x3114] && fp_SetSceneTitle &&
                (active_system & ~(uintptr_t)1) ==
                    (fp_ST_LogoSystem & ~(uintptr_t)1))
            {
                /* Save data is loaded during the logo/resource phase.  Apply
                 * the first-run route only now; doing it when AppMain first
                 * appeared let the later save load overwrite these fields. */
                if (initial_name_route_pending)
                {
                    app[0x16c0] = 1;
                    g_initial_name_expected = true;
                    uint32_t no_name_dialog = 0;
                    memcpy(app + 0x16c2, &no_name_dialog,
                           sizeof(no_name_dialog));
                    initial_name_route_pending = false;
                    trace("[SF4CE] Initial player-name flow enabled at title\n");
                }
                title_scene_restored = true;
                trace("[SF4CE] Boot resources ready; entering original title scene\n");
                fp_SetSceneTitle(g_app_main);
            }
        }

        if (options.debug_input_replay && title_scene_restored && !roster_logged && get_roster) {
            int characters[64] = {};
            int unrestricted = get_roster(g_app_main, characters, false);
            int selectable = get_roster(g_app_main, characters, true);
            trace("[Roster] native list total=%d selectable=%d\n", unrestricted, selectable);
            roster_logged = true;
        }

        UpdateIme();

        /* Android's HTTP completion arrives after the UI handler returns.
         * Preserve that ordering for name confirmation and offline errors. */
        NetworkCompletionFunc network_completion =
            g_network_completion_pending;
        if (network_completion)
        {
            g_network_completion_pending = NULL;
            network_completion();
        }

        /* Android delivers network completion on a later renderer frame.  Do
         * the same here so a Java connectHTTP callback never re-enters game
         * code while it is still setting up the request object. */
        if (g_http_failure_pending)
        {
            g_http_failure_pending = false;
            if (fp_NativeHttpResponse)
                fp_NativeHttpResponse(fake_env, NULL, 0, 0);
            if (fp_NativeHttpFailure)
                fp_NativeHttpFailure(fake_env, NULL,
                                     (void *)"VITA_OFFLINE",
                                     (void *)"Online services are unavailable");
            ClearLocalRefs();
        }

        // Call native update (processes inputs and updates game state)
        uint64_t native_start = options.debug_timing ? sceKernelGetProcessTimeWide() : 0;
        if (fp_NativeUpdate)
        {
            fp_NativeUpdate(fake_env, NULL, g_touch_tbl);
            ClearLocalRefs();
            static short last_traced_action = -1;
            static unsigned int touch_trace_frames;
            if (g_touch_tbl[6] != last_traced_action)
            {
                last_traced_action = g_touch_tbl[6];
                touch_trace_frames = 3;
            }
            if (g_app_main && touch_trace_frames)
            {
                uint8_t *touch = (uint8_t *)g_app_main + 0xeac;
                void *touch_obj = (uint8_t *)g_app_main + 0xe9c;
                int count = *(int16_t *)(touch + 0x16c);
                int state = count > 0 ? *(int16_t *)(touch + 0xb4 + 30) : 0;
                float x = count > 0 ? *(float *)(touch + 0xb4 + 4) : 0.0f;
                float y = count > 0 ? *(float *)(touch + 0xb4 + 8) : 0.0f;
                trace("[Touch] native action=%d active=%d -> count=%d state=%d pos=%.1f,%.1f active-ST=%p next-ST=%p\n",
                      g_touch_tbl[6], g_touch_tbl[7], count, state, x, y,
                      (void *)*(uintptr_t *)((uint8_t *)g_app_main + 0xdec),
                      (void *)*(uintptr_t *)((uint8_t *)g_app_main + 0xdf4));
                if (count > 0 && fp_GetTouchPosX && fp_GetTouchPosY &&
                    fp_CheckDisplayModeX && fp_CheckDisplayModeY &&
                    fp_GetZoomUIW && fp_GetZoomUIH && fp_GetScreenScale &&
                    fp_GetDeviceWidth && fp_GetDeviceHeight)
                {
                    int display_mode = *(int *)touch_obj;
                    int device_w = fp_GetDeviceWidth(g_app_main);
                    int device_h = fp_GetDeviceHeight(g_app_main);
                    float raw_x = fp_GetTouchPosX(touch_obj, 0);
                    float raw_y = fp_GetTouchPosY(touch_obj, 0);
                    float mode_x = fp_CheckDisplayModeX(
                        touch_obj, display_mode, raw_x, device_w);
                    float mode_y = fp_CheckDisplayModeY(
                        touch_obj, display_mode, raw_y, device_h);
                    float scale = fp_GetScreenScale();
                    float zoom_w = fp_GetZoomUIW(g_app_main);
                    float zoom_h = fp_GetZoomUIH(g_app_main);
                    float offset_x = *(float *)((uint8_t *)g_app_main + 0xda8);
                    float offset_y = *(float *)((uint8_t *)g_app_main + 0xdac);
                    float ui_x = (mode_x - offset_x / scale) / zoom_w;
                    float ui_y = (mode_y - offset_y / scale) / zoom_h;
                    trace("[TouchXform] mode=%d device=%dx%d raw=%.1f,%.1f "
                          "offset=%.1f,%.1f scale=%.3f zoom=%.3f,%.3f "
                          "ui=%.1f,%.1f\n",
                          display_mode, device_w, device_h, raw_x, raw_y,
                          offset_x, offset_y, scale, zoom_w, zoom_h,
                          ui_x, ui_y);
                }
                --touch_trace_frames;
            }
        }
        uint64_t native_update_end = options.debug_timing ? sceKernelGetProcessTimeWide() : 0;
        if (g_mark_name_after_update)
        {
            g_mark_name_after_update = false;
            MarkNameEntryConfirmed();
            trace("[SF4CE] Player name confirmed by game UI\n");
        }

        // Call native draw (renders the frame via vitaGL)
        if (fp_NativeDraw)
        {
            fp_NativeDraw(fake_env, NULL);
            ClearLocalRefs();
        }

        uint64_t native_draw_end = options.debug_timing ? sceKernelGetProcessTimeWide() : 0;
        DrawMenuPointer();
        g_menu_back = false;
        // vitaGL must update/render the system dialog on every IME frame.
        // Vita3K draws its own overlay even without this, hiding the hardware bug.
        vglSwapBuffers(g_ime_active ? GL_TRUE : GL_FALSE);
        if (options.debug_timing)
            TraceSlowFrame(native_start, native_update_end, native_draw_end,
                           sceKernelGetProcessTimeWide(), true);
        if (scheduled_interval != g_simulation_interval_us)
            next_simulation = now_us + g_simulation_interval_us;
        ++simulated; ++presented;
        update_cost += sceKernelGetProcessTimeWide() - now_us;

    }

    // Cleanup (unreachable in normal operation)
    if (fp_NativeTerm)
    {
        fp_NativeTerm(fake_env, NULL);
        ClearLocalRefs();
    }
    free(g_data_buf);
    free(g_send_buf);
    free(g_receive_buf);

    return 0;
}

// ============================================================
// Entry point
// ============================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    traceReset();
    printf("[SF4CE] process entry\n");

    // Initialize app util (loads required modules)
    SceAppUtilInitParam init_param;
    SceAppUtilBootParam boot_param;
    memset(&init_param, 0, sizeof(init_param));
    memset(&boot_param, 0, sizeof(boot_param));
    int app_util_result = sceAppUtilInit(&init_param, &boot_param);
    printf("[SF4CE] sceAppUtilInit -> 0x%08X\n", app_util_result);
    int ime_module_result = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    printf("[SF4CE] SCE_SYSMODULE_IME -> 0x%08X\n", ime_module_result);

    // Load configuration
    loadOptions();

    // Create main thread
    int thread_id = sceKernelCreateThread("main_thread", main_thread, 0x40,
                                          0x100000, 0x00, 0x00, NULL);
    printf("[SF4CE] sceKernelCreateThread -> 0x%08X\n", thread_id);
    if (thread_id >= 0)
    {
        int start_result = sceKernelStartThread(thread_id, 0, NULL);
        printf("[SF4CE] sceKernelStartThread -> 0x%08X\n", start_result);
    }

    // Wait forever
    sceKernelDelayThreadCB(0xFFFFFFFF);
    return 0;
}
