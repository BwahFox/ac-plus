# Build environment for ac-plus (Nookery 1.0 based): the Linux build, the Windows build (clang-cl against Microsoft's
# SDK, downloaded by xwin only when you accept its license: ./ac-plus.sh build-windows --accept-msvc-license) and the
# City Folk converter. Used by ./ac-plus.sh; nothing here needs to be run by hand.
# The Linux build uses GCC (as Nookery's does), the Windows one clang-cl. Debian 13: an older C library than most desktops have, so the Linux build runs on them (and on SteamOS) too.
FROM docker.io/library/debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc g++ clang lld llvm cmake ninja-build pkg-config git curl ca-certificates xz-utils file \
        python3 python3-numpy python3-pil patchelf \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev \
        libegl-dev libgl-dev libgles-dev libvulkan-dev libibus-1.0-dev liburing-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev libudev-dev libdbus-1-dev \
        libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

# clang-cl, lld-link and the other LLVM tools by their plain names (Debian keeps them in /usr/lib/llvm-<version>/bin,
# and has no clang-cl: it's clang started under that name)
RUN B="$(ls -d /usr/lib/llvm-*/bin | sort -V | tail -1)" \
    && ln -sf "$B/clang" /usr/local/bin/clang-cl \
    && for t in lld-link llvm-lib llvm-rc llvm-mt llvm-ar; do ln -sf "$B/$t" /usr/local/bin/$t; done \
    && clang-cl --version | head -1 && lld-link --version

# Rust (Nookery reads disc images with nod, a Rust library), with the Windows target for the Windows build
ENV RUSTUP_HOME=/opt/rust CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --profile minimal --default-toolchain stable \
        -t x86_64-pc-windows-msvc \
    && cargo install --locked xwin \
    && chmod -R a+rwX /opt/rust /opt/cargo

# A Windows Python with numpy and Pillow, shipped in the Windows game folder (cf-tools/python/) to convert the
# player's City Folk disc on the first start (pc/src/pc_cf_setup.c).
ARG PYEMBED_URL=https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip
ARG PYEMBED_SHA256=4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3
ARG NUMPY_URL=https://files.pythonhosted.org/packages/36/fa/8c9210162ca1b88529ab76b41ba02d433fd54fecaf6feb70ef9f124683f1/numpy-2.2.6-cp312-cp312-win_amd64.whl
ARG NUMPY_SHA256=c1f9540be57940698ed329904db803cf7a402f3fc200bfe599334c9bd84a40b2
ARG PILLOW_URL=https://files.pythonhosted.org/packages/ff/cd/73a79ecdc822076002eb24b5e1e4a530b0bb5310fcb6d239abbec877110b/pillow-13.0.0-cp312-cp312-win_amd64.whl
ARG PILLOW_SHA256=ff3fe9eb621fbb9db4c4be33ab946f6bac5f98ab50ca8d3260503653656c961b
RUN curl -fsSL -o /tmp/py.zip "$PYEMBED_URL" && echo "$PYEMBED_SHA256  /tmp/py.zip" | sha256sum -c - \
    && curl -fsSL -o /tmp/np.whl "$NUMPY_URL" && echo "$NUMPY_SHA256  /tmp/np.whl" | sha256sum -c - \
    && curl -fsSL -o /tmp/pil.whl "$PILLOW_URL" && echo "$PILLOW_SHA256  /tmp/pil.whl" | sha256sum -c - \
    && python3 -c "import zipfile; zipfile.ZipFile('/tmp/py.zip').extractall('/opt/winpython'); \
zipfile.ZipFile('/tmp/np.whl').extractall('/opt/winpython/Lib/site-packages'); \
zipfile.ZipFile('/tmp/pil.whl').extractall('/opt/winpython/Lib/site-packages')" \
    && printf 'Lib\\site-packages\r\n' >> /opt/winpython/python312._pth \
    && find /opt/winpython/Lib/site-packages/numpy -type d -name tests -prune -exec rm -rf {} + \
    && rm -f /tmp/py.zip /tmp/np.whl /tmp/pil.whl && chmod -R a+rX /opt/winpython

