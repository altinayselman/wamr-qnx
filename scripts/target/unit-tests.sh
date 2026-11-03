#!/bin/sh
# Runs the cross-compiled upstream WAMR unit tests on the QNX target.
# Usage: unit-tests.sh [dir]   (dir holds one sub-directory per suite)
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
root="${1:-/data/wamr/unit}"
cd "$root" || exit 1
total=0; tfail=0
for d in */; do
  d=${d%/}
  for t in "$d"/*; do
    case "$t" in *.*) continue ;; esac
    [ -f "$t" ] && [ -x "$t" ] || continue
    name=$(basename "$t")
    log="$root/$name.log"
    ( cd "$d" && timeout 900 "./$name" --gtest_color=no >"$log" 2>&1 )
    rc=$?
    pass=$(sed -n 's/^\[  PASSED  \] \([0-9]*\) test.*/\1/p' "$log")
    fails=$(grep '^\[  FAILED  \] [A-Za-z]' "$log" | sed 's/ (.*//' | sort -u | wc -l | tr -d ' ')
    ran=$(sed -n 's/^\[==========\] \([0-9]*\) tests\{0,1\} from.*ran.*/\1/p' "$log")
    echo "$name: ran ${ran:-?} passed ${pass:-0} failed $fails (exit $rc)"
    grep '^\[  FAILED  \] [A-Za-z]' "$log" | sed 's/ (.*//; s/^/    /' | sort -u
    total=$((total + ${ran:-0})); tfail=$((tfail + fails))
  done
done
echo "TOTAL ran $total, failed $tfail"
