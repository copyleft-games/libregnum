/* lrg-scene-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include <graylib.h>
#include "lrg-scene-panel.h"

/* Exponential-smoothing time constant (seconds); convergence epsilon. */
#define LRG_SCENE_PANEL_TAU 0.10f
#define LRG_SCENE_PANEL_EPSILON 0.004f

enum
{
	SIGNAL_TRANSFORM_SETTLED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _LrgScenePanel
{
	GObject parent_instance;

	guint64 key;

	/* Source sub-rectangle within the captured frame, in pixels. */
	gint sx, sy, sw, sh;

	/* GPU resources (created lazily, on first texture/draw, so a panel can be
	   constructed without a live GL context). */
	GrlMesh    *mesh;
	GrlModel   *model;
	GrlTexture *texture;

	/* Current transform: centre position, yaw (deg), size (world units), and
	   depth-of-field focus weight in [0, 1]. */
	gfloat px, py, pz, yaw, w, h, focus;

	/* Target transform the current eases toward. */
	gfloat tpx, tpy, tpz, tyaw, tw, th, tfocus;

	gboolean animating;

	/* TRUE once a target has been applied: the first placement snaps (the panel
	   appears where it belongs), later target changes ease. */
	gboolean placed;

	/* TRUE when the user has grabbed/dragged this panel: the arrangement layout
	   (set_target / set_immediate) is then ignored so it stays put. */
	gboolean pinned;
};

G_DEFINE_FINAL_TYPE (LrgScenePanel, lrg_scene_panel, G_TYPE_OBJECT)

static gfloat
panel_delta (LrgScenePanel *self)
{
	return fabsf (self->px - self->tpx) + fabsf (self->py - self->tpy)
		 + fabsf (self->pz - self->tpz) + fabsf (self->yaw - self->tyaw)
		 + fabsf (self->w - self->tw) + fabsf (self->h - self->th)
		 + fabsf (self->focus - self->tfocus);
}

/* Ensure the plane mesh + model exist (needs a live GL context). */
static void
ensure_model (LrgScenePanel *self)
{
	if (self->model != NULL)
		return;

	self->mesh = grl_mesh_new_plane (1.0f, 1.0f, 1, 1);
	if (self->mesh != NULL)
		self->model = grl_model_new_from_mesh (self->mesh);
}

/* Build the full model matrix.  The plane is generated in XZ (normal +Y); a
   +90 deg rotation about X stands it up to face +Z, then yaw turns it about
   world Y.  graylib/raylib grl_matrix_multiply(a, b) yields a matrix that
   applies a THEN b, so the args read in application order: scale, stand-up,
   yaw, translate.  (Listing them in the reverse/world order leaves the scale
   acting in world space after the stand-up, which collapses the panel height.)
   Returns (transfer full).  */
static GrlMatrix *
build_matrix (LrgScenePanel *self)
{
	g_autoptr (GrlMatrix) s = grl_matrix_new_scale (self->w, 1.0f, self->h);
	g_autoptr (GrlMatrix) rx = grl_matrix_new_rotate_xyz ((gfloat) G_PI_2, 0.0f, 0.0f);
	g_autoptr (GrlMatrix) ry =
		grl_matrix_new_rotate_xyz (0.0f, self->yaw * (gfloat) (G_PI / 180.0), 0.0f);
	/* Depth-of-field: a focused panel pops toward the camera, unfocused recede. */
	gfloat zpop = (self->focus - 0.5f) * 0.5f;
	g_autoptr (GrlMatrix) t = grl_matrix_new_translate (self->px, self->py,
	                                                    self->pz + zpop);
	g_autoptr (GrlMatrix) sr = grl_matrix_multiply (s, rx);
	g_autoptr (GrlMatrix) sry = grl_matrix_multiply (sr, ry);

	return grl_matrix_multiply (sry, t);
}

/* Transform a local point by M (column-major, OpenGL layout). */
static void
matrix_point (const GrlMatrix *m,
			  gfloat           x,
			  gfloat           y,
			  gfloat           z,
			  gfloat          *ox,
			  gfloat          *oy,
			  gfloat          *oz)
{
	*ox = m->m0 * x + m->m4 * y + m->m8 * z + m->m12;
	*oy = m->m1 * x + m->m5 * y + m->m9 * z + m->m13;
	*oz = m->m2 * x + m->m6 * y + m->m10 * z + m->m14;
}

LrgScenePanel *
lrg_scene_panel_new (guint64 key)
{
	LrgScenePanel *self = g_object_new (LRG_TYPE_SCENE_PANEL, NULL);

	self->key = key;
	return self;
}

guint64
lrg_scene_panel_get_key (LrgScenePanel *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_PANEL (self), 0);
	return self->key;
}

