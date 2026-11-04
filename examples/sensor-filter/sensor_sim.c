/*
 * sensor_sim.c - native QNX client for filter.wasm.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sends a noisy sine wave with injected spikes to the Wasm server over QNX
 * message passing (name_open + MsgSendv) and measures each round trip with
 * ClockCycles().
 *
 * Usage: sensor_sim [samples] [service-name] [--stop]
 */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#include "wamr_qnx_ipc_proto.h"
#include "sensor_msg.h"

static int
cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static long
send_sample(int coid, sensor_sample_t *s, sensor_result_t *r)
{
    wamr_qnx_msg_hdr_t hdr = { WAMR_QNX_MSG_TYPE, 0, sizeof(*s) };
    iov_t siov[2], riov[1];

    SETIOV(&siov[0], &hdr, sizeof(hdr));
    SETIOV(&siov[1], s, sizeof(*s));
    SETIOV(&riov[0], r, sizeof(*r));
    return MsgSendv(coid, siov, 2, riov, 1);
}

int
main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1000;
    const char *name = argc > 2 ? argv[2] : SENSOR_SERVICE_NAME;
    int stop = argc > 3 && strcmp(argv[3], "--stop") == 0;
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    uint64_t *rtt;
    int coid, i, spikes_sent = 0, spikes_seen = 0;
    double sum = 0.0;

    if (n < 1)
        n = 1;
    rtt = calloc((size_t)n, sizeof(*rtt));
    coid = name_open(name, 0);
    if (coid == -1) {
        fprintf(stderr, "sensor_sim: name_open(%s): %s\n", name,
                strerror(errno));
        return 1;
    }

    for (i = 0; i < n; i++) {
        sensor_sample_t s = { SENSOR_MAGIC, SENSOR_CMD_SAMPLE, (uint32_t)i, 0 };
        sensor_result_t r;
        uint64_t t0, t1;

        s.value =
            (float)(20.0 + 3.0 * sin(i / 25.0) + ((rand() % 100) - 50) / 100.0);
        if (i % 97 == 50) { /* injected spike */
            s.value += 40.0f;
            spikes_sent++;
        }
        t0 = ClockCycles();
        if (send_sample(coid, &s, &r) == -1) {
            fprintf(stderr, "sensor_sim: MsgSend: %s\n", strerror(errno));
            return 1;
        }
        t1 = ClockCycles();
        rtt[i] = (t1 - t0) * 1000000000ULL / cps;
        sum += rtt[i];
        spikes_seen += r.spike ? 1 : 0;
        if (r.seq != s.seq) {
            fprintf(stderr, "sensor_sim: reply out of order\n");
            return 1;
        }
        if (i < 3 || i == n - 1)
            printf("sample %4d raw %7.3f -> filtered %7.3f%s\n", i, s.value,
                   r.filtered, r.spike ? "  (spike rejected)" : "");
    }

    qsort(rtt, (size_t)n, sizeof(*rtt), cmp_u64);
    printf("sensor_sim: %d samples, spikes injected %d, flagged by wasm %d\n",
           n, spikes_sent, spikes_seen);
    printf("round trip native->wasm->native (ns): min %" PRIu64
           " median %" PRIu64 " p99 %" PRIu64 " max %" PRIu64 " mean %.0f\n",
           rtt[0], rtt[n / 2], rtt[(n * 99) / 100], rtt[n - 1], sum / n);

    if (stop) {
        sensor_sample_t s = { SENSOR_MAGIC, SENSOR_CMD_STOP, 0, 0 };
        sensor_result_t r;
        send_sample(coid, &s, &r);
    }
    name_close(coid);
    free(rtt);
    return spikes_seen == spikes_sent ? 0 : 2;
}
