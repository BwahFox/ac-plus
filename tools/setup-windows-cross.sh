#!/bin/sh
# Sets up building the Windows version on Linux (ac-plus): a Rust toolchain with the Windows target and xwin, then
# Microsoft's Windows SDK + MSVC runtime through xwin, all in one folder (default ~/.cache/ac-plus-wintools).
# Running xwin accepts Microsoft's license for those files (--accept-license): read it first if you haven't
# (https://visualstudio.microsoft.com/license-terms/), the same terms as for Visual Studio's build tools.
# Needs: clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt (LLVM), cmake, ninja, curl.
# Then:  . <folder>/env.sh && cmake --preset windows-cross && cmake --build --preset windows-cross
set -e
W=${1:-$HOME/.cache/ac-plus-wintools}
mkdir -p "$W"
export RUSTUP_HOME="$W/rustup" CARGO_HOME="$W/cargo" PATH="$W/cargo/bin:$PATH"
if [ ! -x "$W/cargo/bin/cargo" ]; then
    curl -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --profile minimal -t x86_64-pc-windows-msvc
fi
rustup target add x86_64-pc-windows-msvc
[ -x "$W/cargo/bin/xwin" ] || cargo install --locked xwin
[ -d "$W/xwin/sdk" ] || xwin --accept-license --arch x86_64 splat --output "$W/xwin"
# headers some code includes with other capitals than the SDK has them (fine on Windows, not on Linux)
ln -sf wbemidl.h "$W/xwin/sdk/include/um/Wbemidl.h"
cat > "$W/env.sh" <<ENV
export XWIN_DIR="$W/xwin" RUSTUP_HOME="$W/rustup" CARGO_HOME="$W/cargo" PATH="$W/cargo/bin:\$PATH"
ENV
echo "Ready: . $W/env.sh"