void
lrg_scene_panel_set_source_rect (LrgScenePanel *self,
						   gint      x,
						   gint      y,
						   gint      width,
						   gint      height)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	self->sx = x;
	self->sy = y;
	self->sw = width;
	self->sh = height;
}

void
lrg_scene_panel_get_source_rect (LrgScenePanel *self,
						   gint     *x,
						   gint     *y,
						   gint     *width,
						   gint     *height)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	if (x != NULL)
		*x = self->sx;
	if (y != NULL)
		*y = self->sy;
	if (width != NULL)
		*width = self->sw;
	if (height != NULL)
		*height = self->sh;
}

void
lrg_scene_panel_set_target (LrgScenePanel *self,
					  gfloat    px,
					  gfloat    py,
					  gfloat    pz,
					  gfloat    yaw,
					  gfloat    width,
					  gfloat    height)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	if (self->pinned)
		return;

	self->tpx = px;
	self->tpy = py;
	self->tpz = pz;
	self->tyaw = yaw;
	self->tw = width;
	self->th = height;
	if (!self->placed)
	{
		/* First placement snaps so the panel appears where it belongs. */
		self->px = px;
		self->py = py;
		self->pz = pz;
		self->yaw = yaw;
		self->w = width;
		self->h = height;
		self->placed = TRUE;
		self->animating = FALSE;
	}
	else
		self->animating = (panel_delta (self) > LRG_SCENE_PANEL_EPSILON);
}

void
lrg_scene_panel_set_immediate (LrgScenePanel *self,
						 gfloat    px,
						 gfloat    py,
						 gfloat    pz,
						 gfloat    yaw,
						 gfloat    width,
						 gfloat    height)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	if (self->pinned)
		return;

	self->px = self->tpx = px;
	self->py = self->tpy = py;
	self->pz = self->tpz = pz;
	self->yaw = self->tyaw = yaw;
	self->w = self->tw = width;
	self->h = self->th = height;
	self->placed = TRUE;
	self->animating = (panel_delta (self) > LRG_SCENE_PANEL_EPSILON);
}

void
lrg_scene_panel_set_target_focus (LrgScenePanel *self,
							gfloat    focus)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	self->tfocus = CLAMP (focus, 0.0f, 1.0f);
	self->animating = (panel_delta (self) > LRG_SCENE_PANEL_EPSILON);
}

void
lrg_scene_panel_pin (LrgScenePanel *self,
                     gfloat         px,
                     gfloat         py,
                     gfloat         pz,
                     gfloat         yaw,
                     gfloat         width,
                     gfloat         height)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	self->pinned = TRUE;
	self->px = self->tpx = px;
	self->py = self->tpy = py;
	self->pz = self->tpz = pz;
	self->yaw = self->tyaw = yaw;
	self->w = self->tw = width;
	self->h = self->th = height;
	self->placed = TRUE;
	self->animating = FALSE;
}

void
lrg_scene_panel_unpin (LrgScenePanel *self)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));
	self->pinned = FALSE;
}

gboolean
lrg_scene_panel_is_pinned (LrgScenePanel *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_PANEL (self), FALSE);
	return self->pinned;
}

