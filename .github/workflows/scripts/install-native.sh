#!/usr/bin/env bash

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

# For libslirp0 on ubuntu-18.04:
# add-apt-repository ppa:openstack-ubuntu-testing/ussuri

apt-get update -y -q
apt-get install -y -q --no-install-recommends \
    libgcrypt20 \
    libglib2.0-0 \
    libpixman-1-0 \
    libpng16-16 \
    libsdl2-2.0-0 \
    libslirp0 \
&& :
