/* lrg-3d-surface.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include <graylib.h>
#include "lrg-3d-surface.h"
#include "lrg-2d-surface.h"
#include "lrg-scene-panel.h"
#include "lrg-mode-registry.h"

/* Focus weight an unfocused panel eases to (still readable, just dimmed). */
#define LRG_3D_UNFOCUSED 0.35f

enum
{
	SIGNAL_ANIMATION_STARTED,
	SIGNAL_ANIMATION_FINISHED,
	SIGNAL_ARRANGEMENT_CHANGED,
	SIGNAL_ENVIRONMENT_CHANGED,
	SIGNAL_PANEL_FOCUSED,
	SIGNAL_PANEL_MOVED,
	SIGNAL_PANEL_PINNED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _Lrg3DSurface
{
	LrgFrameSurface parent_instance;

	/* Owns the OS window and rasterises the flat frame to the default
	   framebuffer (we delegate all primitive draws to it). */
	Lrg2DSurface *content;

	LrgSpatialCamera    *camera;
	GPtrArray           *panels;       /* LrgScenePanel*, owned */
	LrgSceneArrangement *arrangement;
	LrgPanelEnvironment *environment;

	GHashTable *seen;                  /* window-sync scratch: key set */
	gboolean    drawing_content;
	gboolean    was_animating;
	guint       content_generation;

	/* Frame dimensions the panels were last laid out for; a change (window
	   resize) re-fits them. */
	gint        laid_out_w;
	gint        laid_out_h;

	/* Panel-grab (manual move) state.  Captured at grab time so the drag math is
	   stable while the pointer moves; the panel follows the pointer in the
	   camera's right/up plane at the panel's depth. */
	gboolean    grabbing;
	guint64     grab_key;
	gfloat      grab_sx, grab_sy;            /* device pixel at grab */
	gfloat      grab_cx, grab_cy, grab_cz;   /* panel centre at grab */
	gfloat      grab_rx, grab_ry, grab_rz;   /* camera right (unit) */
	gfloat      grab_ux, grab_uy, grab_uz;   /* camera up (unit) */
	gfloat      grab_scale;                  /* world units per device pixel */
	gfloat      grab_yaw, grab_w, grab_h;    /* preserved while dragging */

	/* Persistent manual placements: window key -> gfloat[6]
	   {px,py,pz,yaw,w,h}.  Re-applied after every layout so a pinned panel keeps
	   its place across resizes, redisplays and arrangement switches (which
	   recreate the panel objects). */
	GHashTable *pins;
};

G_DEFINE_FINAL_TYPE (Lrg3DSurface, lrg_3d_surface, LRG_TYPE_FRAME_SURFACE)

/* ----------------------------------------------------------------- helpers - */

static GrlWindow *
content_window (Lrg3DSurface *self)
{
	if (self->content == NULL)
		return NULL;
	return lrg_frame_surface_get_window (LRG_FRAME_SURFACE (self->content));
}

static LrgScenePanel *
find_panel (Lrg3DSurface *self,
			guint64       key)
{
	guint i;

	for (i = 0; i < self->panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (self->panels, i);
		if (lrg_scene_panel_get_key (p) == key)
			return p;
	}
	return NULL;
}

/* Re-apply persistent manual placements on top of whatever the arrangement just
   laid out, so a pinned panel keeps the spot the user dragged it to (even after
   its object was recreated by a window-sync or arrangement switch). */
static void
apply_pins (Lrg3DSurface *self)
{
	guint i;

	if (g_hash_table_size (self->pins) == 0)
		return;

	for (i = 0; i < self->panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (self->panels, i);
		guint64 key = lrg_scene_panel_get_key (p);
		gfloat *t = g_hash_table_lookup (self->pins,
										 GSIZE_TO_POINTER ((gsize) key));
		if (t != NULL)
			lrg_scene_panel_pin (p, t[0], t[1], t[2], t[3], t[4], t[5]);
	}
}

/* Pin @key at the given transform: record it (so it survives panel recreation)
   and apply it to the live panel if present. */
static void
pin_panel_to (Lrg3DSurface *self,
              guint64       key,
              gfloat        px,
              gfloat        py,
              gfloat        pz,
              gfloat        yaw,
              gfloat        w,
              gfloat        h)
{
	LrgScenePanel *p = find_panel (self, key);
	gfloat *t = g_new (gfloat, 6);

	t[0] = px; t[1] = py; t[2] = pz;
	t[3] = yaw; t[4] = w; t[5] = h;
	g_hash_table_replace (self->pins, GSIZE_TO_POINTER ((gsize) key), t);
	if (p != NULL)
		lrg_scene_panel_pin (p, px, py, pz, yaw, w, h);
}

static void
run_layout (Lrg3DSurface *self)
{
	LrgFrameSurface *fs = LRG_FRAME_SURFACE (self);

	if (self->panels->len == 0)
		return;

	/* Each panel's first placement snaps (so it appears in place); later target
	   changes ease.  The embedder drives the easing via a recompose clock while
	   lrg_3d_surface_is_animating () / the animation-started signal hold. */
	if (self->arrangement != NULL)
		lrg_scene_arrangement_layout (self->arrangement, self->panels,
									  lrg_frame_surface_get_width (fs),
									  lrg_frame_surface_get_height (fs),
									  lrg_frame_surface_get_scale (fs));
	apply_pins (self);
}

/* Single-panel mode (and any state with no synced windows) needs at least one
   panel covering the whole frame. */
static void
ensure_default_panel (Lrg3DSurface *self)
{
	LrgFrameSurface *fs = LRG_FRAME_SURFACE (self);
	LrgScenePanel *p;

	if (self->panels->len > 0)
		return;

	p = lrg_scene_panel_new (0);
	lrg_scene_panel_set_source_rect (p, 0, 0,
									 lrg_frame_surface_get_width (fs),
									 lrg_frame_surface_get_height (fs));
	g_ptr_array_add (self->panels, p);
	run_layout (self);
}

static void
render_scene (Lrg3DSurface *self)
{
	GrlWindow *win = content_window (self);
	g_autoptr (GrlColor) clear = NULL;
	g_autoptr (GrlColor) dim = NULL;
	guint i;

	if (win == NULL)
		return;

	ensure_default_panel (self);

	if (self->environment != NULL)
		clear = lrg_panel_environment_get_clear_color (self->environment);
	if (clear == NULL)
		clear = grl_color_new (16, 16, 20, 255);

	grl_window_clear_background (win, clear);

	lrg_spatial_camera_begin (self->camera);

	if (self->environment != NULL)
		lrg_panel_environment_draw_ambient (self->environment);

	/* Panels are single planes; without this their back face is culled and a
	   panel rotated away (or the camera swung behind it) vanishes.  Draw them
	   double-sided so the back shows the front mirrored.  Restore culling after
	   so the environment (next frame) is unaffected. */
	grl_rlgl_disable_backface_culling ();
	dim = grl_color_new (0, 0, 0, 255);
	for (i = 0; i < self->panels->len; i++)
		lrg_scene_panel_draw (g_ptr_array_index (self->panels, i), dim);
	grl_rlgl_enable_backface_culling ();

	lrg_spatial_camera_end (self->camera);

	grl_window_swap_buffers (win);
}

/* ---------------------------------------------------------- surface vtable - */

static void
lrg_3d_surface_begin_frame (LrgFrameSurface *surface)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	LrgFrameSurface *content;

