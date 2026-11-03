/*
 * oob.c - shows that a faulty Wasm module cannot touch memory outside its
 * sandbox. Each case is run as a separate iwasm invocation; the runtime turns
 * the access into a Wasm trap (on QNX via the SIGSEGV-based bounds check) and
 * the host process keeps running.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage: oob.wasm load|store|stack|ok
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static volatile uint32_t sink;

static int
recurse(int depth)
{
    volatile char pad[512];
    pad[0] = (char)depth;
    return recurse(depth + 1) + pad[0];
}

int
main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "ok";
    /* 3 GiB: far beyond the module's linear memory (a few MiB) */
    volatile uint32_t *far = (volatile uint32_t *)(uintptr_t)0xC0000000u;

    printf("oob.wasm: mode %s\n", mode);
    fflush(stdout);
    if (strcmp(mode, "load") == 0)
        sink = *far;
    else if (strcmp(mode, "store") == 0)
        *far = 42;
    else if (strcmp(mode, "stack") == 0)
        sink = (uint32_t)recurse(0);
    printf("oob.wasm: finished without a trap\n");
    return 0;
}
