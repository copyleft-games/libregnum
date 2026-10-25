/* lrg-editor-gizmo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless transform gizmo controller. See lrg-editor-gizmo.h.
 *
 * Drag model: when a drag starts, the pivot, the gizmo axes and every
 * selected node's local TRS and world matrix are frozen.  Each drag update
 * recomputes the transforms from those frozen values (never incrementally),
 * so there is no accumulated drift and cancelling is exact.
 */

#include "lrg-editor-gizmo.h"
#include "lrg-editor.h"
#include "lrg-editor-command.h"
#include "lrg-editor-selection.h"
#include "lrg-node.h"
#include <graylib.h>
#include <math.h>
#include <string.h>

#define RING_SEGMENTS      (48)
#define CENTER_PICK_PIXELS (10.0f)
#define VIEW_RING_FACTOR   (1.2f)
#define PLANE_NEAR         (0.2f)
#define PLANE_FAR          (0.45f)
#define MIN_AXIS_PIXELS    (6.0f)

typedef struct
{
	LrgNode *node;      /* ref */
	gfloat   trs0[9];   /* local loc, rot, scale at drag start */
	gfloat   world0[16];
	gfloat   parent_inv[16];
} DragEntry;

struct _LrgEditorGizmo
{
	GObject parent_instance;

	LrgEditor            *editor;   /* ref */

	LrgEditorGizmoHandle  hovered;
	LrgEditorGizmoHandle  active;
	gboolean              dragging;
	LrgEditorTool         drag_tool;

	GArray               *entries;  /* DragEntry */
	gfloat                pivot0[3];
	gfloat                axes0[3][3];
	gfloat                view_axis0[3];

	/* Drag start measurements. */
	gfloat                mouse0[2];
	gfloat                param0;     /* axis translate/scale parameter */
	gfloat                hit0[3];    /* plane hit point */
	gfloat                angle_prev;
	gfloat                angle_total;
	gboolean              screen_rotate; /* edge-on ring fallback */

	gfloat                delta[3];
};

G_DEFINE_FINAL_TYPE (LrgEditorGizmo, lrg_editor_gizmo, G_TYPE_OBJECT)

/* ==========================================================================
 * Vector helpers
 * ========================================================================== */

static inline gfloat
dot3 (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
cross3 (const gfloat *a, const gfloat *b, gfloat *o)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	o[0] = x; o[1] = y; o[2] = z;
}

static inline void
normalize3 (gfloat *a)
{
	gfloat len = sqrtf (dot3 (a, a));

	if (len > 1e-12f)
	{
		a[0] /= len; a[1] /= len; a[2] /= len;
	}
}

static gfloat
point_segment_distance (gfloat px, gfloat py,
                        gfloat ax, gfloat ay,
                        gfloat bx, gfloat by)
{
	gfloat dx = bx - ax;
	gfloat dy = by - ay;
	gfloat len2 = dx * dx + dy * dy;
	gfloat t = 0.0f;
	gfloat cx, cy;

	if (len2 > 1e-12f)
		t = CLAMP (((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f);

	cx = ax + t * dx - px;
	cy = ay + t * dy - py;
	return sqrtf (cx * cx + cy * cy);
}

/* Point in convex quad (screen space), any winding. */
static gboolean
point_in_quad (gfloat px, gfloat py, const gfloat q[4][2])
{
	gint i;
	gint sign = 0;

	for (i = 0; i < 4; i++)
	{
		const gfloat *a = q[i];
		const gfloat *b = q[(i + 1) % 4];
		gfloat        c = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
		gint          s = (c > 0.0f) ? 1 : (c < 0.0f ? -1 : 0);

		if (s == 0)
			continue;
		if (sign == 0)
			sign = s;
		else if (s != sign)
			return FALSE;
	}

	return TRUE;
}

/* Axis-angle rotation matrix (column-major). */
static void
rotation_matrix (const gfloat *axis,
                 gfloat        angle,
                 gfloat       *out)
{
	gfloat c = cosf (angle), s = sinf (angle), t = 1.0f - c;
	gfloat x = axis[0], y = axis[1], z = axis[2];

	lrg_editor_math_identity (out);
	out[0] = t * x * x + c;     out[4] = t * x * y - s * z; out[8]  = t * x * z + s * y;
	out[1] = t * x * y + s * z; out[5] = t * y * y + c;     out[9]  = t * y * z - s * x;
	out[2] = t * x * z - s * y; out[6] = t * y * z + s * x; out[10] = t * z * z + c;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_entries (LrgEditorGizmo *self)
{
	guint i;

	for (i = 0; i < self->entries->len; i++)
		g_object_unref (g_array_index (self->entries, DragEntry, i).node);
	g_array_set_size (self->entries, 0);
}

static void
lrg_editor_gizmo_finalize (GObject *object)
{
	LrgEditorGizmo *self = LRG_EDITOR_GIZMO (object);

	clear_entries (self);
	g_array_unref (self->entries);
	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_gizmo_parent_class)->finalize (object);
}

static void
lrg_editor_gizmo_class_init (LrgEditorGizmoClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_gizmo_finalize;
}

static void
lrg_editor_gizmo_init (LrgEditorGizmo *self)
{
	self->entries = g_array_new (FALSE, TRUE, sizeof (DragEntry));
}

LrgEditorGizmo *
lrg_editor_gizmo_new (LrgEditor *editor)
{
	LrgEditorGizmo *self;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_GIZMO, NULL);
	self->editor = g_object_ref (editor);
	return self;
}

/* ==========================================================================
 * Frame
 * ========================================================================== */

static LrgNode *
primary_node (LrgEditorGizmo *self)
{
	return lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
}

gboolean
lrg_editor_gizmo_is_visible (LrgEditorGizmo *self)
{
	LrgNode *primary;

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), FALSE);

	if (self->dragging)
		return TRUE;
	if (lrg_editor_get_tool (self->editor) == LRG_EDITOR_TOOL_SELECT)
		return FALSE;

	primary = primary_node (self);
	return primary != NULL && !lrg_node_get_locked (primary);
}

