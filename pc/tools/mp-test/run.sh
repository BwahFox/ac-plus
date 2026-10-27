#!/bin/bash
# Native tests and fuzzing for the multiplayer network layer (pc/src/mp).
#   ./run.sh            build with ASan + UBSan and run the tests (checks crypto against libsodium if installed)
#   ./run.sh fuzz [s]   also run both libFuzzer targets for s seconds each (default 60)
set -e
cd "$(dirname "$0")"
SRC=../../src/mp
LIB=../../lib/monocypher
OUT=${MP_TEST_OUT:-build}
mkdir -p "$OUT"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1"
CC=${CC:-clang}
SODIUM=""
if pkg-config --exists libsodium 2>/dev/null; then SODIUM="-DMP_TEST_SODIUM $(pkg-config --cflags --libs libsodium)"; fi
$CC $SAN -Wall -Wextra -Wno-unused-parameter -I$SRC -I$LIB mp_test.c $SRC/mp_wire.c $SRC/mp_noise.c $SRC/mp_link.c \
    $SRC/mp_mm.c $LIB/monocypher.c $SODIUM -lm -o "$OUT/mp_test"
# the matchmaking test talks to the real server (server/ac-plus-server.py) on free ports
MM_DATA=$(mktemp -d)
read HTTP_PORT MM_PORT < <(python3 -c 'import socket
a=socket.socket(); a.bind(("127.0.0.1",0)); b=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); b.bind(("127.0.0.1",0))
print(a.getsockname()[1], b.getsockname()[1])')
python3 ../../../server/ac-plus-server.py --data "$MM_DATA" serve --bind 127.0.0.1 --port "$HTTP_PORT" --mm-port "$MM_PORT" --relay \
    > "$OUT/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null; rm -rf "$MM_DATA"' EXIT
sleep 1
MP_TEST_MM_PORT=$MM_PORT "$OUT/mp_test"
if [ "$1" = fuzz ]; then
    T=${2:-60}
    for t in wire noise; do
        $CC -fsanitize=fuzzer,address,undefined -g -O1 -I$SRC -I$LIB fuzz_$t.c $SRC/mp_wire.c $SRC/mp_noise.c \
            $SRC/mp_mm.c $LIB/monocypher.c -lm -o "$OUT/fuzz_$t"
        mkdir -p "$OUT/corpus_$t"
        "$OUT/fuzz_$t" -max_total_time="$T" -max_len=1300 "$OUT/corpus_$t" 2>&1 | tail -3
    done
fi
