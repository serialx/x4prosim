#!/usr/bin/env bash
# QEMU v10.1.0 tests/docker/dockerfiles/emsdk-wasm32-cross.docker, on macOS.
# Uses Homebrew Emscripten/ninja/autoconf/automake/libtool and pinned pip
# Meson 1.5.0 + tomli in build-wasm-deps/venv. No Docker or changes to QEMU sources are required.
# Run from any directory. FORCE=1 rebuilds all dependencies from cached sources.
# Downloads (including Meson's PCRE2 fallback) survive rebuilds for offline use.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build-wasm-deps"
SRC="$BUILD/src"
SYSROOT="$BUILD/sysroot"
CROSS="$ROOT/x4prosim/wasm/cross.meson"
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
FORCE=${FORCE:-0}
mkdir -p "$SRC" "$SYSROOT/lib/pkgconfig"

if ! command -v emcc >/dev/null 2>&1; then
    # Homebrew serializes installs; retry only its explicit lock error.
    while ! brew install emscripten >"$BUILD/brew.log" 2>&1; do
        cat "$BUILD/brew.log" >&2
        if ! grep -Eq 'already locked|another process has already locked' "$BUILD/brew.log"; then
            exit 1
        fi
        sleep 30
    done
fi
for tool in emcc em++ emar emranlib emconfigure emmake ninja pkg-config \
            autoreconf aclocal glibtoolize; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done

# Keep Emscripten's writable compiler cache out of the Homebrew installation.
export EM_CACHE="${EM_CACHE:-$BUILD/em-cache}"
export CPATH="$SYSROOT/include"
export PKG_CONFIG_PATH="$SYSROOT/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
export EM_PKG_CONFIG_PATH="$PKG_CONFIG_PATH"
export EM_PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR"
export CFLAGS='-O3 -pthread -DWASM_BIGINT'
export CXXFLAGS="$CFLAGS"
# WASM_BIGINT is now the default; Emscripten 6 accepts it with a deprecation
# warning. Retain it to match the upstream build flags.
# Keep -pthread on links too: the static libraries contain shared-memory code.
export LDFLAGS="-pthread -sWASM_BIGINT -sASYNCIFY=1 -L$SYSROOT/lib"

needed() {
    if [[ "$FORCE" != 1 && -f "$SYSROOT/lib/pkgconfig/$1.pc" ]]; then
        echo "Skipping $1 (installed)"
        return 1
    fi
}

fetch() {
    local name=$1 url=$2 digest=$3
    if [[ ! -f "$SRC/$name" ]]; then
        curl --fail --location --retry 3 "$url" -o "$SRC/$name.part"
        mv "$SRC/$name.part" "$SRC/$name"
    fi
    printf '%s  %s\n' "$digest" "$SRC/$name" | shasum -a 256 -c -
}

extract() {
    local archive=$1 directory=$2
    rm -rf "$BUILD/$directory"
    mkdir -p "$BUILD/$directory"
    tar -xf "$SRC/$archive" -C "$BUILD/$directory" --strip-components=1
}

meson_bootstrap() {
    if [[ ! -x "$BUILD/venv/bin/meson" ]]; then
        python3 -m venv "$BUILD/venv"
        "$BUILD/venv/bin/python" -m pip install meson==1.5.0 tomli
    fi
}

if needed zlib; then
    fetch zlib-1.3.1.tar.gz https://zlib.net/fossils/zlib-1.3.1.tar.gz \
        9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    extract zlib-1.3.1.tar.gz zlib
    (
        cd "$BUILD/zlib"
        # Otherwise zlib detects Darwin and uses Apple libtool, which drops
        # wasm objects and silently produces an empty archive.
        emconfigure ./configure --prefix="$SYSROOT" --static --uname=emscripten
        emmake make -j"$JOBS" install
    )
fi