void
lrg_editor_gizmo_get_origin (LrgEditorGizmo *self,
                             gfloat         *out)
{
	LrgNode *primary;

	g_return_if_fail (LRG_IS_EDITOR_GIZMO (self));

	/* During a drag the gizmo follows the primary node. */
	primary = primary_node (self);
	if (primary == NULL)
	{
		if (self->dragging)
			memcpy (out, self->pivot0, 3 * sizeof (gfloat));
		else
			out[0] = out[1] = out[2] = 0.0f;
		return;
	}

	lrg_editor_node_get_world_position (primary, out);
}

static LrgEditorTool
current_tool (LrgEditorGizmo *self)
{
	return self->dragging ? self->drag_tool : lrg_editor_get_tool (self->editor);
}

void
lrg_editor_gizmo_get_axis (LrgEditorGizmo *self,
                           guint           index,
                           gfloat         *out)
{
	LrgNode *primary;
	gfloat   world[16];

	g_return_if_fail (LRG_IS_EDITOR_GIZMO (self));
	g_return_if_fail (index < 3);

	out[0] = out[1] = out[2] = 0.0f;
	out[index] = 1.0f;

	primary = primary_node (self);
	if (primary == NULL)
		return;

	if (lrg_editor_get_space (self->editor) == LRG_EDITOR_SPACE_WORLD &&
	    current_tool (self) != LRG_EDITOR_TOOL_SCALE)
		return;

	/* Local axes are the primary node's world basis columns. */
	lrg_editor_node_get_world_matrix (primary, world);
	out[0] = world[index * 4 + 0];
	out[1] = world[index * 4 + 1];
	out[2] = world[index * 4 + 2];
	normalize3 (out);
	if (dot3 (out, out) < 0.5f)
	{
		out[0] = out[1] = out[2] = 0.0f;
		out[index] = 1.0f;
	}
}

gfloat
lrg_editor_gizmo_get_size (LrgEditorGizmo      *self,
                           const LrgEditorView *view)
{
	gfloat origin[3];

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), 1.0f);
	g_return_val_if_fail (view != NULL, 1.0f);

	lrg_editor_gizmo_get_origin (self, origin);
	return lrg_editor_view_pixel_world_size (view, origin) * LRG_EDITOR_GIZMO_SIZE_PIXELS;
}

/* ==========================================================================
 * Hit testing
 * ========================================================================== */

static void
view_forward (const LrgEditorView *view,
              gfloat              *out)
{
	out[0] = view->target[0] - view->position[0];
	out[1] = view->target[1] - view->position[1];
	out[2] = view->target[2] - view->position[2];
	normalize3 (out);
}