void
lrg_scene_panel_update_texture (LrgScenePanel *self,
						  GrlImage *frame)
{
	GrlImage *img;
	gboolean owned = FALSE;
	gint fw, fh, iw, ih;
	gint rx, ry, rw, rh;

	g_return_if_fail (LRG_IS_SCENE_PANEL (self));
	g_return_if_fail (frame != NULL);

	ensure_model (self);
	if (self->model == NULL)
		return;

	fw = grl_image_get_width (frame);
	fh = grl_image_get_height (frame);

	/* Clamp the source rect to the captured frame.  During a window resize the
	   panel rect (derived from the Emacs window pixel edges) and the freshly
	   captured framebuffer momentarily disagree in size; cropping with an
	   out-of-bounds rect reads past the image allocation and crashes (raylib's
	   ImageFromImage has no bounds check).  A non-positive width/height means
	   "whole frame" (a panel placed before its first window sync).  */
	rx = CLAMP (self->sx, 0, fw);
	ry = CLAMP (self->sy, 0, fh);
	rw = self->sw > 0 ? CLAMP (self->sw, 0, fw - rx) : fw - rx;
	rh = self->sh > 0 ? CLAMP (self->sh, 0, fh - ry) : fh - ry;
	if (rw <= 0 || rh <= 0)
		return;

	if (rx == 0 && ry == 0 && rw == fw && rh == fh)
	{
		img = frame;
	}
	else
	{
		g_autoptr (GrlRectangle) rect =
			grl_rectangle_new ((gfloat) rx, (gfloat) ry,
							   (gfloat) rw, (gfloat) rh);
		img = grl_image_from_region (frame, rect);
		owned = TRUE;
	}

	if (img == NULL)
		return;

	iw = grl_image_get_width (img);
	ih = grl_image_get_height (img);

	/* A resized window changes the sub-image dimensions; raylib's in-place
	   update requires a matching size, so drop and recreate on a mismatch. */
	if (self->texture != NULL
		&& (grl_texture_get_width (self->texture) != iw
			|| grl_texture_get_height (self->texture) != ih))
		g_clear_object (&self->texture);

	if (self->texture == NULL)
	{
		self->texture = grl_texture_new_from_image (img);
		if (self->texture != NULL)
		{
			grl_texture_gen_mipmaps (self->texture);
			grl_texture_set_filter (self->texture,
									GRL_TEXTURE_FILTER_ANISOTROPIC_16X);
			grl_model_set_texture (self->model, 0, GRL_MATERIAL_MAP_ALBEDO,
								   self->texture);
		}
	}
	else
	{
		grl_texture_update (self->texture, img);
		grl_texture_gen_mipmaps (self->texture);
	}

	if (owned)
		g_object_unref (img);
}

gboolean
lrg_scene_panel_step (LrgScenePanel *self,
				gfloat    dt)
{
	gfloat f;

	g_return_val_if_fail (LRG_IS_SCENE_PANEL (self), FALSE);

	if (!self->animating)
		return FALSE;

	if (dt < 0.0f)
		dt = 0.0f;
	f = 1.0f - expf (-dt / LRG_SCENE_PANEL_TAU);

	self->px += (self->tpx - self->px) * f;
	self->py += (self->tpy - self->py) * f;
	self->pz += (self->tpz - self->pz) * f;
	self->yaw += (self->tyaw - self->yaw) * f;
	self->w += (self->tw - self->w) * f;
	self->h += (self->th - self->h) * f;
	self->focus += (self->tfocus - self->focus) * f;

	if (panel_delta (self) <= LRG_SCENE_PANEL_EPSILON)
	{
		self->px = self->tpx;
		self->py = self->tpy;
		self->pz = self->tpz;
		self->yaw = self->tyaw;
		self->w = self->tw;
		self->h = self->th;
		self->focus = self->tfocus;
		self->animating = FALSE;
		g_signal_emit (self, signals[SIGNAL_TRANSFORM_SETTLED], 0);
	}

	return self->animating;
}

gboolean
lrg_scene_panel_is_animating (LrgScenePanel *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_PANEL (self), FALSE);
	return self->animating;
}

