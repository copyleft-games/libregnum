/* lrg-reflection-probe.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbe: (stub, being written)
 */

#include "config.h"

#include "lrg-reflection-probe.h"

struct _LrgReflectionProbe
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgReflectionProbe, lrg_reflection_probe, G_TYPE_OBJECT)

static void
lrg_reflection_probe_class_init (LrgReflectionProbeClass *klass)
{
}

static void
lrg_reflection_probe_init (LrgReflectionProbe *self)
{
}

LrgReflectionProbe *
lrg_reflection_probe_new (void)
{
	return g_object_new (LRG_TYPE_REFLECTION_PROBE, NULL);
}
