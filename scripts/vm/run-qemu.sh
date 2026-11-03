#!/usr/bin/env bash
# Boot the image under KVM (Linux or WSL2) with SSH forwarded to localhost:2222.
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
# Usage: scripts/vm/run-qemu.sh <vm-dir>
set -euo pipefail
out="${1:?vm directory}/output"
exec qemu-system-x86_64 -enable-kvm -cpu host -smp 2 -m 1G \
  -drive file="$out/disk-qemu",format=raw,if=ide,id=drv0 -kernel "$out/ifs.bin" \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=net0 \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
  -display none -serial file:"$out/qemu.out" -pidfile "$out/qemu.pid" -daemonize