/* Distance in pixels from (x, y) to the projected ring around @axis. */
static gfloat
ring_distance (const LrgEditorView *view,
               const gfloat        *origin,
               const gfloat        *axis,
               gfloat               radius,
               gfloat               x,
               gfloat               y)
{
	gfloat u[3], v[3];
	gfloat helper[3] = { 0.0f, 1.0f, 0.0f };
	gfloat best = G_MAXFLOAT;
	gfloat prev[2] = { 0.0f, 0.0f };
	gboolean have_prev = FALSE;
	gint   i;

	/* Orthonormal basis (u, v) spanning the ring plane. */
	if (fabsf (axis[1]) > 0.9f)
	{
		helper[0] = 1.0f;
		helper[1] = 0.0f;
	}
	cross3 (axis, helper, u);
	normalize3 (u);
	cross3 (axis, u, v);

	for (i = 0; i <= RING_SEGMENTS; i++)
	{
		gfloat a = (gfloat) i / RING_SEGMENTS * 2.0f * (gfloat) G_PI;
		gfloat p[3];
		gfloat sx, sy;
		gint   k;

		for (k = 0; k < 3; k++)
			p[k] = origin[k] + (u[k] * cosf (a) + v[k] * sinf (a)) * radius;

		if (!lrg_editor_view_project (view, p, &sx, &sy, NULL))
		{
			have_prev = FALSE;
			continue;
		}
		if (have_prev)
			best = MIN (best, point_segment_distance (x, y, prev[0], prev[1], sx, sy));
		prev[0] = sx;
		prev[1] = sy;
		have_prev = TRUE;
	}

	return best;
}

LrgEditorGizmoHandle
lrg_editor_gizmo_hit_test (LrgEditorGizmo      *self,
                           const LrgEditorView *view,
                           gfloat               x,
                           gfloat               y)
{
	LrgEditorTool        tool;
	gfloat               origin[3];
	gfloat               axes[3][3];
	gfloat               size;
	gfloat               ox, oy;
	gfloat               best = LRG_EDITOR_GIZMO_PICK_PIXELS;
	LrgEditorGizmoHandle best_handle = LRG_EDITOR_GIZMO_HANDLE_NONE;
	gint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), LRG_EDITOR_GIZMO_HANDLE_NONE);
	g_return_val_if_fail (view != NULL, LRG_EDITOR_GIZMO_HANDLE_NONE);

	if (!lrg_editor_gizmo_is_visible (self))
		return LRG_EDITOR_GIZMO_HANDLE_NONE;

	tool = current_tool (self);
	lrg_editor_gizmo_get_origin (self, origin);
	for (i = 0; i < 3; i++)
		lrg_editor_gizmo_get_axis (self, i, axes[i]);
	size = lrg_editor_gizmo_get_size (self, view);

	if (!lrg_editor_view_project (view, origin, &ox, &oy, NULL))
		return LRG_EDITOR_GIZMO_HANDLE_NONE;

	if (tool == LRG_EDITOR_TOOL_ROTATE)
	{
		gfloat r;

		for (i = 0; i < 3; i++)
		{
			gfloat d = ring_distance (view, origin, axes[i], size, x, y);

			if (d < best)
			{
				best = d;
				best_handle = LRG_EDITOR_GIZMO_HANDLE_X + i;
			}
		}

		/* The outer view-aligned ring is a screen-space circle. */
		r = LRG_EDITOR_GIZMO_SIZE_PIXELS * VIEW_RING_FACTOR;
		if (fabsf (sqrtf ((x - ox) * (x - ox) + (y - oy) * (y - oy)) - r) < best)
			best_handle = LRG_EDITOR_GIZMO_HANDLE_CENTER;

		return best_handle;
	}

	/* Centre handle wins when the cursor is on the pivot. */
	if (sqrtf ((x - ox) * (x - ox) + (y - oy) * (y - oy)) <= CENTER_PICK_PIXELS)
		return LRG_EDITOR_GIZMO_HANDLE_CENTER;

	/* Plane squares (translate only) take priority over the arms. */
	if (tool == LRG_EDITOR_TOOL_TRANSLATE)
	{
		static const gint pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
		static const LrgEditorGizmoHandle plane_handles[3] = {
			LRG_EDITOR_GIZMO_HANDLE_XY,
			LRG_EDITOR_GIZMO_HANDLE_YZ,
			LRG_EDITOR_GIZMO_HANDLE_XZ
		};

		for (i = 0; i < 3; i++)
		{
			const gfloat *a = axes[pairs[i][0]];
			const gfloat *b = axes[pairs[i][1]];
			gfloat        quad[4][2];
			const gfloat  ca[4] = { PLANE_NEAR, PLANE_FAR, PLANE_FAR, PLANE_NEAR };
			const gfloat  cb[4] = { PLANE_NEAR, PLANE_NEAR, PLANE_FAR, PLANE_FAR };
			gboolean      ok = TRUE;
			gint          c, k;

			for (c = 0; c < 4 && ok; c++)
			{
				gfloat p[3];

				for (k = 0; k < 3; k++)
					p[k] = origin[k] + (a[k] * ca[c] + b[k] * cb[c]) * size;
				ok = lrg_editor_view_project (view, p, &quad[c][0], &quad[c][1], NULL);
			}

			if (ok && point_in_quad (x, y, (const gfloat (*)[2]) quad))
				return plane_handles[i];
		}
	}

	/* Arms. */
	for (i = 0; i < 3; i++)
	{
		gfloat end[3];
		gfloat ex, ey;
		gfloat d;
		gint   k;

		for (k = 0; k < 3; k++)
			end[k] = origin[k] + axes[i][k] * size;
		if (!lrg_editor_view_project (view, end, &ex, &ey, NULL))
			continue;

		/* An arm pointing at the camera is not grabbable. */
		if (sqrtf ((ex - ox) * (ex - ox) + (ey - oy) * (ey - oy)) < MIN_AXIS_PIXELS)
			continue;

		d = point_segment_distance (x, y, ox, oy, ex, ey);
		if (d < best)
		{
			best = d;
			best_handle = LRG_EDITOR_GIZMO_HANDLE_X + i;
		}
	}

	return best_handle;
}

