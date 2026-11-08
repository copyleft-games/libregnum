/* lrg-editor-math.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless editor math. See lrg-editor-math.h for the conventions.
 */

#include "lrg-editor-math.h"
#include "../core/lrg-math3d.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-node-tilemap.h"
#include "../tilemap/lrg-tilemap.h"
#include "../graphics/lrg-terrain-asset.h"
#include "../lrg-enums.h"
#include <graylib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define M(m, row, col) ((m)[(col) * 4 + (row)])

#define ICON_HALF_EXTENT   (0.25f)
#define THIN_HALF_EXTENT   (0.01f)
#define MIN_PITCH_COS      (0.0175f)   /* ~89 degrees */

/* ==========================================================================
 * Small vector helpers
 * ========================================================================== */

static inline void
v3_set (gfloat *o, gfloat x, gfloat y, gfloat z)
{
	o[0] = x; o[1] = y; o[2] = z;
}

static inline void
v3_sub (const gfloat *a, const gfloat *b, gfloat *o)
{
	o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2];
}

static inline void
v3_add_scaled (gfloat *o, const gfloat *d, gfloat s)
{
	o[0] += d[0] * s; o[1] += d[1] * s; o[2] += d[2] * s;
}

static inline gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
v3_cross (const gfloat *a, const gfloat *b, gfloat *o)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	o[0] = x; o[1] = y; o[2] = z;
}

static inline gfloat
v3_length (const gfloat *a)
{
	return sqrtf (v3_dot (a, a));
}

static inline gboolean
v3_normalize (gfloat *a)
{
	gfloat len = v3_length (a);

	if (len < 1e-12f)
		return FALSE;
	a[0] /= len; a[1] /= len; a[2] /= len;
	return TRUE;
}

/* Rotates @v around unit @axis by @angle (Rodrigues). */
static void
v3_rotate (gfloat *v, const gfloat *axis, gfloat angle)
{
	gfloat c = cosf (angle);
	gfloat s = sinf (angle);
	gfloat k[3];
	gfloat cr[3];
	gfloat d;
	gint   i;

	memcpy (k, axis, sizeof k);
	v3_cross (k, v, cr);
	d = v3_dot (k, v);
	for (i = 0; i < 3; i++)
		v[i] = v[i] * c + cr[i] * s + k[i] * d * (1.0f - c);
}

/* ==========================================================================
 * Matrices
 *
 * Since 0.9.0 the implementations live in core (src/core/lrg-math3d.c) so
 * the runtime level module and games can use them without the editor.
 * These keep the 0.8 editor API as thin wrappers; results are bit-identical
 * (tests/test-math3d.c).
 * ========================================================================== */

void
lrg_editor_math_identity (gfloat *out)
{
	lrg_math3d_identity (out);
}

void
lrg_editor_math_compose (const gfloat *location,
                         const gfloat *rotation,
                         const gfloat *scale,
                         gfloat       *out)
{
	lrg_math3d_compose (location, rotation, scale, out);
}

gboolean
lrg_editor_math_decompose (const gfloat *m,
                           gfloat       *location,
                           gfloat       *rotation,
                           gfloat       *scale)
{
	return lrg_math3d_decompose (m, location, rotation, scale);
}

void
lrg_editor_math_multiply (const gfloat *a,
                          const gfloat *b,
                          gfloat       *out)
{
	lrg_math3d_multiply (a, b, out);
}

gboolean
lrg_editor_math_invert (const gfloat *m,
                        gfloat       *out)
{
	return lrg_math3d_invert (m, out);
}

void
lrg_editor_math_transform_point (const gfloat *m,
                                 const gfloat *p,
                                 gfloat       *out)
{
	lrg_math3d_transform_point (m, p, out);
}

void
lrg_editor_math_transform_direction (const gfloat *m,
                                     const gfloat *d,
                                     gfloat       *out)
{
	lrg_math3d_transform_direction (m, d, out);
}

/* ==========================================================================
 * Node transforms
 * ========================================================================== */

void
lrg_editor_node_get_local_matrix (LrgNode *node,
                                  gfloat  *out)
{
	/* Implemented by the runtime level module since 0.9.0. */
	lrg_node_get_local_matrix (node, out);
}

void
lrg_editor_node_get_world_matrix (LrgNode *node,
                                  gfloat  *out)
{
	/* Implemented by the runtime level module since 0.9.0. */
	lrg_node_get_world_matrix (node, out);
}

void
lrg_editor_node_get_world_position (LrgNode *node,
                                    gfloat  *out)
{
	gfloat m[16];

	lrg_editor_node_get_world_matrix (node, m);
	v3_set (out, M (m, 0, 3), M (m, 1, 3), M (m, 2, 3));
}