	if (self->content == NULL)
		return;

	content = LRG_FRAME_SURFACE (self->content);
	lrg_frame_surface_begin_frame (content);

	/* Mirror the content surface's (possibly resized) geometry onto ourselves
	   so the arrangement/capture use the right dimensions. */
	lrg_frame_surface_set_geometry (surface,
									lrg_frame_surface_get_width (content),
									lrg_frame_surface_get_height (content),
									lrg_frame_surface_get_scale (content));

	/* On a geometry change (window resize), re-fit the panels.  Without this the
	   panels keep their old world size while the window grows -- the scene shows
	   a small, off-centre frame floating in empty space ("the UI drags along on
	   resize").  The single / default whole-frame panel (key 0) also re-tracks
	   the new frame so the capture crops the full content.  Per-window panels get
	   their rects from the window-tree sync the embedder runs next, but
	   re-laying-out here keeps single-panel correct (it has no sync) and is a
	   harmless re-fit for the others.  */
	{
		gint w = lrg_frame_surface_get_width (surface);
		gint h = lrg_frame_surface_get_height (surface);

		if (w != self->laid_out_w || h != self->laid_out_h)
		{
			self->laid_out_w = w;
			self->laid_out_h = h;
			if (self->panels->len == 1)
			{
				LrgScenePanel *p = g_ptr_array_index (self->panels, 0);
				if (lrg_scene_panel_get_key (p) == 0)
					lrg_scene_panel_set_source_rect (p, 0, 0, w, h);
			}
			run_layout (self);
		}
	}
}

static void
lrg_3d_surface_end_frame (LrgFrameSurface *surface)
{
	render_scene (LRG_3D_SURFACE (surface));
}

static void
lrg_3d_surface_begin_content (LrgFrameSurface *surface)
{
	LRG_3D_SURFACE (surface)->drawing_content = TRUE;
}

