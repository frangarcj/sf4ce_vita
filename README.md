# Street Fighter IV CE - PS Vita Port

Port of *Street Fighter IV CE (Android)* for the *PS Vita*.

The first release candidate is **v1.0-rc1**, prepared as a GitHub prerelease.
It has been exercised in Vita3K; hardware performance, memory use and audio
latency still need validation on a real Vita. This is an offline port.

## How It Works

The port works by loading the official Android ARMv7 native library (`libGltest2jni.so`) into memory, resolving its imports with native Vita functions, and providing a fake JNI environment so the Android code can run on Vita.

### Architecture

```
Android:                          Vita:
─────────                         ─────────────────
GlTest2Activity (Java)    →      main.cpp (C++)
GlTest2Renderer (Java)    →      Fake JNI Environment
TouchSurfaceView (Java)   →      Input mapping (Vita → touch)
libGltest2jni.so (ARMv7)  →      libGltest2jni.so (dlopen)
Android OpenGL ES         →      vitaGL (GLES → GXM)
Cricket Audio mixer      →      Vita PCM audio thread
```

### JNI Bridge

The native library uses JNI to call back into Java for:
- **File I/O**: Loading `.obm` assets, save files
- **Network**: HTTP connections for achievements/leaderboards
- **System**: Language, device info queries

These calls are implemented or isolated by the compatibility layer in
`loader/main.cpp` and `loader/bionic_compat.cpp`.

### Input Mapping

Menus retain native touch input. Combat controls are sent through the game's
Android controller bitfield, while the mobile combat overlay is hidden:
- **Cross** → Combat button 1 (attack)
- **Circle** → Combat button 2
- **Square** → Combat button 3
- **Triangle** → Combat button 4
- **D-Pad / Left Stick** → Directional input

## Setup Instructions

### Prerequisites