gboolean
lrg_editor_world_to_local (LrgNode      *parent,
                           const gfloat *world,
                           gfloat       *location,
                           gfloat       *rotation,
                           gfloat       *scale)
{
	gfloat parent_world[16];
	gfloat inv[16];
	gfloat local[16];

	if (parent == NULL)
		return lrg_editor_math_decompose (world, location, rotation, scale);

	lrg_editor_node_get_world_matrix (parent, parent_world);
	if (!lrg_editor_math_invert (parent_world, inv))
		return FALSE;

	lrg_editor_math_multiply (inv, world, local);
	return lrg_editor_math_decompose (local, location, rotation, scale);
}

/* tilemap_local_bounds:
 * A TILEMAP node covers its grid, not a unit sprite. Orthogonal cells
 * are tile_size world units (row 0 along y = 0, rows down -Y). Other
 * orientations use the same pixel map size as LrgTilemap, scaled by
 * tile_size / cell width and flipped in Y. */
static gboolean
tilemap_local_bounds (LrgNodeVisual *visual,
                      gfloat        *min,
                      gfloat        *max)
{
	LrgNodeTilemap        *grid;
	LrgTilemapOrientation  orientation;
	LrgTilemapStaggerAxis  axis;
	guint                  hex = 0, cw = 0, ch = 0;
	guint                  w, h;
	gdouble                size;
	gfloat                 ts, s, pw, ph, along, side, side_offset;

	grid = lrg_node_visual_get_tilemap (visual);
	if (grid == NULL)
		return FALSE;
	w = lrg_node_tilemap_get_width (grid);
	h = lrg_node_tilemap_get_height (grid);
	if (w == 0 || h == 0)
		return FALSE;

	size = lrg_node_visual_get_param_double (visual, "tile_size", 1.0);
	ts = (size > 0.0) ? (gfloat) size : 1.0f;
	lrg_node_tilemap_get_grid (grid, &orientation, &axis, NULL, &hex, &cw, &ch);

	if (orientation == LRG_TILEMAP_ORIENTATION_ORTHOGONAL || cw == 0 || ch == 0)
	{
		v3_set (min, 0.0f, -(gfloat) h * ts, -THIN_HALF_EXTENT);
		v3_set (max, (gfloat) w * ts, 0.0f, THIN_HALF_EXTENT);
		return TRUE;
	}

	s = ts / (gfloat) cw;
	if (orientation == LRG_TILEMAP_ORIENTATION_ISOMETRIC)
	{
		pw = (gfloat) (w + h) * (gfloat) cw * 0.5f;
		ph = (gfloat) (w + h) * (gfloat) ch * 0.5f;
	}
	else
	{
		along = (axis == LRG_TILEMAP_STAGGER_AXIS_X) ? (gfloat) cw : (gfloat) ch;
		side = (hex > 0) ? MIN ((gfloat) hex, along) : along * 0.5f;
		side_offset = (along - side) * 0.5f;
		if (axis == LRG_TILEMAP_STAGGER_AXIS_X)
		{
			pw = (gfloat) w * (side_offset + side) + side_offset;
			ph = (gfloat) h * (gfloat) ch + ((w > 1) ? (gfloat) ch * 0.5f : 0.0f);
		}
		else
		{
			pw = (gfloat) w * (gfloat) cw + ((h > 1) ? (gfloat) cw * 0.5f : 0.0f);
			ph = (gfloat) h * (side_offset + side) + side_offset;
		}
	}

	v3_set (min, 0.0f, -ph * s, -THIN_HALF_EXTENT);
	v3_set (max, pw * s, 0.0f, THIN_HALF_EXTENT);
	return TRUE;
}