static void
lrg_3d_surface_end_content (LrgFrameSurface *surface)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	g_autoptr (GrlImage) shot = NULL;
	gint w, h;
	guint i;

	if (!self->drawing_content)
		return;
	self->drawing_content = FALSE;

	if (content_window (self) == NULL)
		return;

	w = lrg_frame_surface_get_width (surface);
	h = lrg_frame_surface_get_height (surface);

	/* raylib batches draws; a scissor begin/end forces the flush so the
	   readback sees the flat frame we just rasterised. */
	grl_draw_begin_scissor_mode (0, 0, w, h);
	grl_draw_end_scissor_mode ();

	shot = grl_image_new_from_screen ();
	if (shot != NULL)
	{
		for (i = 0; i < self->panels->len; i++)
			lrg_scene_panel_update_texture (g_ptr_array_index (self->panels, i),
											shot);
	}

	self->content_generation++;
}

static void
lrg_3d_surface_clear (LrgFrameSurface *surface,
					  const GrlColor  *color)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_clear (LRG_FRAME_SURFACE (self->content), color);
}

static void
lrg_3d_surface_fill_rect (LrgFrameSurface *surface,
						  gint             x,
						  gint             y,
						  gint             width,
						  gint             height,
						  const GrlColor  *color)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_fill_rect (LRG_FRAME_SURFACE (self->content),
									 x, y, width, height, color);
}

static void
lrg_3d_surface_draw_rect_outline (LrgFrameSurface *surface,
								  gint             x,
								  gint             y,
								  gint             width,
								  gint             height,
								  gfloat           thickness,
								  const GrlColor  *color)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_draw_rect_outline (LRG_FRAME_SURFACE (self->content),
											 x, y, width, height, thickness,
											 color);
}

static void
lrg_3d_surface_draw_line (LrgFrameSurface *surface,
						  gint             x1,
						  gint             y1,
						  gint             x2,
						  gint             y2,
						  gfloat           thickness,
						  const GrlColor  *color)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_draw_line (LRG_FRAME_SURFACE (self->content),
									 x1, y1, x2, y2, thickness, color);
}

static void
lrg_3d_surface_push_clip (LrgFrameSurface *surface,
						  gint             x,
						  gint             y,
						  gint             width,
						  gint             height)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_push_clip (LRG_FRAME_SURFACE (self->content),
									 x, y, width, height);
}

static void
lrg_3d_surface_pop_clip (LrgFrameSurface *surface)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_pop_clip (LRG_FRAME_SURFACE (self->content));
}

static void
lrg_3d_surface_draw_glyph (LrgFrameSurface   *surface,
						   LrgGlyphAtlas     *atlas,
						   const LrgGlyphKey *key,
						   gfloat             x,
						   gfloat             y,
						   const GrlColor    *fg)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_draw_glyph (LRG_FRAME_SURFACE (self->content),
									  atlas, key, x, y, fg);
}

static void
lrg_3d_surface_draw_texture_region (LrgFrameSurface    *surface,
									GrlTexture         *texture,
									const GrlRectangle *src,
									gfloat              dx,
									gfloat              dy,
									gfloat              dw,
									gfloat              dh,
									const GrlColor     *tint)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (surface);
	if (self->content != NULL)
		lrg_frame_surface_draw_texture_region (LRG_FRAME_SURFACE (self->content),
											   texture, src, dx, dy, dw, dh,
											   tint);
}

/* Ray-cast a device pixel back to a frame pixel: shoot a ray from the camera
   through (px,py), intersect it with each panel quad, and on the nearest hit map
   the (u,v) on that quad to a pixel in the panel's source window rect.  This is
   what makes the mouse land on the right buffer cell in 3D.  @out_key (optional)
   receives the hit panel's key, so callers can focus/grab that panel.  */
