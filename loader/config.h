#ifndef __CONFIG_H__
#define __CONFIG_H__

#define LOAD_ADDRESS 0x98000000
#define CXX_LOAD_ADDRESS 0x97000000

#define MEMORY_NEWLIB_MB 192
#define MEMORY_VITAGL_THRESHOLD_MB 8
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_SAMPLES_PER_BUF 8192

#define DATA_PATH "ux0:data/sf4ce"
#define SO_PATH DATA_PATH "/" \
                          "libGltest2jni.so"
#define CXX_SO_PATH DATA_PATH "/libc++_shared.so"
#define CONFIG_FILE_PATH DATA_PATH "/" \
                                   "options.cfg"

#define DEF_SCREEN_W 960
#define DEF_SCREEN_H 544

// JNI buffers
#define DATA_BUF_SIZE (32 * 1024 * 1024) // 32 MB
#define SEND_BUF_SIZE 4800
#define RECEIVE_BUF_SIZE 4800

// Touch
#define TOUCH_MAX 5
#define TOUCH_TBL_SIZE (TOUCH_MAX * 8) // 40 shorts

// SF4 CE advances its game simulation once per renderer update.  The Android
// release targets a 20 Hz update cadence; driving it at 60 Hz makes gameplay
// run three times too fast.
#define FRAME_INTERVAL_US 50000

typedef struct
{
    int res;
    int bilinear;
    int msaa;
    int postfx;
} config_opts;
extern config_opts options;

#endif
