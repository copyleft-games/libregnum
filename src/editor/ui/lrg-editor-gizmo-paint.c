/* lrg-editor-gizmo-paint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Viewport overlays recorded into an LrgPaintList (the SDF paint path of
 * the retained UI), replacing the raylib 2D calls the scene renderer used
 * to make after its 3D pass.
 *
 * Geometry always comes from the headless objects that also do the hit
 * testing: LrgEditorGizmo (origin, axes, on-screen size, hovered and
 * active handle), the LrgEditorView the gizmo math projects through, and
 * the orientation gizmo layout of LrgEditorViewportNav (margin, radius,
 * per-axis end offsets from the view matrix). Points are projected into
 * viewport-local pixels and only then mapped to the canvas with
 * (origin + p * scale), the inverse of what LrgViewport does to pointer
 * positions, so a handle is drawn exactly where it is hit.
 *
 * Axis colours are semantic (X red, Y green, Z blue, highlight yellow)
 * and therefore theme-independent; everything that is UI chrome (chips,
 * labels, outlines, the box-select rectangle) takes its colours, font
 * and metrics from the theme.
 */

#include "config.h"

#include "lrg-editor-gizmo-paint.h"
#include "../lrg-editor-viewport-nav.h"
#include "../lrg-node.h"
#include "../lrg-node-visual.h"
#include "../lrg-node-tilemap.h"
#include "../../paint/lrg-paint-fx.h"
#include <math.h>
#include <string.h>

#define RING_SEGMENTS    (64)
#define ARM_WIDTH        (3.0f)
#define RING_WIDTH       (2.5f)
#define ARROW_LENGTH     (14.0f)
#define ARROW_HALF_WIDTH (6.5f)
#define SCALE_BOX        (10.0f)
#define CENTER_RADIUS    (7.0f)
#define DIM_ALPHA        (90)

/* ==========================================================================
 * Colours
 * ========================================================================== */

static GrlColor
rgba (guint8 r,
      guint8 g,
      guint8 b,
      guint8 a)
{
	GrlColor c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

static GrlColor
with_alpha (GrlColor c,
            guint8   a)
{
	c.a = a;
	return c;
}

/* Fades a colour by @k (0..1) on top of its own alpha. */
static GrlColor
fade (GrlColor c,
      gfloat   k)
{
	c.a = (guint8) CLAMP ((gfloat) c.a * k, 0.0f, 255.0f);
	return c;
}

/* The semantic colour of axis @i (0..2 = X Y Z). */
static GrlColor
axis_color (gint i)
{
	switch (i)
	{
	case 0:  return rgba (0xf3, 0x5d, 0x6f, 0xff);
	case 1:  return rgba (0x8c, 0xd8, 0x5a, 0xff);
	default: return rgba (0x4f, 0x9c, 0xf9, 0xff);
	}
}

static GrlColor
highlight_color (void)
{
	return rgba (0xff, 0xd8, 0x4d, 0xff);
}

/* A theme colour with a fallback when the theme lacks the role. */
static GrlColor
theme_color (LrgTheme      *theme,
             LrgThemeColor  role,
             GrlColor       fallback)
{
	const GrlColor *c = (theme != NULL) ? lrg_theme_get_color (theme, role) : NULL;

	return (c != NULL) ? *c : fallback;
}

static gfloat
theme_metric (LrgTheme       *theme,
              LrgThemeMetric  metric,
              gfloat          fallback)
{
	gfloat v = (theme != NULL) ? lrg_theme_get_metric (theme, metric) : 0.0f;

	return (v > 0.0f) ? v : fallback;
}

static GrlFont *
theme_font (LrgTheme *theme)
{
	return (theme != NULL) ? lrg_theme_get_ui_font (theme) : NULL;
}

/*
 * The colour of a gizmo handle: the dragged handle and (when not
 * dragging) the hovered one light up; while a drag runs the others dim
 * so the moving axis reads clearly.
 */
static GrlColor
handle_color (LrgEditorGizmo       *gizmo,
              LrgEditorGizmoHandle  handle,
              GrlColor              base)
{
	gboolean dragging = lrg_editor_gizmo_is_dragging (gizmo);

	if (lrg_editor_gizmo_get_active (gizmo) == handle ||
	    (!dragging && lrg_editor_gizmo_get_hovered (gizmo) == handle))
		return highlight_color ();
	if (dragging)
		return with_alpha (base, DIM_ALPHA);
	return base;
}

/* ==========================================================================
 * Projection
 * ========================================================================== */

typedef struct
{
	const LrgEditorView *view;
	gfloat               ox;
	gfloat               oy;
	gfloat               scale;
} Mapper;

/* World point -> canvas point; FALSE behind the camera. */
static gboolean
map_point (const Mapper *m,
           const gfloat *p,
           gfloat       *x,
           gfloat       *y)
{
	gfloat u, v;

	if (!lrg_editor_view_project (m->view, p, &u, &v, NULL))
		return FALSE;
	*x = m->ox + u * m->scale;
	*y = m->oy + v * m->scale;
	return TRUE;
}

/* ==========================================================================
 * Transform gizmo
 * ========================================================================== */

/* Three rings (one per axis plane) and the screen-aligned outer ring. */
static void
paint_rotate (LrgPaintList   *list,
              LrgEditorGizmo *gizmo,
              const Mapper   *m,
              const gfloat   *origin,
              gfloat          axes[3][3],
              gfloat          size,
              gfloat          cx,
              gfloat          cy)
{
	gint i, s, k;

	for (i = 0; i < 3; i++)
	{
		const gfloat *u = axes[(i + 1) % 3];
		const gfloat *v = axes[(i + 2) % 3];
		GrlColor      c = handle_color (gizmo, (LrgEditorGizmoHandle) (LRG_EDITOR_GIZMO_HANDLE_X + i), axis_color (i));
		gfloat        px = 0.0f, py = 0.0f;
		gboolean      have_prev = FALSE;

		for (s = 0; s <= RING_SEGMENTS; s++)
		{
			gfloat a = (gfloat) s / (gfloat) RING_SEGMENTS * 2.0f * (gfloat) G_PI;
			gfloat p[3], qx, qy;

			for (k = 0; k < 3; k++)
				p[k] = origin[k] + (u[k] * cosf (a) + v[k] * sinf (a)) * size;
			if (map_point (m, p, &qx, &qy))
			{
				if (have_prev)
					lrg_paint_list_line (list, px, py, qx, qy, RING_WIDTH * m->scale, &c);
				px = qx;
				py = qy;
				have_prev = TRUE;
			}
			else
				have_prev = FALSE;
		}
	}

	/* The view ring: a constant-size circle outline around the centre */
	{
		GrlColor c = handle_color (gizmo, LRG_EDITOR_GIZMO_HANDLE_CENTER, rgba (0xe6, 0xe9, 0xf2, 0xd0));
		gfloat   r = LRG_EDITOR_GIZMO_SIZE_PIXELS * 1.2f * m->scale;
		gfloat   px = cx + r, py = cy;

		for (s = 1; s <= RING_SEGMENTS; s++)
		{
			gfloat a = (gfloat) s / (gfloat) RING_SEGMENTS * 2.0f * (gfloat) G_PI;
			gfloat qx = cx + cosf (a) * r, qy = cy + sinf (a) * r;

			lrg_paint_list_line (list, px, py, qx, qy, 1.5f * m->scale, &c);
			px = qx;
			py = qy;
		}
	}
}

/* The translate plane squares: XY tinted like Z, YZ like X, XZ like Y. */
static void
paint_planes (LrgPaintList   *list,
              LrgEditorGizmo *gizmo,
              const Mapper   *m,
              const gfloat   *origin,
              gfloat          axes[3][3],
              gfloat          size)
{
	static const gint pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
	static const gint tints[3] = { 2, 0, 1 };
	const gfloat      ca[4] = { 0.2f, 0.45f, 0.45f, 0.2f };
	const gfloat      cb[4] = { 0.2f, 0.2f, 0.45f, 0.45f };
	gint              i, j, k;

	for (i = 0; i < 3; i++)
	{
		gfloat   qx[4], qy[4];
		gboolean ok = TRUE;
		GrlColor c = handle_color (gizmo, (LrgEditorGizmoHandle) (LRG_EDITOR_GIZMO_HANDLE_XY + i),
		                           axis_color (tints[i]));
		GrlColor fill;

		for (j = 0; j < 4 && ok; j++)
		{
			gfloat p[3];

			for (k = 0; k < 3; k++)
				p[k] = origin[k] + (axes[pairs[i][0]][k] * ca[j] + axes[pairs[i][1]][k] * cb[j]) * size;
			ok = map_point (m, p, &qx[j], &qy[j]);
		}
		if (!ok)
			continue;

		fill = fade (c, 0.38f);
		lrg_paint_list_triangle (list, qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], &fill);
		lrg_paint_list_triangle (list, qx[0], qy[0], qx[2], qy[2], qx[3], qy[3], &fill);
		for (j = 0; j < 4; j++)
			lrg_paint_list_line (list, qx[j], qy[j], qx[(j + 1) % 4], qy[(j + 1) % 4], 1.25f * m->scale, &c);
	}
}

