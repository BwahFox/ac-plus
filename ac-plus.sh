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
IMAGE=ac-plus-build:3
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
        run -e AC_BUILD_DIR=/build/src/pc/build-container -e PC_CONSOLE=OFF -e PC_DEV=OFF "$IMAGE" ./build_wine.sh
        mkdir -p "$DIST/rom" "$DIST/save" "$DIST/texture_pack" "$DIST/music" "$DIST/shaders"
        cp pc/build-container/bin/AnimalCrossing.exe "$DIST/"
        cp pc/build-container/bin/shaders/* "$DIST/shaders/"
        cp pc/build-container/bin/ac-plus-inhibit "$DIST/"
        cp pc/tools/ac-sync.sh "$DIST/"
        # the City Folk converter the game runs on its first start: the tools and a Windows Python
        rm -rf "$DIST/cf-tools"
        mkdir -p "$DIST/cf-tools"
        cp pc/tools/*.py src/data/npc/model/mdl/owl_1.c "$DIST/cf-tools/"
        run -v "$DIST/cf-tools:/out:Z" "$IMAGE" cp -r /opt/winpython /out/python
        cp README.md LICENSE "$DIST/"
        rm -f "$DIST/rom/PUT_YOUR_GAMECUBE_DISC_HERE.txt"
        printf '%s\r\n' "Put your own disc images here:" \
            "- Animal Crossing (GameCube, USA, GAFE01): .iso, .gcm or .ciso" \
            "- Animal Crossing: City Folk (Wii, USA, RUUE01): .wbfs or .iso" \
            "The first start converts what ac-plus uses from City Folk (about a minute)." > "$DIST/rom/PUT_YOUR_DISCS_HERE.txt"
        echo
        echo "=== Done: $DIST ==="
        echo "Next:"
        echo "  1. put your GameCube and City Folk disc images in $DIST/rom/"
        echo "  2. run AnimalCrossing.exe (on Linux or a Steam Deck: through Wine or Proton);"
        echo "     the first start converts what ac-plus uses from City Folk"
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
        run -i "$IMAGE" python3 - "dist/ac-plus-$version.zip" "$version" <<'PY'
import os, sys, zipfile
out, version = sys.argv[1], sys.argv[2]
keep = ['AnimalCrossing.exe', 'ac-plus-inhibit', 'ac-sync.sh', 'README.md', 'LICENSE', 'shaders/default.vert', 'shaders/default.frag',
        'rom/PUT_YOUR_DISCS_HERE.txt']
for top, dirs, files in os.walk('dist/ac-plus/cf-tools'):  # the City Folk converter
    dirs[:] = [d for d in dirs if d != '__pycache__']
    keep += [os.path.relpath(os.path.join(top, f), 'dist/ac-plus') for f in files]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(keep):
        z.write(os.path.join('dist/ac-plus', f), 'ac-plus/' + f.replace(os.sep, '/'))
    for d in ('save', 'texture_pack', 'music'):
        z.writestr('ac-plus/%s/' % d, '')
    z.writestr('ac-plus/VERSION', version + '\n')  # the updater's idea of this release (pc_updater.c)
print('wrote', out)
PY
        # The updater installs only signed releases: sign here, on the host, where the key is (never in the image)
        if [ -f "${AC_PLUS_SIGNING_KEY:-$HOME/.config/ac-plus/release-signing-key.pem}" ]; then
            python3 "$ROOT/pc/tools/release-sign.py" sign "$ROOT/dist/ac-plus-$version.zip" &&
                echo "upload both ac-plus-$version.zip and ac-plus-$version.zip.sig to the release"
        else
            echo "WARNING: no release signing key here; the in-game updater won't install this zip" >&2
            echo "(pc/tools/release-sign.py keygen makes one; the public half goes in pc_updater.c)" >&2
        fi
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
