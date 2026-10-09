#!/usr/bin/env bash
# Headless Node launcher. Ctrl-a c switches between USB-CDC and the HMP monitor.
# HMP screendump paths use /host followed by the absolute host path.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
if [ "$#" -lt 2 ]; then
    echo "usage: $0 flash.bin sd.img [qemu args...]" >&2
    exit 2
fi
flash=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
sd=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
shift 2
chip=$(od -An -tu1 -j12 -N1 "$flash" | tr -d ' ')
machine=${X4MACHINE:-$([ "$chip" = 5 ] && echo x3 || echo x4pro)}
case "$machine" in
    x3) arch=riscv32; shift_bits=0 ;;
    x4pro) arch=xtensa; shift_bits=2 ;;
    *) echo "unsupported machine: $machine" >&2; exit 2 ;;
esac
cp "$flash" "$flash.run"
chmod u+w "$flash.run"
exec "${NODE:-node}" "${WASM_BUILD_DIR:-$ROOT/build-wasm}/qemu-system-$arch.js" \
    -L "/host$ROOT/pc-bios" \
    -machine "$machine" -icount "shift=$shift_bits,sleep=on" \
    -drive "file=/host${flash//,/,,}.run,if=mtd,format=raw,cache.direct=off" \
    -drive "file=/host${sd//,/,,},if=sd,format=raw,cache.direct=off" \
    -chardev stdio,id=cdc,mux=on -serial null \
    -global driver=misc.esp32s3.usb_serial_jtag,property=chardev,value=cdc \
    -mon chardev=cdc,mode=readline -display none -nic none "$@"