gboolean
lrg_editor_node_get_local_bounds (LrgNode *node,
                                  gfloat  *min,
                                  gfloat  *max)
{
	LrgNodeVisual *visual;
	gfloat         hx = 0.5f, hy = 0.5f, hz = 0.5f;

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	visual = lrg_node_get_visual (node);
	if (visual == NULL)
		goto icon;

	switch (lrg_node_visual_get_kind (visual))
	{
	case LRG_NODE_VISUAL_PRIMITIVE:
		switch (lrg_node_visual_get_primitive (visual))
		{
		case LRG_PRIMITIVE_PLANE:
		case LRG_PRIMITIVE_CIRCLE:
		case LRG_PRIMITIVE_GRID:
			hy = THIN_HALF_EXTENT;
			break;
		case LRG_PRIMITIVE_TORUS:
			hy = 0.125f;
			break;
		case LRG_PRIMITIVE_RECTANGLE_2D:
		case LRG_PRIMITIVE_CIRCLE_2D:
			hz = THIN_HALF_EXTENT;
			break;
		default:
			break;
		}
		break;
	case LRG_NODE_VISUAL_SPRITE:
		hz = THIN_HALF_EXTENT;
		break;
	case LRG_NODE_VISUAL_TILEMAP:
		if (tilemap_local_bounds (visual, min, max))
			return TRUE;
		hz = THIN_HALF_EXTENT;
		break;
	case LRG_NODE_VISUAL_MESH_ASSET:
	case LRG_NODE_VISUAL_PREFAB_INSTANCE:
	case LRG_NODE_VISUAL_CAD_PART:
		break;
	default:
		goto icon;
	}

	v3_set (min, -hx, -hy, -hz);
	v3_set (max, hx, hy, hz);
	return TRUE;

icon:
	v3_set (min, -ICON_HALF_EXTENT, -ICON_HALF_EXTENT, -ICON_HALF_EXTENT);
	v3_set (max, ICON_HALF_EXTENT, ICON_HALF_EXTENT, ICON_HALF_EXTENT);
	return FALSE;
}

/* ==========================================================================
 * View
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgEditorView, lrg_editor_view,
                     lrg_editor_view_copy, lrg_editor_view_free)

void
lrg_editor_view_init_default (LrgEditorView *view,
                              gfloat         width,
                              gfloat         height)
{
	g_return_if_fail (view != NULL);

	memset (view, 0, sizeof *view);
	v3_set (view->position, 8.0f, 6.0f, 8.0f);
	v3_set (view->target, 0.0f, 0.0f, 0.0f);
	v3_set (view->up, 0.0f, 1.0f, 0.0f);
	view->fovy = 45.0f;
	view->ortho_height = 10.0f;
	view->orthographic = FALSE;
	view->near_plane = 0.05f;
	view->far_plane = 2000.0f;
	view->width = MAX (width, 1.0f);
	view->height = MAX (height, 1.0f);
}

LrgEditorView *
lrg_editor_view_copy (const LrgEditorView *view)
{
	g_return_val_if_fail (view != NULL, NULL);

	return g_memdup2 (view, sizeof *view);
}

void
lrg_editor_view_free (LrgEditorView *view)
{
	g_free (view);
}

/*
 * view_basis:
 *
 * Computes the orthonormal camera basis.  When the view direction is
 * (anti)parallel to the up vector (top/bottom views) the world -Z axis is
 * used as up instead, so a top view is "north up".
 */
static void
view_basis (const LrgEditorView *view,
            gfloat              *forward,
            gfloat              *right,
            gfloat              *up)
{
	gfloat world_up[3];

	v3_sub (view->target, view->position, forward);
	if (!v3_normalize (forward))
		v3_set (forward, 0.0f, 0.0f, -1.0f);

	memcpy (world_up, view->up, sizeof world_up);
	if (!v3_normalize (world_up))
		v3_set (world_up, 0.0f, 1.0f, 0.0f);

	v3_cross (forward, world_up, right);
	if (!v3_normalize (right))
	{
		/* Looking straight down: screen-up is -Z; straight up: +Z. */
		v3_set (world_up, 0.0f, 0.0f, forward[1] < 0.0f ? -1.0f : 1.0f);
		v3_cross (forward, world_up, right);
		v3_normalize (right);
	}
	v3_cross (right, forward, up);
}

void
lrg_editor_view_get_view_matrix (const LrgEditorView *view,
                                 gfloat              *out)
{
	g_return_if_fail (view != NULL);

	/* lrg_math3d_look_at() builds the same basis as view_basis(). */
	lrg_math3d_look_at (view->position, view->target, view->up, out);
}

static gfloat
view_aspect (const LrgEditorView *view)
{
	return (view->height > 0.0f) ? view->width / view->height : 1.0f;
}

void
lrg_editor_view_get_projection_matrix (const LrgEditorView *view,
                                       gfloat              *out)
{
	gfloat n, f, aspect;

	g_return_if_fail (view != NULL);

	/* Sanitise the clip range, then defer to the shared core helpers. */
	n = view->near_plane > 0.0f ? view->near_plane : 0.05f;
	f = view->far_plane > n ? view->far_plane : n + 1000.0f;
	aspect = view_aspect (view);

	if (view->orthographic)
	{
		gfloat top = view->ortho_height * 0.5f;
		gfloat right = top * aspect;

		lrg_math3d_ortho (-right, right, -top, top, n, f, out);
	}
	else
	{
		/* fovy is stored in degrees; the core helper takes radians. */
		lrg_math3d_perspective (view->fovy * (gfloat) G_PI / 180.0f,
		                        aspect, n, f, out);
	}
}

