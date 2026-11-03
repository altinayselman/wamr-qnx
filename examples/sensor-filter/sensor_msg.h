/*
 * sensor_msg.h - payload shared by filter.wasm and the native sensor_sim.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SENSOR_MSG_H
#define SENSOR_MSG_H

#include <stdint.h>

#define SENSOR_SERVICE_NAME "wasm_filter"
#define SENSOR_MAGIC 0x53454e53u /* 'SENS' */
#define SENSOR_CMD_SAMPLE 1
#define SENSOR_CMD_STOP 2
#define SENSOR_SPIKE_LIMIT 5.0

typedef struct {
    uint32_t magic;
    uint32_t command;
    uint32_t seq;
    float value;
} sensor_sample_t;

typedef struct {
    uint32_t seq;
    uint32_t spike;
    uint32_t handled;
    float filtered;
} sensor_result_t;

#endif
