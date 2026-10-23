#!/bin/bash
# build_wine.sh - Cross-compile the Windows build of the PC port from Linux (run with Wine/Proton)
#
# Prerequisites:
#   mingw-w64-gcc (i686-w64-mingw32-gcc), cmake
#   SDL2 MinGW dev package unpacked next to the repo: ../SDL2-2.30.10/
#     https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-devel-2.30.10-mingw.tar.gz
#
# Usage:
#   ./build_wine.sh
#   cd pc/build32/bin && wine AnimalCrossing.exe

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${AC_BUILD_DIR:-$SCRIPT_DIR/pc/build32}"
BIN_DIR="$BUILD_DIR/bin"
SDL2_DIR="$SCRIPT_DIR/../SDL2-2.30.10/i686-w64-mingw32"

if [ ! -f "$SDL2_DIR/include/SDL2/SDL.h" ]; then
    echo "Error: SDL2 MinGW dev package not found at $SDL2_DIR"
    exit 1
fi

# -msse2 -mfpmath=sse: this compiler defaults to x87 float math, whose excess
# precision breaks camera transitions and animations. SSE gives strict
# single-precision floats like the GameCube. Do not remove.
# The extra -I is for sources that include <SDL2/SDL_scancode.h>.
FLAGS="-I$SDL2_DIR/include -msse2 -mfpmath=sse -mstackrealign"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f Makefile ]; then
    echo "=== Configuring CMake ==="
    # find_package/pkg-config are disabled so CMake does not pick up the host's
    # Linux SDL2; the port's own fallback then finds ../SDL2-2.30.10.
    cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-mingw32.cmake \
        -DPC_CONSOLE="${PC_CONSOLE:-ON}" \
        -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON \
        -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
        -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS"
fi

echo "=== Building PC port ==="
make -j"$(nproc)"

mkdir -p "$BIN_DIR/rom" "$BIN_DIR/texture_pack" "$BIN_DIR/save"

echo ""
echo "=== Build complete! ==="
echo "Run: cd pc/build32/bin && wine AnimalCrossing.exe"
