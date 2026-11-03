/*
 * cyclic.c - a periodic control task written as a WebAssembly module.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0
 *
 * The module asks the host for SCHED_FIFO priority, starts a periodic QNX
 * timer that delivers a pulse to its own channel, and runs a small PID step
 * on every pulse. It records how far each interval between two activations
 * is from the requested period (jitter) and prints a summary.
 *
 * Usage (inside iwasm): cyclic.wasm [period_us] [cycles] [priority]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_qnx_ipc.h"

#define PULSE_TICK 1 /* _PULSE_CODE_MINAVAIL */
#define MAX_CYCLES 100000

static uint32_t jitter_ns[MAX_CYCLES];

static int
cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

int
main(int argc, char **argv)
{
    uint64_t period_us = argc > 1 ? strtoull(argv[1], NULL, 10) : 1000;
    int cycles = argc > 2 ? atoi(argv[2]) : 5000;
    int prio = argc > 3 ? atoi(argv[3]) : 10;
    uint64_t period = period_us * 1000ULL, prev = 0;
    double setpoint = 1.0, plant = 0.0, integ = 0.0, prev_err = 0.0;
    uint64_t sum = 0;
    int32_t ch, timer, r;
    int i, n = 0;

    if (cycles < 10 || cycles > MAX_CYCLES)
        cycles = 5000;

    ch = qnx_attach("wasm_cyclic");
    if (ch < 0) {
        printf("cyclic.wasm: qnx_attach failed: %d\n", (int)ch);
        return 1;
    }
    r = qnx_set_priority(prio);
    printf("cyclic.wasm: SCHED_FIFO priority %d -> %s\n", prio,
           r == 0 ? "ok" : "refused by host");

    timer = qnx_timer_pulse(ch, period, PULSE_TICK);
    if (timer < 0) {
        printf("cyclic.wasm: qnx_timer_pulse failed: %d\n", (int)timer);
        return 1;
    }

    for (i = 0; i < cycles; i++) {
        int32_t reply, code = 0, value;
        uint32_t size;
        uint64_t now;
        double err, deriv, u;

        r = qnx_receive(ch, NULL, 0, &reply, &size, &code, &value);
        if (r != QNX_RECV_PULSE || code != PULSE_TICK) {
            printf("cyclic.wasm: unexpected receive %d code %d\n", (int)r,
                   (int)code);
            return 1;
        }
        now = qnx_clock_ns();
        if (prev) {
            /* jitter: how far this interval is from the requested period */
            uint64_t interval = now - prev;
            uint64_t dev =
                interval > period ? interval - period : period - interval;
            jitter_ns[n] = dev > UINT32_MAX ? UINT32_MAX : (uint32_t)dev;
            sum += jitter_ns[n];
            n++;
        }
        prev = now;

        /* one PID step on a first-order plant */
        err = setpoint - plant;
        integ += err * 0.001;
        deriv = (err - prev_err) / 0.001;
        prev_err = err;
        u = 2.0 * err + 0.5 * integ + 0.01 * deriv;
        plant += (u - plant) * 0.05;
    }

    qnx_timer_stop(timer);
    qnx_detach(ch);

    qsort(jitter_ns, (size_t)n, sizeof(jitter_ns[0]), cmp_u32);
    printf("cyclic.wasm: %d activations at %llu us, plant output %.4f\n",
           cycles, (unsigned long long)period_us, plant);
    printf("period jitter |interval - period| (us): median %.1f p99 %.1f "
           "p99.9 %.1f max %.1f mean %.1f\n",
           jitter_ns[n / 2] / 1000.0, jitter_ns[(n * 99) / 100] / 1000.0,
           jitter_ns[(n * 999) / 1000] / 1000.0, jitter_ns[n - 1] / 1000.0,
           (double)sum / n / 1000.0);
    return 0;
}
