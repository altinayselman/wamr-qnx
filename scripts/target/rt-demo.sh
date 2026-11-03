#!/bin/sh
# Runs on the QNX target: a Wasm control task woken by a QNX timer pulse
# every period, idle system vs. CPU hogs at normal priority.
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
cd "${1:-/data/wamr}" || exit 1
kind="${2:-aot}"; period="${3:-1000}"; cycles="${4:-5000}"; prio="${5:-50}"
lib="$PWD/libwamr_qnx_ipc.so"
run() { WAMR_QNX_IPC_ALLOW=wasm_cyclic WAMR_QNX_MAX_PRIO=60 ./iwasm --native-lib="$lib" cyclic.$kind "$period" "$cycles" "$1"; }

echo "== idle system, priority $prio"
run "$prio"

echo "== 4 busy loops at priority 10 (default), Wasm task at priority $prio"
hogs=""
for i in 1 2 3 4; do sh -c 'while :; do :; done' & hogs="$hogs $!"; done
sleep 1
run "$prio"
echo "== same load, Wasm task left at priority 10"
run 10
kill $hogs
