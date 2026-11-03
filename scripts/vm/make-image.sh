#!/usr/bin/env bash
# Build the QNX 8.0 x86_64 QEMU image used for the results in this repository.
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
#
# Usage: scripts/vm/make-image.sh <vm-dir> <ssh-public-key>
# Needs the SDP environment (mkqnximage on PATH). On Windows run it from a
# shell where qnxsdp-env.bat has been applied (mkqnximage uses the SDP's bash).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
vm="${1:?vm directory}"; key="${2:?ssh public key}"
mkdir -p "$vm" && cd "$vm"
opts=(--type=qemu --arch=x86_64 --ip=dhcp --hostname=qnxwamr --cpu=2 --ram=1G
      --data-size=512 --ssh-ident="$key" --sshd-pregen=no --noprompt)
[ -d local/snippets ] || mkqnximage "${opts[@]}"
cp "$here/ifs_files.custom" local/snippets/ifs_files.custom
mkqnximage "${opts[@]}" --build
ls -l output/ifs.bin output/disk-qemu
