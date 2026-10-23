#!/bin/bash
# ac-plus.sh - build ac-plus and set it up, in a container (podman or docker), with nothing else to install.
#
#   ./ac-plus.sh build                        build the game into dist/ac-plus/
#   ./ac-plus.sh extract-cf <City Folk disc>  convert your City Folk disc into dist/ac-plus/cf_sky/ (required,
#                                             once). The disc is your own copy: a .wbfs or .iso, or a folder
#                                             Dolphin extracted its files to.
#   ./ac-plus.sh package                      zip dist/ac-plus/ (as built, without a disc or saves) for a release
#   ./ac-plus.sh shell                        a shell in the build container
#
# GAME_DIR=<folder> sets where extract-cf writes (default dist/ac-plus). It needs your GameCube disc in
# its rom/ folder first (Celeste's look is fitted to the GameCube's Blathers).
set -euo pipefail

cd "$(dirname "$0")"
ROOT="$PWD"
IMAGE=ac-plus-build:1
DIST="$ROOT/dist/ac-plus"

if command -v podman > /dev/null 2>&1; then
    ENGINE=podman
    USER_ARGS=(--userns=keep-id)
elif command -v docker > /dev/null 2>&1; then
    ENGINE=docker
    USER_ARGS=(-u "$(id -u):$(id -g)")
else
    echo "Neither podman nor docker is installed. Install one of them (podman: https://podman.io)." >&2
    exit 1
fi

image() {
    if ! "$ENGINE" image inspect "$IMAGE" > /dev/null 2>&1; then
        echo "=== Making the build container (first time only, a few minutes) ==="
        "$ENGINE" build -t "$IMAGE" -f Containerfile .
    fi
}

run() {
    "$ENGINE" run --rm "${USER_ARGS[@]}" -v "$ROOT:/build/src:Z" "$@"
}

case "${1:-}" in
    build)
        image
        run -e AC_BUILD_DIR=/build/src/pc/build-container -e PC_CONSOLE=OFF "$IMAGE" ./build_wine.sh
        mkdir -p "$DIST/rom" "$DIST/save" "$DIST/texture_pack" "$DIST/music" "$DIST/shaders"
        cp pc/build-container/bin/AnimalCrossing.exe "$DIST/"
        cp pc/build-container/bin/shaders/* "$DIST/shaders/"
        cp pc/tools/ac-sync.sh "$DIST/"
        cp README.md LICENSE "$DIST/"
        [ -e "$DIST/rom/PUT_YOUR_GAMECUBE_DISC_HERE.txt" ] ||
            echo "Put your own Animal Crossing (USA, GAFE01) disc image here: .iso, .gcm or .ciso." \
                > "$DIST/rom/PUT_YOUR_GAMECUBE_DISC_HERE.txt"
        echo
        echo "=== Done: $DIST ==="
        echo "Next:"
        echo "  1. put your GameCube disc image in $DIST/rom/"
        echo "  2. $0 extract-cf <your City Folk disc image>"
        echo "  3. run AnimalCrossing.exe (on Linux or a Steam Deck: through Wine or Proton)"
        echo "See README.md."
        ;;
    extract-cf)
        [ $# -ge 2 ] || { echo "usage: $0 extract-cf <City Folk .wbfs/.iso, or its extracted files folder>" >&2; exit 1; }
        src="$(realpath "$2")"
        game="$(realpath "${GAME_DIR:-$DIST}")"
        [ -e "$src" ] || { echo "$src: not found" >&2; exit 1; }
        [ -d "$game" ] || { echo "$game: not found (run '$0 build' first, or set GAME_DIR)" >&2; exit 1; }
        image
        # the disc read-only; the game folder (and its rom/, which may hold links) read-write
        src_dir="$(dirname "$src")"
        rom_args=()
        for f in "$game"/rom/*; do
            [ -L "$f" ] && rom_args+=(-v "$(realpath "$f"):/game/rom/$(basename "$f"):ro,Z")
        done
        run -v "$src_dir:/cf:ro,Z" -v "$game:/game:Z" "${rom_args[@]}" "$IMAGE" \
            python3 pc/tools/extract_cf_sky.py "/cf/$(basename "$src")" /game
        ;;
    package)
        [ -f "$DIST/AnimalCrossing.exe" ] || { echo "run '$0 build' first" >&2; exit 1; }
        version="$(git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo dev)"
        image
        # only what the build put there: no disc image, saves, settings or City Folk files
        run -i "$IMAGE" python3 - "dist/ac-plus-$version.zip" <<'PY'
import os, sys, zipfile
out = sys.argv[1]
keep = ['AnimalCrossing.exe', 'ac-sync.sh', 'README.md', 'LICENSE', 'shaders/default.vert', 'shaders/default.frag',
        'rom/PUT_YOUR_GAMECUBE_DISC_HERE.txt']
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in keep:
        z.write(os.path.join('dist/ac-plus', f), 'ac-plus/' + f)
    for d in ('save', 'texture_pack', 'music'):
        z.writestr('ac-plus/%s/' % d, '')
print('wrote', out)
PY
        ;;
    shell)
        image
        run -it "$IMAGE" bash
        ;;
    *)
        sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
        exit 1
        ;;
esac
