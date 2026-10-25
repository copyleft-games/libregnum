/* lrg-editor-math.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless editor math. See lrg-editor-math.h for the conventions.
 */

#include "lrg-editor-math.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "../lrg-enums.h"
#include <graylib.h>
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
 * ========================================================================== */

void
lrg_editor_math_identity (gfloat *out)
{
	memset (out, 0, 16 * sizeof (gfloat));
	out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void
lrg_editor_math_compose (const gfloat *location,
                         const gfloat *rotation,
                         const gfloat *scale,
                         gfloat       *out)
{
	gfloat ca = cosf (rotation[0]), sa = sinf (rotation[0]);
	gfloat cb = cosf (rotation[1]), sb = sinf (rotation[1]);
	gfloat cc = cosf (rotation[2]), sc = sinf (rotation[2]);
	gfloat r[3][3];
	gint   row, col;

	/* R = Rz(c) * Ry(b) * Rx(a) */
	r[0][0] = cb * cc;
	r[0][1] = sa * sb * cc - ca * sc;
	r[0][2] = ca * sb * cc + sa * sc;
	r[1][0] = cb * sc;
	r[1][1] = sa * sb * sc + ca * cc;
	r[1][2] = ca * sb * sc - sa * cc;
	r[2][0] = -sb;
	r[2][1] = sa * cb;
	r[2][2] = ca * cb;

	/* Columns of R scaled by S, then the translation column. */
	for (col = 0; col < 3; col++)
	{
		for (row = 0; row < 3; row++)
			M (out, row, col) = r[row][col] * scale[col];
		M (out, 3, col) = 0.0f;
	}
	M (out, 0, 3) = location[0];
	M (out, 1, 3) = location[1];
	M (out, 2, 3) = location[2];
	M (out, 3, 3) = 1.0f;
}

gboolean
lrg_editor_math_decompose (const gfloat *m,
                           gfloat       *location,
                           gfloat       *rotation,
                           gfloat       *scale)
{
	gfloat r[3][3];
	gfloat det;
	gint   row, col;

	location[0] = M (m, 0, 3);
	location[1] = M (m, 1, 3);
	location[2] = M (m, 2, 3);

	/* Scale is the length of each basis column. */
	for (col = 0; col < 3; col++)
	{
		gfloat c[3] = { M (m, 0, col), M (m, 1, col), M (m, 2, col) };

		scale[col] = v3_length (c);
		if (scale[col] < 1e-12f)
		{
			v3_set (rotation, 0.0f, 0.0f, 0.0f);
			return FALSE;
		}
	}

	/* A mirrored basis is represented as a negative X scale. */
	det = M (m, 0, 0) * (M (m, 1, 1) * M (m, 2, 2) - M (m, 2, 1) * M (m, 1, 2)) -
	      M (m, 0, 1) * (M (m, 1, 0) * M (m, 2, 2) - M (m, 2, 0) * M (m, 1, 2)) +
	      M (m, 0, 2) * (M (m, 1, 0) * M (m, 2, 1) - M (m, 2, 0) * M (m, 1, 1));
	if (det < 0.0f)
		scale[0] = -scale[0];

	for (col = 0; col < 3; col++)
		for (row = 0; row < 3; row++)
			r[row][col] = M (m, row, col) / scale[col];

	/* Invert R = Rz * Ry * Rx (see compose). */
	if (r[2][0] < 0.9999f && r[2][0] > -0.9999f)
	{
		rotation[1] = asinf (-r[2][0]);
		rotation[0] = atan2f (r[2][1], r[2][2]);
		rotation[2] = atan2f (r[1][0], r[0][0]);
	}
	else
	{
		/* Gimbal lock: fold Z into X. */
		rotation[1] = (r[2][0] <= -0.9999f) ? (gfloat) G_PI_2 : (gfloat) -G_PI_2;
		rotation[0] = atan2f (-r[1][2], r[1][1]);
		rotation[2] = 0.0f;
	}

	return TRUE;
}

void
lrg_editor_math_multiply (const gfloat *a,
                          const gfloat *b,
                          gfloat       *out)
{
	gfloat tmp[16];
	gint   row, col, k;

	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++)
		{
			gfloat sum = 0.0f;

			for (k = 0; k < 4; k++)
				sum += M (a, row, k) * M (b, k, col);
			M (tmp, row, col) = sum;
		}

	memcpy (out, tmp, sizeof tmp);
}

gboolean
lrg_editor_math_invert (const gfloat *m,
                        gfloat       *out)
{
	gfloat inv[16];
	gfloat det;
	gint   i;

	/* Cofactor expansion (MESA gluInvertMatrix), layout-agnostic. */
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
	         m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
	         m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
	         m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
	          m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
	         m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
	         m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
	         m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
	          m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
	         m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
	         m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
	          m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
	          m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
	         m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
	         m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
	          m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
	          m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (fabsf (det) < 1e-20f)
	{
		lrg_editor_math_identity (out);
		return FALSE;
	}

	det = 1.0f / det;
	for (i = 0; i < 16; i++)
		out[i] = inv[i] * det;

	return TRUE;
}

