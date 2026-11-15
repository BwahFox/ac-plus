# Porting ac-plus to other platforms

ac-plus is built and tested on Windows and Linux (and the Steam Deck). It sits on
[Nookery](https://github.com/flyngmt/Nookery), which also runs on macOS, Android and iOS, and almost all of ac-plus
is plain game code that doesn't care where it runs. This note lists the parts that do, so a port knows what to
switch off or replace.

## The `ACPLUS_DESKTOP` switch

A CMake option, on by default except on Android and iOS. It covers:

| Part | What it does | Without it |
|---|---|---|
| `pc/src/pc_updater.c` | Looks for a newer release on GitHub, downloads the zip, replaces the game's files. Installs on Windows only (elsewhere it never offers anything). | No updates from inside the game. |
| `pc/src/pc_cf_setup.c` (the conversion half) | On the first start, runs `cf-tools/extract_cf_sky.py` (Python with numpy and Pillow) on the City Folk disc in `rom/` to make `cf_sky/`. | `cf_sky/` has to be made on a computer (`./ac-plus.sh extract-cf`, or by starting a desktop build once) and copied into the data folder. The game still checks its version and says so if it's missing or old. |

## Other platform-specific code

- **HTTP** (`pc/src/pc_http.c`), used by in-game save sync and the updater: WinHTTP on Windows, libcurl elsewhere
  (opened at runtime with `dlopen`; without it, sync acts as if offline). A platform with neither needs its own
  `pc_http_request`.
- **Folder listing** (`pc/include/pc_dirent.h`): `<dirent.h>`, or a small FindFirstFile version for MSVC.
- **Screen saver**: `pc/src/pc_inhibit.c` only does anything for the Windows build running under Wine/Proton (it
  starts the Linux helper `ac-plus-inhibit`). Everywhere else SDL keeps the screen on by itself.
- **Online visits** (`pc/src/mp/mp_link.c`): UDP sockets, Winsock or BSD sockets.
- **Data folder**: ac-plus keeps everything (saves, `cf_sky/`, `music/`, `ac-plus.ini`) next to the game on every
  platform, and changes into that folder at startup (`pc/src/aurora_main.cpp`: the default `--user-dir`). A mobile
  port wants Nookery's own data folder instead: drop that default and its `current_path` change.
- **Windows cross-build from Linux**: `cmake/clang-cl-xwin.cmake` and `tools/setup-windows-cross.sh`. Nothing else
  depends on them.

## The curved world

City Folk's rolling-log world bends every outdoor vertex in Aurora's vertex shader. Aurora has no hook for that, so
ac-plus carries a small patch, `patches/aurora-curved-world.patch`, which CMake applies to `extern/aurora` when it
configures (see `patches/README.md`). Two more pieces belong to it:

- `pc/src/aurora_widescreen.cpp` hands the curve to Aurora (`GXAuroraSetCurve`) with every perspective projection.
- `pc/src/pc_emu64_curve.cpp` cuts long triangles up while the curve is on, so things don't float on big flat ground
  triangles. `src/static/libforest/emu64/emu64.c` includes `pc_emu64_curve.h` for that.

A port that can't use the patch builds with `-DPC_SKY_CAN_CURVE=0`, drops the `GXAuroraSetCurve` call and the
`pc_emu64_curve.h` include: the world stays flat and everything else works.

## Things that are fine everywhere

City Folk's sky, rooms, characters and furniture draw through GX (`pc/src/pc_acplus_gx.c`, `pc_sky.c`, `pc_cf_*.c`),
so they run wherever Aurora runs. The same goes for the save files (`town.acplus`, `pc/src/pc_ext_save.c`), the
pause menu and settings (drawn by the game itself), music packs (decoded in software) and everything in `src/`.
