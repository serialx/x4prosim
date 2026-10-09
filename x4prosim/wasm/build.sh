#!/usr/bin/env bash
# Build the X3 and X4 Pro system emulators with Homebrew Emscripten and TCI.
# Run build-deps.sh first. Build artifacts and compiler caches stay in the repo.
set -euo pipefail
WEB=0
if [ "${1:-}" = --web ]; then
    WEB=1
    shift
fi
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SYSROOT="$ROOT/build-wasm-deps/sysroot"
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
export EM_CACHE="${EM_CACHE:-$ROOT/build-wasm-deps/em-cache}"
export PKG_CONFIG_PATH="$SYSROOT/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
export EM_PKG_CONFIG_PATH="$PKG_CONFIG_PATH"
export EM_PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR"
export CPATH="$SYSROOT/include"
export CFLAGS='-O3 -pthread -DWASM_BIGINT'
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-pthread -sWASM_BIGINT -sASYNCIFY=1 -L$SYSROOT/lib"
command -v emcc >/dev/null
pkg-config --print-errors --exists glib-2.0 pixman-1 zlib libffi libgcrypt || {
    echo "Run $ROOT/x4prosim/wasm/build-deps.sh first" >&2
    exit 1
}

# Override upstream's fixed 2 GB / ES-module settings for browser and plain Node
# use, while keeping the pthread main-loop and fiber/libffi Asyncify support.
LINK_FLAGS="-O3 -g2 $LDFLAGS -sPROXY_TO_PTHREAD=1 -sFORCE_FILESYSTEM=1"
LINK_FLAGS+=" -sALLOW_TABLE_GROWTH=1 -sALLOW_MEMORY_GROWTH=1"
LINK_FLAGS+=" -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648"
LINK_FLAGS+=" -sSTACK_SIZE=8388608 -sASYNCIFY_STACK_SIZE=1048576"
LINK_FLAGS+=" -sEXIT_RUNTIME=1 -sEXPORT_ES6=0 -sENVIRONMENT=web,worker,node -sASYNCIFY_IMPORTS=ffi_call_js"
LINK_FLAGS+=" -sEXPORTED_RUNTIME_METHODS=addFunction,removeFunction,TTY,FS"
LINK_FLAGS+=" -lnodefs.js --pre-js $ROOT/x4prosim/wasm/node-fs.js"
mkdir -p "$ROOT/build-wasm"
cd "$ROOT/build-wasm"
emconfigure ../configure \
    --static --disable-tools --enable-tcg-interpreter \
    --target-list=xtensa-softmmu,riscv32-softmmu \
    --enable-sdl --disable-sdl-image --disable-opengl \
    --enable-gcrypt --disable-slirp --disable-vnc --disable-gtk \
    --disable-docs --disable-user --disable-capstone --disable-werror \
    --disable-gnutls --disable-nettle --disable-rust --disable-plugins \
    --disable-strip --disable-pie --disable-debug-info \
    -Dc_args="$CFLAGS" -Dcpp_args="$CXXFLAGS" \
    -Dc_link_args="$LINK_FLAGS" -Dcpp_link_args="$LINK_FLAGS" \
    "$@"
ninja -j"$JOBS" qemu-system-riscv32.js qemu-system-xtensa.js

if [ "$WEB" = 1 ]; then
    "$ROOT/x4prosim/wasm/package-web.sh"
fi