void
lrg_editor_math_transform_point (const gfloat *m,
                                 const gfloat *p,
                                 gfloat       *out)
{
	gfloat x = M (m, 0, 0) * p[0] + M (m, 0, 1) * p[1] + M (m, 0, 2) * p[2] + M (m, 0, 3);
	gfloat y = M (m, 1, 0) * p[0] + M (m, 1, 1) * p[1] + M (m, 1, 2) * p[2] + M (m, 1, 3);
	gfloat z = M (m, 2, 0) * p[0] + M (m, 2, 1) * p[1] + M (m, 2, 2) * p[2] + M (m, 2, 3);
	gfloat w = M (m, 3, 0) * p[0] + M (m, 3, 1) * p[1] + M (m, 3, 2) * p[2] + M (m, 3, 3);

	if (w != 0.0f && w != 1.0f)
	{
		x /= w; y /= w; z /= w;
	}
	v3_set (out, x, y, z);
}

void
lrg_editor_math_transform_direction (const gfloat *m,
                                     const gfloat *d,
                                     gfloat       *out)
{
	gfloat x = M (m, 0, 0) * d[0] + M (m, 0, 1) * d[1] + M (m, 0, 2) * d[2];
	gfloat y = M (m, 1, 0) * d[0] + M (m, 1, 1) * d[1] + M (m, 1, 2) * d[2];
	gfloat z = M (m, 2, 0) * d[0] + M (m, 2, 1) * d[1] + M (m, 2, 2) * d[2];

	v3_set (out, x, y, z);
}

/* ==========================================================================
 * Node transforms
 * ========================================================================== */

void
lrg_editor_node_get_local_matrix (LrgNode *node,
                                  gfloat  *out)
{
	GrlVector3 *l, *r, *s;
	gfloat      loc[3], rot[3], scl[3];

	g_return_if_fail (LRG_IS_NODE (node));

	l = lrg_node_get_location (node);
	r = lrg_node_get_rotation (node);
	s = lrg_node_get_scale (node);
	v3_set (loc, l->x, l->y, l->z);
	v3_set (rot, r->x, r->y, r->z);
	v3_set (scl, s->x, s->y, s->z);

	lrg_editor_math_compose (loc, rot, scl, out);
}

void
lrg_editor_node_get_world_matrix (LrgNode *node,
                                  gfloat  *out)
{
	gfloat   local[16];
	LrgNode *p;

	g_return_if_fail (LRG_IS_NODE (node));

	lrg_editor_node_get_local_matrix (node, out);
	for (p = lrg_node_get_parent (node); p != NULL; p = lrg_node_get_parent (p))
	{
		lrg_editor_node_get_local_matrix (p, local);
		lrg_editor_math_multiply (local, out, out);
	}
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
	case LRG_NODE_VISUAL_TILEMAP:
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
	gfloat f[3], r[3], u[3];

	g_return_if_fail (view != NULL);

	view_basis (view, f, r, u);
	lrg_editor_math_identity (out);
	M (out, 0, 0) = r[0]; M (out, 0, 1) = r[1]; M (out, 0, 2) = r[2];
	M (out, 1, 0) = u[0]; M (out, 1, 1) = u[1]; M (out, 1, 2) = u[2];
	M (out, 2, 0) = -f[0]; M (out, 2, 1) = -f[1]; M (out, 2, 2) = -f[2];
	M (out, 0, 3) = -v3_dot (r, view->position);
	M (out, 1, 3) = -v3_dot (u, view->position);
	M (out, 2, 3) = v3_dot (f, view->position);
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

	n = view->near_plane > 0.0f ? view->near_plane : 0.05f;
	f = view->far_plane > n ? view->far_plane : n + 1000.0f;
	aspect = view_aspect (view);

	memset (out, 0, 16 * sizeof (gfloat));
	if (view->orthographic)
	{
		gfloat top = view->ortho_height * 0.5f;
		gfloat right = top * aspect;

		M (out, 0, 0) = 1.0f / right;
		M (out, 1, 1) = 1.0f / top;
		M (out, 2, 2) = -2.0f / (f - n);
		M (out, 2, 3) = -(f + n) / (f - n);
		M (out, 3, 3) = 1.0f;
	}
	else
	{
		gfloat t = 1.0f / tanf (view->fovy * (gfloat) G_PI / 360.0f);

		M (out, 0, 0) = t / aspect;
		M (out, 1, 1) = t;
		M (out, 2, 2) = -(f + n) / (f - n);
		M (out, 2, 3) = -(2.0f * f * n) / (f - n);
		M (out, 3, 2) = -1.0f;
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
	lrg_editor_node_get_local_bounds (node, min, max);

	if (!lrg_editor_ray_aabb (lo, ld, min, max, &t))
		return FALSE;

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
