#!/usr/bin/env python3
"""Make an SD image like a real card: MBR, one FAT32 (LBA) partition at 1 MiB.
usage: mksd.py sd.img [size_mb] [dir_to_copy]"""
import os, struct, subprocess, sys
img, mb = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 1024
total = mb * 2048
start = 2048
with open(img, "wb") as f:
    f.truncate(total * 512)
    entry = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 0x0C, b"\xfe\xff\xff", start, total - start)
    f.seek(446); f.write(entry); f.seek(510); f.write(b"\x55\xaa")
subprocess.run(["mkfs.vfat", "-F", "32", "--offset", str(start), img, str((total - start) // 2)],
               check=True, stdout=subprocess.DEVNULL)
if len(sys.argv) > 3:
    # mtools copies a host folder onto the image (e.g. a books/ tree).
    subprocess.run(["mcopy", "-s", "-i", f"{img}@@{start * 512}", *[os.path.join(sys.argv[3], n) for n in os.listdir(sys.argv[3])], "::"], check=True)
