#!/usr/bin/env bash

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

dpkg --add-architecture arm64

apt-get update -y -q
apt-get install -y -q --no-install-recommends \
    binutils-aarch64-linux-gnu \
    build-essential \
    ca-certificates \
    crossbuild-essential-arm64 \
    gcc-aarch64-linux-gnu \
    git \
    libgcrypt20-dev:arm64 \
    libglib2.0-dev:arm64 \
    libgpg-error-dev:arm64 \
    libpixman-1-dev:arm64 \
    libpng-dev:arm64 \
    libsdl2-dev:arm64 \
    libslirp-dev:arm64 \
    ninja-build \
    pkg-config \
    python3 \
    python3-venv \
    xz-utils \
    zlib1g-dev:arm64 \
&& :

# QEMU configure installs its bundled Meson into build/pyvenv.