gboolean
lrg_editor_view_project (const LrgEditorView *view,
                         const gfloat        *world,
                         gfloat              *screen_x,
                         gfloat              *screen_y,
                         gfloat              *depth)
{
	gfloat f[3], r[3], u[3];
	gfloat rel[3];
	gfloat x, y, z;
	gfloat ndc_x, ndc_y;

	g_return_val_if_fail (view != NULL, FALSE);

	view_basis (view, f, r, u);
	v3_sub (world, view->position, rel);
	x = v3_dot (rel, r);
	y = v3_dot (rel, u);
	z = v3_dot (rel, f);

	if (depth != NULL)
		*depth = z;

	if (view->orthographic)
	{
		gfloat top = view->ortho_height * 0.5f;

		ndc_x = x / (top * view_aspect (view));
		ndc_y = y / top;
	}
	else
	{
		gfloat t = tanf (view->fovy * (gfloat) G_PI / 360.0f);

		if (z <= 1e-6f)
		{
			if (screen_x != NULL)
				*screen_x = -1.0f;
			if (screen_y != NULL)
				*screen_y = -1.0f;
			return FALSE;
		}
		ndc_x = x / (z * t * view_aspect (view));
		ndc_y = y / (z * t);
	}

	if (screen_x != NULL)
		*screen_x = (ndc_x + 1.0f) * 0.5f * view->width;
	if (screen_y != NULL)
		*screen_y = (1.0f - ndc_y) * 0.5f * view->height;

	return view->orthographic || z > 0.0f;
}

void
lrg_editor_view_get_ray (const LrgEditorView *view,
                         gfloat               screen_x,
                         gfloat               screen_y,
                         gfloat              *origin,
                         gfloat              *direction)
{
	gfloat f[3], r[3], u[3];
	gfloat ndc_x, ndc_y;

	g_return_if_fail (view != NULL);

	view_basis (view, f, r, u);
	ndc_x = (screen_x / view->width) * 2.0f - 1.0f;
	ndc_y = 1.0f - (screen_y / view->height) * 2.0f;

	if (view->orthographic)
	{
		gfloat top = view->ortho_height * 0.5f;

		memcpy (origin, view->position, 3 * sizeof (gfloat));
		v3_add_scaled (origin, r, ndc_x * top * view_aspect (view));
		v3_add_scaled (origin, u, ndc_y * top);
		memcpy (direction, f, 3 * sizeof (gfloat));
	}
	else
	{
		gfloat t = tanf (view->fovy * (gfloat) G_PI / 360.0f);

		memcpy (origin, view->position, 3 * sizeof (gfloat));
		memcpy (direction, f, 3 * sizeof (gfloat));
		v3_add_scaled (direction, r, ndc_x * t * view_aspect (view));
		v3_add_scaled (direction, u, ndc_y * t);
		v3_normalize (direction);
	}
}

gfloat
lrg_editor_view_pixel_world_size (const LrgEditorView *view,
                                  const gfloat        *world)
{
	gfloat depth = 1.0f;

	g_return_val_if_fail (view != NULL, 1.0f);

	if (view->orthographic)
		return view->ortho_height / view->height;

	lrg_editor_view_project (view, world, NULL, NULL, &depth);
	depth = MAX (depth, view->near_plane > 0.0f ? view->near_plane : 0.05f);

	return 2.0f * depth * tanf (view->fovy * (gfloat) G_PI / 360.0f) / view->height;
}

void
lrg_editor_view_orbit (LrgEditorView *view,
                       gfloat         yaw,
                       gfloat         pitch)
{
	gfloat offset[3];
	gfloat dist;
	gfloat cur_yaw, cur_pitch;
	gfloat limit;

	g_return_if_fail (view != NULL);

	v3_sub (view->position, view->target, offset);
	dist = v3_length (offset);
	if (dist < 1e-6f)
		return;

	/* Spherical coordinates around the target, Y up. */
	cur_yaw = atan2f (offset[0], offset[2]);
	cur_pitch = asinf (CLAMP (offset[1] / dist, -1.0f, 1.0f));

	limit = (gfloat) G_PI_2 - MIN_PITCH_COS;
	cur_yaw += yaw;
	cur_pitch = CLAMP (cur_pitch + pitch, -limit, limit);

	view->position[0] = view->target[0] + dist * cosf (cur_pitch) * sinf (cur_yaw);
	view->position[1] = view->target[1] + dist * sinf (cur_pitch);
	view->position[2] = view->target[2] + dist * cosf (cur_pitch) * cosf (cur_yaw);
	v3_set (view->up, 0.0f, 1.0f, 0.0f);
}