static gboolean
pick_internal (Lrg3DSurface *self,
               gfloat        px,
               gfloat        py,
               gfloat       *out_x,
               gfloat       *out_y,
               guint64      *out_key)
{
	LrgFrameSurface *surface = LRG_FRAME_SURFACE (self);
	g_autoptr (LrgPose) pose = NULL;
	g_autoptr (GrlVector2) screen = NULL;
	g_autoptr (GrlVector3) cpos = NULL;
	g_autoptr (GrlVector3) ctgt = NULL;
	g_autoptr (GrlVector3) cup = NULL;
	GrlRay *ray = NULL;
	gfloat ppx, ppy, ppz, tx, ty, tz;
	gfloat best = G_MAXFLOAT, best_x = px, best_y = py;
	guint64 best_key = 0;
	gboolean found = FALSE;
	guint i;

	if (self->camera == NULL)
		return FALSE;

	pose = lrg_spatial_camera_get_pose (self->camera);
	lrg_pose_get_position (pose, &ppx, &ppy, &ppz);
	lrg_pose_get_target (pose, &tx, &ty, &tz);

	screen = grl_vector2_new (px, py);
	cpos = grl_vector3_new (ppx, ppy, ppz);
	ctgt = grl_vector3_new (tx, ty, tz);
	cup = grl_vector3_new (0.0f, 1.0f, 0.0f);
	ray = grl_collision_get_ray_from_screen (screen, cpos, ctgt, cup,
	                                         lrg_pose_get_fovy (pose),
	                                         lrg_frame_surface_get_width (surface),
	                                         lrg_frame_surface_get_height (surface));
	if (ray == NULL)
		return FALSE;

	for (i = 0; i < self->panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (self->panels, i);
		gfloat c[12];
		g_autoptr (GrlVector3) c0 = NULL;
		g_autoptr (GrlVector3) c1 = NULL;
		g_autoptr (GrlVector3) c2 = NULL;
		g_autoptr (GrlVector3) c3 = NULL;
		GrlRayCollision *hit = NULL;

		lrg_scene_panel_get_corners (p, c);
		c0 = grl_vector3_new (c[0], c[1], c[2]);
		c1 = grl_vector3_new (c[3], c[4], c[5]);
		c2 = grl_vector3_new (c[6], c[7], c[8]);
		c3 = grl_vector3_new (c[9], c[10], c[11]);

		hit = grl_collision_ray_quad (ray, c0, c1, c2, c3);
		if (hit != NULL && hit->hit && hit->distance < best)
		{
			/* (u,v) of the hit: project onto the c0->c1 (right) and c0->c3
			   (down) edges. */
			gfloat ux = c[3] - c[0], uy = c[4] - c[1], uz = c[5] - c[2];
			gfloat vx = c[9] - c[0], vy = c[10] - c[1], vz = c[11] - c[2];
			gfloat hx = hit->point.x - c[0];
			gfloat hy = hit->point.y - c[1];
			gfloat hz = hit->point.z - c[2];
			gfloat ul = ux * ux + uy * uy + uz * uz;
			gfloat vl = vx * vx + vy * vy + vz * vz;
			gfloat u = ul > 0.0f ? (hx * ux + hy * uy + hz * uz) / ul : 0.0f;
			gfloat v = vl > 0.0f ? (hx * vx + hy * vy + hz * vz) / vl : 0.0f;
			gint sx, sy, sw, sh;

			u = CLAMP (u, 0.0f, 1.0f);
			v = CLAMP (v, 0.0f, 1.0f);
			lrg_scene_panel_get_source_rect (p, &sx, &sy, &sw, &sh);
			best = hit->distance;
			best_x = (gfloat) sx + u * (gfloat) sw;
			best_y = (gfloat) sy + v * (gfloat) sh;
			best_key = lrg_scene_panel_get_key (p);
			found = TRUE;
		}
		g_clear_pointer (&hit, grl_ray_collision_free);
	}

	grl_ray_free (ray);
	if (!found)
		return FALSE;
	if (out_x != NULL)
		*out_x = best_x;
	if (out_y != NULL)
		*out_y = best_y;
	if (out_key != NULL)
		*out_key = best_key;
	return TRUE;
}

static gboolean
lrg_3d_surface_real_pick (LrgFrameSurface *surface,
                          gfloat           px,
                          gfloat           py,
                          gfloat          *out_x,
                          gfloat          *out_y)
{
	return pick_internal (LRG_3D_SURFACE (surface), px, py, out_x, out_y, NULL);
}

gboolean
lrg_3d_surface_pick_panel (Lrg3DSurface *self,
                           gfloat        px,
                           gfloat        py,
                           gfloat       *out_x,
                           gfloat       *out_y,
                           guint64      *out_key)
{
	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);
	return pick_internal (self, px, py, out_x, out_y, out_key);
}

static GrlWindow *
lrg_3d_surface_real_get_window (LrgFrameSurface *surface)
{
	return content_window (LRG_3D_SURFACE (surface));
}

/* --------------------------------------------------------------- scene API - */

LrgSpatialCamera *
lrg_3d_surface_get_camera (Lrg3DSurface *self)
{
	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), NULL);
	return self->camera;
}

void
lrg_3d_surface_set_arrangement (Lrg3DSurface        *self,
								LrgSceneArrangement *arrangement)
{
	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	if (!g_set_object (&self->arrangement, arrangement))
		return;

	/* A new arrangement may map panels differently (one whole-frame panel vs
	   one-per-window); drop the current panels so the next present rebuilds the
	   right set via the window sync / default-panel path.  */
	g_ptr_array_set_size (self->panels, 0);
	run_layout (self);
	g_signal_emit (self, signals[SIGNAL_ARRANGEMENT_CHANGED], 0);
}

gboolean
lrg_3d_surface_set_arrangement_id (Lrg3DSurface *self,
								   const gchar  *id)
{
	g_autoptr (LrgSceneArrangement) a = NULL;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	a = lrg_mode_registry_create_arrangement (lrg_mode_registry_get_default (), id);
	if (a == NULL)
		return FALSE;

	lrg_3d_surface_set_arrangement (self, a);
	return TRUE;
}

