/*
 * wasm_qnx_ipc.h - guest-side (WebAssembly) API for QNX native message passing.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compile Wasm modules that include this header with wasi-sdk, for example:
 *   clang --target=wasm32-wasip1 -O2 -I qnx-ipc/include app.c -o app.wasm
 *
 * The functions are imported from the "qnx" module. They are implemented on
 * the host by libwamr_qnx_ipc.so, which iwasm loads with
 *   iwasm --native-lib=/usr/lib/libwamr_qnx_ipc.so app.wasm
 *
 * Handles are small integers that index a host-side table. A module can only
 * use channels and connections it opened through this API, and only for the
 * names the host allows (WAMR_QNX_IPC_ALLOW). Raw QNX chid/coid/rcvid values
 * never reach the sandbox.
 *
 * Every function returns a negative errno value on failure.
 */
#ifndef WASM_QNX_IPC_H
#define WASM_QNX_IPC_H

#include <stdint.h>

#ifndef __wasm__
#error "wasm_qnx_ipc.h is the guest API: compile with --target=wasm32-wasip1"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QNX_IMPORT(name) \
    __attribute__((import_module("qnx"), import_name(name)))

#include "wamr_qnx_ipc_proto.h" /* QNX_RECV_* */

/* Register a server name (name_attach). Returns a channel handle. */
QNX_IMPORT("attach")
int32_t qnx_attach(const char *name);

/* Release a channel handle (name_detach). */
QNX_IMPORT("detach")
int32_t qnx_detach(int32_t channel);

/*
 * Block until a message or pulse arrives on the channel (MsgReceive).
 * For a message: copies up to len bytes into buf, stores a reply handle in
 * *reply and returns QNX_RECV_MESSAGE; *size gets the byte count.
 * For a pulse: stores the code and value and returns QNX_RECV_PULSE.
 */
QNX_IMPORT("receive")
int32_t qnx_receive(int32_t channel, void *buf, uint32_t len, int32_t *reply,
                    uint32_t *size, int32_t *pulse_code, int32_t *pulse_value);

/* Answer a received message (MsgReply). */
QNX_IMPORT("reply")
int32_t qnx_reply(int32_t reply, int32_t status, const void *buf,
                  uint32_t len);

/* Connect to a server by name (name_open). Returns a connection handle. */
QNX_IMPORT("connect")
int32_t qnx_connect(const char *name);

/* Close a connection handle (name_close). */
QNX_IMPORT("disconnect")
int32_t qnx_disconnect(int32_t connection);

/*
 * Send a message and block for the reply (MsgSend). Returns the server's
 * status value; up to rlen reply bytes are copied into rbuf.
 */
QNX_IMPORT("send")
int32_t qnx_send(int32_t connection, const void *sbuf, uint32_t slen,
                 void *rbuf, uint32_t rlen);

/*
 * Start a periodic timer that delivers a pulse with the given code to the
 * channel every period_ns nanoseconds (timer_create + SIGEV_PULSE).
 * Returns a timer handle.
 */
QNX_IMPORT("timer_pulse")
int32_t qnx_timer_pulse(int32_t channel, uint64_t period_ns, int32_t code);

/* Stop a periodic timer. */
QNX_IMPORT("timer_stop")
int32_t qnx_timer_stop(int32_t timer);

/* CLOCK_MONOTONIC in nanoseconds. */
QNX_IMPORT("clock_ns")
uint64_t qnx_clock_ns(void);

/*
 * Set the calling thread's priority with SCHED_FIFO. The host refuses
 * priorities above WAMR_QNX_MAX_PRIO (default 10).
 */
QNX_IMPORT("set_priority")
int32_t qnx_set_priority(int32_t priority);

#undef QNX_IMPORT

#ifdef __cplusplus
}
#endif

#endif /* WASM_QNX_IPC_H */
