/* lrg-environment-sky.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-environment-sky.h"
#include "lrg-panel-environment.h"

/* Ground grid: half-extent in world units, line spacing and height (the same
   floor height as the workshop room, so switching keeps the panels grounded). */
#define LRG_SKY_GRID_EXTENT  40
#define LRG_SKY_GRID_STEP    2
#define LRG_SKY_FLOOR_Y      (-2.4f)

struct _LrgEnvironmentSky
{
	GObject parent_instance;

	LrgSkyDome *dome;
};

static void lrg_environment_sky_iface_init (LrgPanelEnvironmentInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEnvironmentSky, lrg_environment_sky, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PANEL_ENVIRONMENT,
                                                      lrg_environment_sky_iface_init))

static const gchar *
sky_get_id (LrgPanelEnvironment *self)
{
	(void) self;
	return "sky";
}

/* The clear colour only shows where the dome does not reach (it covers the
   whole view in practice); use the horizon so a gap would blend in. */
static GrlColor *
sky_get_clear_color (LrgPanelEnvironment *env)
{
	LrgEnvironmentSky *self = LRG_ENVIRONMENT_SKY (env);
	GrlColor horizon;

	lrg_sky_dome_get_horizon_color (self->dome, &horizon);
	return grl_color_new (horizon.r, horizon.g, horizon.b, 255);
}

/* The dome is camera-centred: it needs the eye position every frame. */
static void
sky_draw_sky (LrgPanelEnvironment *env,
              const LrgPose       *camera)
{
	LrgEnvironmentSky *self = LRG_ENVIRONMENT_SKY (env);
	g_autoptr (GrlVector3) eye = NULL;
	gfloat ex, ey, ez;

	lrg_pose_get_position (camera, &ex, &ey, &ez);
	eye = grl_vector3_new (ex, ey, ez);
	lrg_sky_dome_draw (self->dome, eye);
}

/* A sparse ground grid, a darkened horizon colour so it reads at any time of
   day without competing with the panels. */
static void
sky_draw_ambient (LrgPanelEnvironment *env)
{
	LrgEnvironmentSky *self = LRG_ENVIRONMENT_SKY (env);
	g_autoptr (GrlColor) line = NULL;
	GrlColor horizon;
	gfloat ext = (gfloat) LRG_SKY_GRID_EXTENT;
	gint i;

	lrg_sky_dome_get_horizon_color (self->dome, &horizon);
	line = grl_color_new ((guint8) (horizon.r / 2), (guint8) (horizon.g / 2),
	                      (guint8) (horizon.b / 2), 255);

	for (i = -LRG_SKY_GRID_EXTENT; i <= LRG_SKY_GRID_EXTENT; i += LRG_SKY_GRID_STEP)
	{
		g_autoptr (GrlVector3) a = grl_vector3_new ((gfloat) i, LRG_SKY_FLOOR_Y, -ext);
		g_autoptr (GrlVector3) b = grl_vector3_new ((gfloat) i, LRG_SKY_FLOOR_Y, ext);
		g_autoptr (GrlVector3) c = grl_vector3_new (-ext, LRG_SKY_FLOOR_Y, (gfloat) i);
		g_autoptr (GrlVector3) d = grl_vector3_new (ext, LRG_SKY_FLOOR_Y, (gfloat) i);

		grl_draw_line_3d (a, b, line);
		grl_draw_line_3d (c, d, line);
	}
}

static void
lrg_environment_sky_iface_init (LrgPanelEnvironmentInterface *iface)
{
	iface->get_id = sky_get_id;
	iface->get_clear_color = sky_get_clear_color;
	iface->draw_ambient = sky_draw_ambient;
	iface->draw_sky = sky_draw_sky;
}

static void
lrg_environment_sky_dispose (GObject *object)
{
	LrgEnvironmentSky *self = LRG_ENVIRONMENT_SKY (object);

	g_clear_object (&self->dome);
	G_OBJECT_CLASS (lrg_environment_sky_parent_class)->dispose (object);
}

static void
lrg_environment_sky_class_init (LrgEnvironmentSkyClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_environment_sky_dispose;
}

static void
lrg_environment_sky_init (LrgEnvironmentSky *self)
{
	/* The dome's geometry and colours are computed headlessly; only drawing
	   needs GL, so this is safe before a window exists. */
	self->dome = lrg_sky_dome_new ();
	lrg_sky_dome_set_time_of_day (self->dome, 10.5f);
}

LrgEnvironmentSky *
lrg_environment_sky_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT_SKY, NULL);
}

void
lrg_environment_sky_set_time_of_day (LrgEnvironmentSky *self,
                                     gfloat             hours)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT_SKY (self));
	lrg_sky_dome_set_time_of_day (self->dome, hours);
}

gfloat
lrg_environment_sky_get_time_of_day (LrgEnvironmentSky *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT_SKY (self), 0.0f);
	return lrg_sky_dome_get_time_of_day (self->dome);
}

LrgSkyDome *
lrg_environment_sky_get_dome (LrgEnvironmentSky *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT_SKY (self), NULL);
	return self->dome;
}
