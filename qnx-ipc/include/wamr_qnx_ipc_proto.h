/*
 * wamr_qnx_ipc_proto.h - wire format shared by libwamr_qnx_ipc.so and native
 * QNX programs that talk to Wasm modules.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * name_open()/name_attach() servers also receive _IO_CONNECT messages and
 * system pulses, so every application message starts with a small header
 * whose type is above _IO_MAX. The host library adds and strips it; Wasm
 * modules only see the payload.
 */
#ifndef WAMR_QNX_IPC_PROTO_H
#define WAMR_QNX_IPC_PROTO_H

#include <stdint.h>

#define WAMR_QNX_MSG_TYPE 0x5741u /* 'WA', above _IO_MAX (0x1ff) */
#define WAMR_QNX_MSG_MAX 4096u    /* largest payload one message carries */

typedef struct {
    uint16_t type;    /* WAMR_QNX_MSG_TYPE */
    uint16_t subtype; /* reserved, 0 */
    uint32_t length;  /* payload bytes that follow the header */
} wamr_qnx_msg_hdr_t;

#endif /* WAMR_QNX_IPC_PROTO_H */
