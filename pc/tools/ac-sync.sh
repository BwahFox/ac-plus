#!/bin/bash
# ac-sync.sh - keep the town save on a server: download before playing, upload after.
#
# Put this next to AnimalCrossing.exe and launch the game through it:
#   PC:          ./ac-sync.sh wine AnimalCrossing.exe
#   Steam Deck:  Steam launch options:  <game folder>/ac-sync.sh %command%
#   By hand:     ./ac-sync.sh pull   |   ./ac-sync.sh push   |   ./ac-sync.sh status
#
# Settings are the game's, in settings.ini (in-game: Settings > Saving), read before each sync:
#   server_sync = 1                     off (0) by default: then this just starts the game
#   server_sync_host = you@your-server  ssh destination (key login, no password prompt)
#   server_sync_dir = ac-plus-saves/sync  folder on the server, relative to its home
# An older ac-sync.conf (AC_SYNC_HOST=, AC_SYNC_DIR=) is used until settings.ini has server_sync;
# the game copies it over on its next start.
#
# What moves: save/card_a/*.gci and their .gci.acplus companions, always as one set.
# Which way: .sync-state remembers the hash of the set at the last sync, so
#   - only the server changed -> download    - only this machine changed -> upload
#   - both changed (played on two machines without syncing) -> the newer set wins,
#     and the other one is kept: locally in save/sync-backups/, on the server in history/.
# Nothing is ever deleted except old history beyond the last $KEEP_HISTORY uploads.
# If the server can't be reached the game still starts; the next sync catches up.

set -u

HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
CARD="$HERE/save/card_a"
CONF="$HERE/ac-sync.conf"
SETTINGS="$HERE/settings.ini"
STATE="$HERE/save/.sync-state"
LOG="$HERE/ac-sync.log"
KEEP_HISTORY=50

# Sets AC_SYNC_ON, AC_SYNC_HOST, AC_SYNC_DIR.
setting() {
    sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$SETTINGS" 2>/dev/null | tail -1 | sed 's/[[:space:]]*$//'
}
load_settings() {
    AC_SYNC_ON=0 AC_SYNC_HOST= AC_SYNC_DIR=ac-plus-saves/sync
    if grep -q '^[[:space:]]*server_sync[[:space:]]*=' "$SETTINGS" 2>/dev/null; then
        [ "$(setting server_sync)" = 1 ] && AC_SYNC_ON=1
        AC_SYNC_HOST="$(setting server_sync_host)"
        AC_SYNC_DIR="$(setting server_sync_dir)"
        [ -n "$AC_SYNC_DIR" ] || AC_SYNC_DIR=ac-plus-saves/sync
    elif [ -f "$CONF" ]; then
        # shellcheck source=/dev/null
        . "$CONF"
        AC_SYNC_ON=1
    fi
    [ -n "$AC_SYNC_HOST" ] || AC_SYNC_ON=0
}

log() {
    echo "[ac-sync] $*" >&2
    echo "$(date '+%F %T') $*" >> "$LOG"
}

# Steam's runtime preloads its overlay into everything; keep it out of ssh.
remote() {
    env -u LD_PRELOAD -u LD_LIBRARY_PATH ssh -o BatchMode=yes -o ConnectTimeout=6 \
        -o ServerAliveInterval=5 -o ServerAliveCountMax=3 "$AC_SYNC_HOST" "$@"
}

# The save set: town/passport .gci files plus their .acplus companions (no .bak*).
local_files() {
    (cd "$CARD" 2>/dev/null && ls -1 -- *.gci *.gci.acplus 2>/dev/null) | sort
}

# One hash for the whole set (names + contents), "none" when there is nothing.
local_hash() {
    local files
    files="$(local_files)"
    [ -n "$files" ] || { echo none; return; }
    (cd "$CARD" && echo "$files" | xargs -d '\n' sha256sum) | sha256sum | cut -c1-64
}

# Newest modification time in the set (seconds), 0 when empty.
local_mtime() {
    local files
    files="$(local_files)"
    [ -n "$files" ] || { echo 0; return; }
    (cd "$CARD" && echo "$files" | xargs -d '\n' stat -c %Y) | sort -n | tail -1
}

