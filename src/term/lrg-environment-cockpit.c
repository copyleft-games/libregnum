/* lrg-environment-cockpit.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <graylib.h>
#include "lrg-environment-cockpit.h"
#include "lrg-panel-environment.h"

/* Half-extent of the room and the planes the walls/floor sit on. */
#define LRG_COCKPIT_EXT 7
#define LRG_COCKPIT_FLOOR_Y (-2.6f)
#define LRG_COCKPIT_CEIL_Y (3.2f)
#define LRG_COCKPIT_BACK_Z (-7.0f)

struct _LrgEnvironmentCockpit
{
	GObject parent_instance;

	/* Lazily-created textured back-wall quad.  back_tex is a live texture (e.g.
	   a libregnum view's FBO colour attachment) the embedder refreshes each
	   present; held via a ref. */
	GrlMesh    *wall_mesh;
	GrlModel   *wall_model;
	GrlTexture *back_tex;
};

static void lrg_environment_cockpit_iface_init (LrgPanelEnvironmentInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEnvironmentCockpit, lrg_environment_cockpit, G_TYPE_OBJECT,
							   G_IMPLEMENT_INTERFACE (LRG_TYPE_PANEL_ENVIRONMENT,
													  lrg_environment_cockpit_iface_init))

static const gchar *
cockpit_get_id (LrgPanelEnvironment *self)
{
	(void) self;
	return "cockpit";
}

static GrlColor *
cockpit_get_clear_color (LrgPanelEnvironment *self)
{
	(void) self;
	return grl_color_new (8, 9, 14, 255);
}

/* Draw the live back-wall texture (a dashboard / the gnuseye globe) as a large
   quad in front of the back grid, if one has been set.  */
static void
cockpit_draw_back_wall (LrgEnvironmentCockpit *self)
{
	g_autoptr (GrlMatrix) s = NULL;
	g_autoptr (GrlMatrix) rx = NULL;
	g_autoptr (GrlMatrix) sr = NULL;
	g_autoptr (GrlMatrix) tr = NULL;
	g_autoptr (GrlMatrix) m = NULL;
	g_autoptr (GrlVector3) origin = NULL;
	g_autoptr (GrlColor) white = NULL;

	if (self->back_tex == NULL)
		return;
	if (self->wall_model == NULL)
	{
		self->wall_mesh = grl_mesh_new_plane (1.0f, 1.0f, 1, 1);
		if (self->wall_mesh != NULL)
			self->wall_model = grl_model_new_from_mesh (self->wall_mesh);
	}
	if (self->wall_model == NULL)
		return;

	grl_model_set_texture (self->wall_model, 0, GRL_MATERIAL_MAP_ALBEDO,
	                       self->back_tex);
	/* Same compose order as a panel: scale, stand up (+90 X), translate. */
	s = grl_matrix_new_scale (12.0f, 1.0f, 5.0f);
	rx = grl_matrix_new_rotate_xyz ((gfloat) G_PI_2, 0.0f, 0.0f);
	sr = grl_matrix_multiply (s, rx);
	tr = grl_matrix_new_translate (0.0f, 0.3f, LRG_COCKPIT_BACK_Z + 0.15f);
	m = grl_matrix_multiply (sr, tr);
	grl_model_set_transform (self->wall_model, m);
	origin = grl_vector3_new (0.0f, 0.0f, 0.0f);
	white = grl_color_new (255, 255, 255, 255);
	grl_model_draw (self->wall_model, origin, 1.0f, white);
}

