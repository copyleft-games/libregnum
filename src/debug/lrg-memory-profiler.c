/* lrg-memory-profiler.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMemoryProfiler: (stub, being written)
 */

#include "config.h"

#include "lrg-memory-profiler.h"

struct _LrgMemoryProfiler
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMemoryProfiler, lrg_memory_profiler, G_TYPE_OBJECT)

static void
lrg_memory_profiler_class_init (LrgMemoryProfilerClass *klass)
{
}

static void
lrg_memory_profiler_init (LrgMemoryProfiler *self)
{
}

LrgMemoryProfiler *
lrg_memory_profiler_new (void)
{
	return g_object_new (LRG_TYPE_MEMORY_PROFILER, NULL);
}
