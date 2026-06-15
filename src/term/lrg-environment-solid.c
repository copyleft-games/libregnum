/* lrg-environment-solid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <graylib.h>
#include "lrg-environment-solid.h"
#include "lrg-panel-environment.h"

struct _LrgEnvironmentSolid
{
	GObject parent_instance;
};

static void lrg_environment_solid_iface_init (LrgPanelEnvironmentInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEnvironmentSolid, lrg_environment_solid, G_TYPE_OBJECT,
							   G_IMPLEMENT_INTERFACE (LRG_TYPE_PANEL_ENVIRONMENT,
													  lrg_environment_solid_iface_init))

static const gchar *
solid_get_id (LrgPanelEnvironment *self)
{
	(void) self;
	return "void";
}

static GrlColor *
solid_get_clear_color (LrgPanelEnvironment *self)
{
	(void) self;
	return grl_color_new (16, 16, 20, 255);
}

static void
lrg_environment_solid_iface_init (LrgPanelEnvironmentInterface *iface)
{
	iface->get_id = solid_get_id;
	iface->get_clear_color = solid_get_clear_color;
	iface->draw_ambient = NULL;
}

static void
lrg_environment_solid_class_init (LrgEnvironmentSolidClass *klass)
{
	(void) klass;
}

static void
lrg_environment_solid_init (LrgEnvironmentSolid *self)
{
	(void) self;
}

LrgEnvironmentSolid *
lrg_environment_solid_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT_SOLID, NULL);
}