static void
cockpit_draw_ambient (LrgPanelEnvironment *self)
{
	g_autoptr (GrlColor) floor_c = grl_color_new (40, 64, 80, 255);
	g_autoptr (GrlColor) wall_c = grl_color_new (28, 36, 54, 255);
	gfloat ext = (gfloat) LRG_COCKPIT_EXT;
	gint i;

	(void) self;

	for (i = -LRG_COCKPIT_EXT; i <= LRG_COCKPIT_EXT; i++)
	{
		gfloat fi = (gfloat) i;
		/* Floor grid (XZ plane at the bottom). */
		g_autoptr (GrlVector3) fa = grl_vector3_new (fi, LRG_COCKPIT_FLOOR_Y, -ext);
		g_autoptr (GrlVector3) fb = grl_vector3_new (fi, LRG_COCKPIT_FLOOR_Y, ext);
		g_autoptr (GrlVector3) fc = grl_vector3_new (-ext, LRG_COCKPIT_FLOOR_Y, fi);
		g_autoptr (GrlVector3) fd = grl_vector3_new (ext, LRG_COCKPIT_FLOOR_Y, fi);
		/* Back wall grid (XY plane at the back). */
		g_autoptr (GrlVector3) ba = grl_vector3_new (fi, LRG_COCKPIT_FLOOR_Y, LRG_COCKPIT_BACK_Z);
		g_autoptr (GrlVector3) bb = grl_vector3_new (fi, LRG_COCKPIT_CEIL_Y, LRG_COCKPIT_BACK_Z);

		grl_draw_line_3d (fa, fb, floor_c);
		grl_draw_line_3d (fc, fd, floor_c);
		grl_draw_line_3d (ba, bb, wall_c);
	}

	/* Horizontal rules up the back wall + the side wall verticals. */
	for (i = 0; i <= 6; i++)
	{
		gfloat y = LRG_COCKPIT_FLOOR_Y
				   + (LRG_COCKPIT_CEIL_Y - LRG_COCKPIT_FLOOR_Y) * (gfloat) i / 6.0f;
		g_autoptr (GrlVector3) ha = grl_vector3_new (-ext, y, LRG_COCKPIT_BACK_Z);
		g_autoptr (GrlVector3) hb = grl_vector3_new (ext, y, LRG_COCKPIT_BACK_Z);
		/* Left + right wall rails sloping from the back to the camera. */
		g_autoptr (GrlVector3) la = grl_vector3_new (-ext, y, LRG_COCKPIT_BACK_Z);
		g_autoptr (GrlVector3) lb = grl_vector3_new (-ext, y, ext);
		g_autoptr (GrlVector3) ra = grl_vector3_new (ext, y, LRG_COCKPIT_BACK_Z);
		g_autoptr (GrlVector3) rb = grl_vector3_new (ext, y, ext);

		grl_draw_line_3d (ha, hb, wall_c);
		grl_draw_line_3d (la, lb, wall_c);
		grl_draw_line_3d (ra, rb, wall_c);
	}

	cockpit_draw_back_wall (LRG_ENVIRONMENT_COCKPIT (self));
}

static void
lrg_environment_cockpit_iface_init (LrgPanelEnvironmentInterface *iface)
{
	iface->get_id = cockpit_get_id;
	iface->get_clear_color = cockpit_get_clear_color;
	iface->draw_ambient = cockpit_draw_ambient;
}

static void
lrg_environment_cockpit_dispose (GObject *object)
{
	LrgEnvironmentCockpit *self = LRG_ENVIRONMENT_COCKPIT (object);

	g_clear_object (&self->back_tex);
	g_clear_object (&self->wall_model);
	g_clear_object (&self->wall_mesh);

	G_OBJECT_CLASS (lrg_environment_cockpit_parent_class)->dispose (object);
}

static void
lrg_environment_cockpit_class_init (LrgEnvironmentCockpitClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_environment_cockpit_dispose;
}

static void
lrg_environment_cockpit_init (LrgEnvironmentCockpit *self)
{
	(void) self;
}

LrgEnvironmentCockpit *
lrg_environment_cockpit_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT_COCKPIT, NULL);
}

void
lrg_environment_cockpit_set_back_texture (LrgEnvironmentCockpit *self,
                                          GrlTexture            *texture)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT_COCKPIT (self));
	g_set_object (&self->back_tex, texture);
}
