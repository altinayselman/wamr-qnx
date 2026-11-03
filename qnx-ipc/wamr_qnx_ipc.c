/*
 * wamr_qnx_ipc.c - QNX native message passing for WebAssembly modules run by
 * WAMR (iwasm --native-lib=libwamr_qnx_ipc.so).
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Exposes name_attach/MsgReceive/MsgReply/name_open/MsgSend, periodic pulse
 * timers, CLOCK_MONOTONIC and SCHED_FIFO priority to Wasm guests under the
 * import module "qnx". See include/wasm_qnx_ipc.h for the guest API.
 *
 * Capability model:
 *  - WAMR_QNX_IPC_ALLOW: comma-separated names a module may attach or connect
 *    to; an entry ending in '*' is a prefix. Unset or empty = nothing allowed.
 *  - WAMR_QNX_MAX_PRIO: highest SCHED_FIFO priority a module may request
 *    (default 10).
 *  - Guests get table indices, never raw chid/coid/rcvid values, so a module
 *    cannot reply to, or send on, an IPC object it did not obtain itself.
 */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>

#include "wasm_export.h"
#include "wamr_qnx_ipc_proto.h"
#include "wasm_qnx_ipc.h" /* only for QNX_RECV_* values */

#define MAX_CHANNELS 8
#define MAX_CONNECTIONS 16
#define MAX_REPLIES 32
#define MAX_TIMERS 8

typedef struct {
    name_attach_t *attach;
    int self_coid; /* side-channel connection for timer pulses */
} channel_t;

typedef struct {
    int used;
    int channel; /* channel index the timer pulses */
    timer_t id;
} qtimer_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static channel_t g_channels[MAX_CHANNELS];
static int g_connections[MAX_CONNECTIONS]; /* coid + 1, 0 = free */
static int g_replies[MAX_REPLIES];         /* rcvid, 0 = free */
static qtimer_t g_timers[MAX_TIMERS];
static int g_max_prio = 10;

/* ---- helpers ---------------------------------------------------------- */

static int
name_allowed(const char *name)
{
    const char *allow = getenv("WAMR_QNX_IPC_ALLOW");
    size_t nlen = strlen(name);

    if (!allow || !*allow || nlen == 0 || nlen > 255)
        return 0;

    while (*allow) {
        const char *end = strchr(allow, ',');
        size_t len = end ? (size_t)(end - allow) : strlen(allow);

        if (len > 0 && allow[len - 1] == '*') {
            if (nlen >= len - 1 && strncmp(name, allow, len - 1) == 0)
                return 1;
        }
        else if (len == nlen && strncmp(name, allow, len) == 0) {
            return 1;
        }
        if (!end)
            break;
        allow = end + 1;
    }
    return 0;
}

/* Validate a guest pointer and translate it; NULL if out of bounds. */
static void *
guest_ptr(wasm_exec_env_t env, uint32_t off, uint32_t size)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);

    if (!wasm_runtime_validate_app_addr(inst, (uint64_t)off, (uint64_t)size))
        return NULL;
    return wasm_runtime_addr_app_to_native(inst, (uint64_t)off);
}

static channel_t *
get_channel(int32_t h)
{
    if (h < 0 || h >= MAX_CHANNELS || !g_channels[h].attach)
        return NULL;
    return &g_channels[h];
}

/* ---- natives ------------------------------------------------------------ */

