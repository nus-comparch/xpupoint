// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause
#ifndef GTPIN_SHIM_H
#define  GTPIN_SHIM_H
typedef enum {XPU_EVENT_INVALID, KOI_START, KOI_STOP, REGION_START, REGION_STOP, XPU_EVENT_NONE} XPU_EVENT;
typedef void (*cpu_on_xpu_event_ptr_t)(const char * kname, uint32_t iteration, XPU_EVENT e);
typedef void (*cpu_on_gpu_init_ptr_t)(void);
typedef void (*cpu_on_gpu_fini_ptr_t)(void);
#endif
