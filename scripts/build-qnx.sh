#!/usr/bin/env bash
# Cross-compile the WAMR QNX port (iwasm + libiwasm + libwamr_qnx_ipc.so +
# native examples) for QNX 8.0.
#
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
#
# Prerequisites: QNX SDP 8.0 environment sourced (QNX_HOST, QNX_TARGET),
# cmake >= 3.21, ninja, and scripts/fetch-upstream.sh already run.
# Works from Git Bash on Windows and from bash on Linux.
#
# Usage: scripts/build-qnx.sh [x86_64|aarch64le] [build-root]
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
arch="${1:-x86_64}"
out="${2:-$here/build}/$arch"
wamr="$here/upstream/wasm-micro-runtime"
tc="$here/cmake/qnx-toolchain.cmake"

: "${QNX_HOST:?source qnxsdp-env first}"
: "${QNX_TARGET:?source qnxsdp-env first}"
[ -d "$wamr/core/shared/platform/qnx" ] || { echo "run scripts/fetch-upstream.sh first" >&2; exit 1; }

case "$arch" in
  x86_64) wtarget=X86_64 ;;
  aarch64le) wtarget=AARCH64 ;;
  *) echo "unknown arch $arch" >&2; exit 1 ;;
esac

# 1. iwasm (interpreter + fast interpreter + AOT loader, WASI, SIMD, HW bounds checks)
cmake -S "$wamr/product-mini/platforms/qnx" -B "$out/iwasm" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$tc" -DQNX_ARCH="$arch" -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_BUILD_TARGET="$wtarget" -DWAMR_BUILD_LIBC_WASI=1 -DWAMR_BUILD_AOT=1 \
  -DWAMR_BUILD_FAST_INTERP=1 -DWAMR_BUILD_LIB_PTHREAD=1 -DWAMR_BUILD_DUMP_CALL_STACK=1
cmake --build "$out/iwasm"

# 2. classic interpreter build, for the interpreter-vs-AOT comparison
cmake -S "$wamr/product-mini/platforms/qnx" -B "$out/iwasm-classic" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$tc" -DQNX_ARCH="$arch" -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_BUILD_TARGET="$wtarget" -DWAMR_BUILD_FAST_INTERP=0
cmake --build "$out/iwasm-classic"

# 3. QNX IPC native library and the native example programs
cmake -S "$here" -B "$out/qnx" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$tc" -DQNX_ARCH="$arch" -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_ROOT="$wamr"
cmake --build "$out/qnx"

echo "built into $out"
