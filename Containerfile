# Build environment for ac-plus: the Windows build (MinGW, 32-bit), its small Linux helper (gcc) and the City
# Folk converter.
# Used by ./ac-plus.sh; nothing here needs to be run by hand.
FROM docker.io/library/debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-i686 g++-mingw-w64-i686 gcc libc6-dev cmake make \
        python3 python3-numpy curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# SDL2 for MinGW, where build_wine.sh looks for it (next to the source folder, /build/src)
ARG SDL2_URL=https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-devel-2.30.10-mingw.tar.gz
ARG SDL2_SHA256=a7763f9439ea25685b053e9257dac1eac012e5cd0824f1a801b27b1d92ebe321
RUN curl -fsSL -o /tmp/sdl2.tgz "$SDL2_URL" \
    && echo "$SDL2_SHA256  /tmp/sdl2.tgz" | sha256sum -c - \
    && mkdir -p /build && tar xzf /tmp/sdl2.tgz -C /build && rm /tmp/sdl2.tgz

# A Windows Python with numpy, shipped in the game folder (cf-tools/python/) to convert the player's City Folk
# disc on the first start (pc/src/pc_cf_setup.c)
ARG PYEMBED_URL=https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip
ARG PYEMBED_SHA256=4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3
ARG NUMPY_URL=https://files.pythonhosted.org/packages/36/fa/8c9210162ca1b88529ab76b41ba02d433fd54fecaf6feb70ef9f124683f1/numpy-2.2.6-cp312-cp312-win_amd64.whl
ARG NUMPY_SHA256=c1f9540be57940698ed329904db803cf7a402f3fc200bfe599334c9bd84a40b2
RUN curl -fsSL -o /tmp/py.zip "$PYEMBED_URL" && echo "$PYEMBED_SHA256  /tmp/py.zip" | sha256sum -c - \
    && curl -fsSL -o /tmp/np.whl "$NUMPY_URL" && echo "$NUMPY_SHA256  /tmp/np.whl" | sha256sum -c - \
    && python3 -c "import zipfile; zipfile.ZipFile('/tmp/py.zip').extractall('/opt/winpython'); \
zipfile.ZipFile('/tmp/np.whl').extractall('/opt/winpython/Lib/site-packages')" \
    && printf 'Lib\\site-packages\r\n' >> /opt/winpython/python312._pth \
    && find /opt/winpython/Lib/site-packages/numpy -type d -name tests -prune -exec rm -rf {} + \
    && rm -f /tmp/py.zip /tmp/np.whl && chmod -R a+rX /opt/winpython

WORKDIR /build/src
