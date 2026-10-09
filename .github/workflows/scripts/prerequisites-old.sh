#!/usr/bin/env bash
# Compatibility entry point: QEMU 11.1 requires a newer build environment.
# Use Debian 12 or a distribution with equivalent Python and library versions.
set -euo pipefail
exec "$(dirname "$0")/prerequisites-native.sh" "$@"