LrgEditorGizmoHandle
lrg_editor_gizmo_update_hover (LrgEditorGizmo      *self,
                               const LrgEditorView *view,
                               gfloat               x,
                               gfloat               y)
{
	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), LRG_EDITOR_GIZMO_HANDLE_NONE);

	if (!self->dragging)
		self->hovered = lrg_editor_gizmo_hit_test (self, view, x, y);

	return self->hovered;
}

LrgEditorGizmoHandle
lrg_editor_gizmo_get_hovered (LrgEditorGizmo *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), LRG_EDITOR_GIZMO_HANDLE_NONE);

	return self->hovered;
}

LrgEditorGizmoHandle
lrg_editor_gizmo_get_active (LrgEditorGizmo *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), LRG_EDITOR_GIZMO_HANDLE_NONE);

	return self->dragging ? self->active : LRG_EDITOR_GIZMO_HANDLE_NONE;
}

gboolean
lrg_editor_gizmo_is_dragging (LrgEditorGizmo *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), FALSE);

	return self->dragging;
}

/* ==========================================================================
 * Drag measurement
 * ========================================================================== */

/* Parameter along the line (p + s * a) closest to the mouse ray. */
static gboolean
axis_param (const LrgEditorView *view,
            const gfloat        *p,
            const gfloat        *a,
            gfloat               x,
            gfloat               y,
            gfloat              *s)
{
	gfloat o[3], d[3], w[3];
	gfloat A, B, C, D, E, denom;

	lrg_editor_view_get_ray (view, x, y, o, d);
	w[0] = p[0] - o[0]; w[1] = p[1] - o[1]; w[2] = p[2] - o[2];

	A = dot3 (a, a);
	B = dot3 (a, d);
	C = dot3 (d, d);
	D = dot3 (a, w);
	E = dot3 (d, w);
	denom = A * C - B * B;
	if (fabsf (denom) < 1e-8f)
		return FALSE;

	*s = (B * E - C * D) / denom;
	return TRUE;
}

static gboolean
plane_hit (const LrgEditorView *view,
           const gfloat        *point,
           const gfloat        *normal,
           gfloat               x,
           gfloat               y,
           gfloat              *hit)
{
	gfloat o[3], d[3];
	gfloat t;

	lrg_editor_view_get_ray (view, x, y, o, d);
	if (!lrg_editor_ray_plane (o, d, point, normal, &t))
		return FALSE;

	hit[0] = o[0] + d[0] * t;
	hit[1] = o[1] + d[1] * t;
	hit[2] = o[2] + d[2] * t;
	return TRUE;
}

/* The plane normal used to drag @handle, from the frozen axes. */
static void
handle_plane_normal (LrgEditorGizmo       *self,
                     LrgEditorGizmoHandle  handle,
                     gfloat               *out)
{
	switch (handle)
	{
	case LRG_EDITOR_GIZMO_HANDLE_XY:
		memcpy (out, self->axes0[2], 3 * sizeof (gfloat));
		break;
	case LRG_EDITOR_GIZMO_HANDLE_YZ:
		memcpy (out, self->axes0[0], 3 * sizeof (gfloat));
		break;
	case LRG_EDITOR_GIZMO_HANDLE_XZ:
		memcpy (out, self->axes0[1], 3 * sizeof (gfloat));
		break;
	default:
		memcpy (out, self->view_axis0, 3 * sizeof (gfloat));
		break;
	}
}

