#!/usr/bin/env bash
# Fetch upstream WAMR at the pinned tag and apply the QNX port patches.
#
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
#
# Usage: scripts/fetch-upstream.sh [dest-dir]   (default: ./upstream/wasm-micro-runtime)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
url="https://github.com/bytecodealliance/wasm-micro-runtime.git"
tag="WAMR-2.4.5"
dest="${1:-$here/upstream/wasm-micro-runtime}"

if [ ! -d "$dest/.git" ]; then
  git clone --depth 1 --branch "$tag" "$url" "$dest"
fi
cd "$dest"
if git apply --check "$here"/patches/*.patch 2>/dev/null; then
  git apply "$here"/patches/*.patch
  echo "patched $dest ($tag) with $(ls "$here"/patches/*.patch | wc -l) patch(es)"
elif git apply --reverse --check "$here"/patches/*.patch 2>/dev/null; then
  echo "patches already applied in $dest"
else
  echo "patches do not apply cleanly to $dest" >&2
  exit 1
fi