void
lrg_editor_view_pan (LrgEditorView *view,
                     gfloat         dx,
                     gfloat         dy)
{
	gfloat f[3], r[3], u[3];
	gfloat px;
	gfloat move[3] = { 0.0f, 0.0f, 0.0f };

	g_return_if_fail (view != NULL);

	view_basis (view, f, r, u);
	px = lrg_editor_view_pixel_world_size (view, view->target);

	/* Dragging right moves the world right, i.e. the camera left. */
	v3_add_scaled (move, r, -dx * px);
	v3_add_scaled (move, u, dy * px);
	v3_add_scaled (view->position, move, 1.0f);
	v3_add_scaled (view->target, move, 1.0f);
}

void
lrg_editor_view_dolly (LrgEditorView *view,
                       gfloat         factor)
{
	gfloat offset[3];
	gfloat dist;

	g_return_if_fail (view != NULL);

	if (factor <= 0.0f)
		return;

	if (view->orthographic)
	{
		view->ortho_height = CLAMP (view->ortho_height * factor, 0.01f, 100000.0f);
		return;
	}

	v3_sub (view->position, view->target, offset);
	dist = v3_length (offset);
	if (dist < 1e-6f)
		return;

	/* Never pass through the target. */
	factor = MAX (factor, 0.05f / dist);
	view->position[0] = view->target[0] + offset[0] * factor;
	view->position[1] = view->target[1] + offset[1] * factor;
	view->position[2] = view->target[2] + offset[2] * factor;
}

void
lrg_editor_view_fly (LrgEditorView *view,
                     gfloat         forward,
                     gfloat         right,
                     gfloat         up,
                     gfloat         yaw,
                     gfloat         pitch)
{
	gfloat       f[3], r[3], u[3];
	gfloat       dir[3];
	gfloat       dist;
	const gfloat world_up[3] = { 0.0f, 1.0f, 0.0f };

	g_return_if_fail (view != NULL);

	v3_sub (view->target, view->position, dir);
	dist = v3_length (dir);
	if (dist < 1e-6f)
		dist = 1.0f;

	view_basis (view, f, r, u);
	memcpy (dir, f, sizeof dir);

	/* Look: yaw about world Y, pitch about the camera right axis, with
	 * the same pitch limit as orbiting. */
	v3_rotate (dir, world_up, -yaw);
	v3_cross (dir, world_up, r);
	if (v3_normalize (r))
	{
		gfloat tmp[3];

		memcpy (tmp, dir, sizeof tmp);
		v3_rotate (tmp, r, -pitch);
		if (fabsf (tmp[1]) < 1.0f - MIN_PITCH_COS * 0.5f)
			memcpy (dir, tmp, sizeof dir);
	}
	v3_normalize (dir);
	v3_cross (dir, world_up, r);
	v3_normalize (r);

	/* Move. */
	v3_add_scaled (view->position, dir, forward);
	v3_add_scaled (view->position, r, right);
	v3_add_scaled (view->position, world_up, up);

	memcpy (view->target, view->position, sizeof view->target);
	v3_add_scaled (view->target, dir, dist);
	v3_set (view->up, 0.0f, 1.0f, 0.0f);
}

void
lrg_editor_view_frame (LrgEditorView *view,
                       const gfloat  *center,
                       gfloat         radius)
{
	gfloat back[3];
	gfloat dist;

	g_return_if_fail (view != NULL);
	g_return_if_fail (center != NULL);

	radius = MAX (radius, 0.1f);
	v3_sub (view->position, view->target, back);
	if (!v3_normalize (back))
		v3_set (back, 0.577f, 0.577f, 0.577f);

	if (view->orthographic)
	{
		view->ortho_height = radius * 2.4f;
		dist = MAX (radius * 4.0f, 10.0f);
	}
	else
	{
		gfloat half = view->fovy * (gfloat) G_PI / 360.0f;
		gfloat aspect = MIN (view_aspect (view), 1.0f);

		/* Fit the narrower of the two view extents, with a margin. */
		dist = radius / sinf (atanf (tanf (half) * aspect)) * 1.15f;
	}

	memcpy (view->target, center, sizeof view->target);
	memcpy (view->position, center, sizeof view->position);
	v3_add_scaled (view->position, back, dist);
}

