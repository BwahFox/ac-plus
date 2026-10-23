# Build environment for ac-plus: the Windows build (MinGW, 32-bit) and the City Folk extraction tools.
# Used by ./ac-plus.sh; nothing here needs to be run by hand.
FROM docker.io/library/debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-i686 g++-mingw-w64-i686 cmake make \
        python3 python3-numpy curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# SDL2 for MinGW, where build_wine.sh looks for it (next to the source folder, /build/src)
ARG SDL2_URL=https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-devel-2.30.10-mingw.tar.gz
ARG SDL2_SHA256=a7763f9439ea25685b053e9257dac1eac012e5cd0824f1a801b27b1d92ebe321
RUN curl -fsSL -o /tmp/sdl2.tgz "$SDL2_URL" \
    && echo "$SDL2_SHA256  /tmp/sdl2.tgz" | sha256sum -c - \
    && mkdir -p /build && tar xzf /tmp/sdl2.tgz -C /build && rm /tmp/sdl2.tgz

# Wiimms ISO Tools, to read City Folk disc images (extract_cf_sky.py)
ARG WIT_URL=https://wit.wiimm.de/download/wit-v3.05a-r8638-x86_64.tar.gz
ARG WIT_SHA256=df95a14f1b4b2bd95d3111b70c6304e91d4ed3e4c7f72b1ad5306c3616d8933f
RUN curl -fsSL -o /tmp/wit.tgz "$WIT_URL" \
    && echo "$WIT_SHA256  /tmp/wit.tgz" | sha256sum -c - \
    && tar xzf /tmp/wit.tgz -C /opt && rm /tmp/wit.tgz \
    && ln -s /opt/wit-v3.05a-r8638-x86_64/bin/wit /usr/local/bin/wit

WORKDIR /build/src
