/*
 * filter.c - a WebAssembly module that acts as a QNX message-passing server.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * It registers the name "wasm_filter" (name_attach), then answers each
 * MsgSend from a native client with an exponentially smoothed value and a
 * spike flag. The module has no other access to the system: only the names
 * the host allows through WAMR_QNX_IPC_ALLOW.
 *
 * Build: clang --target=wasm32-wasip1 -O2 -I ../../qnx-ipc/include filter.c -o filter.wasm
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_qnx_ipc.h"
#include "sensor_msg.h"

int
main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : SENSOR_SERVICE_NAME;
    double alpha = argc > 2 ? atof(argv[2]) : 0.2;
    double ema = 0.0;
    int have_ema = 0;
    uint32_t handled = 0, spikes = 0;
    int32_t ch = qnx_attach(name);

    if (ch < 0) {
        printf("filter.wasm: qnx_attach(%s) failed: %d\n", name, (int)ch);
        return 1;
    }
    printf("filter.wasm: serving '%s' (alpha %.2f) from inside the Wasm sandbox\n",
           name, alpha);
    fflush(stdout);

    for (;;) {
        sensor_sample_t in;
        sensor_result_t out;
        int32_t reply, code, value;
        uint32_t size;
        int32_t kind =
            qnx_receive(ch, &in, sizeof(in), &reply, &size, &code, &value);

        if (kind < 0) {
            printf("filter.wasm: receive failed: %d\n", (int)kind);
            return 1;
        }
        if (kind == QNX_RECV_PULSE)
            continue;
        if (size != sizeof(in) || in.magic != SENSOR_MAGIC) {
            qnx_reply(reply, -22 /* EINVAL */, NULL, 0);
            continue;
        }
        if (in.command == SENSOR_CMD_STOP) {
            qnx_reply(reply, 0, NULL, 0);
            break;
        }

        if (!have_ema) {
            ema = in.value;
            have_ema = 1;
        }
        memset(&out, 0, sizeof(out));
        out.seq = in.seq;
        out.spike = fabs(in.value - ema) > SENSOR_SPIKE_LIMIT;
        if (!out.spike) /* spikes do not drag the filter */
            ema = alpha * in.value + (1.0 - alpha) * ema;
        out.filtered = (float)ema;
        out.handled = ++handled;
        spikes += out.spike;
        qnx_reply(reply, 0, &out, sizeof(out));
    }

    printf("filter.wasm: stop after %u samples, %u spikes rejected\n",
           (unsigned)handled, (unsigned)spikes);
    qnx_detach(ch);
    return 0;
}