void
lrg_editor_gizmo_paint_transform (LrgPaintList        *list,
                                  LrgEditorGizmo      *gizmo,
                                  LrgEditorTool        tool,
                                  const LrgEditorView *view,
                                  gfloat               origin_x,
                                  gfloat               origin_y,
                                  gfloat               scale)
{
	Mapper m;
	gfloat origin[3], axes[3][3], size, cx, cy;
	gint   i, k;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (LRG_IS_EDITOR_GIZMO (gizmo));
	g_return_if_fail (view != NULL);

	if (tool == LRG_EDITOR_TOOL_SELECT || !lrg_editor_gizmo_is_visible (gizmo))
		return;

	m.view = view;
	m.ox = origin_x;
	m.oy = origin_y;
	m.scale = scale;

	lrg_editor_gizmo_get_origin (gizmo, origin);
	for (i = 0; i < 3; i++)
		lrg_editor_gizmo_get_axis (gizmo, (guint) i, axes[i]);
	size = lrg_editor_gizmo_get_size (gizmo, view);
	if (!map_point (&m, origin, &cx, &cy))
		return;

	if (tool == LRG_EDITOR_TOOL_ROTATE)
	{
		paint_rotate (list, gizmo, &m, origin, axes, size, cx, cy);
		return;
	}
	if (tool == LRG_EDITOR_TOOL_TRANSLATE)
		paint_planes (list, gizmo, &m, origin, axes, size);

	/* Arms with arrow heads (translate) or boxes (scale) */
	for (i = 0; i < 3; i++)
	{
		GrlColor c = handle_color (gizmo, (LrgEditorGizmoHandle) (LRG_EDITOR_GIZMO_HANDLE_X + i), axis_color (i));
		gfloat   tip[3], tx, ty, len, dx, dy;

		for (k = 0; k < 3; k++)
			tip[k] = origin[k] + axes[i][k] * size;
		if (!map_point (&m, tip, &tx, &ty))
			continue;
		len = hypotf (tx - cx, ty - cy);
		if (len < 4.0f * scale)
			continue;   /* the axis points at the camera */
		dx = (tx - cx) / len;
		dy = (ty - cy) / len;

		lrg_paint_list_line (list, cx, cy, tx, ty, ARM_WIDTH * scale, &c);
		if (tool == LRG_EDITOR_TOOL_SCALE)
		{
			gfloat b = SCALE_BOX * scale;

			lrg_paint_list_rect (list, tx - b * 0.5f, ty - b * 0.5f, b, b, 2.0f * scale, &c);
		}
		else
		{
			gfloat ax = tx + dx * ARROW_LENGTH * scale, ay = ty + dy * ARROW_LENGTH * scale;
			gfloat hw = ARROW_HALF_WIDTH * scale;

			lrg_paint_list_triangle (list, ax, ay, tx - dy * hw, ty + dx * hw, tx + dy * hw, ty - dx * hw, &c);
		}
	}

	/* Centre handle: a ring (translate) or a box (uniform scale) */
	{
		GrlColor c = handle_color (gizmo, LRG_EDITOR_GIZMO_HANDLE_CENTER, rgba (0xe6, 0xe9, 0xf2, 0xff));
		GrlColor shadow = rgba (0, 0, 0, 0x60);

		if (tool == LRG_EDITOR_TOOL_SCALE)
		{
			gfloat b = 12.0f * scale;

			lrg_paint_list_rect (list, cx - b * 0.5f, cy - b * 0.5f, b, b, 2.5f * scale, &c);
		}
		else
		{
			lrg_paint_list_circle (list, cx, cy, (CENTER_RADIUS + 1.5f) * scale, &shadow);
			lrg_paint_list_circle (list, cx, cy, CENTER_RADIUS * scale, &c);
			lrg_paint_list_circle (list, cx, cy, (CENTER_RADIUS - 2.0f) * scale, &shadow);
		}
	}
}

gchar *
lrg_editor_gizmo_paint_format_delta (LrgEditorGizmo *gizmo,
                                     LrgEditorTool   tool)
{
	gfloat d[3] = { 0.0f, 0.0f, 0.0f };

	g_return_val_if_fail (LRG_IS_EDITOR_GIZMO (gizmo), NULL);

	if (!lrg_editor_gizmo_is_dragging (gizmo))
		return NULL;
	lrg_editor_gizmo_get_drag_delta (gizmo, d);
	switch (tool)
	{
	case LRG_EDITOR_TOOL_ROTATE:
		return g_strdup_printf ("Rotate  %.1f deg", (gdouble) (d[0] * 180.0f / (gfloat) G_PI));
	case LRG_EDITOR_TOOL_SCALE:
		return g_strdup_printf ("Scale  x%.2f  y%.2f  z%.2f", (gdouble) d[0], (gdouble) d[1], (gdouble) d[2]);
	default:
		return g_strdup_printf ("Move  dx %.2f  dy %.2f  dz %.2f", (gdouble) d[0], (gdouble) d[1], (gdouble) d[2]);
	}
}

void
lrg_editor_gizmo_paint_drag_readout (LrgPaintList   *list,
                                     LrgTheme       *theme,
                                     LrgEditorGizmo *gizmo,
                                     LrgEditorTool   tool,
                                     gfloat          x,
                                     gfloat          y)
{
	g_autofree gchar *text = NULL;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (LRG_IS_EDITOR_GIZMO (gizmo));

	text = lrg_editor_gizmo_paint_format_delta (gizmo, tool);
	if (text == NULL)
		return;
	lrg_editor_gizmo_paint_chip (list, theme, text, x + 18.0f, y + 20.0f, FALSE, NULL);
}

/* ==========================================================================
 * Orientation gizmo
 * ========================================================================== */

