#!/bin/sh
# Runs on the QNX target. Each faulty access must end as a Wasm trap
# (non-zero exit, "Exception: ..." on stderr) while the system keeps running.
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
cd "${1:-/data/wamr}" || exit 1
pass=0; fail=0
for kind in wasm aot; do
  for mode in ok load store stack; do
    out=$(./iwasm oob.$kind $mode 2>&1); rc=$?
    if [ $mode = ok ]; then want=0; else want=1; fi
    if [ $want = 0 ] && [ $rc = 0 ]; then res=PASS
    elif [ $want = 1 ] && [ $rc != 0 ] && echo "$out" | grep -q "Exception"; then res=PASS
    else res=FAIL; fi
    [ $res = PASS ] && pass=$((pass+1)) || fail=$((fail+1))
    echo "$res oob.$kind $mode rc=$rc :: $(echo "$out" | grep -i exception | head -1)"
  done
done
echo "sandbox tests: $pass passed, $fail failed"
[ $fail = 0 ]