static int32_t
qnx_attach_wrapper(wasm_exec_env_t env, const char *name)
{
    int32_t h, ret = -EMFILE;

    (void)env;
    if (!name_allowed(name))
        return -EPERM;

    pthread_mutex_lock(&g_lock);
    for (h = 0; h < MAX_CHANNELS; h++)
        if (!g_channels[h].attach)
            break;
    if (h < MAX_CHANNELS) {
        name_attach_t *na = name_attach(NULL, name, 0);
        if (!na) {
            ret = -errno;
        }
        else {
            g_channels[h].attach = na;
            g_channels[h].self_coid = -1;
            ret = h;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return ret;
}

static int32_t
qnx_detach_wrapper(wasm_exec_env_t env, int32_t h)
{
    channel_t *ch;
    int i;

    (void)env;
    pthread_mutex_lock(&g_lock);
    ch = get_channel(h);
    if (!ch) {
        pthread_mutex_unlock(&g_lock);
        return -EBADF;
    }
    for (i = 0; i < MAX_TIMERS; i++) {
        if (g_timers[i].used && g_timers[i].channel == h) {
            timer_delete(g_timers[i].id);
            g_timers[i].used = 0;
        }
    }
    if (ch->self_coid != -1)
        ConnectDetach(ch->self_coid);
    name_detach(ch->attach, 0);
    memset(ch, 0, sizeof(*ch));
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int32_t
qnx_receive_wrapper(wasm_exec_env_t env, int32_t h, uint32_t buf_off,
                    uint32_t len, uint32_t reply_off, uint32_t size_off,
                    uint32_t code_off, uint32_t value_off)
{
    union {
        struct _pulse pulse;
        uint16_t type;
        struct {
            wamr_qnx_msg_hdr_t hdr;
            uint8_t data[WAMR_QNX_MSG_MAX];
        } app;
    } msg;
    channel_t *ch;
    int chid;
    void *buf;
    int32_t *reply, *code, *value;
    uint32_t *size;

    if (len > WAMR_QNX_MSG_MAX)
        len = WAMR_QNX_MSG_MAX;
    buf = guest_ptr(env, buf_off, len ? len : 1);
    reply = guest_ptr(env, reply_off, 4);
    size = guest_ptr(env, size_off, 4);
    code = guest_ptr(env, code_off, 4);
    value = guest_ptr(env, value_off, 4);
    if (!buf || !reply || !size || !code || !value)
        return -EFAULT;

    pthread_mutex_lock(&g_lock);
    ch = get_channel(h);
    chid = ch ? ch->attach->chid : -1;
    pthread_mutex_unlock(&g_lock);
    if (chid == -1)
        return -EBADF;

    for (;;) {
        int rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1)
            return -errno;

        if (rcvid == 0) {
            /* A pulse. Kernel pulses for client bookkeeping stay here. */
            if (msg.pulse.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(msg.pulse.scoid);
                continue;
            }
            if (msg.pulse.code == _PULSE_CODE_UNBLOCK)
                continue;
            *code = msg.pulse.code;
            *value = msg.pulse.value.sival_int;
            *size = 0;
            *reply = -1;
            return QNX_RECV_PULSE;
        }

        if (msg.type == _IO_CONNECT) {
            /* name_open() handshake from a client */
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }
        if (msg.type != WAMR_QNX_MSG_TYPE) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        {
            uint32_t n = msg.app.hdr.length;
            int slot;

            if (n > WAMR_QNX_MSG_MAX)
                n = WAMR_QNX_MSG_MAX;
            if (n > len)
                n = len;

            pthread_mutex_lock(&g_lock);
            for (slot = 0; slot < MAX_REPLIES; slot++)
                if (!g_replies[slot])
                    break;
            if (slot == MAX_REPLIES) {
                pthread_mutex_unlock(&g_lock);
                MsgError(rcvid, EAGAIN);
                continue;
            }
            g_replies[slot] = rcvid;
            pthread_mutex_unlock(&g_lock);

            memcpy(buf, msg.app.data, n);
            *size = n;
            *reply = slot;
            *code = 0;
            *value = 0;
            return QNX_RECV_MESSAGE;
        }
    }
}

static int32_t
qnx_reply_wrapper(wasm_exec_env_t env, int32_t slot, int32_t status,
                  const void *buf, uint32_t len)
{
    int rcvid;

    (void)env;
    if (len > WAMR_QNX_MSG_MAX)
        return -EMSGSIZE;
    pthread_mutex_lock(&g_lock);
    if (slot < 0 || slot >= MAX_REPLIES || !g_replies[slot]) {
        pthread_mutex_unlock(&g_lock);
        return -EBADF;
    }
    rcvid = g_replies[slot];
    g_replies[slot] = 0;
    pthread_mutex_unlock(&g_lock);

    if (MsgReply(rcvid, status, buf, len) == -1)
        return -errno;
    return 0;
}

static int32_t
qnx_connect_wrapper(wasm_exec_env_t env, const char *name)
{
    int32_t h, ret = -EMFILE;

    (void)env;
    if (!name_allowed(name))
        return -EPERM;

    pthread_mutex_lock(&g_lock);
    for (h = 0; h < MAX_CONNECTIONS; h++)
        if (!g_connections[h])
            break;
    if (h < MAX_CONNECTIONS) {
        /* Reserve the slot, then drop the lock: name_open() can block. */
        g_connections[h] = -1;
        pthread_mutex_unlock(&g_lock);
        {
            int coid = name_open(name, 0);
            pthread_mutex_lock(&g_lock);
            if (coid == -1) {
                ret = -errno;
                g_connections[h] = 0;
            }
            else {
                g_connections[h] = coid + 1;
                ret = h;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return ret;
}

static int32_t
qnx_disconnect_wrapper(wasm_exec_env_t env, int32_t h)
{
    int coid;

    (void)env;
    pthread_mutex_lock(&g_lock);
    if (h < 0 || h >= MAX_CONNECTIONS || g_connections[h] <= 0) {
        pthread_mutex_unlock(&g_lock);
        return -EBADF;
    }
    coid = g_connections[h] - 1;
    g_connections[h] = 0;
    pthread_mutex_unlock(&g_lock);
    name_close(coid);
    return 0;
}

static int32_t
qnx_send_wrapper(wasm_exec_env_t env, int32_t h, const void *sbuf,
                 uint32_t slen, void *rbuf, uint32_t rlen)
{
    wamr_qnx_msg_hdr_t hdr;
    iov_t siov[2], riov[1];
    int coid;
    long status;

    (void)env;
    if (slen > WAMR_QNX_MSG_MAX)
        return -EMSGSIZE;
    pthread_mutex_lock(&g_lock);
    coid = (h >= 0 && h < MAX_CONNECTIONS && g_connections[h] > 0)
               ? g_connections[h] - 1
               : -1;
    pthread_mutex_unlock(&g_lock);
    if (coid == -1)
        return -EBADF;

    hdr.type = WAMR_QNX_MSG_TYPE;
    hdr.subtype = 0;
    hdr.length = slen;
    SETIOV(&siov[0], &hdr, sizeof(hdr));
    SETIOV(&siov[1], sbuf, slen);
    SETIOV(&riov[0], rbuf, rlen);

    status = MsgSendv(coid, siov, 2, riov, 1);
    if (status == -1)
        return -errno;
    return (int32_t)status;
}

static int32_t
qnx_timer_pulse_wrapper(wasm_exec_env_t env, int32_t h, int64_t period_ns,
                        int32_t code)
{
    struct sigevent ev;
    struct itimerspec its;
    channel_t *ch;
    int t, ret;

    (void)env;
    if (period_ns < 10000) /* 10 us floor */
        return -EINVAL;
    if (code < _PULSE_CODE_MINAVAIL || code > _PULSE_CODE_MAXAVAIL)
        return -EINVAL;

    pthread_mutex_lock(&g_lock);
    ch = get_channel(h);
    if (!ch) {
        ret = -EBADF;
        goto out;
    }
    for (t = 0; t < MAX_TIMERS; t++)
        if (!g_timers[t].used)
            break;
    if (t == MAX_TIMERS) {
        ret = -EMFILE;
        goto out;
    }
    if (ch->self_coid == -1) {
        ch->self_coid =
            ConnectAttach(0, 0, ch->attach->chid, _NTO_SIDE_CHANNEL, 0);
        if (ch->self_coid == -1) {
            ret = -errno;
            goto out;
        }
    }
    /* Pulse priority follows the receiving thread (-1 = sender's prio). */
    SIGEV_PULSE_INIT(&ev, ch->self_coid, SIGEV_PULSE_PRIO_INHERIT, code, t);
    if (timer_create(CLOCK_MONOTONIC, &ev, &g_timers[t].id) == -1) {
        ret = -errno;
        goto out;
    }
    its.it_value.tv_sec = period_ns / 1000000000LL;
    its.it_value.tv_nsec = period_ns % 1000000000LL;
    its.it_interval = its.it_value;
    if (timer_settime(g_timers[t].id, 0, &its, NULL) == -1) {
        ret = -errno;
        timer_delete(g_timers[t].id);
        goto out;
    }
    g_timers[t].used = 1;
    g_timers[t].channel = h;
    ret = t;
out:
    pthread_mutex_unlock(&g_lock);
    return ret;
}

static int32_t
qnx_timer_stop_wrapper(wasm_exec_env_t env, int32_t t)
{
    int ret = 0;

    (void)env;
    pthread_mutex_lock(&g_lock);
    if (t < 0 || t >= MAX_TIMERS || !g_timers[t].used)
        ret = -EBADF;
    else {
        timer_delete(g_timers[t].id);
        g_timers[t].used = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return ret;
}

static int64_t
qnx_clock_ns_wrapper(wasm_exec_env_t env)
{
    struct timespec ts;

    (void)env;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int32_t
qnx_set_priority_wrapper(wasm_exec_env_t env, int32_t prio)
{
    struct sched_param sp;
    int err;

    (void)env;
    if (prio < 1 || prio > g_max_prio)
        return -EPERM;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    return err ? -err : 0;
}

/* ---- registration ---------------------------------------------------- */

/* clang-format off */
#define REG(name, sig) { #name, qnx_##name##_wrapper, sig, NULL }

static NativeSymbol g_symbols[] = {
    REG(attach,       "($)i"),
    REG(detach,       "(i)i"),
    REG(receive,      "(iiiiiii)i"),
    REG(reply,        "(ii*~)i"),
    REG(connect,      "($)i"),
    REG(disconnect,   "(i)i"),
    REG(send,         "(i*~*~)i"),
    REG(timer_pulse,  "(iIi)i"),
    REG(timer_stop,   "(i)i"),
    REG(clock_ns,     "()I"),
    REG(set_priority, "(i)i"),
};
/* clang-format on */

int
init_native_lib(void)
{
    const char *p = getenv("WAMR_QNX_MAX_PRIO");

    if (p && *p) {
        int v = atoi(p);
        if (v >= 1 && v <= 255)
            g_max_prio = v;
    }
    return 0;
}

void
deinit_native_lib(void)
{
    int i;

    for (i = 0; i < MAX_TIMERS; i++)
        if (g_timers[i].used)
            timer_delete(g_timers[i].id);
    for (i = 0; i < MAX_CHANNELS; i++)
        if (g_channels[i].attach) {
            if (g_channels[i].self_coid != -1)
                ConnectDetach(g_channels[i].self_coid);
            name_detach(g_channels[i].attach, 0);
        }
    for (i = 0; i < MAX_CONNECTIONS; i++)
        if (g_connections[i] > 0)
            name_close(g_connections[i] - 1);
}

uint32_t
get_native_lib(char **p_module_name, NativeSymbol **p_native_symbols)
{
    *p_module_name = "qnx";
    *p_native_symbols = g_symbols;
    return sizeof(g_symbols) / sizeof(g_symbols[0]);
}