void
lrg_scene_panel_draw (LrgScenePanel       *self,
				const GrlColor *dim)
{
	g_autoptr (GrlMatrix) m = NULL;
	g_autoptr (GrlVector3) origin = NULL;
	g_autoptr (GrlColor) tint = NULL;
	gint dr = 0, dg = 0, db = 0;
	gint r, g, b;

	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	ensure_model (self);
	if (self->model == NULL || self->texture == NULL)
		return;

	if (dim != NULL)
	{
		dr = grl_color_get_r (dim);
		dg = grl_color_get_g (dim);
		db = grl_color_get_b (dim);
	}

	/* tint = lerp(dim, white, focus): a focused panel shows its texture at full
	   brightness; an unfocused one fades toward @dim (depth-of-field dimming). */
	r = dr + (gint) ((255 - dr) * self->focus);
	g = dg + (gint) ((255 - dg) * self->focus);
	b = db + (gint) ((255 - db) * self->focus);
	tint = grl_color_new ((guint8) CLAMP (r, 0, 255),
						  (guint8) CLAMP (g, 0, 255),
						  (guint8) CLAMP (b, 0, 255), 255);

	m = build_matrix (self);
	grl_model_set_transform (self->model, m);

	origin = grl_vector3_new (0.0f, 0.0f, 0.0f);
	grl_model_draw (self->model, origin, 1.0f, tint);
}

void
lrg_scene_panel_get_geometry (LrgScenePanel *self,
						gfloat   *px,
						gfloat   *py,
						gfloat   *pz,
						gfloat   *yaw,
						gfloat   *width,
						gfloat   *height)
{
	g_return_if_fail (LRG_IS_SCENE_PANEL (self));

	if (px != NULL)
		*px = self->px;
	if (py != NULL)
		*py = self->py;
	if (pz != NULL)
		*pz = self->pz;
	if (yaw != NULL)
		*yaw = self->yaw;
	if (width != NULL)
		*width = self->w;
	if (height != NULL)
		*height = self->h;
}

void
lrg_scene_panel_get_corners (LrgScenePanel *self,
					   gfloat   *out_xyz12)
{
	g_autoptr (GrlMatrix) m = NULL;
	/* Local unit-plane corners (XZ): top-left, top-right, bottom-right,
	   bottom-left.  +90 about X maps -Z (back) to +Y (up). */
	static const gfloat local[4][3] = {
		{ -0.5f, 0.0f, -0.5f },
		{ 0.5f, 0.0f, -0.5f },
		{ 0.5f, 0.0f, 0.5f },
		{ -0.5f, 0.0f, 0.5f }
	};
	gint i;

	g_return_if_fail (LRG_IS_SCENE_PANEL (self));
	g_return_if_fail (out_xyz12 != NULL);

	m = build_matrix (self);
	for (i = 0; i < 4; i++)
		matrix_point (m, local[i][0], local[i][1], local[i][2],
					  &out_xyz12[i * 3], &out_xyz12[i * 3 + 1],
					  &out_xyz12[i * 3 + 2]);
}

static void
lrg_scene_panel_dispose (GObject *object)
{
	LrgScenePanel *self = LRG_SCENE_PANEL (object);

	g_clear_object (&self->texture);
	g_clear_object (&self->model);
	g_clear_object (&self->mesh);

	G_OBJECT_CLASS (lrg_scene_panel_parent_class)->dispose (object);
}

static void
lrg_scene_panel_class_init (LrgScenePanelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_scene_panel_dispose;

	/**
	 * LrgScenePanel::transform-settled:
	 * @self: the panel
	 *
	 * Emitted when the panel finishes easing to its target transform/focus.
	 */
	signals[SIGNAL_TRANSFORM_SETTLED] =
		g_signal_new ("transform-settled",
					  G_TYPE_FROM_CLASS (klass),
					  G_SIGNAL_RUN_FIRST,
					  0, NULL, NULL, NULL,
					  G_TYPE_NONE, 0);
}

static void
lrg_scene_panel_init (LrgScenePanel *self)
{
	self->w = self->tw = 1.0f;
	self->h = self->th = 1.0f;
	self->focus = self->tfocus = 1.0f;
	self->animating = FALSE;
	self->placed = FALSE;
	self->pinned = FALSE;
}
