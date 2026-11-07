/* lrg-reflection-probe-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbeComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-reflection-probe-component.h"

struct _LrgReflectionProbeComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgReflectionProbeComponent, lrg_reflection_probe_component, LRG_TYPE_COMPONENT)

static void
lrg_reflection_probe_component_class_init (LrgReflectionProbeComponentClass *klass)
{
}

static void
lrg_reflection_probe_component_init (LrgReflectionProbeComponent *self)
{
}

LrgReflectionProbeComponent *
lrg_reflection_probe_component_new (void)
{
	return g_object_new (LRG_TYPE_REFLECTION_PROBE_COMPONENT, NULL);
}