const gchar *
lrg_3d_surface_get_arrangement_id (Lrg3DSurface *self)
{
	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), NULL);
	return self->arrangement != NULL
			   ? lrg_scene_arrangement_get_id (self->arrangement)
			   : NULL;
}

void
lrg_3d_surface_set_environment (Lrg3DSurface        *self,
								LrgPanelEnvironment *environment)
{
	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	if (!g_set_object (&self->environment, environment))
		return;

	g_signal_emit (self, signals[SIGNAL_ENVIRONMENT_CHANGED], 0);
}

gboolean
lrg_3d_surface_set_environment_id (Lrg3DSurface *self,
								   const gchar  *id)
{
	g_autoptr (LrgPanelEnvironment) e = NULL;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	e = lrg_mode_registry_create_environment (lrg_mode_registry_get_default (), id);
	if (e == NULL)
		return FALSE;

	lrg_3d_surface_set_environment (self, e);
	return TRUE;
}

const gchar *
lrg_3d_surface_get_environment_id (Lrg3DSurface *self)
{
	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), NULL);
	return self->environment != NULL
			   ? lrg_panel_environment_get_id (self->environment)
			   : NULL;
}

LrgPanelEnvironment *
lrg_3d_surface_get_environment (Lrg3DSurface *self)
{
	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), NULL);
	return self->environment;
}

void
lrg_3d_surface_begin_window_sync (Lrg3DSurface *self)
{
	g_return_if_fail (LRG_IS_3D_SURFACE (self));
	g_hash_table_remove_all (self->seen);
}

void
lrg_3d_surface_sync_window (Lrg3DSurface *self,
							guint64       key,
							gint          x,
							gint          y,
							gint          width,
							gint          height)
{
	LrgScenePanel *p;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	p = find_panel (self, key);
	if (p == NULL)
	{
		p = lrg_scene_panel_new (key);
		g_ptr_array_add (self->panels, p);
	}
	lrg_scene_panel_set_source_rect (p, x, y, width, height);
	g_hash_table_add (self->seen, GSIZE_TO_POINTER ((gsize) key));
}

void
lrg_3d_surface_end_window_sync (Lrg3DSurface *self)
{
	guint i;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	/* Drop panels for windows that disappeared this pass. */
	for (i = self->panels->len; i > 0; i--)
	{
		LrgScenePanel *p = g_ptr_array_index (self->panels, i - 1);
		guint64 key = lrg_scene_panel_get_key (p);

		if (!g_hash_table_contains (self->seen, GSIZE_TO_POINTER ((gsize) key)))
			g_ptr_array_remove_index (self->panels, i - 1);
	}

	run_layout (self);
}

void
lrg_3d_surface_set_focus_window (Lrg3DSurface *self,
								 guint64       key)
{
	gboolean match = FALSE;
	guint i;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	/* Is there a panel for the focused window?  In single-panel mode (and before
	   the first per-window sync) the sole panel has key 0 and never matches a
	   real window key -- dimming on a miss would grey out the whole scene, so
	   keep every panel fully focused in that case.  */
	for (i = 0; i < self->panels->len; i++)
		if (lrg_scene_panel_get_key (g_ptr_array_index (self->panels, i)) == key)
		{
			match = TRUE;
			break;
		}

	for (i = 0; i < self->panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (self->panels, i);
		gfloat focus = (!match || lrg_scene_panel_get_key (p) == key)
						   ? 1.0f : LRG_3D_UNFOCUSED;
		lrg_scene_panel_set_target_focus (p, focus);
	}
}

/* --- Interaction / manipulation ------------------------------------------ */

gboolean
lrg_3d_surface_focus_panel (Lrg3DSurface *self,
                            guint64       key)
{
	LrgScenePanel *p;
	g_autoptr (LrgPose) pose = NULL;
	gfloat cx, cy, cz, yaw, w, h;
	gfloat nx, nz, dist, rad;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	p = find_panel (self, key);
	if (p == NULL)
		return FALSE;

	lrg_scene_panel_get_geometry (p, &cx, &cy, &cz, &yaw, &w, &h);

	/* The panel faces +Z rotated by @yaw about world Y, so its normal is
	   (sin yaw, 0, cos yaw); sit the camera out along it, looking back. */
	rad = yaw * (gfloat) (G_PI / 180.0);
	nx = sinf (rad);
	nz = cosf (rad);

	/* Distance at which the panel height fills the 45 deg vertical FOV (+margin). */
	dist = (h * 0.5f) / tanf (0.5f * 45.0f * (gfloat) (G_PI / 180.0)) * 1.12f;
	if (dist < 0.5f)
		dist = 0.5f;

	pose = lrg_pose_new (cx + nx * dist, cy, cz + nz * dist,
	                     cx, cy, cz,
	                     0.0f, 1.0f, 0.0f, 45.0f);
	if (self->camera != NULL)
		lrg_spatial_camera_set_target_pose (self->camera, pose);

	lrg_3d_surface_set_focus_window (self, key);
	g_signal_emit (self, signals[SIGNAL_PANEL_FOCUSED], 0, key);
	return TRUE;
}

