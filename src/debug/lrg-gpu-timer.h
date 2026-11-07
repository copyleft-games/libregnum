/* lrg-gpu-timer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuTimer: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_GPU_TIMER (lrg_gpu_timer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGpuTimer, lrg_gpu_timer, LRG, GPU_TIMER, GObject)

/**
 * lrg_gpu_timer_new:
 *
 * Returns: (transfer full): a new #LrgGpuTimer
 */
LRG_AVAILABLE_IN_ALL
LrgGpuTimer * lrg_gpu_timer_new (void);

G_END_DECLS
