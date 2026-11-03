/*
 * cyclic.c - a periodic control task written as a WebAssembly module.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * The module asks the host for SCHED_FIFO priority, starts a periodic QNX
 * timer that delivers a pulse to its own channel, and runs a small PID step
 * on every pulse. It records how late each activation is against the ideal
 * schedule and prints a latency summary.
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

static uint32_t late_ns[MAX_CYCLES];

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
    uint64_t period = period_us * 1000ULL, start, next;
    double setpoint = 1.0, plant = 0.0, integ = 0.0, prev_err = 0.0;
    uint64_t sum = 0;
    int32_t ch, timer, r;
    int i;

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
    start = qnx_clock_ns();
    next = start + period;

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
        /* lateness against the ideal timeline; the first period is setup */
        late_ns[i] = now > next ? (uint32_t)(now - next) : 0;
        sum += late_ns[i];
        next += period;
        while (next + period < now) /* overrun: skip missed slots */
            next += period;

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

    qsort(late_ns, (size_t)cycles, sizeof(late_ns[0]), cmp_u32);
    printf("cyclic.wasm: %d activations at %llu us, plant output %.4f\n",
           cycles, (unsigned long long)period_us, plant);
    printf("activation latency (us): min %.1f median %.1f p99 %.1f p99.9 %.1f "
           "max %.1f mean %.1f\n",
           late_ns[0] / 1000.0, late_ns[cycles / 2] / 1000.0,
           late_ns[(cycles * 99) / 100] / 1000.0,
           late_ns[(cycles * 999) / 1000] / 1000.0,
           late_ns[cycles - 1] / 1000.0, (double)sum / cycles / 1000.0);
    return 0;
}
