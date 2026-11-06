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