static gfloat
ring_angle (LrgEditorGizmo *self,
            const gfloat   *axis,
            const gfloat   *hit)
{
	gfloat v0[3], v[3], c[3];

	v0[0] = self->hit0[0] - self->pivot0[0];
	v0[1] = self->hit0[1] - self->pivot0[1];
	v0[2] = self->hit0[2] - self->pivot0[2];
	v[0] = hit[0] - self->pivot0[0];
	v[1] = hit[1] - self->pivot0[1];
	v[2] = hit[2] - self->pivot0[2];
	cross3 (v0, v, c);

	return atan2f (dot3 (c, axis), dot3 (v0, v));
}

static const gfloat *
rotation_axis (LrgEditorGizmo *self)
{
	if (self->active == LRG_EDITOR_GIZMO_HANDLE_CENTER)
		return self->view_axis0;

	return self->axes0[self->active - LRG_EDITOR_GIZMO_HANDLE_X];
}

/* ==========================================================================
 * Drag lifecycle
 * ========================================================================== */

static void
freeze_selection (LrgEditorGizmo *self)
{
	g_autoptr(GPtrArray) roots = NULL;
	guint                i;

	clear_entries (self);

	/* Only selection roots move: children follow their parents. Locked
	 * nodes are left alone. */
	roots = lrg_editor_get_selected_roots (self->editor);
	for (i = 0; i < roots->len; i++)
	{
		LrgNode    *node = g_ptr_array_index (roots, i);
		LrgNode    *parent = lrg_node_get_parent (node);
		GrlVector3 *l, *r, *s;
		DragEntry   e;

		if (lrg_node_get_locked (node))
			continue;

		memset (&e, 0, sizeof e);
		e.node = g_object_ref (node);
		l = lrg_node_get_location (node);
		r = lrg_node_get_rotation (node);
		s = lrg_node_get_scale (node);
		e.trs0[0] = l->x; e.trs0[1] = l->y; e.trs0[2] = l->z;
		e.trs0[3] = r->x; e.trs0[4] = r->y; e.trs0[5] = r->z;
		e.trs0[6] = s->x; e.trs0[7] = s->y; e.trs0[8] = s->z;
		lrg_editor_node_get_world_matrix (node, e.world0);

		if (parent != NULL)
		{
			gfloat pw[16];

			lrg_editor_node_get_world_matrix (parent, pw);
			lrg_editor_math_invert (pw, e.parent_inv);
		}
		else
		{
			lrg_editor_math_identity (e.parent_inv);
		}

		g_array_append_val (self->entries, e);
	}
}

gboolean
lrg_editor_gizmo_begin_drag (LrgEditorGizmo      *self,
                             const LrgEditorView *view,
                             gfloat               x,
                             gfloat               y)
{
	LrgEditorGizmoHandle handle;

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), FALSE);

	handle = lrg_editor_gizmo_hit_test (self, view, x, y);
	if (handle == LRG_EDITOR_GIZMO_HANDLE_NONE)
		return FALSE;

	return lrg_editor_gizmo_begin_drag_handle (self, view, handle, x, y);
}

