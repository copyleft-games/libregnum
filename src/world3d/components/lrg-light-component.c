/* lrg-light-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-light-component.h"

struct _LrgLightComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLightComponent, lrg_light_component, G_TYPE_OBJECT)

static void
lrg_light_component_class_init (LrgLightComponentClass *klass)
{
}

static void
lrg_light_component_init (LrgLightComponent *self)
{
}

LrgLightComponent *
lrg_light_component_new (void)
{
	return g_object_new (LRG_TYPE_LIGHT_COMPONENT, NULL);
}