void
lrg_3d_surface_orbit_room (Lrg3DSurface *self,
                           gfloat        dyaw,
                           gfloat        dpitch)
{
	gfloat cx = 0.0f, cy = 0.0f, cz = 0.0f;
	guint i, n;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	if (self->camera == NULL)
		return;

	/* Pivot = centroid of the panel centres (the "room"), so the orbit circles
	   the whole workspace rather than a single focused panel. */
	n = self->panels->len;
	for (i = 0; i < n; i++)
	{
		gfloat px, py, pz;
		lrg_scene_panel_get_geometry (g_ptr_array_index (self->panels, i),
									  &px, &py, &pz, NULL, NULL, NULL);
		cx += px; cy += py; cz += pz;
	}
	if (n > 0)
	{
		cx /= (gfloat) n;
		cy /= (gfloat) n;
		cz /= (gfloat) n;
	}
	lrg_spatial_camera_orbit_around_drag (self->camera, cx, cy, cz,
										  dyaw, dpitch);
}

gboolean
lrg_3d_surface_grab_panel (Lrg3DSurface *self,
                           guint64       key,
                           gfloat        px,
                           gfloat        py)
{
	LrgScenePanel *p;
	g_autoptr (LrgPose) pose = NULL;
	gfloat ex, ey, ez, tx, ty, tz, fovy;
	gfloat fx, fy, fz, len, dist, vh, sh;
	gfloat cx, cy, cz, yaw, w, h;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	if (self->camera == NULL)
		return FALSE;
	p = find_panel (self, key);
	if (p == NULL)
		return FALSE;

	lrg_scene_panel_get_geometry (p, &cx, &cy, &cz, &yaw, &w, &h);

	pose = lrg_spatial_camera_get_pose (self->camera);
	lrg_pose_get_position (pose, &ex, &ey, &ez);
	lrg_pose_get_target (pose, &tx, &ty, &tz);
	fovy = lrg_pose_get_fovy (pose);

	/* forward = normalize(target - eye) */
	fx = tx - ex; fy = ty - ey; fz = tz - ez;
	len = sqrtf (fx * fx + fy * fy + fz * fz);
	if (len < 0.0001f)
		return FALSE;
	fx /= len; fy /= len; fz /= len;

	/* right = normalize(forward x worldUp(0,1,0)) = normalize(-fz, 0, fx) */
	self->grab_rx = -fz; self->grab_ry = 0.0f; self->grab_rz = fx;
	len = sqrtf (self->grab_rx * self->grab_rx + self->grab_rz * self->grab_rz);
	if (len < 0.0001f)
	{
		/* Camera looks straight up/down: fall back to world X for "right". */
		self->grab_rx = 1.0f; self->grab_ry = 0.0f; self->grab_rz = 0.0f;
		len = 1.0f;
	}
	self->grab_rx /= len; self->grab_rz /= len;

	/* up = right x forward (orthonormal) */
	self->grab_ux = self->grab_ry * fz - self->grab_rz * fy;
	self->grab_uy = self->grab_rz * fx - self->grab_rx * fz;
	self->grab_uz = self->grab_rx * fy - self->grab_ry * fx;

	/* world units per device pixel at the panel's depth. */
	dist = sqrtf ((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey)
				  + (cz - ez) * (cz - ez));
	sh = (gfloat) lrg_frame_surface_get_height (LRG_FRAME_SURFACE (self));
	if (sh < 1.0f)
		sh = 1.0f;
	vh = 2.0f * dist * tanf (0.5f * fovy * (gfloat) (G_PI / 180.0));
	self->grab_scale = vh / sh;

	self->grabbing = TRUE;
	self->grab_key = key;
	self->grab_sx = px; self->grab_sy = py;
	self->grab_cx = cx; self->grab_cy = cy; self->grab_cz = cz;
	self->grab_yaw = yaw; self->grab_w = w; self->grab_h = h;
	return TRUE;
}

void
lrg_3d_surface_drag_panel (Lrg3DSurface *self,
                           gfloat        px,
                           gfloat        py)
{
	LrgScenePanel *p;
	gfloat dsx, dsy, wx, wy, wz;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	if (!self->grabbing)
		return;
	p = find_panel (self, self->grab_key);
	if (p == NULL)
	{
		self->grabbing = FALSE;
		return;
	}

	/* Pixel delta -> world delta in the camera's right/up plane (screen +y is
	   down, so the up component is subtracted). */
	dsx = (px - self->grab_sx) * self->grab_scale;
	dsy = (py - self->grab_sy) * self->grab_scale;
	wx = self->grab_cx + self->grab_rx * dsx - self->grab_ux * dsy;
	wy = self->grab_cy + self->grab_ry * dsx - self->grab_uy * dsy;
	wz = self->grab_cz + self->grab_rz * dsx - self->grab_uz * dsy;

	(void) p;
	pin_panel_to (self, self->grab_key, wx, wy, wz,
	              self->grab_yaw, self->grab_w, self->grab_h);
	g_signal_emit (self, signals[SIGNAL_PANEL_MOVED], 0, self->grab_key);
}

