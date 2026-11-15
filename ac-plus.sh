#!/bin/bash
# ac-plus.sh - build ac-plus and set it up, in a container (podman or docker), with nothing else to install.
#
#   ./ac-plus.sh build                        the Linux game into dist/ac-plus-linux/
#   ./ac-plus.sh build-windows --accept-msvc-license
#                                             the Windows game into dist/ac-plus-windows/. It's built with
#                                             Microsoft's Windows SDK, which the build downloads (once) with xwin:
#                                             the flag says you accept Microsoft's license for it
#                                             (https://go.microsoft.com/fwlink/?LinkId=2086102)
#   ./ac-plus.sh extract-cf <City Folk disc>  convert your City Folk disc into the game folder's cf_sky/ (the game also
#                                             does it by itself on its first start). The disc is your own copy: a
#                                             .wbfs or .iso, or a folder Dolphin extracted its files to.
#   ./ac-plus.sh package                      zip what was built (no discs or saves) for a release, signed for the
#                                             in-game updater if the signing key is here
#   ./ac-plus.sh shell                        a shell in the build container
#
# GAME_DIR=<folder> sets where extract-cf writes (default: the Linux build, else the Windows one). It needs your
# GameCube disc in its rom/ folder first (Celeste's look is fitted to the GameCube's Blathers).
set -euo pipefail

cd "$(dirname "$0")"
ROOT="$PWD"
IMAGE=ac-plus-build:11
DIST_LINUX="$ROOT/dist/ac-plus-linux"
DIST_WINDOWS="$ROOT/dist/ac-plus-windows"
EXE=AnimalCrossing # ACPLUS_EXE_NAME in CMakeLists.txt

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
        echo "=== Making the build container (first time only, several minutes) ==="
        "$ENGINE" build -t "$IMAGE" -f Containerfile .
    fi
}

run() {
    "$ENGINE" run --rm "${USER_ARGS[@]}" -e HOME=/tmp -v "$ROOT:/build/src:Z" "$@"
}