void
lrg_editor_view_align (LrgEditorView *view,
                       const gfloat  *direction)
{
	gfloat offset[3];
	gfloat dir[3];
	gfloat dist;

	g_return_if_fail (view != NULL);
	g_return_if_fail (direction != NULL);

	memcpy (dir, direction, sizeof dir);
	if (!v3_normalize (dir))
		return;

	v3_sub (view->position, view->target, offset);
	dist = MAX (v3_length (offset), 0.1f);

	memcpy (view->position, view->target, sizeof view->position);
	v3_add_scaled (view->position, dir, dist);
	v3_set (view->up, 0.0f, 1.0f, 0.0f);
}

/* ==========================================================================
 * Intersections
 * ========================================================================== */

gboolean
lrg_editor_ray_aabb (const gfloat *origin,
                     const gfloat *direction,
                     const gfloat *min,
                     const gfloat *max,
                     gfloat       *t)
{
	gfloat tmin = -G_MAXFLOAT;
	gfloat tmax = G_MAXFLOAT;
	gint   i;

	/* Slab test. */
	for (i = 0; i < 3; i++)
	{
		if (fabsf (direction[i]) < 1e-12f)
		{
			if (origin[i] < min[i] || origin[i] > max[i])
				return FALSE;
		}
		else
		{
			gfloat inv = 1.0f / direction[i];
			gfloat t1 = (min[i] - origin[i]) * inv;
			gfloat t2 = (max[i] - origin[i]) * inv;

			if (t1 > t2)
			{
				gfloat tmp = t1;

				t1 = t2;
				t2 = tmp;
			}
			tmin = MAX (tmin, t1);
			tmax = MIN (tmax, t2);
			if (tmin > tmax)
				return FALSE;
		}
	}

	if (tmax < 0.0f)
		return FALSE;

	if (t != NULL)
		*t = MAX (tmin, 0.0f);
	return TRUE;
}

gboolean
lrg_editor_ray_plane (const gfloat *origin,
                      const gfloat *direction,
                      const gfloat *point,
                      const gfloat *normal,
                      gfloat       *t)
{
	gfloat denom = v3_dot (direction, normal);
	gfloat diff[3];
	gfloat hit;

	if (fabsf (denom) < 1e-9f)
		return FALSE;

	v3_sub (point, origin, diff);
	hit = v3_dot (diff, normal) / denom;
	if (hit < 0.0f)
		return FALSE;

	if (t != NULL)
		*t = hit;
	return TRUE;
}

typedef struct
{
	gchar           *path;
	gint64           mtime;
	LrgTerrainAsset *asset;
} TerrainPick;

static void
terrain_pick_free (gpointer data)
{
	TerrainPick *pick = data;

	g_free (pick->path);
	g_clear_object (&pick->asset);
	g_free (pick);
}

/* The .rterrain for a terrain node, or NULL when there is nothing to
 * raycast (not a terrain, no asset, or the file did not load). A loaded
 * asset is kept on the node and replaced when the file's mtime changes. */
static LrgTerrainAsset *
terrain_asset_for_node (LrgNode *node)
{
	LrgNodeVisual *visual;
	const gchar   *ref;
	const gchar   *level_path;
	g_autofree gchar *path = NULL;
	TerrainPick   *pick;
	GStatBuf       st;

	visual = lrg_node_get_visual (node);
	if (visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_TERRAIN)
		return NULL;
	ref = lrg_node_visual_get_asset (visual);
	if (ref == NULL || ref[0] == '\0')
		return NULL;

	if (g_path_is_absolute (ref))
		path = g_strdup (ref);
	else
	{
		g_autofree gchar *dir = NULL;

		level_path = lrg_node_get_level_path (node);
		if (level_path == NULL || level_path[0] == '\0')
			return NULL;
		dir = g_path_get_dirname (level_path);
		path = g_build_filename (dir, ref, NULL);
	}
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR) || g_stat (path, &st) != 0)
		return NULL;

	pick = g_object_get_data (G_OBJECT (node), "lrg-editor-terrain-pick");
	if (pick != NULL && g_strcmp0 (pick->path, path) == 0
	    && pick->mtime == (gint64) st.st_mtime && pick->asset != NULL)
		return pick->asset;

	{
		g_autoptr(GError) error = NULL;
		LrgTerrainAsset *asset = lrg_terrain_asset_new_from_file (path, &error);

		if (asset == NULL)
			return NULL;
		if (pick == NULL)
		{
			pick = g_new0 (TerrainPick, 1);
			g_object_set_data_full (G_OBJECT (node), "lrg-editor-terrain-pick",
			                        pick, terrain_pick_free);
		}
		else
		{
			g_free (pick->path);
			g_clear_object (&pick->asset);
		}
		pick->path = g_strdup (path);
		pick->mtime = (gint64) st.st_mtime;
		pick->asset = asset;
		return pick->asset;
	}
}