void
lrg_3d_surface_release_panel (Lrg3DSurface *self)
{
	guint64 key;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	if (!self->grabbing)
		return;
	key = self->grab_key;
	self->grabbing = FALSE;
	g_signal_emit (self, signals[SIGNAL_PANEL_PINNED], 0, key);
}

gboolean
lrg_3d_surface_pin_panel (Lrg3DSurface *self,
                          guint64       key)
{
	LrgScenePanel *p;
	gfloat px, py, pz, yaw, w, h;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	p = find_panel (self, key);
	if (p == NULL)
		return FALSE;
	lrg_scene_panel_get_geometry (p, &px, &py, &pz, &yaw, &w, &h);
	pin_panel_to (self, key, px, py, pz, yaw, w, h);
	g_signal_emit (self, signals[SIGNAL_PANEL_PINNED], 0, key);
	return TRUE;
}

gboolean
lrg_3d_surface_move_panel (Lrg3DSurface *self,
                           guint64       key,
                           gfloat        dx,
                           gfloat        dy,
                           gfloat        dz)
{
	LrgScenePanel *p;
	gfloat px, py, pz, yaw, w, h;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	p = find_panel (self, key);
	if (p == NULL)
		return FALSE;
	lrg_scene_panel_get_geometry (p, &px, &py, &pz, &yaw, &w, &h);
	pin_panel_to (self, key, px + dx, py + dy, pz + dz, yaw, w, h);
	g_signal_emit (self, signals[SIGNAL_PANEL_MOVED], 0, key);
	return TRUE;
}

void
lrg_3d_surface_unpin_panel (Lrg3DSurface *self,
                            guint64       key)
{
	LrgScenePanel *p;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	g_hash_table_remove (self->pins, GSIZE_TO_POINTER ((gsize) key));
	p = find_panel (self, key);
	if (p != NULL)
		lrg_scene_panel_unpin (p);
	run_layout (self);
}

void
lrg_3d_surface_unpin_all (Lrg3DSurface *self)
{
	guint i;

	g_return_if_fail (LRG_IS_3D_SURFACE (self));

	g_hash_table_remove_all (self->pins);
	for (i = 0; i < self->panels->len; i++)
		lrg_scene_panel_unpin (g_ptr_array_index (self->panels, i));
	run_layout (self);
}

gboolean
lrg_3d_surface_is_panel_pinned (Lrg3DSurface *self,
                                guint64       key)
{
	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	return g_hash_table_contains (self->pins, GSIZE_TO_POINTER ((gsize) key));
}

gboolean
lrg_3d_surface_step (Lrg3DSurface *self,
					 gfloat        dt)
{
	gboolean any = FALSE;
	guint i;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	if (self->camera != NULL && lrg_spatial_camera_step (self->camera, dt))
		any = TRUE;

	for (i = 0; i < self->panels->len; i++)
		if (lrg_scene_panel_step (g_ptr_array_index (self->panels, i), dt))
			any = TRUE;

	if (self->arrangement != NULL
		&& lrg_scene_arrangement_wants_continuous (self->arrangement))
		any = TRUE;

	if (any && !self->was_animating)
		g_signal_emit (self, signals[SIGNAL_ANIMATION_STARTED], 0);
	else if (!any && self->was_animating)
		g_signal_emit (self, signals[SIGNAL_ANIMATION_FINISHED], 0);
	self->was_animating = any;

	return any;
}

gboolean
lrg_3d_surface_is_animating (Lrg3DSurface *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_3D_SURFACE (self), FALSE);

	/* Report the LIVE pending-animation state (camera / any panel / a continuous
	   arrangement), NOT the cached edge-flag that _step () maintains for its
	   started/finished signals.  The embedder gates its 60 Hz recompose tick on
	   this; gating on the cached flag dead-locks (the flag is only set once
	   _step () runs, but the tick only calls _step () once the flag is set), so
	   camera moves, focus changes and resize re-fits would never play out.  */
	if (self->camera != NULL && lrg_spatial_camera_is_animating (self->camera))
		return TRUE;
	for (i = 0; i < self->panels->len; i++)
		if (lrg_scene_panel_is_animating (g_ptr_array_index (self->panels, i)))
			return TRUE;
	if (self->arrangement != NULL
		&& lrg_scene_arrangement_wants_continuous (self->arrangement))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------- new / type -- */