gboolean
lrg_editor_gizmo_begin_drag_handle (LrgEditorGizmo       *self,
                                    const LrgEditorView  *view,
                                    LrgEditorGizmoHandle  handle,
                                    gfloat                x,
                                    gfloat                y)
{
	LrgEditorTool tool;
	gint          i;

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	if (self->dragging || handle == LRG_EDITOR_GIZMO_HANDLE_NONE)
		return FALSE;

	tool = lrg_editor_get_tool (self->editor);
	if (tool == LRG_EDITOR_TOOL_SELECT || primary_node (self) == NULL)
		return FALSE;

	/* Plane handles only exist for translation. */
	if (tool != LRG_EDITOR_TOOL_TRANSLATE &&
	    (handle == LRG_EDITOR_GIZMO_HANDLE_XY ||
	     handle == LRG_EDITOR_GIZMO_HANDLE_YZ ||
	     handle == LRG_EDITOR_GIZMO_HANDLE_XZ))
		return FALSE;

	freeze_selection (self);
	if (self->entries->len == 0)
		return FALSE;

	self->drag_tool = tool;
	self->active = handle;
	self->hovered = handle;
	lrg_editor_gizmo_get_origin (self, self->pivot0);
	for (i = 0; i < 3; i++)
		lrg_editor_gizmo_get_axis (self, i, self->axes0[i]);

	/* The view axis points from the pivot toward the camera. */
	view_forward (view, self->view_axis0);
	self->view_axis0[0] = -self->view_axis0[0];
	self->view_axis0[1] = -self->view_axis0[1];
	self->view_axis0[2] = -self->view_axis0[2];

	self->mouse0[0] = x;
	self->mouse0[1] = y;
	self->param0 = 0.0f;
	self->angle_prev = 0.0f;
	self->angle_total = 0.0f;
	self->screen_rotate = FALSE;
	memset (self->delta, 0, sizeof self->delta);

	switch (tool)
	{
	case LRG_EDITOR_TOOL_TRANSLATE:
		if (handle >= LRG_EDITOR_GIZMO_HANDLE_X && handle <= LRG_EDITOR_GIZMO_HANDLE_Z)
		{
			if (!axis_param (view, self->pivot0, self->axes0[handle - LRG_EDITOR_GIZMO_HANDLE_X],
			                 x, y, &self->param0))
				goto fail;
		}
		else
		{
			gfloat n[3];

			handle_plane_normal (self, handle, n);
			if (!plane_hit (view, self->pivot0, n, x, y, self->hit0))
				goto fail;
		}
		break;

	case LRG_EDITOR_TOOL_ROTATE:
		{
			const gfloat *axis = rotation_axis (self);
			gfloat        o[3], d[3];

			/* Edge-on rings cannot be dragged around the circle; fall back
			 * to horizontal mouse motion. */
			lrg_editor_view_get_ray (view, x, y, o, d);
			if (fabsf (dot3 (d, axis)) < 0.15f ||
			    !plane_hit (view, self->pivot0, axis, x, y, self->hit0))
				self->screen_rotate = TRUE;
		}
		break;

	case LRG_EDITOR_TOOL_SCALE:
		if (handle != LRG_EDITOR_GIZMO_HANDLE_CENTER)
		{
			if (!axis_param (view, self->pivot0, self->axes0[handle - LRG_EDITOR_GIZMO_HANDLE_X],
			                 x, y, &self->param0))
				goto fail;
			/* Grabbing right at the pivot would give a huge ratio. */
			if (fabsf (self->param0) < 1e-3f)
				self->param0 = 1e-3f;
		}
		break;

	default:
		goto fail;
	}

	self->dragging = TRUE;
	return TRUE;

fail:
	clear_entries (self);
	self->active = LRG_EDITOR_GIZMO_HANDLE_NONE;
	return FALSE;
}

static gboolean
snapping (LrgEditorGizmo *self,
          gboolean        invert)
{
	gboolean on = lrg_editor_get_snap_enabled (self->editor);

	return invert ? !on : on;
}

static void
apply_translation (LrgEditorGizmo *self,
                   const gfloat   *world_delta)
{
	guint i;

	for (i = 0; i < self->entries->len; i++)
	{
		DragEntry *e = &g_array_index (self->entries, DragEntry, i);
		gfloat     wp[3], lp[3];

		/* New world origin, expressed in the parent's space. */
		wp[0] = e->world0[12] + world_delta[0];
		wp[1] = e->world0[13] + world_delta[1];
		wp[2] = e->world0[14] + world_delta[2];
		lrg_editor_math_transform_point (e->parent_inv, wp, lp);
		lrg_node_set_location_xyz (e->node, lp[0], lp[1], lp[2]);
	}
}

static void
apply_rotation (LrgEditorGizmo *self,
                const gfloat   *axis,
                gfloat          angle)
{
	gfloat rot[16], tp[16], tn[16], about[16];
	guint  i;

	/* about = T(pivot) * R * T(-pivot) */
	rotation_matrix (axis, angle, rot);
	lrg_editor_math_identity (tp);
	lrg_editor_math_identity (tn);
	tp[12] = self->pivot0[0]; tp[13] = self->pivot0[1]; tp[14] = self->pivot0[2];
	tn[12] = -self->pivot0[0]; tn[13] = -self->pivot0[1]; tn[14] = -self->pivot0[2];
	lrg_editor_math_multiply (rot, tn, about);
	lrg_editor_math_multiply (tp, about, about);

	for (i = 0; i < self->entries->len; i++)
	{
		DragEntry *e = &g_array_index (self->entries, DragEntry, i);
		gfloat     world[16], local[16];
		gfloat     l[3], r[3], s[3];

		lrg_editor_math_multiply (about, e->world0, world);
		lrg_editor_math_multiply (e->parent_inv, world, local);
		if (!lrg_editor_math_decompose (local, l, r, s))
			continue;

		/* Keep the frozen scale to avoid decomposition noise. */
		lrg_node_set_location_xyz (e->node, l[0], l[1], l[2]);
		lrg_node_set_rotation_xyz (e->node, r[0], r[1], r[2]);
	}
}