if needed libffi; then
    fetch libffi-3.4.7.tar.gz https://codeload.github.com/libffi/libffi/tar.gz/refs/tags/v3.4.7 \
        f07c08c9c14977eafb9b5f9277713d91358ec18fc8aaa5607d6790cde90cba12
    extract libffi-3.4.7.tar.gz libffi
    (
        cd "$BUILD/libffi"
        autoreconf -fiv
        emconfigure ./configure --host=wasm32-unknown-linux \
            --prefix="$SYSROOT" --enable-static --disable-shared \
            --disable-dependency-tracking --disable-builddir \
            --disable-multi-os-directory --disable-raw-api --disable-docs
        emmake make -j"$JOBS" install SUBDIRS=include
    )
fi

if needed pixman-1; then
    meson_bootstrap
    fetch pixman-0.44.2.tar.gz https://cairographics.org/releases/pixman-0.44.2.tar.gz \
        6349061ce1a338ab6952b92194d1b0377472244208d47ff25bef86fc71973466
    extract pixman-0.44.2.tar.gz pixman
    "$BUILD/venv/bin/meson" setup "$BUILD/pixman/_build" "$BUILD/pixman" \
        --prefix="$SYSROOT" --libdir=lib --cross-file="$CROSS" \
        --default-library=static --buildtype=release -Dtests=disabled -Ddemos=disabled
    ninja -C "$BUILD/pixman/_build" -j"$JOBS" install
fi

if [[ "$FORCE" == 1 || ! -f "$SYSROOT/lib/libresolv.a" ]]; then
    mkdir -p "$BUILD/stub"
    cat > "$BUILD/stub/res_query.c" <<'STUB'
#include <netdb.h>
int res_query(const char *name, int class, int type, unsigned char *dest, int len)
{
    h_errno = HOST_NOT_FOUND;
    return -1;
}
STUB
    emcc $CFLAGS -c "$BUILD/stub/res_query.c" -fPIC -o "$BUILD/stub/libresolv.o"
    emar rcs "$SYSROOT/lib/libresolv.a" "$BUILD/stub/libresolv.o"
else
    echo 'Skipping libresolv (installed)'
fi

if needed glib-2.0; then
    meson_bootstrap
    fetch glib-2.84.0.tar.xz https://download.gnome.org/sources/glib/2.84/glib-2.84.0.tar.xz \
        f8823600cb85425e2815cfad82ea20fdaa538482ab74e7293d58b3f64a5aff6a
    extract glib-2.84.0.tar.xz glib
    # GLib 2.84 only checks the size_t typedef for compiler IDs gcc/clang.
    # Meson calls emcc "emscripten"; current emcc uses unsigned long size_t,
    # so the size-only fallback incorrectly selects unsigned int for gsize.
    # Enable the existing type check instead of suppressing pointer errors.
    python3 - "$BUILD/glib/meson.build" <<'PYTHON'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
old = "if cc.get_id() == 'gcc' or cc.get_id() == 'clang'\n  foreach type_name"
new = "if cc.get_id() in ['gcc', 'clang', 'emscripten']\n  foreach type_name"
source = path.read_text()
assert source.count(old) == 1
path.write_text(source.replace(old, new))
PYTHON
    mkdir -p "$SRC/meson-packagecache"
    ln -s "$SRC/meson-packagecache" "$BUILD/glib/subprojects/packagecache"
    "$BUILD/venv/bin/meson" setup "$BUILD/glib/_build" "$BUILD/glib" \
        --prefix="$SYSROOT" --libdir=lib --cross-file="$CROSS" \
        --default-library=static --buildtype=release --force-fallback-for=pcre2 \
        -Dc_args="-O3 -pthread -DWASM_BIGINT -Wno-incompatible-function-pointer-types" \
        -Dselinux=disabled -Dxattr=false -Dlibmount=disabled -Dnls=disabled \
        -Dtests=false -Dglib_debug=disabled -Dglib_assert=false -Dglib_checks=false
    # Match upstream: these probes can succeed although the final link fails.
    sed -i.bak -E '/#define HAVE_POSIX_SPAWN 1/d; /#define HAVE_PTHREAD_GETNAME_NP 1/d' \
        "$BUILD/glib/_build/config.h"
    ninja -C "$BUILD/glib/_build" -j"$JOBS" install
fi

echo "Wasm dependency sysroot ready: $SYSROOT"