Lrg3DSurface *
lrg_3d_surface_new (gint         width,
					gint         height,
					const gchar *title)
{
	Lrg3DSurface *self = g_object_new (LRG_TYPE_3D_SURFACE, NULL);

	g_return_val_if_fail (width > 0 && height > 0, self);

	self->content = lrg_2d_surface_new (width, height, title);
	lrg_frame_surface_set_render_mode (LRG_FRAME_SURFACE (self),
									   LRG_RENDER_MODE_3D);
	lrg_frame_surface_set_geometry (
		LRG_FRAME_SURFACE (self),
		lrg_frame_surface_get_width (LRG_FRAME_SURFACE (self->content)),
		lrg_frame_surface_get_height (LRG_FRAME_SURFACE (self->content)),
		lrg_frame_surface_get_scale (LRG_FRAME_SURFACE (self->content)));

	return self;
}

static void
lrg_3d_surface_dispose (GObject *object)
{
	Lrg3DSurface *self = LRG_3D_SURFACE (object);

	g_clear_object (&self->content);
	g_clear_object (&self->camera);
	g_clear_object (&self->arrangement);
	g_clear_object (&self->environment);
	g_clear_pointer (&self->panels, g_ptr_array_unref);
	g_clear_pointer (&self->seen, g_hash_table_unref);
	g_clear_pointer (&self->pins, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_3d_surface_parent_class)->dispose (object);
}

static void
lrg_3d_surface_class_init (Lrg3DSurfaceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgFrameSurfaceClass *surface_class = LRG_FRAME_SURFACE_CLASS (klass);

	object_class->dispose = lrg_3d_surface_dispose;

	surface_class->begin_frame = lrg_3d_surface_begin_frame;
	surface_class->end_frame = lrg_3d_surface_end_frame;
	surface_class->begin_content = lrg_3d_surface_begin_content;
	surface_class->end_content = lrg_3d_surface_end_content;
	surface_class->clear = lrg_3d_surface_clear;
	surface_class->fill_rect = lrg_3d_surface_fill_rect;
	surface_class->draw_rect_outline = lrg_3d_surface_draw_rect_outline;
	surface_class->draw_line = lrg_3d_surface_draw_line;
	surface_class->push_clip = lrg_3d_surface_push_clip;
	surface_class->pop_clip = lrg_3d_surface_pop_clip;
	surface_class->draw_glyph = lrg_3d_surface_draw_glyph;
	surface_class->draw_texture_region = lrg_3d_surface_draw_texture_region;
	surface_class->get_window = lrg_3d_surface_real_get_window;
	surface_class->pick = lrg_3d_surface_real_pick;

	signals[SIGNAL_ANIMATION_STARTED] =
		g_signal_new ("animation-started", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 0);
	signals[SIGNAL_ANIMATION_FINISHED] =
		g_signal_new ("animation-finished", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 0);
	signals[SIGNAL_ARRANGEMENT_CHANGED] =
		g_signal_new ("arrangement-changed", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 0);
	signals[SIGNAL_ENVIRONMENT_CHANGED] =
		g_signal_new ("environment-changed", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 0);

	/**
	 * Lrg3DSurface::panel-focused:
	 * @self: the surface
	 * @key: the focused panel/window key
	 *
	 * Emitted when a panel is brought front-and-centre (lrg_3d_surface_focus_panel()).
	 */
	signals[SIGNAL_PANEL_FOCUSED] =
		g_signal_new ("panel-focused", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 1, G_TYPE_UINT64);
	/**
	 * Lrg3DSurface::panel-moved:
	 * @self: the surface
	 * @key: the moved panel/window key
	 *
	 * Emitted on each step of a manual panel drag (lrg_3d_surface_drag_panel()).
	 */
	signals[SIGNAL_PANEL_MOVED] =
		g_signal_new ("panel-moved", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 1, G_TYPE_UINT64);
	/**
	 * Lrg3DSurface::panel-pinned:
	 * @self: the surface
	 * @key: the pinned panel/window key
	 *
	 * Emitted when a manual drag ends and the panel stays pinned
	 * (lrg_3d_surface_release_panel()).
	 */
	signals[SIGNAL_PANEL_PINNED] =
		g_signal_new ("panel-pinned", G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
					  G_TYPE_NONE, 1, G_TYPE_UINT64);
}

static void
lrg_3d_surface_init (Lrg3DSurface *self)
{
	LrgModeRegistry *reg = lrg_mode_registry_get_default ();

	self->panels = g_ptr_array_new_with_free_func (g_object_unref);
	self->seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->pins = g_hash_table_new_full (g_direct_hash, g_direct_equal,
										NULL, g_free);
	self->camera = lrg_spatial_camera_new ();
	self->arrangement = lrg_mode_registry_create_arrangement (reg, "single-panel");
	self->environment = lrg_mode_registry_create_environment (reg, "void");
	self->drawing_content = FALSE;
	self->was_animating = FALSE;
	self->content_generation = 0;
}
