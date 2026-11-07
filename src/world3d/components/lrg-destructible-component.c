/* lrg-destructible-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDestructibleComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-destructible-component.h"

struct _LrgDestructibleComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDestructibleComponent, lrg_destructible_component, LRG_TYPE_COMPONENT)

static void
lrg_destructible_component_class_init (LrgDestructibleComponentClass *klass)
{
}

static void
lrg_destructible_component_init (LrgDestructibleComponent *self)
{
}

LrgDestructibleComponent *
lrg_destructible_component_new (void)
{
	return g_object_new (LRG_TYPE_DESTRUCTIBLE_COMPONENT, NULL);
}
