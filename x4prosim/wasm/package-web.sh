#!/usr/bin/env bash
# Assemble a standalone, dependency-free browser distribution.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
case "${WASM64_MODE:-32}" in
    32) DEFAULT_BUILD="$ROOT/build-wasm-32limit" ;;
    64) DEFAULT_BUILD="$ROOT/build-wasm" ;;
    *) echo 'WASM64_MODE must be 64 or 32' >&2; exit 1 ;;
esac
BUILD=${WASM_BUILD_DIR:-$DEFAULT_BUILD}
DEST="$BUILD/web-dist"
mkdir -p "$DEST"
cp "$ROOT"/x4prosim/wasm/web/* "$DEST/"
for arch in riscv32 xtensa; do
    cp "$BUILD/qemu-system-$arch.js" "$BUILD/qemu-system-$arch.wasm" "$DEST/"
    if [ -f "$BUILD/qemu-system-$arch.worker.js" ]; then
        cp "$BUILD/qemu-system-$arch.worker.js" "$DEST/"
    fi
done
cp "$ROOT/pc-bios/esp32c3-rom.bin" "$ROOT/pc-bios/esp32s3_rev0_rom.bin" "$DEST/"
if [ ! -f "$DEST/blank-sd.img" ]; then
    python3 "$ROOT/x4prosim/mksd.py" "$DEST/blank-sd.img" 64
fi
printf 'Web package: %s\nRun: python3 x4prosim/wasm/serve.py %s\n' "$DEST" "$DEST"