/* Heightfield hit in local ray units. *handled is TRUE when an asset
 * loaded, so a miss is a miss (the node box is not a stand-in). */
static gboolean
terrain_ray_t (LrgNode      *node,
               const gfloat *origin,
               const gfloat *direction,
               gfloat       *t,
               gboolean     *handled)
{
	LrgTerrainAsset *asset;
	gfloat           hit[3];
	gfloat           diff[3];
	gfloat           denom;

	*handled = FALSE;
	asset = terrain_asset_for_node (node);
	if (asset == NULL)
		return FALSE;

	*handled = TRUE;
	if (!lrg_terrain_asset_raycast (asset, origin, direction, 1.0e6f, hit))
		return FALSE;
	v3_sub (hit, origin, diff);
	denom = v3_dot (direction, direction);
	if (denom < 1e-12f)
		return FALSE;
	*t = v3_dot (diff, direction) / denom;
	return *t >= 0.0f;
}

gboolean
lrg_editor_ray_node (LrgNode      *node,
                     const gfloat *origin,
                     const gfloat *direction,
                     gfloat       *distance)
{
	gfloat world[16];
	gfloat inv[16];
	gfloat lo[3], ld[3];
	gfloat min[3], max[3];
	gfloat t;
	gfloat hit_local[3], hit_world[3], diff[3];

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	lrg_editor_node_get_world_matrix (node, world);
	if (!lrg_editor_math_invert (world, inv))
		return FALSE;

	/* Test in local space; t is then in local units, so convert the hit
	 * point back to measure the world distance. */
	lrg_editor_math_transform_point (inv, origin, lo);
	lrg_editor_math_transform_direction (inv, direction, ld);

	{
		gboolean handled = FALSE;

		if (terrain_ray_t (node, lo, ld, &t, &handled))
			goto have_hit;
		if (handled)
			return FALSE;
	}

	lrg_editor_node_get_local_bounds (node, min, max);

	if (!lrg_editor_ray_aabb (lo, ld, min, max, &t))
		return FALSE;

have_hit:

	if (distance != NULL)
	{
		memcpy (hit_local, lo, sizeof hit_local);
		v3_add_scaled (hit_local, ld, t);
		lrg_editor_math_transform_point (world, hit_local, hit_world);
		v3_sub (hit_world, origin, diff);
		*distance = v3_length (diff);
	}

	return TRUE;
}

typedef struct
{
	LrgNode *node;
	gfloat   distance;
	gboolean geometry;
} PickHit;

static gint
compare_hits (gconstpointer a,
              gconstpointer b)
{
	const PickHit *ha = a;
	const PickHit *hb = b;

	/* Nearest first; on a near tie (1mm), real geometry wins over icons. */
	if (fabsf (ha->distance - hb->distance) < 1e-3f && ha->geometry != hb->geometry)
		return ha->geometry ? -1 : 1;
	if (ha->distance < hb->distance)
		return -1;
	if (ha->distance > hb->distance)
		return 1;
	return 0;
}

static void
pick_recursive (LrgNode      *node,
                const gfloat *origin,
                const gfloat *direction,
                gboolean      include_locked,
                GArray       *hits)
{
	GPtrArray *children;
	guint      i;
	gfloat     d;

	/* An invisible node hides its whole subtree. */
	if (!lrg_node_get_visible (node))
		return;

	if ((include_locked || !lrg_node_get_locked (node)) &&
	    lrg_editor_ray_node (node, origin, direction, &d))
	{
		gfloat  min[3], max[3];
		PickHit hit;

		hit.node = node;
		hit.distance = d;
		hit.geometry = lrg_editor_node_get_local_bounds (node, min, max);
		g_array_append_val (hits, hit);
	}

	children = lrg_node_get_children (node);
	for (i = 0; i < children->len; i++)
		pick_recursive (g_ptr_array_index (children, i), origin, direction,
		                include_locked, hits);
}

static GArray *
collect_hits (LrgLevel     *level,
              const gfloat *origin,
              const gfloat *direction,
              gboolean      include_locked)
{
	GArray    *hits = g_array_new (FALSE, FALSE, sizeof (PickHit));
	GPtrArray *top = lrg_node_get_children (lrg_level_get_root (level));
	guint      i;

	for (i = 0; i < top->len; i++)
		pick_recursive (g_ptr_array_index (top, i), origin, direction,
		                include_locked, hits);

	g_array_sort (hits, compare_hits);
	return hits;
}