static void
apply_scale (LrgEditorGizmo *self,
             gint            axis_index,   /* -1 = uniform */
             gfloat          factor,
             gboolean        snap)
{
	gdouble snap_scale = 0.0;
	guint   i;

	lrg_editor_get_snap (self->editor, NULL, NULL, &snap_scale);

	for (i = 0; i < self->entries->len; i++)
	{
		DragEntry *e = &g_array_index (self->entries, DragEntry, i);
		gfloat     scl[3];
		gint       k;

		for (k = 0; k < 3; k++)
		{
			scl[k] = e->trs0[6 + k];
			if (axis_index < 0 || axis_index == k)
			{
				scl[k] *= factor;
				if (snap)
					scl[k] = lrg_editor_snap_value (scl[k], (gfloat) snap_scale);
			}
		}
		lrg_node_set_scale_xyz (e->node, scl[0], scl[1], scl[2]);

		/* Uniform scaling of a multi-selection also spreads the nodes
		 * apart around the pivot. */
		if (axis_index < 0 && self->entries->len > 1)
		{
			gfloat wp[3], lp[3];

			for (k = 0; k < 3; k++)
				wp[k] = self->pivot0[k] + (e->world0[12 + k] - self->pivot0[k]) * factor;
			lrg_editor_math_transform_point (e->parent_inv, wp, lp);
			lrg_node_set_location_xyz (e->node, lp[0], lp[1], lp[2]);
		}
	}
}

void
lrg_editor_gizmo_drag (LrgEditorGizmo      *self,
                       const LrgEditorView *view,
                       gfloat               x,
                       gfloat               y,
                       gboolean             invert_snap)
{
	gboolean snap;
	gdouble  snap_t = 0.0, snap_r = 0.0;

	g_return_if_fail (LRG_IS_EDITOR_GIZMO (self));
	g_return_if_fail (view != NULL);

	if (!self->dragging)
		return;

	snap = snapping (self, invert_snap);
	lrg_editor_get_snap (self->editor, &snap_t, &snap_r, NULL);

	switch (self->drag_tool)
	{
	case LRG_EDITOR_TOOL_TRANSLATE:
		{
			gfloat world_delta[3] = { 0.0f, 0.0f, 0.0f };
			gint   k;

			if (self->active >= LRG_EDITOR_GIZMO_HANDLE_X &&
			    self->active <= LRG_EDITOR_GIZMO_HANDLE_Z)
			{
				const gfloat *a = self->axes0[self->active - LRG_EDITOR_GIZMO_HANDLE_X];
				gfloat        s;

				if (!axis_param (view, self->pivot0, a, x, y, &s))
					return;
				s -= self->param0;
				if (snap)
					s = lrg_editor_snap_value (s, (gfloat) snap_t);
				for (k = 0; k < 3; k++)
					world_delta[k] = a[k] * s;
			}
			else
			{
				gfloat n[3], hit[3], d[3];

				handle_plane_normal (self, self->active, n);
				if (!plane_hit (view, self->pivot0, n, x, y, hit))
					return;
				for (k = 0; k < 3; k++)
					d[k] = hit[k] - self->hit0[k];

				if (self->active == LRG_EDITOR_GIZMO_HANDLE_CENTER)
				{
					/* Free move: snap each world component. */
					for (k = 0; k < 3; k++)
						world_delta[k] = snap ? lrg_editor_snap_value (d[k], (gfloat) snap_t) : d[k];
				}
				else
				{
					/* Plane move: snap along the two plane axes. */
					static const gint pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
					const gint       *pr = pairs[self->active - LRG_EDITOR_GIZMO_HANDLE_XY];
					const gfloat     *a = self->axes0[pr[0]];
					const gfloat     *b = self->axes0[pr[1]];
					gfloat            da = dot3 (d, a);
					gfloat            db = dot3 (d, b);

					if (snap)
					{
						da = lrg_editor_snap_value (da, (gfloat) snap_t);
						db = lrg_editor_snap_value (db, (gfloat) snap_t);
					}
					for (k = 0; k < 3; k++)
						world_delta[k] = a[k] * da + b[k] * db;
				}
			}

			memcpy (self->delta, world_delta, sizeof self->delta);
			apply_translation (self, world_delta);
		}
		break;

	case LRG_EDITOR_TOOL_ROTATE:
		{
			const gfloat *axis = rotation_axis (self);
			gfloat        angle;

			if (self->screen_rotate)
			{
				self->angle_total = (x - self->mouse0[0]) * 0.01f;
			}
			else
			{
				gfloat hit[3];
				gfloat step;

				if (!plane_hit (view, self->pivot0, axis, x, y, hit))
					return;

				/* Accumulate wrapped increments so dragging past 180
				 * degrees keeps turning instead of flipping. */
				angle = ring_angle (self, axis, hit);
				step = angle - self->angle_prev;
				while (step > (gfloat) G_PI)
					step -= 2.0f * (gfloat) G_PI;
				while (step < (gfloat) -G_PI)
					step += 2.0f * (gfloat) G_PI;
				self->angle_total += step;
				self->angle_prev = angle;
			}

			angle = self->angle_total;
			if (snap)
				angle = lrg_editor_snap_value (angle, (gfloat) snap_r);

			self->delta[0] = angle;
			self->delta[1] = 0.0f;
			self->delta[2] = 0.0f;
			apply_rotation (self, axis, angle);
		}
		break;

	case LRG_EDITOR_TOOL_SCALE:
		{
			gfloat factor;
			gint   axis_index = -1;

			if (self->active == LRG_EDITOR_GIZMO_HANDLE_CENTER)
			{
				factor = 1.0f + ((x - self->mouse0[0]) - (y - self->mouse0[1])) * 0.01f;
			}
			else
			{
				gfloat s;

				axis_index = self->active - LRG_EDITOR_GIZMO_HANDLE_X;
				if (!axis_param (view, self->pivot0, self->axes0[axis_index], x, y, &s))
					return;
				factor = s / self->param0;
			}
			factor = MAX (factor, 0.001f);

			self->delta[0] = self->delta[1] = self->delta[2] = 1.0f;
			if (axis_index < 0)
				self->delta[0] = self->delta[1] = self->delta[2] = factor;
			else
				self->delta[axis_index] = factor;
			apply_scale (self, axis_index, factor, snap);
		}
		break;

	default:
		break;
	}
}