# The game files from a build into a game folder, replacing only what the build makes: the program, its libraries,
# res/, cf-tools/ and the docs. Everything of the player's stays as it is: save/, rom/ (their discs), cf_sky/,
# music/, texture_pack/, nes_roms/, qr/, jank/ and the settings (ac-plus.ini, config.json, input.json...), so a
# folder someone plays from can be rebuilt into.
install_program() {
    local bin="$1" dist="$2" entry name
    mkdir -p "$dist"
    rm -rf "$dist/res" "$dist/cf-tools"
    rm -f "$dist/$EXE" "$dist/$EXE.exe" "$dist"/*.dll "$dist"/*.so "$dist"/*.so.* "$dist"/*.pdb \
        "$dist/ac-plus-inhibit" "$dist/ac-sync.sh" "$dist/README.md" "$dist/LICENSE" "$dist/VERSION"
    for entry in "$bin"/*; do
        name="$(basename "$entry")"
        case "$name" in
            save|nes_roms|texture_pack|rom|music|cf_sky|*.debug|*.pdb) ;; # (the build's empty folders, or not shipped)
            *) cp -r "$entry" "$dist/" ;;
        esac
    done
}

# what both game folders get besides the program: the City Folk converter's scripts, docs, the empty folders
common_files() {
    local dist="$1"
    mkdir -p "$dist/rom" "$dist/save" "$dist/texture_pack" "$dist/music"
    rm -rf "$dist/cf-tools"
    mkdir -p "$dist/cf-tools"
    cp pc/tools/*.py pc/tools/*.tsv src/data/npc/model/mdl/owl_1.c "$dist/cf-tools/"
    cp pc/tools/ac-sync.sh README.md LICENSE "$dist/"
    printf '%s\r\n' "Put your own disc images here:" \
        "- Animal Crossing (GameCube, USA, GAFE01): .iso, .gcm, .ciso or .rvz" \
        "- Animal Crossing: City Folk (Wii, USA, RUUE01): .wbfs or .iso" \
        "The first start converts what ac-plus uses from City Folk (about a minute)." > "$dist/rom/PUT_YOUR_DISCS_HERE.txt"
}

case "${1:-}" in
    build)
        image
        run "$IMAGE" sh -c '
            cmake -S . -B build/c-linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPC_DEV=OFF \
                -DAURORA_SDL3_PROVIDER=vendor -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ &&
            cmake --build build/c-linux &&
            clang -O2 -o build/c-linux/ac-plus-inhibit pc/tools/ac-plus-inhibit.c'
        install_program build/c-linux/bin "$DIST_LINUX"
        run "$IMAGE" strip --strip-debug "dist/ac-plus-linux/$EXE" # (300 MB of debug info otherwise)
        common_files "$DIST_LINUX"
        # its own Python for the first-start City Folk conversion (Containerfile), as the Windows build has
        run -v "$DIST_LINUX/cf-tools:/out:Z" "$IMAGE" cp -r /opt/linuxpython /out/python
        echo
        echo "=== Done: $DIST_LINUX ==="
        echo "Put your GameCube and City Folk disc images in its rom/ folder and run ./$EXE"
        echo "(the first start converts what ac-plus uses from City Folk, with the Python in cf-tools/python)."
        ;;
    build-windows)
        if [ "${2:-}" != "--accept-msvc-license" ]; then
            sed -n '5,9p' "$0" | sed 's/^# \{0,1\}//'
            exit 1
        fi
        image
        run "$IMAGE" sh -c '
            set -e
            if [ ! -d build/xwin/sdk ]; then
                xwin --accept-license --cache-dir build/xwin-cache --arch x86_64 splat --output build/xwin
            fi
            # a header some code includes with other capitals than the SDK has (fine on Windows, not here)
            ln -sf wbemidl.h build/xwin/sdk/include/um/Wbemidl.h
            cmake -S . -B build/c-windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPC_DEV=OFF \
                -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_DIR=$PWD/build/xwin \
                -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON &&
            cmake --build build/c-windows &&
            clang -O2 -o build/c-windows/ac-plus-inhibit pc/tools/ac-plus-inhibit.c'
        install_program build/c-windows/bin "$DIST_WINDOWS"
        # the Linux helper that keeps the desktop awake when the game runs through Wine or Proton (pc_inhibit.c)
        cp build/c-windows/ac-plus-inhibit "$DIST_WINDOWS/"
        common_files "$DIST_WINDOWS"
        run -v "$DIST_WINDOWS/cf-tools:/out:Z" "$IMAGE" cp -r /opt/winpython /out/python
        echo
        echo "=== Done: $DIST_WINDOWS ==="
        echo "Put your GameCube and City Folk disc images in its rom/ folder and run $EXE.exe"
        echo "(on Linux or a Steam Deck the Linux build is better: '$0 build')."
        ;;
    extract-cf)
        [ $# -ge 2 ] || { echo "usage: $0 extract-cf <City Folk .wbfs/.iso, or its extracted files folder>" >&2; exit 1; }
        src="$(realpath "$2")"
        default_game="$DIST_LINUX"
        [ -d "$default_game" ] || default_game="$DIST_WINDOWS"
        game="$(realpath "${GAME_DIR:-$default_game}")"
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
        # ACPLUS_VERSION=beta-2 ./ac-plus.sh package: the release's name (its VERSION file, which the updater reads)
        version="${ACPLUS_VERSION:-$(git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo dev)}"
        made=()
        image
        # The Windows build as ac-plus-<version>.zip (the only .zip of a release: the in-game updater takes the first
        # .zip it sees), the Linux build as ac-plus-<version>-linux.tar.gz. Only what the build put there: no disc
        # image, saves, settings or City Folk files.
        if [ -f "$DIST_WINDOWS/$EXE.exe" ]; then
            run -i "$IMAGE" python3 - "dist/ac-plus-$version.zip" "$version" "dist/ac-plus-windows" <<'PY'
import os, sys, zipfile
out, version, src = sys.argv[1:4]
# Only the game's own files (the folder may also hold someone's saves, discs, settings...)
def ours(rel):
    top = rel.split('/')[0]
    if '/' not in rel:
        return rel in ('AnimalCrossing.exe', 'ac-plus-inhibit', 'ac-sync.sh', 'README.md', 'LICENSE') or rel.endswith('.dll')
    return top in ('res', 'cf-tools') or rel == 'rom/PUT_YOUR_DISCS_HERE.txt'
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for top, dirs, files in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d != '__pycache__')
        for f in sorted(files):
            p = os.path.join(top, f)
            rel = os.path.relpath(p, src).replace(os.sep, '/')
            if ours(rel):
                z.write(p, 'ac-plus/' + rel)
    for d in ('save', 'texture_pack', 'music'):
        z.writestr('ac-plus/%s/' % d, '')
    z.writestr('ac-plus/VERSION', version + '\n')  # the updater's idea of this release (pc_updater.c)
print('wrote', out)
PY
            made+=("dist/ac-plus-$version.zip")
        fi
        if [ -f "$DIST_LINUX/$EXE" ]; then
            # only the game's own files (the folder may also hold someone's saves, discs, settings...)
            stage="$(mktemp -d)"
            mkdir -p "$stage/ac-plus/rom" "$stage/ac-plus/save" "$stage/ac-plus/texture_pack" "$stage/ac-plus/music"
            cp -r "$DIST_LINUX/$EXE" "$DIST_LINUX/res" "$DIST_LINUX/cf-tools" "$DIST_LINUX/ac-sync.sh" \
                "$DIST_LINUX/README.md" "$DIST_LINUX/LICENSE" "$stage/ac-plus/"
            cp "$DIST_LINUX/rom/PUT_YOUR_DISCS_HERE.txt" "$stage/ac-plus/rom/"
            find "$stage" -name __pycache__ -prune -exec rm -rf {} +
            echo "$version" > "$stage/ac-plus/VERSION"
            tar -C "$stage" -czf "dist/ac-plus-$version-linux.tar.gz" ac-plus
            rm -rf "$stage"
            echo "wrote dist/ac-plus-$version-linux.tar.gz"
            made+=("dist/ac-plus-$version-linux.tar.gz")
        fi
        [ ${#made[@]} -gt 0 ] || { echo "run '$0 build' or '$0 build-windows' first" >&2; exit 1; }
        # The updater installs only signed releases: sign here, on the host, where the key is (never in the image)
        if [ -f "${AC_PLUS_SIGNING_KEY:-$HOME/.config/ac-plus/release-signing-key.pem}" ]; then
            for f in "${made[@]}"; do
                python3 "$ROOT/pc/tools/release-sign.py" sign "$ROOT/$f"
            done
            echo "upload these and their .sig files to the release"
        else
            echo "WARNING: no release signing key here; the in-game updater won't install these" >&2
            echo "(pc/tools/release-sign.py keygen makes one; the public half goes in pc_updater.c)" >&2
        fi
        ;;
    shell)
        image
        run -it "$IMAGE" bash
        ;;
    *)
        sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
        exit 1
        ;;
esac
