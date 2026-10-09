#!/usr/bin/env bash

set -euo pipefail

brew install \
  glib \
  libgcrypt \
  libpng \
  libslirp \
  ninja \
  pixman \
  pkg-config \
  python3 \
  sdl2 \
&& :

# QEMU configure installs its bundled Meson into build/pyvenv.
command -v python3
python3 --version
