/* lrg-gpu-timer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuTimer: (stub, being written)
 */

#include "config.h"

#include "lrg-gpu-timer.h"

struct _LrgGpuTimer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgGpuTimer, lrg_gpu_timer, G_TYPE_OBJECT)

static void
lrg_gpu_timer_class_init (LrgGpuTimerClass *klass)
{
}

static void
lrg_gpu_timer_init (LrgGpuTimer *self)
{
}

LrgGpuTimer *
lrg_gpu_timer_new (void)
{
	return g_object_new (LRG_TYPE_GPU_TIMER, NULL);
}
