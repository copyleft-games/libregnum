/* lrg-billboard-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBillboardComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-billboard-component.h"

struct _LrgBillboardComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgBillboardComponent, lrg_billboard_component, G_TYPE_OBJECT)

static void
lrg_billboard_component_class_init (LrgBillboardComponentClass *klass)
{
}

static void
lrg_billboard_component_init (LrgBillboardComponent *self)
{
}

LrgBillboardComponent *
lrg_billboard_component_new (void)
{
	return g_object_new (LRG_TYPE_BILLBOARD_COMPONENT, NULL);
}
