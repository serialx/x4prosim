#!/bin/sh
# Merge a PlatformIO or ESP-IDF build (bootloader, partition table, app) into a 16 MB flash image.
# usage: x4prosim/mkflash.sh <build dir> out.bin
#   pio: .pio/build/<env>   idf: build (bootloader/ and partition_table/ subfolders are found too)
# The chip comes from the bootloader header (ESP32-S3 or ESP32-C3). For the C3 (X3/X4 image) the
# NVS partition (0x9000, the Arduino layout) is seeded with CrossPoint's cached device type
# cphw/dev_det=2 (X3), what the firmware writes after its first boot: CrossPoint up to 1.6.5
# deadlocks on a first boot that runs the X3 fingerprint probe (it uses Wire before Wire.begin).
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$1
find1() { find "$b" -maxdepth 2 -name "$1" | head -1; }
boot=$(find1 bootloader.bin); part=$(find1 "partition*.bin")
app=$b/firmware.bin
[ -f "$app" ] || app=$(find "$b" -maxdepth 1 -name "*.bin" ! -name "bootloader.bin" ! -name "partition*.bin" | head -1)
[ -f "$boot" ] && [ -f "$part" ] && [ -f "$app" ] || { echo "need bootloader.bin, partition*.bin and an app .bin in $b" >&2; exit 1; }
chip=$(od -An -tu1 -j12 -N1 "$boot" | tr -d ' ')
if [ "$chip" = 5 ]; then
  nvs=$(mktemp); python3 "$here/mknvs.py" "$nvs" 0x5000 cphw dev_det=2
  python3 -m esptool --chip esp32c3 merge_bin --fill-flash-size 16MB -o "$2" 0x0 "$boot" 0x8000 "$part" 0x9000 "$nvs" 0x10000 "$app"
  rm -f "$nvs"
else
  python3 -m esptool --chip esp32s3 merge_bin --fill-flash-size 16MB -o "$2" 0x0 "$boot" 0x8000 "$part" 0x10000 "$app"
fi