void
lrg_editor_gizmo_paint_axes (LrgPaintList              *list,
                             LrgTheme                  *theme,
                             const LrgEditorViewCamera *camera,
                             gint                       hot,
                             gboolean                   lit,
                             gfloat                     origin_x,
                             gfloat                     origin_y,
                             gfloat                     scale)
{
	static const gchar *names[3] = { "X", "Y", "Z" };
	gfloat              view[16], dx[6], dy[6], depth[6];
	gfloat              cx, cy, r;
	gint                order[6], i, j;
	GrlColor            disc;
	GrlColor            ink = rgba (0x11, 0x11, 0x1b, 0xff);
	GrlFont            *font = theme_font (theme);
	gfloat              fs;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (camera != NULL);

	if (camera->view_2d)
		return;

	/* Same placement and per-end offsets as LrgEditorViewportNav */
	cx = origin_x + LRG_EDITOR_NAV_GIZMO_MARGIN * scale;
	cy = origin_y + (camera->view.height - LRG_EDITOR_NAV_GIZMO_MARGIN) * scale;
	lrg_editor_view_get_view_matrix (&camera->view, view);
	for (i = 0; i < 6; i++)
	{
		gint   axis = i % 3;
		gfloat sign = (i < 3) ? 1.0f : -1.0f;

		dx[i] = view[axis * 4 + 0] * sign * LRG_EDITOR_NAV_GIZMO_RADIUS * scale;
		dy[i] = -view[axis * 4 + 1] * sign * LRG_EDITOR_NAV_GIZMO_RADIUS * scale;
		depth[i] = view[axis * 4 + 2] * sign;
		order[i] = i;
	}

	/* The drag area: a translucent disc, brighter while hot */
	r = (LRG_EDITOR_NAV_GIZMO_RADIUS + LRG_EDITOR_NAV_GIZMO_END_HIT) * scale;
	disc = theme_color (theme, LRG_THEME_COLOR_SURFACE, rgba (0x1e, 0x1e, 0x2e, 0xff));
	disc.a = (lit || hot != -1) ? 0xc0 : 0x70;
	lrg_paint_list_circle (list, cx, cy, r, &disc);

	/* Far ends first so near ones paint on top (selection sort of six) */
	for (i = 0; i < 6; i++)
		for (j = i + 1; j < 6; j++)
			if (depth[order[j]] < depth[order[i]])
			{
				gint t = order[i];

				order[i] = order[j];
				order[j] = t;
			}

	fs = 11.0f * scale;
	for (i = 0; i < 6; i++)
	{
		gint     e = order[i];
		GrlColor col = axis_color (e % 3);
		gfloat   ex = cx + dx[e], ey = cy + dy[e];

		if (e < 3)
		{
			gfloat tw = 0.0f, th = 0.0f;
			gfloat er = (hot == e ? 9.0f : 7.5f) * scale;

			lrg_paint_list_line (list, cx, cy, ex, ey, 2.0f * scale, &col);
			lrg_paint_list_circle (list, ex, ey, er, &col);
			lrg_paint_measure_text (font, names[e], fs, 0.0f, &tw, &th);
			lrg_paint_list_text (list, font, names[e], roundf (ex - tw * 0.5f), roundf (ey - th * 0.5f), fs, 0.0f,
			                     &ink);
		}
		else
		{
			GrlColor neg = with_alpha (col, 0x90);

			lrg_paint_list_circle (list, ex, ey, (hot == e ? 7.5f : 6.0f) * scale, &neg);
		}
	}
}

/* ==========================================================================
 * Box select, chips, tiles, play frame
 * ========================================================================== */

void
lrg_editor_gizmo_paint_box_select (LrgPaintList *list,
                                   LrgTheme     *theme,
                                   gfloat        x,
                                   gfloat        y,
                                   gfloat        width,
                                   gfloat        height)
{
	GrlColor accent;
	GrlColor fill;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));

	accent = theme_color (theme, LRG_THEME_COLOR_ACCENT, rgba (0xb4, 0xbe, 0xfe, 0xff));
	fill = with_alpha (accent, 0x30);
	lrg_paint_list_rect (list, x, y, width, height, 2.0f, &fill);
	lrg_paint_list_border (list, x, y, width, height, 2.0f, 1.0f, &accent);
}

gfloat
lrg_editor_gizmo_paint_chip (LrgPaintList   *list,
                             LrgTheme       *theme,
                             const gchar    *text,
                             gfloat          x,
                             gfloat          y,
                             gboolean        align_right,
                             const GrlColor *accent)
{
	GrlFont        *font = theme_font (theme);
	gfloat          fs = theme_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL, 12.0f);
	gfloat          px = 8.0f, py = 4.0f;
	gfloat          tw = 0.0f, th = 0.0f, w, h, radius;
	GrlColor        ink;
	GrlColor        shadow;
	LrgPaintGlass   glass;
	LrgCornerRadii  radii;

	g_return_val_if_fail (LRG_IS_PAINT_LIST (list), 0.0f);
	g_return_val_if_fail (text != NULL, 0.0f);

	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, &th);
	w = tw + px * 2.0f;
	h = MAX (th, fs) + py * 2.0f;
	if (align_right)
		x -= w;
	radius = MIN (h * 0.5f, theme_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS, 6.0f) + 2.0f);

	/* Soft shadow, frosted glass tinted with the panel colour, text */
	shadow = theme_color (theme, LRG_THEME_COLOR_SHADOW, rgba (0, 0, 0, 0x60));
	shadow.a = MIN (shadow.a, 0x70);
	lrg_paint_list_shadow (list, x, y, w, h, radius, 10.0f, 0.0f, 0.0f, 2.0f, &shadow);
	lrg_paint_glass_init_default (&glass);
	glass.frost = 0.9f;
	glass.tint = theme_color (theme, LRG_THEME_COLOR_PANEL, rgba (0x1e, 0x1e, 0x2e, 0xff));
	glass.tint.a = 0xb8;
	glass.refraction = 0.0f;
	glass.dispersion = 0.0f;
	glass.rim_light = 0.25f;
	radii.top_left = radii.top_right = radii.bottom_left = radii.bottom_right = radius;
	lrg_paint_list_glass (list, x, y, w, h, &radii, &glass);

	ink = (accent != NULL) ? *accent : theme_color (theme, LRG_THEME_COLOR_TEXT, rgba (0xcd, 0xd6, 0xf4, 0xff));
	lrg_paint_list_text (list, font, text, roundf (x + px), roundf (y + py), fs, 0.0f, &ink);
	return h;
}

void
lrg_editor_gizmo_paint_tile_hover (LrgPaintList        *list,
                                   LrgTheme            *theme,
                                   const LrgEditorView *view,
                                   LrgNode             *node,
                                   gint                 cell_x,
                                   gint                 cell_y,
                                   gboolean             erase,
                                   gfloat               origin_x,
                                   gfloat               origin_y,
                                   gfloat               scale)
{
	LrgNodeVisual *visual;
	Mapper         m;
	gfloat         world[16], ts;
	gfloat         local[4][3], qx[4], qy[4];
	GrlColor       edge, fill;
	gint           i;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (view != NULL);
	g_return_if_fail (LRG_IS_NODE (node));

	visual = lrg_node_get_visual (node);
	ts = (visual != NULL) ? (gfloat) lrg_node_visual_get_param_double (visual, "tile_size", 1.0) : 1.0f;
	if (!(ts > 0.0f))
		ts = 1.0f;

	/* The cell in the node's local XY plane (rows grow towards -Y, as
	 * the scene renderer lays them out) */
	local[0][0] = (gfloat) cell_x * ts;       local[0][1] = -(gfloat) cell_y * ts;
	local[1][0] = (gfloat) (cell_x + 1) * ts; local[1][1] = -(gfloat) cell_y * ts;
	local[2][0] = (gfloat) (cell_x + 1) * ts; local[2][1] = -(gfloat) (cell_y + 1) * ts;
	local[3][0] = (gfloat) cell_x * ts;       local[3][1] = -(gfloat) (cell_y + 1) * ts;
	for (i = 0; i < 4; i++)
		local[i][2] = 0.003f;

	m.view = view;
	m.ox = origin_x;
	m.oy = origin_y;
	m.scale = scale;
	lrg_editor_node_get_world_matrix (node, world);
	for (i = 0; i < 4; i++)
	{
		gfloat p[3];

		lrg_editor_math_transform_point (world, local[i], p);
		if (!map_point (&m, p, &qx[i], &qy[i]))
			return;
	}

	edge = erase ? theme_color (theme, LRG_THEME_COLOR_DANGER, rgba (0xf3, 0x8b, 0xa8, 0xff))
	             : theme_color (theme, LRG_THEME_COLOR_ACCENT, rgba (0x89, 0xb4, 0xfa, 0xff));
	fill = with_alpha (edge, 0x40);
	lrg_paint_list_triangle (list, qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], &fill);
	lrg_paint_list_triangle (list, qx[0], qy[0], qx[2], qy[2], qx[3], qy[3], &fill);
	for (i = 0; i < 4; i++)
		lrg_paint_list_line (list, qx[i], qy[i], qx[(i + 1) % 4], qy[(i + 1) % 4], 2.0f, &edge);
}