LrgNode *
lrg_editor_pick (LrgLevel     *level,
                 const gfloat *origin,
                 const gfloat *direction,
                 gboolean      include_locked,
                 gfloat       *distance)
{
	GArray  *hits;
	LrgNode *node = NULL;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	hits = collect_hits (level, origin, direction, include_locked);
	if (hits->len > 0)
	{
		PickHit *best = &g_array_index (hits, PickHit, 0);

		node = best->node;
		if (distance != NULL)
			*distance = best->distance;
	}
	g_array_unref (hits);

	return node;
}

GPtrArray *
lrg_editor_pick_all (LrgLevel     *level,
                     const gfloat *origin,
                     const gfloat *direction,
                     gboolean      include_locked)
{
	GArray    *hits;
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	hits = collect_hits (level, origin, direction, include_locked);
	out = g_ptr_array_sized_new (hits->len);
	for (i = 0; i < hits->len; i++)
		g_ptr_array_add (out, g_array_index (hits, PickHit, i).node);
	g_array_unref (hits);

	return out;
}

typedef struct
{
	const LrgEditorView *view;
	gfloat               x0, y0, x1, y1;
	gboolean             include_locked;
	GPtrArray           *out;
} BoxData;

static void
box_recursive (LrgNode *node,
               BoxData *data)
{
	GPtrArray *children;
	gfloat     pos[3];
	gfloat     sx, sy;
	guint      i;

	if (!lrg_node_get_visible (node))
		return;

	lrg_editor_node_get_world_position (node, pos);
	if ((data->include_locked || !lrg_node_get_locked (node)) &&
	    lrg_editor_view_project (data->view, pos, &sx, &sy, NULL) &&
	    sx >= data->x0 && sx <= data->x1 && sy >= data->y0 && sy <= data->y1)
		g_ptr_array_add (data->out, node);

	children = lrg_node_get_children (node);
	for (i = 0; i < children->len; i++)
		box_recursive (g_ptr_array_index (children, i), data);
}

GPtrArray *
lrg_editor_box_select (LrgLevel            *level,
                       const LrgEditorView *view,
                       gfloat               x0,
                       gfloat               y0,
                       gfloat               x1,
                       gfloat               y1,
                       gboolean             include_locked)
{
	BoxData    data;
	GPtrArray *top;
	guint      i;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);
	g_return_val_if_fail (view != NULL, NULL);

	data.view = view;
	data.x0 = MIN (x0, x1);
	data.x1 = MAX (x0, x1);
	data.y0 = MIN (y0, y1);
	data.y1 = MAX (y0, y1);
	data.include_locked = include_locked;
	data.out = g_ptr_array_new ();

	top = lrg_node_get_children (lrg_level_get_root (level));
	for (i = 0; i < top->len; i++)
		box_recursive (g_ptr_array_index (top, i), &data);

	return data.out;
}

typedef struct
{
	gfloat   min[3];
	gfloat   max[3];
	gboolean any;
} BoundsData;

static gboolean
bounds_visit (LrgNode  *node,
              gpointer  user_data)
{
	BoundsData *b = user_data;
	gfloat      world[16];
	gfloat      lmin[3], lmax[3];
	gint        corner, i;

	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_node_get_local_bounds (node, lmin, lmax);

	for (corner = 0; corner < 8; corner++)
	{
		gfloat p[3];
		gfloat w[3];

		p[0] = (corner & 1) ? lmax[0] : lmin[0];
		p[1] = (corner & 2) ? lmax[1] : lmin[1];
		p[2] = (corner & 4) ? lmax[2] : lmin[2];
		lrg_editor_math_transform_point (world, p, w);

		for (i = 0; i < 3; i++)
		{
			if (!b->any || w[i] < b->min[i])
				b->min[i] = w[i];
			if (!b->any || w[i] > b->max[i])
				b->max[i] = w[i];
		}
		b->any = TRUE;
	}

	return TRUE;
}

gboolean
lrg_editor_compute_bounds (GPtrArray *nodes,
                           gfloat    *center,
                           gfloat    *radius)
{
	BoundsData b;
	gfloat     half[3];
	guint      i;

	g_return_val_if_fail (nodes != NULL, FALSE);

	memset (&b, 0, sizeof b);
	for (i = 0; i < nodes->len; i++)
		lrg_node_foreach (g_ptr_array_index (nodes, i), bounds_visit, &b);

	if (!b.any)
		return FALSE;

	for (i = 0; i < 3; i++)
	{
		center[i] = (b.min[i] + b.max[i]) * 0.5f;
		half[i] = (b.max[i] - b.min[i]) * 0.5f;
	}
	*radius = MAX (v3_length (half), 0.5f);

	return TRUE;
}

gfloat
lrg_editor_snap_value (gfloat value,
                       gfloat increment)
{
	if (increment <= 0.0f)
		return value;

	return roundf (value / increment) * increment;
}