# Prints "<hash> <mtime>" for the server's set, "none 0" when empty; fails if unreachable.
remote_info() {
    remote "mkdir -p '$AC_SYNC_DIR/current' && cd '$AC_SYNC_DIR/current' &&
        f=\$(ls -1 -- *.gci *.gci.acplus 2>/dev/null | sort)
        if [ -z \"\$f\" ]; then echo none 0; exit 0; fi
        h=\$(echo \"\$f\" | xargs -d '\n' sha256sum | sha256sum | cut -c1-64)
        m=\$(echo \"\$f\" | xargs -d '\n' stat -c %Y | sort -n | tail -1)
        echo \"\$h \$m\""
}

stamp() { date '+%Y-%m-%d_%H%M%S'; }

# Copy the local set aside before it gets replaced.
backup_local() {
    local dir
    dir="$HERE/save/sync-backups/$(stamp)-$1"
    mkdir -p "$dir"
    (cd "$CARD" && local_files | xargs -d '\n' -r cp -p -t "$dir")
    log "kept the local save in ${dir#"$HERE"/}"
}

download() {
    local tmp
    tmp="$(mktemp -d "$HERE/save/.incoming.XXXXXX")" || return 1
    if ! remote "cd '$AC_SYNC_DIR/current' && ls -1 -- *.gci *.gci.acplus 2>/dev/null | tar -cf - -T -" |
        tar -xpf - -C "$tmp"; then
        rm -rf "$tmp"
        log "download failed, keeping the local save"
        return 1
    fi
    if ! ls "$tmp"/*.gci > /dev/null 2>&1; then
        rm -rf "$tmp"
        log "download was empty, keeping the local save"
        return 1
    fi
    [ -n "$(local_files)" ] && backup_local "before-download"
    mkdir -p "$CARD"
    (cd "$CARD" && local_files | xargs -d '\n' -r rm -f --)
    mv -f "$tmp"/* "$CARD"/
    rmdir "$tmp"
    return 0
}

# The server keeps what it had in history/ before taking the new set.
upload() {
    local who
    who="$(uname -n)"
    (cd "$CARD" && local_files | tar -cf - -T -) |
        remote "set -e; cd '$AC_SYNC_DIR'; mkdir -p incoming current history
            rm -rf incoming/*; tar -xpf - -C incoming
            ls incoming/*.gci > /dev/null
            if ls current/* > /dev/null 2>&1; then
                h=history/\$(date +%Y-%m-%d_%H%M%S)-before-$who; mkdir -p \"\$h\"; mv current/* \"\$h\"/
            fi
            mv incoming/* current/
            ls -1d history/*/ 2>/dev/null | sort | head -n -$KEEP_HISTORY | xargs -r rm -rf"
}

# $1 = pull or push: which way to lean when nothing tells the sets apart.
sync_now() {
    load_settings
    [ "$AC_SYNC_ON" = 1 ] || return 0
    local mode="$1" info rh rt lh lt last
    if ! info="$(remote_info)"; then
        log "$mode: can't reach $AC_SYNC_HOST, playing with the local save"
        return 0
    fi
    rh="${info% *}"
    rt="${info#* }"
    lh="$(local_hash)"
    lt="$(local_mtime)"
    last="$(cat "$STATE" 2>/dev/null || echo none)"

    if [ "$lh" = "$rh" ]; then
        echo "$lh" > "$STATE"
        log "$mode: already in sync"
        return 0
    fi
    if [ "$rh" = none ]; then
        upload && echo "$lh" > "$STATE" && log "$mode: server was empty, uploaded this save"
        return 0
    fi
    if [ "$lh" = none ]; then
        download && echo "$rh" > "$STATE" && log "$mode: no local save, downloaded the server's"
        return 0
    fi
    if [ "$lh" = "$last" ]; then
        download && echo "$rh" > "$STATE" && log "$mode: downloaded the newer save from the server"
        return 0
    fi
    if [ "$rh" = "$last" ]; then
        upload && echo "$lh" > "$STATE" && log "$mode: uploaded this save"
        return 0
    fi

    # Both sides changed since the last sync (or this machine never synced).
    if [ "$lt" -ge "$rt" ]; then
        upload && echo "$lh" > "$STATE" &&
            log "$mode: CONFLICT - both saves changed; this one is newer and was uploaded (server kept the other in history/)"
    else
        download && echo "$rh" > "$STATE" &&
            log "$mode: CONFLICT - both saves changed; the server's is newer and was downloaded (this one is in save/sync-backups/)"
    fi
}

status() {
    local info
    load_settings
    [ "$AC_SYNC_ON" = 1 ] || { echo "server sync is off (Settings > Saving in the game, or settings.ini)"; return; }
    echo "server: $AC_SYNC_HOST:$AC_SYNC_DIR/current"
    echo "local:  $(local_hash) mtime $(date -d "@$(local_mtime)" '+%F %T')"
    if info="$(remote_info)"; then
        echo "remote: ${info% *} mtime $(date -d "@${info#* }" '+%F %T')"
    else
        echo "remote: unreachable"
    fi
    echo "last:   $(cat "$STATE" 2>/dev/null || echo never synced)"
}

mkdir -p "$CARD"
case "${1:-}" in
    pull|push) sync_now "$1"; exit 0 ;;
    status) status; exit 0 ;;
    "") echo "usage: $0 <game command...> | pull | push | status" >&2; exit 2 ;;
esac

sync_now pull
# Steam's "Exit game" signals the whole process tree, this script included:
# pass it on, wait for the game to close, then upload anyway.
"$@" &
child=$!
trap 'kill -TERM "$child" 2>/dev/null' TERM INT HUP
while :; do
    wait "$child"
    code=$?
    kill -0 "$child" 2>/dev/null || break
done
trap - TERM INT HUP
sync_now push
exit $code