# The same for Linux (cf-tools/python/ in the Linux game folder): a portable CPython (python-build-standalone, runs
# on any glibc 2.17+ system, the Steam Deck included) with numpy and Pillow, so the first start doesn't depend on
# the player's Python having them. Symlinks become plain files (the in-game updater's tar reader takes files only),
# and what the converter never uses (Tk, tests, headers, pip) is left out.
ARG LINPY_URL=https://github.com/astral-sh/python-build-standalone/releases/download/20261026/cpython-3.12.16%2B20261026-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz
ARG LINPY_SHA256=ce4f6d0a6cfdb3bed6e44c2f9affd85943c76a42e25ad3a259002f31d6fb8a20
ARG LINNUMPY_URL=https://files.pythonhosted.org/packages/8c/3d/1e1db36cfd41f895d266b103df00ca5b3cbe965184df824dec5c08c6b803/numpy-2.2.6-cp312-cp312-manylinux_2_17_x86_64.manylinux2014_x86_64.whl
ARG LINNUMPY_SHA256=fd83c01228a688733f1ded5201c678f0c53ecc1006ffbc404db9f7a899ac6249
ARG LINPILLOW_URL=https://files.pythonhosted.org/packages/e6/99/fdf65cc539e266ab93546c261bdf98411fe165c6e4e13f9dbf597097192b/pillow-13.0.0-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl
ARG LINPILLOW_SHA256=42d63be3af4f9b951b199b0a9d23365d37ab14687e6d77b74940432c8515c4ca
RUN curl -fsSL -o /tmp/lpy.tgz "$LINPY_URL" && echo "$LINPY_SHA256  /tmp/lpy.tgz" | sha256sum -c - \
    && curl -fsSL -o /tmp/lnp.whl "$LINNUMPY_URL" && echo "$LINNUMPY_SHA256  /tmp/lnp.whl" | sha256sum -c - \
    && curl -fsSL -o /tmp/lpil.whl "$LINPILLOW_URL" && echo "$LINPILLOW_SHA256  /tmp/lpil.whl" | sha256sum -c - \
    && mkdir -p /tmp/lpy && tar -xzf /tmp/lpy.tgz -C /tmp/lpy && mv /tmp/lpy/python /opt/linuxpython \
    && SP=/opt/linuxpython/lib/python3.12/site-packages \
    && python3 -c "import zipfile,sys; [zipfile.ZipFile(w).extractall(sys.argv[1]) for w in sys.argv[2:]]" \
        "$SP" /tmp/lnp.whl /tmp/lpil.whl \
    && cd /opt/linuxpython \
    && rm -rf include share lib/pkgconfig lib/tcl* lib/tk* lib/itcl* lib/thread* bin/idle* bin/pydoc* bin/pip* \
        bin/*-config lib/python3.12/test lib/python3.12/idlelib lib/python3.12/tkinter lib/python3.12/turtledemo \
        lib/python3.12/ensurepip lib/python3.12/config-3.12-x86_64-linux-gnu "$SP"/pip* \
        lib/python3.12/lib-dynload/_tkinter* lib/libpython3* lib/libtcl* lib/libtk* \
    && find "$SP/numpy" -type d -name tests -prune -exec rm -rf {} + \
    && find . -name __pycache__ -prune -exec rm -rf {} + \
    && rm -f bin/python bin/python3 && mv bin/python3.12 bin/python3 \
    && for l in $(find . -type l); do cp --remove-destination "$(readlink -f "$l")" "$l"; done \
    && test -z "$(find . -type l)" && ./bin/python3 -c "import numpy, PIL.Image; print('linux python ok')" \
    && cd / && rm -rf /tmp/lpy /tmp/lpy.tgz /tmp/lnp.whl /tmp/lpil.whl && chmod -R a+rX /opt/linuxpython

WORKDIR /build/src
