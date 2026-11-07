/* lrg-trigger-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTriggerComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-trigger-component.h"

struct _LrgTriggerComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTriggerComponent, lrg_trigger_component, G_TYPE_OBJECT)

static void
lrg_trigger_component_class_init (LrgTriggerComponentClass *klass)
{
}

static void
lrg_trigger_component_init (LrgTriggerComponent *self)
{
}

LrgTriggerComponent *
lrg_trigger_component_new (void)
{
	return g_object_new (LRG_TYPE_TRIGGER_COMPONENT, NULL);
}