gboolean
lrg_editor_gizmo_end_drag (LrgEditorGizmo *self)
{
	LrgEditorCommand *group;
	const gchar      *label;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (self), FALSE);

	if (!self->dragging)
		return FALSE;

	switch (self->drag_tool)
	{
	case LRG_EDITOR_TOOL_ROTATE:
		label = "Rotate";
		break;
	case LRG_EDITOR_TOOL_SCALE:
		label = "Scale";
		break;
	default:
		label = "Move";
		break;
	}

	group = lrg_editor_command_new_group (label);
	for (i = 0; i < self->entries->len; i++)
	{
		DragEntry  *e = &g_array_index (self->entries, DragEntry, i);
		GrlVector3 *l = lrg_node_get_location (e->node);
		GrlVector3 *r = lrg_node_get_rotation (e->node);
		GrlVector3 *s = lrg_node_get_scale (e->node);
		gfloat      after[9];

		after[0] = l->x; after[1] = l->y; after[2] = l->z;
		after[3] = r->x; after[4] = r->y; after[5] = r->z;
		after[6] = s->x; after[7] = s->y; after[8] = s->z;

		if (memcmp (after, e->trs0, sizeof after) != 0)
			lrg_editor_command_group_append (group,
				lrg_editor_command_new_transform (e->node, e->trs0, after));
	}

	self->dragging = FALSE;
	self->active = LRG_EDITOR_GIZMO_HANDLE_NONE;
	clear_entries (self);

	if (lrg_editor_command_group_get_n_children (group) == 0)
	{
		g_object_unref (group);
		return FALSE;
	}

	/* Applying re-sets the "after" transforms, which the nodes already
	 * have; this just records the step. */
	lrg_editor_execute (self->editor, group);
	return TRUE;
}

void
lrg_editor_gizmo_cancel_drag (LrgEditorGizmo *self)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_GIZMO (self));

	if (!self->dragging)
		return;

	for (i = 0; i < self->entries->len; i++)
	{
		DragEntry *e = &g_array_index (self->entries, DragEntry, i);

		lrg_node_set_location_xyz (e->node, e->trs0[0], e->trs0[1], e->trs0[2]);
		lrg_node_set_rotation_xyz (e->node, e->trs0[3], e->trs0[4], e->trs0[5]);
		lrg_node_set_scale_xyz (e->node, e->trs0[6], e->trs0[7], e->trs0[8]);
	}

	self->dragging = FALSE;
	self->active = LRG_EDITOR_GIZMO_HANDLE_NONE;
	clear_entries (self);
}

void
lrg_editor_gizmo_get_drag_delta (LrgEditorGizmo *self,
                                 gfloat         *out)
{
	g_return_if_fail (LRG_IS_EDITOR_GIZMO (self));

	memcpy (out, self->delta, sizeof self->delta);
}
