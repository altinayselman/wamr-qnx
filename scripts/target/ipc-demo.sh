#!/bin/sh
# Runs on the QNX target: a Wasm module serves QNX messages, a native
# client sends sensor samples to it.
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
cd "${1:-/data/wamr}" || exit 1
kind="${2:-aot}"
n="${3:-2000}"
lib="$PWD/libwamr_qnx_ipc.so"

echo "== capability check: no allowlist, attach must be refused"
WAMR_QNX_IPC_ALLOW= ./iwasm --native-lib="$lib" filter.$kind
echo "rc=$?"

echo "== filter.$kind serving 'wasm_filter'"
WAMR_QNX_IPC_ALLOW=wasm_filter ./iwasm --native-lib="$lib" filter.$kind &
srv=$!
i=0
while [ ! -e /dev/name/local/wasm_filter ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
ls -l /dev/name/local/
pidin -p $srv -f aN 2>/dev/null | head -3
./sensor_sim "$n" wasm_filter --stop
rc=$?
wait $srv
echo "sensor_sim rc=$rc"
exit $rc