void
lrg_editor_gizmo_paint_play_frame (LrgPaintList *list,
                                   LrgTheme     *theme,
                                   gfloat        x,
                                   gfloat        y,
                                   gfloat        width,
                                   gfloat        height,
                                   gboolean      paused)
{
	GrlColor     c;
	GrlColor     ink;
	GrlColor     glow;
	GrlFont     *font = theme_font (theme);
	const gchar *label;
	gfloat       fs, tw = 0.0f, th = 0.0f, pw, ph, px, py;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));

	c = paused ? theme_color (theme, LRG_THEME_COLOR_WARNING, rgba (0xf9, 0xe2, 0xaf, 0xff))
	           : theme_color (theme, LRG_THEME_COLOR_SUCCESS, rgba (0xa6, 0xe3, 0xa1, 0xff));

	/* A soft inner glow under a crisp 2 px frame */
	glow = with_alpha (c, 0x50);
	lrg_paint_list_border (list, x, y, width, height, 0.0f, 6.0f, &glow);
	lrg_paint_list_border (list, x, y, width, height, 0.0f, 2.0f, &c);

	label = paused ? "PAUSED   F7 resume  F10 step  Shift+F5 stop"
	               : "PLAYING   changes are discarded on stop (Shift+F5)";
	fs = theme_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL, 12.0f);
	lrg_paint_measure_text (font, label, fs, 0.0f, &tw, &th);
	pw = tw + 24.0f;
	ph = MAX (th, fs) + 10.0f;
	px = roundf (x + (width - pw) * 0.5f);
	py = roundf (y + height - ph - 12.0f);
	lrg_paint_list_shadow (list, px, py, pw, ph, ph * 0.5f, 12.0f, 0.0f, 0.0f, 3.0f, &glow);
	lrg_paint_list_rect (list, px, py, pw, ph, ph * 0.5f, &c);
	ink = rgba (0x11, 0x11, 0x1b, 0xff);
	lrg_paint_list_text (list, font, label, px + 12.0f, roundf (py + (ph - MAX (th, fs)) * 0.5f), fs, 0.0f, &ink);
}

void
lrg_editor_gizmo_paint_missing_asset (LrgPaintList        *list,
                                      LrgTheme            *theme,
                                      const LrgEditorView *view,
                                      LrgNode             *node,
                                      gfloat               origin_x,
                                      gfloat               origin_y,
                                      gfloat               scale)
{
	Mapper            m;
	gfloat            world[16], min[3], max[3];
	gfloat            x0 = G_MAXFLOAT, y0 = G_MAXFLOAT, x1 = -G_MAXFLOAT, y1 = -G_MAXFLOAT;
	GrlColor          magenta = rgba (0xff, 0x2b, 0xd6, 0xff);
	GrlColor          fill = rgba (0xff, 0x2b, 0xd6, 0x28);
	LrgNodeVisual    *visual;
	const gchar      *asset;
	g_autofree gchar *base = NULL;
	g_autofree gchar *tag = NULL;
	gint              i;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (view != NULL);
	g_return_if_fail (LRG_IS_NODE (node));

	m.view = view;
	m.ox = origin_x;
	m.oy = origin_y;
	m.scale = scale;

	/* Screen bounds of the eight local-bounds corners */
	lrg_editor_node_get_local_bounds (node, min, max);
	lrg_editor_node_get_world_matrix (node, world);
	for (i = 0; i < 8; i++)
	{
		gfloat c[3], p[3], sx, sy;

		c[0] = (i & 1) ? max[0] : min[0];
		c[1] = (i & 2) ? max[1] : min[1];
		c[2] = (i & 4) ? max[2] : min[2];
		lrg_editor_math_transform_point (world, c, p);
		if (!map_point (&m, p, &sx, &sy))
			return;
		x0 = MIN (x0, sx);
		y0 = MIN (y0, sy);
		x1 = MAX (x1, sx);
		y1 = MAX (y1, sy);
	}
	if (x1 - x0 < 6.0f)
	{
		x0 -= 3.0f;
		x1 += 3.0f;
	}
	if (y1 - y0 < 6.0f)
	{
		y0 -= 3.0f;
		y1 += 3.0f;
	}

	lrg_paint_list_rect (list, x0, y0, x1 - x0, y1 - y0, 3.0f, &fill);
	lrg_paint_list_border (list, x0, y0, x1 - x0, y1 - y0, 3.0f, 2.0f, &magenta);

	visual = lrg_node_get_visual (node);
	asset = (visual != NULL) ? lrg_node_visual_get_asset (visual) : NULL;
	base = (asset != NULL) ? g_path_get_basename (asset) : g_strdup ("(no asset)");
	tag = g_strdup_printf ("missing: %s", base);
	lrg_editor_gizmo_paint_chip (list, theme, tag, x0, y1 + 4.0f, FALSE, &magenta);
}

void
lrg_editor_gizmo_paint_active_outline (LrgPaintList *list,
                                       LrgTheme     *theme,
                                       gfloat        x,
                                       gfloat        y,
                                       gfloat        width,
                                       gfloat        height,
                                       gfloat        strength)
{
	GrlColor accent;
	GrlColor glow;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));

	if (strength <= 0.01f)
		return;
	accent = fade (theme_color (theme, LRG_THEME_COLOR_ACCENT, rgba (0x89, 0xb4, 0xfa, 0xff)), strength);
	glow = with_alpha (accent, (guint8) (0x40 * strength));
	lrg_paint_list_border (list, x, y, width, height, 2.0f, 4.0f, &glow);
	lrg_paint_list_border (list, x, y, width, height, 2.0f, 1.5f, &accent);
}

/* ==========================================================================
 * Volume gizmos (0.9.0)
 *
 * The volume of the selected node, in world space, and the handles that
 * resize it. Everything here is derived from the node's world matrix and
 * visual params the same way the scene renderer derives what it draws:
 *
 *   point light   sphere of radius "range" at the node position
 *   spot light    cone from the node position along its local -Z, length
 *                 "range", full angle "spot_angle" (inner "spot_inner_angle",
 *                 default 0.8 x outer, as the viewport lights it)
 *   decal         the unit box (-0.5 .. 0.5) through world x diag(size_x,
 *                 size_y, size_z); it projects along local -Y
 *   probe         an axis-aligned world box of half sizes extents_x/y/z at
 *                 the node position (probes ignore rotation and scale), with
 *                 the blend band "blend_distance" inside it; probe volumes
 *                 (light probe grids, 0.9.0 M3) the same box without a band
 *
 * Handles are world points; hit testing projects them through the same
 * LrgEditorView the paint uses. A drag turns the pointer ray into a value
 * by the closest point on the handle's axis (ranges, sizes, extents) or
 * the intersection with the cone's base plane (angles).
 * ========================================================================== */

#define VOLUME_SEGMENTS (48)
#define HANDLE_RADIUS   (5.0f)

typedef enum
{
	VOL_NONE,
	VOL_POINT,
	VOL_SPOT,
	VOL_DECAL,
	VOL_PROBE
} VolumeKind;

