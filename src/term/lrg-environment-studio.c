/* lrg-environment-studio.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-environment-studio.h"
#include "lrg-panel-environment.h"

#define LRG_STUDIO_FLOOR_Y (-2.4f)
#define LRG_STUDIO_RINGS   8
#define LRG_STUDIO_RING_STEP 2.5f

struct _LrgEnvironmentStudio
{
	GObject parent_instance;
};

static void lrg_environment_studio_iface_init (LrgPanelEnvironmentInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEnvironmentStudio, lrg_environment_studio, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PANEL_ENVIRONMENT,
                                                      lrg_environment_studio_iface_init))

static const gchar *
studio_get_id (LrgPanelEnvironment *self)
{
	(void) self;
	return "studio";
}

static GrlColor *
studio_get_clear_color (LrgPanelEnvironment *self)
{
	(void) self;
	return grl_color_new (38, 40, 46, 255);
}

/* Concentric floor rings centred under the origin, fading outward: they give
   depth cues (scale, horizon) without the busy look of a grid. */
static void
studio_draw_ambient (LrgPanelEnvironment *self)
{
	g_autoptr (GrlVector3) centre = grl_vector3_new (0.0f, LRG_STUDIO_FLOOR_Y, 0.0f);
	g_autoptr (GrlVector3) axis = grl_vector3_new (1.0f, 0.0f, 0.0f);
	gint i;

	(void) self;

	for (i = 1; i <= LRG_STUDIO_RINGS; i++)
	{
		guint8 v = (guint8) (78 - i * 4);
		g_autoptr (GrlColor) c = grl_color_new (v, v, (guint8) (v + 6), 255);

		/* raylib circles lie in XY; a 90 degree turn about X lays them flat. */
		grl_draw_circle_3d (centre, (gfloat) i * LRG_STUDIO_RING_STEP, axis,
		                    90.0f, c);
	}
}

static void
lrg_environment_studio_iface_init (LrgPanelEnvironmentInterface *iface)
{
	iface->get_id = studio_get_id;
	iface->get_clear_color = studio_get_clear_color;
	iface->draw_ambient = studio_draw_ambient;
	iface->draw_sky = NULL;
}

static void
lrg_environment_studio_class_init (LrgEnvironmentStudioClass *klass)
{
	(void) klass;
}

static void
lrg_environment_studio_init (LrgEnvironmentStudio *self)
{
	(void) self;
}

LrgEnvironmentStudio *
lrg_environment_studio_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT_STUDIO, NULL);
}
