# Street Fighter IV CE - PS Vita Port

Port of *Street Fighter IV CE (Android)* for the *PS Vita*.

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
Android Audio (OpenSLES)  →      OpenSLES stubs
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
- **Cross** → Attack/Confirm
- **Circle** → Block/Cancel
- **Square** → Special
- **Triangle** → Special
- **D-Pad / Left Stick** → Directional input

## Setup Instructions

### Prerequisites

- Install [kubridge](https://github.com/TheOfficialFloW/kubridge) and [FdFix](https://github.com/TheOfficialFloW/FdFix)
- Install `libshacccg.suprx` for shader compilation
- Install [vitaGL](https://github.com/Rinnegatamante/vitaGL)
- Install [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK)

### Kernel Plugins

Add to `ux0:tai/config.txt`:
```
*KERNEL
ux0:tai/kubridge.skprx
ux0:tai/fd_fix.skprx
```

### Game Files

1. Obtain `libGltest2jni.so` from an ARM32 APK (see `sf4ce/` directory)
2. Copy to `ux0:data/sf4ce/`
3. Copy game assets (`.obm` files) to `ux0:data/sf4ce/assets/`
4. Copy demand-loaded assets to `ux0:data/sf4ce/demand_assets/`

### Required Data Structure

```
ux0:data/sf4ce/
├── libGltest2jni.so          # Native library (ARMv7)
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
resolution=0        # 0=960x544, 1=1280x725, 2=1920x1088
bilinear=0          # 0=nearest, 1=bilinear filtering
antialiasing=2      # 0=none, 1=2x, 2=4x MSAA
postfx=0            # 0=none, 1=enabled
```

## Build Instructions

### Requirements

- [vita SDK](https://github.com/vitasdk) with softfp support
- vitaGL, vitaShaRK, kubridge, mathneon compiled with `-mfloat-abi=softfp`

### Build

```bash
mkdir build && cd build
cmake ..
make
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
- [ ] Audio implementation
- [ ] Fix packed 16-bit dynamic texture uploads/color corruption
- [ ] Unlock locally available characters correctly
- [ ] Testing on Vita hardware

Detailed reverse-engineering notes, verified offsets, known failures and the
recommended continuation order are maintained in
[`PORTING_NOTES.md`](PORTING_NOTES.md).

## Credits

- Based on the [FF3 Vita port](https://github.com/frangarcj/ff3_vita) architecture
- vitaGL by Rinnegatamante
- kubridge by TheOfficialFloW
- vitaShaRK by Rinnegatamante
