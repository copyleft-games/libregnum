/* lrg-environment-workshop.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <graylib.h>
#include "lrg-environment-workshop.h"
#include "lrg-panel-environment.h"

/* Grid floor extent (half-size) and the plane it sits on, below the panels. */
#define LRG_WORKSHOP_GRID 6
#define LRG_WORKSHOP_FLOOR_Y (-2.4f)

struct _LrgEnvironmentWorkshop
{
	GObject parent_instance;
};

static void lrg_environment_workshop_iface_init (LrgPanelEnvironmentInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEnvironmentWorkshop, lrg_environment_workshop, G_TYPE_OBJECT,
							   G_IMPLEMENT_INTERFACE (LRG_TYPE_PANEL_ENVIRONMENT,
													  lrg_environment_workshop_iface_init))

static const gchar *
workshop_get_id (LrgPanelEnvironment *self)
{
	(void) self;
	return "workshop";
}

static GrlColor *
workshop_get_clear_color (LrgPanelEnvironment *self)
{
	(void) self;
	return grl_color_new (10, 11, 16, 255);
}

static void
workshop_draw_ambient (LrgPanelEnvironment *self)
{
	g_autoptr (GrlColor) c = grl_color_new (48, 52, 64, 255);
	gfloat y = LRG_WORKSHOP_FLOOR_Y;
	gfloat ext = (gfloat) LRG_WORKSHOP_GRID;
	gint i;

	(void) self;

	for (i = -LRG_WORKSHOP_GRID; i <= LRG_WORKSHOP_GRID; i++)
	{
		g_autoptr (GrlVector3) a = grl_vector3_new ((gfloat) i, y, -ext);
		g_autoptr (GrlVector3) b = grl_vector3_new ((gfloat) i, y, ext);
		g_autoptr (GrlVector3) d = grl_vector3_new (-ext, y, (gfloat) i);
		g_autoptr (GrlVector3) e = grl_vector3_new (ext, y, (gfloat) i);

		grl_draw_line_3d (a, b, c);
		grl_draw_line_3d (d, e, c);
	}
}

static void
lrg_environment_workshop_iface_init (LrgPanelEnvironmentInterface *iface)
{
	iface->get_id = workshop_get_id;
	iface->get_clear_color = workshop_get_clear_color;
	iface->draw_ambient = workshop_draw_ambient;
}

static void
lrg_environment_workshop_class_init (LrgEnvironmentWorkshopClass *klass)
{
	(void) klass;
}

static void
lrg_environment_workshop_init (LrgEnvironmentWorkshop *self)
{
	(void) self;
}

LrgEnvironmentWorkshop *
lrg_environment_workshop_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT_WORKSHOP, NULL);
}