GType
lrg_editor_volume_handle_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_VOLUME_HANDLE_NONE, "LRG_EDITOR_VOLUME_HANDLE_NONE", "none" },
			{ LRG_EDITOR_VOLUME_HANDLE_RANGE, "LRG_EDITOR_VOLUME_HANDLE_RANGE", "range" },
			{ LRG_EDITOR_VOLUME_HANDLE_CONE, "LRG_EDITOR_VOLUME_HANDLE_CONE", "cone" },
			{ LRG_EDITOR_VOLUME_HANDLE_INNER_CONE, "LRG_EDITOR_VOLUME_HANDLE_INNER_CONE", "inner-cone" },
			{ LRG_EDITOR_VOLUME_HANDLE_SIZE_X, "LRG_EDITOR_VOLUME_HANDLE_SIZE_X", "size-x" },
			{ LRG_EDITOR_VOLUME_HANDLE_SIZE_Y, "LRG_EDITOR_VOLUME_HANDLE_SIZE_Y", "size-y" },
			{ LRG_EDITOR_VOLUME_HANDLE_SIZE_Z, "LRG_EDITOR_VOLUME_HANDLE_SIZE_Z", "size-z" },
			{ LRG_EDITOR_VOLUME_HANDLE_EXTENT_X, "LRG_EDITOR_VOLUME_HANDLE_EXTENT_X", "extent-x" },
			{ LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y, "LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y", "extent-y" },
			{ LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z, "LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z", "extent-z" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorVolumeHandle"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

const gchar *
lrg_editor_volume_handle_get_param (LrgEditorVolumeHandle handle)
{
	static const gchar * const params[] = {
		NULL, "range", "spot_angle", "spot_inner_angle", "size_x", "size_y", "size_z",
		"extents_x", "extents_y", "extents_z"
	};

	if ((guint) handle >= G_N_ELEMENTS (params))
		return NULL;
	return params[handle];
}

static const gchar *
visual_string (LrgNodeVisual *visual,
               const gchar   *name)
{
	const GValue *v = lrg_node_visual_get_param (visual, name);

	return (v != NULL && G_VALUE_HOLDS_STRING (v)) ? g_value_get_string (v) : NULL;
}

static VolumeKind
volume_kind (LrgNode *node)
{
	LrgNodeVisual *visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;
	const gchar   *type;

	if (visual == NULL)
		return VOL_NONE;
	switch (lrg_node_visual_get_kind (visual))
	{
	case LRG_NODE_VISUAL_LIGHT:
		type = visual_string (visual, "light_type");
		if (g_strcmp0 (type, "directional") == 0)
			return VOL_NONE;
		if (g_strcmp0 (type, "spot") == 0 || lrg_node_visual_get_param_double (visual, "spot_angle", 0.0) > 0.0)
			return VOL_SPOT;
		return VOL_POINT;
	case LRG_NODE_VISUAL_DECAL:
		return VOL_DECAL;
	case LRG_NODE_VISUAL_REFLECTION_PROBE:
	case LRG_NODE_VISUAL_PROBE_VOLUME:
		/* a probe volume (0.9.0 M3) is the same axis-aligned box of
		 * extents_x/y/z, without a blend band */
		return VOL_PROBE;
	default:
		return VOL_NONE;
	}
}

gboolean
lrg_editor_volume_has_gizmo (LrgNode *node)
{
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);
	return volume_kind (node) != VOL_NONE;
}

/* --- small vector helpers --- */