- Install [kubridge](https://github.com/TheOfficialFloW/kubridge) and [FdFix](https://github.com/TheOfficialFloW/FdFix)
- Install `libshacccg.suprx` for shader compilation
- Install `SF4CE.vpk` from this repository's GitHub release using VitaShell.

Do not install FdFix alongside rePatch. vitaGL and vitaShaRK are build
dependencies and are already linked into the VPK.

### Kernel Plugins

Add to `ux0:tai/config.txt`:
```
*KERNEL
ux0:tai/kubridge.skprx
ux0:tai/fd_fix.skprx
```

### Game Files

1. Obtain `libGltest2jni.so` and its matching `libc++_shared.so` from your own ARM32 APK (tested: 1.05.00, `lib/armeabi-v7a/`)
2. Copy both libraries to `ux0:data/sf4ce/`
3. Copy game assets (`.obm` files) to `ux0:data/sf4ce/assets/`
4. Copy demand-loaded assets to `ux0:data/sf4ce/demand_assets/`
5. Optionally copy the original `sf4_online.mp4` to `demand_assets/`.

No APK, Android libraries, extracted game data, audio or movie is supplied.
The VPK includes only the loader, license notices and original Vita shell
artwork. ARM64-only APKs cannot be used with this ARMv7 loader.

### Required Data Structure

```
ux0:data/sf4ce/
├── libGltest2jni.so          # Native library (ARMv7)
├── libc++_shared.so          # Matching Android C++ runtime
├── options.cfg               # Optional config
├── assets/                   # Game assets (.obm files)
│   ├── abel_01.obm
│   ├── abel_02.obm
│   └── ...
└── demand_assets/            # Demand-loaded assets
    └── ...
```

### Configuration (options.cfg)

```
resolution=0         # 0=960x544, 1=1280x725, 2=1920x1088
antialiasing=2       # 0=none, 1=2x, 2=4x MSAA
audio=1              # 1=Cricket mixer through native Vita stereo output, 0=mute
intro_movie=1        # Play the optional original intro after title inactivity
battle_camera=1      # 0=original Android framing, 1=adaptive console framing
smooth_rendering=1   # Interpolate normal combat drawing towards 60 Hz
debug_log=0          # 1=verbose asset/GL/JNI diagnostics
```

Audio currently supports the ARMv7 1.05.00 library with ELF build ID
`520fc2ea2df898a682742a3f9ba7a22f185fa9e7`. Other libraries keep the muted
fallback. Use matching game assets and `libc++_shared.so` from your copy.
The optional `demand_assets/sf4_online.mp4` intro plays through SceAvPlayer
with audio and its original aspect ratio. Touch, Cross, Circle or Start skips
it; completion returns to the title. If the file/backend is unavailable,
the title stays visible instead of fading into a missing movie.
The local content state is enabled only after the 32-character combat pack
passes file checks; saves are not rewritten to fabricate purchases.

Optional integration test controls (leave disabled for normal play):

```
debug_touch_cursor=0  # Select+D-pad moves a cursor, Select+Cross taps
debug_input_replay=0  # Reads ux0:data/sf4ce/debug-input.txt at the normal update rate
debug_audio_capture=0 # Writes up to 180 seconds to ux0:data/sf4ce/audio-debug.wav
debug_timing=0        # Reports simulation/render cadence and interpolation memory
```

Example replay files are in `tests/replays/`. They use `wait <updates>`,
`tap <x> <y> <updates>` and `pad <Vita-button-mask-hex> <updates>`.
The cursor and replay both use the regular Android touch/controller bridge.
Combat simulation normally runs at 20 Hz; loading/menu phases can request a
different cadence through the original Android interval callback.
`title_movie_skip.txt` exercises the idle intro, Start to skip and subsequent
combat. `title_idle.txt` checks the visible-title fallback with `intro_movie=0`.

`smooth_rendering=1` (default) draws normal combat at a target of 60 Hz using
interpolated compatible geometry and camera matrices between simulation states.
Atlas/texture changes keep the native poses. Input processing, collisions and
simulation remain at their original cadence. This adds up to one simulation tick (50 ms) of visual latency, uses at most 4 MiB of
interpolation buffers and increases rendering work. `smooth_rendering=0` uses
the native drawing cadence. Pauses, fades and cinematic cameras use the native
path; new meshes, changed topology/materials, texture uploads and camera cuts
reset history.


## Build Instructions

### Requirements

- [vita SDK](https://github.com/vitasdk) with hardfp support
- vitaGL, vitaShaRK, kubridge, mathneon compiled with `-mfloat-abi=hard`
- OpenSLES, FreeType, libpng, zlib and bzip2 from the matching SDK setup
- Build vitaGL with `ENABLE_LEGACY_PIPELINE=1` for intro playback

The GitHub Actions workflow follows the `ff3_vita` container build approach,
with pinned vitaGL/vitaShaRK/OpenSLES revisions and package validation.

### Build

```bash
mkdir build && cd build
cmake ..
make
```

The camera patch can be checked on a host compiler without VitaSDK:

```bash
c++ -std=c++17 -Wall -Wextra tests/battle_camera_test.cpp -o /tmp/sf4ce-camera-test
/tmp/sf4ce-camera-test
```

Run all asset-free regression tests and validate the distributable:

```bash
python3 tools/run_host_tests.py
python3 tools/check_package.py build/SF4CE.vpk
```

## JNI Buffer Layout

The game engine uses three direct ByteBuffers:

| Buffer | Size | Purpose |
|--------|------|---------|
| `data_buf` | 32 MB | Game data (assets, state) |
| `send_buf` | 4.8 KB | Data sent to native |
| `receive_buf` | 4.8 KB | Data received from native |

### Touch Input Format

5 touch points × 8 shorts = 40 shorts total:

```
Per touch (8 shorts):
  [0] X current (×10)
  [1] Y current (×10)
  [2] X target
  [3] Y target
  [4] X previous
  [5] Y previous
  [6] type: 1=down, 2=move, 3=up_with_prev, 4=cancel
  [7] dirty flag (1 = has changes)
```

## Native Functions Called

| Function | Purpose |
|----------|---------|
| `NativeInit(data, send, receive)` | Initialize game engine |
| `NativeUpdate(touch_tbl)` | Update game state with inputs |
| `NativeDraw()` | Render current frame |
| `NativePause()` | Pause game |
| `NativeResume()` | Resume game |
| `NativeTerm()` | Shutdown engine |
| `NativeCommon(cmd)` | Generic commands (battle data, timeout, etc.) |
| `NativeP2PUpdate()` | P2P multiplayer update (stubbed) |

## Status

- [x] Project structure
- [x] JNI bridge implementation
- [x] Fake JNI environment
- [x] Physical controller mapping in Vita3K
- [x] Mobile combat overlay hidden
- [x] File I/O stubs
- [x] Boot, name entry, offline menu and combat in Vita3K
- [x] Store/Battle removed from the main panel and right navbar
- [x] Stable static main-menu background (optional replay preview disabled)
- [x] Native ETC1 textures (Vita3K needs ETC1 support, PR #3903)
- [x] Original Vita icon/LiveArea resources, with no APK artwork
- [x] Native Vita PCM output from the embedded Cricket mixer (tested in Vita3K)
- [x] Explicit RGBA8 conversion for 5551 initial/partial texture uploads
- [x] Verify full local combat pack before enabling native 32-character content state
- [ ] Testing on Vita hardware

The current VPK is a test candidate. Hardware performance, memory use with
RGBA8 textures, audio latency and the full set of stages/cinematic cameras
still require validation on a real Vita.

Check the generated package with:

```bash
python3 tools/check_package.py build/SF4CE.vpk
```

Host checks for the new timing and rendering code (Clang):

```bash
clang++ -std=c++17 -fsanitize=address,undefined tests/frame_timing_test.cpp -o /tmp/sf4ce-timing-test
/tmp/sf4ce-timing-test
clang++ -std=c++17 -fsanitize=address,undefined -Itests/gl_stub tests/render_interpolation_test.cpp loader/render_interpolation.cpp -o /tmp/sf4ce-render-test
/tmp/sf4ce-render-test
```

Detailed reverse-engineering notes, verified offsets, known failures and the
recommended continuation order are maintained in
[`PORTING_NOTES.md`](PORTING_NOTES.md).

## Credits

- Based on the [FF3 Vita port](https://github.com/frangarcj/ff3_vita) architecture
- vitaGL by Rinnegatamante
- kubridge by TheOfficialFloW
- vitaShaRK by Rinnegatamante