static gfloat
v3_dot (const gfloat *a,
        const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
v3_cross (const gfloat *a,
          const gfloat *b,
          gfloat       *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static gfloat
v3_normalize (gfloat *v)
{
	gfloat len = sqrtf (v3_dot (v, v));

	if (len > 1e-8f)
	{
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
	return len;
}

/* p = a + b * k */
static void
v3_mad (const gfloat *a,
        const gfloat *b,
        gfloat        k,
        gfloat       *p)
{
	p[0] = a[0] + b[0] * k;
	p[1] = a[1] + b[1] * k;
	p[2] = a[2] + b[2] * k;
}

/* Two unit vectors perpendicular to @axis (unit) and each other. */
static void
perpendiculars (const gfloat *axis,
                gfloat       *u,
                gfloat       *w)
{
	const gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat side[3] = { 1.0f, 0.0f, 0.0f };

	v3_cross (axis, fabsf (axis[1]) < 0.95f ? up : side, u);
	v3_normalize (u);
	v3_cross (axis, u, w);
	v3_normalize (w);
}

/* The camera's right vector. */
static void
view_right (const LrgEditorView *view,
            gfloat              *out)
{
	gfloat fwd[3];

	fwd[0] = view->target[0] - view->position[0];
	fwd[1] = view->target[1] - view->position[1];
	fwd[2] = view->target[2] - view->position[2];
	v3_normalize (fwd);
	v3_cross (fwd, view->up, out);
	if (v3_normalize (out) < 1e-6f)
	{
		out[0] = 1.0f;
		out[1] = 0.0f;
		out[2] = 0.0f;
	}
}

/* The parameter t of the point on the line (o + a t) closest to the ray
 * (ro + rd s); FALSE when they are parallel. */
static gboolean
closest_on_axis (const gfloat *o,
                 const gfloat *a,
                 const gfloat *ro,
                 const gfloat *rd,
                 gfloat       *t)
{
	gfloat w[3], aa, ab, bb, aw, bw, den;

	w[0] = o[0] - ro[0];
	w[1] = o[1] - ro[1];
	w[2] = o[2] - ro[2];
	aa = v3_dot (a, a);
	ab = v3_dot (a, rd);
	bb = v3_dot (rd, rd);
	aw = v3_dot (a, w);
	bw = v3_dot (rd, w);
	den = aa * bb - ab * ab;
	if (fabsf (den) < 1e-9f)
		return FALSE;
	*t = (ab * bw - bb * aw) / den;
	return TRUE;
}

/* Everything one volume needs, from the node. */
typedef struct
{
	VolumeKind kind;
	gfloat     world[16];
	gfloat     pos[3];
	gfloat     dir[3];      /* spot: cone axis; decal: projection (-Y) */
	gfloat     axes[3][3];  /* decal: world box axes (unit); probe: world X Y Z */
	gfloat     axis_len[3]; /* decal: world length of one local unit per axis */
	gfloat     range;
	gfloat     outer;       /* spot: full angles in degrees */
	gfloat     inner;
	gfloat     half[3];     /* decal / probe: half sizes in world units */
	gfloat     blend;       /* probe */
	gfloat     size[3];     /* decal params */
} Volume;

static gboolean
volume_get (LrgNode *node,
            Volume  *vol)
{
	LrgNodeVisual *visual;
	const gfloat   fwd[3] = { 0.0f, 0.0f, -1.0f };
	gint           k;

	memset (vol, 0, sizeof *vol);
	vol->kind = volume_kind (node);
	if (vol->kind == VOL_NONE)
		return FALSE;
	visual = lrg_node_get_visual (node);
	lrg_editor_node_get_world_matrix (node, vol->world);
	vol->pos[0] = vol->world[12];
	vol->pos[1] = vol->world[13];
	vol->pos[2] = vol->world[14];

	switch (vol->kind)
	{
	case VOL_POINT:
	case VOL_SPOT:
		vol->range = (gfloat) MAX (lrg_node_visual_get_param_double (visual, "range", 10.0), 0.01);
		lrg_editor_math_transform_direction (vol->world, fwd, vol->dir);
		v3_normalize (vol->dir);
		vol->outer = (gfloat) lrg_node_visual_get_param_double (visual, "spot_angle", 0.0);
		if (vol->outer <= 0.0f)
			vol->outer = 45.0f;
		vol->outer = CLAMP (vol->outer, 1.0f, 179.0f);
		vol->inner = (gfloat) lrg_node_visual_get_param_double (visual, "spot_inner_angle", vol->outer * 0.8);
		vol->inner = CLAMP (vol->inner, 0.0f, vol->outer);
		break;
	case VOL_DECAL:
		vol->size[0] = (gfloat) lrg_node_visual_get_param_double (visual, "size_x", 1.0);
		vol->size[1] = (gfloat) lrg_node_visual_get_param_double (visual, "size_y", 1.0);
		vol->size[2] = (gfloat) lrg_node_visual_get_param_double (visual, "size_z", 1.0);
		for (k = 0; k < 3; k++)
		{
			vol->axes[k][0] = vol->world[k * 4];
			vol->axes[k][1] = vol->world[k * 4 + 1];
			vol->axes[k][2] = vol->world[k * 4 + 2];
			/* MAX() would normalise twice: take the length first */
			vol->axis_len[k] = v3_normalize (vol->axes[k]);
			vol->axis_len[k] = MAX (vol->axis_len[k], 1e-6f);
			vol->half[k] = 0.5f * vol->size[k] * vol->axis_len[k];
		}
		vol->dir[0] = -vol->axes[1][0];
		vol->dir[1] = -vol->axes[1][1];
		vol->dir[2] = -vol->axes[1][2];
		break;
	case VOL_PROBE:
		vol->half[0] = (gfloat) MAX (lrg_node_visual_get_param_double (visual, "extents_x", 5.0), 0.01);
		vol->half[1] = (gfloat) MAX (lrg_node_visual_get_param_double (visual, "extents_y", 5.0), 0.01);
		vol->half[2] = (gfloat) MAX (lrg_node_visual_get_param_double (visual, "extents_z", 5.0), 0.01);
		vol->blend = (lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PROBE_VOLUME) ? 0.0f
		             : (gfloat) MAX (lrg_node_visual_get_param_double (visual, "blend_distance", 1.0), 0.0);
		for (k = 0; k < 3; k++)
		{
			memset (vol->axes[k], 0, sizeof vol->axes[k]);
			vol->axes[k][k] = 1.0f;
			vol->axis_len[k] = 1.0f;
		}
		break;
	case VOL_NONE:
	default:
		return FALSE;
	}
	return TRUE;
}

/* The handles a volume kind has. */
static gboolean
volume_has_handle (const Volume          *vol,
                   LrgEditorVolumeHandle  handle)
{
	switch (vol->kind)
	{
	case VOL_POINT:
		return handle == LRG_EDITOR_VOLUME_HANDLE_RANGE;
	case VOL_SPOT:
		return handle == LRG_EDITOR_VOLUME_HANDLE_RANGE || handle == LRG_EDITOR_VOLUME_HANDLE_CONE ||
		       handle == LRG_EDITOR_VOLUME_HANDLE_INNER_CONE;
	case VOL_DECAL:
		return handle >= LRG_EDITOR_VOLUME_HANDLE_SIZE_X && handle <= LRG_EDITOR_VOLUME_HANDLE_SIZE_Z;
	case VOL_PROBE:
		return handle >= LRG_EDITOR_VOLUME_HANDLE_EXTENT_X && handle <= LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z;
	case VOL_NONE:
	default:
		return FALSE;
	}
}

/* World position of a handle (the volume is already computed). */
static void
handle_position (const LrgEditorView   *view,
                 const Volume          *vol,
                 LrgEditorVolumeHandle  handle,
                 gfloat                *out)
{
	gfloat u[3], w[3], base[3];
	gint   k;

	switch (handle)
	{
	case LRG_EDITOR_VOLUME_HANDLE_RANGE:
		if (vol->kind == VOL_POINT)
		{
			view_right (view, u);
			v3_mad (vol->pos, u, vol->range, out);
		}
		else
			v3_mad (vol->pos, vol->dir, vol->range, out);
		break;
	case LRG_EDITOR_VOLUME_HANDLE_CONE:
	case LRG_EDITOR_VOLUME_HANDLE_INNER_CONE:
		{
			gfloat angle = (handle == LRG_EDITOR_VOLUME_HANDLE_CONE) ? vol->outer : vol->inner;
			gfloat r = vol->range * tanf (angle * 0.5f * (gfloat) G_PI / 180.0f);

			perpendiculars (vol->dir, u, w);
			v3_mad (vol->pos, vol->dir, vol->range, base);
			/* outer on one side, inner on the other, so both stay grabbable */
			v3_mad (base, u, handle == LRG_EDITOR_VOLUME_HANDLE_CONE ? r : -r, out);
		}
		break;
	case LRG_EDITOR_VOLUME_HANDLE_SIZE_X:
	case LRG_EDITOR_VOLUME_HANDLE_SIZE_Y:
	case LRG_EDITOR_VOLUME_HANDLE_SIZE_Z:
	case LRG_EDITOR_VOLUME_HANDLE_EXTENT_X:
	case LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y:
	case LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z:
		k = (handle >= LRG_EDITOR_VOLUME_HANDLE_EXTENT_X) ? (gint) (handle - LRG_EDITOR_VOLUME_HANDLE_EXTENT_X)
		                                                  : (gint) (handle - LRG_EDITOR_VOLUME_HANDLE_SIZE_X);
		v3_mad (vol->pos, vol->axes[k], vol->half[k], out);
		break;
	case LRG_EDITOR_VOLUME_HANDLE_NONE:
	default:
		memcpy (out, vol->pos, 3 * sizeof (gfloat));
		break;
	}
}

gboolean
lrg_editor_volume_get_handle_position (const LrgEditorView   *view,
                                       LrgNode               *node,
                                       LrgEditorVolumeHandle  handle,
                                       gfloat                *out)
{
	Volume vol;

	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	if (!volume_get (node, &vol) || !volume_has_handle (&vol, handle))
		return FALSE;
	handle_position (view, &vol, handle, out);
	return TRUE;
}

LrgEditorVolumeHandle
lrg_editor_volume_hit_test (const LrgEditorView *view,
                            LrgNode             *node,
                            gfloat               x,
                            gfloat               y)
{
	Volume                vol;
	LrgEditorVolumeHandle best = LRG_EDITOR_VOLUME_HANDLE_NONE;
	gfloat                best_d = LRG_EDITOR_GIZMO_PICK_PIXELS + 3.0f;
	guint                 h;

	g_return_val_if_fail (view != NULL, LRG_EDITOR_VOLUME_HANDLE_NONE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), LRG_EDITOR_VOLUME_HANDLE_NONE);

	if (node == NULL || !volume_get (node, &vol))
		return LRG_EDITOR_VOLUME_HANDLE_NONE;
	for (h = LRG_EDITOR_VOLUME_HANDLE_RANGE; h <= LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z; h++)
	{
		gfloat p[3], u, v, d;

		if (!volume_has_handle (&vol, (LrgEditorVolumeHandle) h))
			continue;
		handle_position (view, &vol, (LrgEditorVolumeHandle) h, p);
		if (!lrg_editor_view_project (view, p, &u, &v, NULL))
			continue;
		d = hypotf (u - x, v - y);
		if (d < best_d)
		{
			best_d = d;
			best = (LrgEditorVolumeHandle) h;
		}
	}
	return best;
}

gboolean
lrg_editor_volume_drag_value (const LrgEditorView   *view,
                              LrgNode               *node,
                              LrgEditorVolumeHandle  handle,
                              gfloat                 x,
                              gfloat                 y,
                              gdouble               *out_value)
{
	Volume vol;
	gfloat ro[3], rd[3], axis[3], t;
	gint   k;

	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (out_value != NULL, FALSE);

	if (!volume_get (node, &vol) || !volume_has_handle (&vol, handle))
		return FALSE;
	lrg_editor_view_get_ray (view, x, y, ro, rd);

	switch (handle)
	{
	case LRG_EDITOR_VOLUME_HANDLE_RANGE:
		if (vol.kind == VOL_POINT)
			view_right (view, axis);
		else
			memcpy (axis, vol.dir, sizeof axis);
		if (!closest_on_axis (vol.pos, axis, ro, rd, &t))
			return FALSE;
		*out_value = MAX (fabsf (t), 0.05f);
		return TRUE;
	case LRG_EDITOR_VOLUME_HANDLE_CONE:
	case LRG_EDITOR_VOLUME_HANDLE_INNER_CONE:
		{
			/* the pointer on the base plane, its distance from the axis */
			gfloat base[3], hit[3], rel[3], along, radial, s, den, angle;

			v3_mad (vol.pos, vol.dir, vol.range, base);
			den = v3_dot (rd, vol.dir);
			if (fabsf (den) < 1e-5f)
				return FALSE;
			s = (v3_dot (base, vol.dir) - v3_dot (ro, vol.dir)) / den;
			if (s < 0.0f)
				return FALSE;
			v3_mad (ro, rd, s, hit);
			rel[0] = hit[0] - vol.pos[0];
			rel[1] = hit[1] - vol.pos[1];
			rel[2] = hit[2] - vol.pos[2];
			along = v3_dot (rel, vol.dir);
			rel[0] -= vol.dir[0] * along;
			rel[1] -= vol.dir[1] * along;
			rel[2] -= vol.dir[2] * along;
			radial = sqrtf (v3_dot (rel, rel));
			angle = 2.0f * atanf (radial / MAX (vol.range, 0.01f)) * 180.0f / (gfloat) G_PI;
			if (handle == LRG_EDITOR_VOLUME_HANDLE_CONE)
				*out_value = CLAMP (angle, 1.0f, 179.0f);
			else
				*out_value = CLAMP (angle, 0.0f, vol.outer);
			return TRUE;
		}
	case LRG_EDITOR_VOLUME_HANDLE_SIZE_X:
	case LRG_EDITOR_VOLUME_HANDLE_SIZE_Y:
	case LRG_EDITOR_VOLUME_HANDLE_SIZE_Z:
		k = (gint) (handle - LRG_EDITOR_VOLUME_HANDLE_SIZE_X);
		if (!closest_on_axis (vol.pos, vol.axes[k], ro, rd, &t))
			return FALSE;
		/* the box stays centred: the full size is twice the reach, in
		 * the node's local units */
		*out_value = MAX (2.0f * fabsf (t) / vol.axis_len[k], 0.01f);
		return TRUE;
	case LRG_EDITOR_VOLUME_HANDLE_EXTENT_X:
	case LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y:
	case LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z:
		k = (gint) (handle - LRG_EDITOR_VOLUME_HANDLE_EXTENT_X);
		if (!closest_on_axis (vol.pos, vol.axes[k], ro, rd, &t))
			return FALSE;
		*out_value = MAX (fabsf (t), 0.05f);
		return TRUE;
	case LRG_EDITOR_VOLUME_HANDLE_NONE:
	default:
		return FALSE;
	}
}

/* --- painting --- */

/* A world-space polyline segment, clipped crudely: both ends must be in
 * front of the camera. */
static void
world_line (LrgPaintList   *list,
            const Mapper   *m,
            const gfloat   *a,
            const gfloat   *b,
            gfloat          width,
            const GrlColor *color)
{
	gfloat ax, ay, bx, by;

	if (map_point (m, a, &ax, &ay) && map_point (m, b, &bx, &by))
		lrg_paint_list_line (list, ax, ay, bx, by, width, color);
}

/* A circle of radius @r around @c in the plane spanned by @u and @w;
 * @dashed skips every other segment. */
static void
world_circle (LrgPaintList   *list,
              const Mapper   *m,
              const gfloat   *c,
              const gfloat   *u,
              const gfloat   *w,
              gfloat          r,
              gfloat          width,
              gboolean        dashed,
              const GrlColor *color)
{
	gfloat prev[3];
	gint   s;

	for (s = 0; s <= VOLUME_SEGMENTS; s++)
	{
		gfloat a = (gfloat) s / (gfloat) VOLUME_SEGMENTS * 2.0f * (gfloat) G_PI;
		gfloat p[3];
		gint   k;

		for (k = 0; k < 3; k++)
			p[k] = c[k] + (u[k] * cosf (a) + w[k] * sinf (a)) * r;
		if (s > 0 && (!dashed || (s & 1) == 0))
			world_line (list, m, prev, p, width, color);
		memcpy (prev, p, sizeof prev);
	}
}

/* The 12 edges of a box: centre, three unit axes and half sizes. */
static void
world_box (LrgPaintList   *list,
           const Mapper   *m,
           const gfloat   *c,
           gfloat          axes[3][3],
           const gfloat   *half,
           gfloat          width,
           const GrlColor *color)
{
	gfloat corner[8][3];
	gint   i, k, e;

	for (i = 0; i < 8; i++)
		for (k = 0; k < 3; k++)
			corner[i][k] = c[k] + axes[0][k] * half[0] * ((i & 1) ? 1.0f : -1.0f) +
			               axes[1][k] * half[1] * ((i & 2) ? 1.0f : -1.0f) +
			               axes[2][k] * half[2] * ((i & 4) ? 1.0f : -1.0f);
	for (i = 0; i < 8; i++)
		for (e = 0; e < 3; e++)
			if (!(i & (1 << e)))
				world_line (list, m, corner[i], corner[i | (1 << e)], width, color);
}

static void
paint_handle (LrgPaintList   *list,
              const Mapper   *m,
              const gfloat   *p,
              gboolean        hot,
              const GrlColor *color)
{
	GrlColor ring = rgba (0x11, 0x11, 0x1b, 0xd0);
	GrlColor fill = hot ? highlight_color () : *color;
	gfloat   x, y;

	if (!map_point (m, p, &x, &y))
		return;
	lrg_paint_list_circle (list, x, y, HANDLE_RADIUS + (hot ? 2.5f : 1.5f), &ring);
	lrg_paint_list_circle (list, x, y, HANDLE_RADIUS + (hot ? 1.0f : 0.0f), &fill);
}

void
lrg_editor_gizmo_paint_volume (LrgPaintList          *list,
                               LrgTheme              *theme,
                               const LrgEditorView   *view,
                               LrgNode               *node,
                               LrgEditorVolumeHandle  hot,
                               gboolean               dragging,
                               gfloat                 origin_x,
                               gfloat                 origin_y,
                               gfloat                 scale)
{
	Volume       vol;
	Mapper       m;
	GrlColor     line;
	GrlColor     soft;
	GrlColor     handle_col;
	gfloat       u[3], w[3], base[3];
	guint        h;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (view != NULL);
	g_return_if_fail (LRG_IS_NODE (node));

	if (!volume_get (node, &vol))
		return;
	m.view = view;
	m.ox = origin_x;
	m.oy = origin_y;
	m.scale = scale;

	/* Colour by kind: lights warm, decals pink, probes cyan */
	switch (vol.kind)
	{
	case VOL_POINT:
	case VOL_SPOT:
		line = rgba (0xf9, 0xe2, 0xaf, 0xe0);
		break;
	case VOL_DECAL:
		line = rgba (0xf5, 0xc2, 0xe7, 0xe0);
		break;
	default:
		line = rgba (0x89, 0xdc, 0xeb, 0xe0);
		break;
	}
	soft = with_alpha (line, 0x60);
	handle_col = line;
	handle_col.a = 0xff;

	switch (vol.kind)
	{
	case VOL_POINT:
		{
			/* the silhouette (camera-facing circle) and two great circles */
			gfloat fwd[3], vr[3], vu[3];
			const gfloat x[3] = { 1.0f, 0.0f, 0.0f };
			const gfloat y[3] = { 0.0f, 1.0f, 0.0f };
			const gfloat z[3] = { 0.0f, 0.0f, 1.0f };

			fwd[0] = view->target[0] - view->position[0];
			fwd[1] = view->target[1] - view->position[1];
			fwd[2] = view->target[2] - view->position[2];
			v3_normalize (fwd);
			view_right (view, vr);
			v3_cross (vr, fwd, vu);
			v3_normalize (vu);
			world_circle (list, &m, vol.pos, vr, vu, vol.range, 2.0f, FALSE, &line);
			world_circle (list, &m, vol.pos, x, z, vol.range, 1.2f, FALSE, &soft);
			world_circle (list, &m, vol.pos, x, y, vol.range, 1.0f, TRUE, &soft);
		}
		break;
	case VOL_SPOT:
		{
			gfloat ro = vol.range * tanf (vol.outer * 0.5f * (gfloat) G_PI / 180.0f);
			gfloat ri = vol.range * tanf (vol.inner * 0.5f * (gfloat) G_PI / 180.0f);
			gint   i;

			perpendiculars (vol.dir, u, w);
			v3_mad (vol.pos, vol.dir, vol.range, base);
			world_circle (list, &m, base, u, w, ro, 2.0f, FALSE, &line);
			world_circle (list, &m, base, u, w, ri, 1.2f, TRUE, &soft);
			for (i = 0; i < 4; i++)
			{
				gfloat a = (gfloat) i * 0.5f * (gfloat) G_PI;
				gfloat rim[3];
				gint   k;

				for (k = 0; k < 3; k++)
					rim[k] = base[k] + (u[k] * cosf (a) + w[k] * sinf (a)) * ro;
				world_line (list, &m, vol.pos, rim, 1.5f, &line);
			}
			world_line (list, &m, vol.pos, base, 1.0f, &soft);
		}
		break;
	case VOL_DECAL:
		{
			gfloat tip[3];

			world_box (list, &m, vol.pos, vol.axes, vol.half, 1.8f, &line);
			/* the projection direction: an arrow down the box's -Y */
			v3_mad (vol.pos, vol.dir, vol.half[1], tip);
			world_line (list, &m, vol.pos, tip, 2.0f, &line);
			{
				gfloat tx, ty;

				if (map_point (&m, tip, &tx, &ty))
					lrg_paint_list_circle (list, tx, ty, 3.0f, &line);
			}
		}
		break;
	case VOL_PROBE:
		{
			gfloat inner[3];
			gint   k;

			world_box (list, &m, vol.pos, vol.axes, vol.half, 2.0f, &line);
			for (k = 0; k < 3; k++)
				inner[k] = MAX (vol.half[k] - vol.blend, 0.0f);
			if (vol.blend > 0.0f)
				world_box (list, &m, vol.pos, vol.axes, inner, 1.0f, &soft);
		}
		break;
	case VOL_NONE:
	default:
		break;
	}

	/* Handles and the value of the one being dragged */
	for (h = LRG_EDITOR_VOLUME_HANDLE_RANGE; h <= LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z; h++)
	{
		gfloat p[3];

		if (!volume_has_handle (&vol, (LrgEditorVolumeHandle) h))
			continue;
		handle_position (view, &vol, (LrgEditorVolumeHandle) h, p);
		paint_handle (list, &m, p, h == (guint) hot, &handle_col);
		if (h == (guint) hot && hot != LRG_EDITOR_VOLUME_HANDLE_NONE)
		{
			LrgNodeVisual    *visual = lrg_node_get_visual (node);
			const gchar      *param = lrg_editor_volume_handle_get_param (hot);
			g_autofree gchar *text = NULL;
			gfloat            x, y, value;

			if (!map_point (&m, p, &x, &y))
				continue;
			if (hot == LRG_EDITOR_VOLUME_HANDLE_CONE)
				value = vol.outer;
			else if (hot == LRG_EDITOR_VOLUME_HANDLE_INNER_CONE)
				value = vol.inner;
			else
				value = (gfloat) lrg_node_visual_get_param_double (visual, param,
				                                                   hot == LRG_EDITOR_VOLUME_HANDLE_RANGE ? 10.0
				                                                   : hot >= LRG_EDITOR_VOLUME_HANDLE_EXTENT_X ? 5.0
				                                                                                              : 1.0);
			text = (hot == LRG_EDITOR_VOLUME_HANDLE_CONE || hot == LRG_EDITOR_VOLUME_HANDLE_INNER_CONE)
			       ? g_strdup_printf ("%s %.1f°", param, (gdouble) value)
			       : g_strdup_printf ("%s %.2f", param, (gdouble) value);
			/* a hovered handle names its param; a dragged one is accented */
			lrg_editor_gizmo_paint_chip (list, theme, text, x + 10.0f, y + 8.0f, FALSE,
			                             dragging ? &handle_col : NULL);
		}
	}
}

/* --- LOD bounds --- */

gfloat
lrg_editor_lod_node_screen_size (const LrgEditorView *view,
                                 LrgNode             *node,
                                 LrgLodGroup         *group)
{
	gfloat world[16], center[3], c[3], radius, scl = 0.0f, dist;
	gint   k;

	g_return_val_if_fail (view != NULL, 0.0f);
	g_return_val_if_fail (LRG_IS_NODE (node), 0.0f);
	g_return_val_if_fail (LRG_IS_LOD_GROUP (group), 0.0f);

	if (!lrg_lod_group_get_bounding_sphere (group, center, &radius))
		return 1.0f;
	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_math_transform_point (world, center, c);
	for (k = 0; k < 3; k++)
		scl = MAX (scl, sqrtf (world[k * 4] * world[k * 4] + world[k * 4 + 1] * world[k * 4 + 1] +
		                       world[k * 4 + 2] * world[k * 4 + 2]));
	radius *= scl;
	if (view->orthographic)
		return (view->ortho_height > 0.0f) ? radius * 2.0f / view->ortho_height : 1.0f;
	dist = sqrtf ((c[0] - view->position[0]) * (c[0] - view->position[0]) +
	              (c[1] - view->position[1]) * (c[1] - view->position[1]) +
	              (c[2] - view->position[2]) * (c[2] - view->position[2]));
	return lrg_lod_compute_screen_size (radius, dist, view->fovy);
}

void
lrg_editor_gizmo_paint_lod_bounds (LrgPaintList        *list,
                                   LrgTheme            *theme,
                                   const LrgEditorView *view,
                                   LrgNode             *node,
                                   LrgLodGroup         *group,
                                   gfloat               origin_x,
                                   gfloat               origin_y,
                                   gfloat               scale)
{
	Mapper            m;
	gfloat            world[16], center[3], c[3], edge[3], right[3], radius, scl = 0.0f;
	gfloat            cx, cy, ex, ey, rpx, size;
	gint              level, k, forced;
	GrlColor          ring = rgba (0xa6, 0xe3, 0xa1, 0xd0);
	GrlColor          glow = rgba (0xa6, 0xe3, 0xa1, 0x30);
	g_autofree gchar *text = NULL;

	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (view != NULL);
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (LRG_IS_LOD_GROUP (group));

	if (!lrg_lod_group_get_bounding_sphere (group, center, &radius))
		return;
	m.view = view;
	m.ox = origin_x;
	m.oy = origin_y;
	m.scale = scale;
	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_math_transform_point (world, center, c);
	for (k = 0; k < 3; k++)
		scl = MAX (scl, sqrtf (world[k * 4] * world[k * 4] + world[k * 4 + 1] * world[k * 4 + 1] +
		                       world[k * 4 + 2] * world[k * 4 + 2]));
	view_right (view, right);
	v3_mad (c, right, radius * scl, edge);
	if (!map_point (&m, c, &cx, &cy) || !map_point (&m, edge, &ex, &ey))
		return;
	rpx = hypotf (ex - cx, ey - cy);
	lrg_paint_list_circle (list, cx, cy, rpx + 3.0f, &glow);
	{
		gint s;

		/* a dashed ring: the sphere the screen size is measured with */
		for (s = 0; s < 64; s += 2)
		{
			gfloat a0 = (gfloat) s / 64.0f * 2.0f * (gfloat) G_PI;
			gfloat a1 = (gfloat) (s + 1) / 64.0f * 2.0f * (gfloat) G_PI;

			lrg_paint_list_line (list, cx + cosf (a0) * rpx, cy + sinf (a0) * rpx, cx + cosf (a1) * rpx,
			                     cy + sinf (a1) * rpx, 1.5f, &ring);
		}
	}

	size = lrg_editor_lod_node_screen_size (view, node, group);
	forced = lrg_lod_group_get_force_level (group);
	level = lrg_lod_group_select_level (group, size, LRG_LOD_LEVEL_NONE);
	if (level == LRG_LOD_LEVEL_CULLED)
		text = g_strdup_printf ("LOD culled   %.0f%% screen", (gdouble) size * 100.0);
	else
		text = g_strdup_printf ("LOD %d%s   %d tris   %.0f%% screen (>= %.0f%%)", level,
		                        forced >= 0 ? " forced" : "",
		                        lrg_lod_group_get_level_triangle_count (group, (guint) level), (gdouble) size * 100.0,
		                        (gdouble) lrg_lod_group_get_level_screen_size (group, (guint) level) * 100.0);
	lrg_editor_gizmo_paint_chip (list, theme, text, cx - rpx, cy + rpx + 6.0f, FALSE, &ring);
}
