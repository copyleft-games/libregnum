/* lrg-paint-icons.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in SDF icons and SVG icon rasterization.
 *
 * Built-in icons are designed on a unit square (0..1 on both axes, y
 * down) and drawn from paint primitives scaled to the requested box:
 * strokes are round-capped lines, so polylines join cleanly, and fills
 * are triangles, circles and rounded rectangles.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <gio/gio.h>

#include "lrg-paint-icons.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Drawing context
 * ========================================================================== */

typedef struct
{
	LrgPaintList   *list;
	gfloat          x;
	gfloat          y;
	gfloat          s;   /* box size */
	gfloat          t;   /* stroke width */
	const GrlColor *c;
} Pen;

/* px / py: unit coordinates to canvas */
static gfloat
px (const Pen *p,
    gfloat     u)
{
	return p->x + u * p->s;
}

static gfloat
py (const Pen *p,
    gfloat     v)
{
	return p->y + v * p->s;
}

/* line: one stroke between two unit points */
static void
line (const Pen *p,
      gfloat     u0,
      gfloat     v0,
      gfloat     u1,
      gfloat     v1)
{
	lrg_paint_list_line (p->list, px (p, u0), py (p, v0), px (p, u1), py (p, v1), p->t, p->c);
}

/* polyline: strokes through @n points given as u, v pairs */
static void
polyline (const Pen    *p,
          const gfloat *pts,
          guint         n)
{
	guint i;

	for (i = 0; i + 1 < n; i++)
		line (p, pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3]);
}

/* disc: a filled circle */
static void
disc (const Pen *p,
      gfloat     u,
      gfloat     v,
      gfloat     r)
{
	lrg_paint_list_circle (p->list, px (p, u), py (p, v), r * p->s, p->c);
}

/* ring: a circle outline of stroke @width (unit) */
static void
ring (const Pen *p,
      gfloat     u,
      gfloat     v,
      gfloat     r,
      gfloat     width)
{
	gfloat rr = r * p->s + width * 0.5f;

	lrg_paint_list_border (p->list, px (p, u) - rr, py (p, v) - rr, rr * 2.0f, rr * 2.0f,
	                       rr, width, p->c);
}

/* box: a filled rounded rectangle in unit coordinates */
static void
box (const Pen *p,
     gfloat     u0,
     gfloat     v0,
     gfloat     u1,
     gfloat     v1,
     gfloat     radius)
{
	lrg_paint_list_rect (p->list, px (p, u0), py (p, v0), (u1 - u0) * p->s, (v1 - v0) * p->s,
	                     radius * p->s, p->c);
}

/* frame: a rounded rectangle outline (stroke centred on the edge) */
static void
frame (const Pen *p,
       gfloat     u0,
       gfloat     v0,
       gfloat     u1,
       gfloat     v1,
       gfloat     radius)
{
	gfloat h = p->t * 0.5f;

	lrg_paint_list_border (p->list, px (p, u0) - h, py (p, v0) - h,
	                       (u1 - u0) * p->s + p->t, (v1 - v0) * p->s + p->t,
	                       radius * p->s + h, p->t, p->c);
}

/*
 * tri: a filled triangle. Each vertex is pushed half a unit away from
 * the centroid so triangles that share an edge overlap instead of
 * leaving an anti-aliasing seam between them.
 */
static void
tri (const Pen *p,
     gfloat     u0,
     gfloat     v0,
     gfloat     u1,
     gfloat     v1,
     gfloat     u2,
     gfloat     v2)
{
	gfloat x[3];
	gfloat y[3];
	gfloat cx;
	gfloat cy;
	guint  i;

	x[0] = px (p, u0); y[0] = py (p, v0);
	x[1] = px (p, u1); y[1] = py (p, v1);
	x[2] = px (p, u2); y[2] = py (p, v2);
	cx = (x[0] + x[1] + x[2]) / 3.0f;
	cy = (y[0] + y[1] + y[2]) / 3.0f;

	for (i = 0; i < 3; i++)
	{
		gfloat dx = x[i] - cx;
		gfloat dy = y[i] - cy;
		gfloat len = sqrtf (dx * dx + dy * dy);

		if (len > 1e-4f)
		{
			x[i] += dx / len * 0.5f;
			y[i] += dy / len * 0.5f;
		}
	}

	lrg_paint_list_triangle (p->list, x[0], y[0], x[1], y[1], x[2], y[2], p->c);
}

/* arc: a stroked arc from @a0 to @a1 degrees (0 = right, clockwise) */
static void
arc (const Pen *p,
     gfloat     u,
     gfloat     v,
     gfloat     r,
     gfloat     a0,
     gfloat     a1)
{
	guint  n = 16;
	guint  i;
	gfloat prev_u = 0.0f;
	gfloat prev_v = 0.0f;

	for (i = 0; i <= n; i++)
	{
		gfloat a = (a0 + (a1 - a0) * (gfloat) i / (gfloat) n) * (gfloat) G_PI / 180.0f;
		gfloat cu = u + cosf (a) * r;
		gfloat cv = v + sinf (a) * r;

		if (i > 0)
			line (p, prev_u, prev_v, cu, cv);
		prev_u = cu;
		prev_v = cv;
	}
}

/* star: five points; @mode 0 filled, 1 left half filled, 2 outline */
static void
star (const Pen *p,
      gint       mode)
{
	gfloat u[10];
	gfloat v[10];
	gfloat cu = 0.5f;
	gfloat cv = 0.54f;
	guint  k;

	/* Outer and inner points alternate, the first one pointing up */
	for (k = 0; k < 10; k++)
	{
		gfloat a = (-90.0f + 36.0f * (gfloat) k) * (gfloat) G_PI / 180.0f;
		gfloat r = (k % 2 == 0) ? 0.44f : 0.18f;

		u[k] = cu + cosf (a) * r;
		v[k] = cv + sinf (a) * r;
	}

	/* Fan from the centre; points 5..9 then 0 cover the left half */
	for (k = 0; k < 10; k++)
	{
		guint next = (k + 1) % 10;

		if (mode == 0 || (mode == 1 && k >= 5))
			tri (p, cu, cv, u[k], v[k], u[next], v[next]);
	}

	if (mode != 0)
		for (k = 0; k < 10; k++)
			line (p, u[k], v[k], u[(k + 1) % 10], v[(k + 1) % 10]);
}

/* curve: a plain cubic stroke in unit coordinates */
static void
curve (const Pen *p,
       gfloat     u0,
       gfloat     v0,
       gfloat     u1,
       gfloat     v1,
       gfloat     u2,
       gfloat     v2,
       gfloat     u3,
       gfloat     v3)
{
	LrgPaintCurveStyle style;

	lrg_paint_curve_style_init (&style, p->c);
	lrg_paint_list_curve (p->list, px (p, u0), py (p, v0), px (p, u1), py (p, v1),
	                      px (p, u2), py (p, v2), px (p, u3), py (p, v3), p->t, &style);
}

/* ==========================================================================
 * Built-in icons
 * ========================================================================== */

void
lrg_paint_list_icon (LrgPaintList   *self,
                     LrgIcon         icon,
                     gfloat          x,
                     gfloat          y,
                     gfloat          size,
                     gfloat          thickness,
                     const GrlColor *color)
{
	Pen pen;
	Pen *p = &pen;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (color != NULL);

	if (size <= 0.0f || color->a == 0)
		return;

	pen.list = self;
	pen.x = x;
	pen.y = y;
	pen.s = size;
	pen.t = thickness > 0.0f ? thickness : MAX (size * 0.09f, 1.0f);
	pen.c = color;

	switch (icon)
	{
	case LRG_ICON_CHECK:
		{
			static const gfloat pts[] = { 0.2f, 0.52f, 0.42f, 0.73f, 0.8f, 0.3f };

			polyline (p, pts, 3);
		}
		break;

	case LRG_ICON_CROSS:
		line (p, 0.25f, 0.25f, 0.75f, 0.75f);
		line (p, 0.75f, 0.25f, 0.25f, 0.75f);
		break;

	case LRG_ICON_CHEVRON_UP:
		{
			static const gfloat pts[] = { 0.25f, 0.62f, 0.5f, 0.37f, 0.75f, 0.62f };

			polyline (p, pts, 3);
		}
		break;

	case LRG_ICON_CHEVRON_DOWN:
		{
			static const gfloat pts[] = { 0.25f, 0.38f, 0.5f, 0.63f, 0.75f, 0.38f };

			polyline (p, pts, 3);
		}
		break;

	case LRG_ICON_CHEVRON_LEFT:
		{
			static const gfloat pts[] = { 0.62f, 0.25f, 0.37f, 0.5f, 0.62f, 0.75f };

			polyline (p, pts, 3);
		}
		break;

	case LRG_ICON_CHEVRON_RIGHT:
		{
			static const gfloat pts[] = { 0.38f, 0.25f, 0.63f, 0.5f, 0.38f, 0.75f };

			polyline (p, pts, 3);
		}
		break;

	case LRG_ICON_PLUS:
		line (p, 0.5f, 0.2f, 0.5f, 0.8f);
		line (p, 0.2f, 0.5f, 0.8f, 0.5f);
		break;

	case LRG_ICON_MINUS:
		line (p, 0.2f, 0.5f, 0.8f, 0.5f);
		break;

	case LRG_ICON_SEARCH:
		ring (p, 0.43f, 0.43f, 0.24f, p->t);
		line (p, 0.61f, 0.61f, 0.82f, 0.82f);
		break;

	case LRG_ICON_MENU:
		line (p, 0.2f, 0.28f, 0.8f, 0.28f);
		line (p, 0.2f, 0.5f, 0.8f, 0.5f);
		line (p, 0.2f, 0.72f, 0.8f, 0.72f);
		break;

	case LRG_ICON_GEAR:
		{
			guint  k;
			gfloat w = MAX (p->t, size * 0.12f);
			Pen    teeth = pen;

			/* A thick ring (its hole is the axle) and eight teeth */
			ring (p, 0.5f, 0.5f, 0.2f, w);
			teeth.t = w;
			for (k = 0; k < 8; k++)
			{
				gfloat a = (gfloat) k * (gfloat) G_PI / 4.0f;

				line (&teeth, 0.5f + cosf (a) * 0.3f, 0.5f + sinf (a) * 0.3f,
				      0.5f + cosf (a) * 0.38f, 0.5f + sinf (a) * 0.38f);
			}
		}
		break;

	case LRG_ICON_STAR:
		star (p, 0);
		break;

	case LRG_ICON_STAR_HALF:
		star (p, 1);
		break;

	case LRG_ICON_STAR_OUTLINE:
		star (p, 2);
		break;

	case LRG_ICON_HEART:
		disc (p, 0.34f, 0.38f, 0.18f);
		disc (p, 0.66f, 0.38f, 0.18f);
		tri (p, 0.175f, 0.45f, 0.825f, 0.45f, 0.5f, 0.84f);
		break;

	case LRG_ICON_PLAY:
		tri (p, 0.3f, 0.2f, 0.3f, 0.8f, 0.8f, 0.5f);
		break;

	case LRG_ICON_PAUSE:
		box (p, 0.27f, 0.2f, 0.42f, 0.8f, 0.03f);
		box (p, 0.58f, 0.2f, 0.73f, 0.8f, 0.03f);
		break;

	case LRG_ICON_STOP:
		box (p, 0.24f, 0.24f, 0.76f, 0.76f, 0.06f);
		break;

	case LRG_ICON_INFO:
		ring (p, 0.5f, 0.5f, 0.4f, p->t);
		disc (p, 0.5f, 0.31f, p->t * 0.75f / size);
		line (p, 0.5f, 0.46f, 0.5f, 0.72f);
		break;

	case LRG_ICON_WARNING:
		{
			static const gfloat pts[] = {
				0.5f, 0.13f, 0.9f, 0.85f, 0.1f, 0.85f, 0.5f, 0.13f
			};

			polyline (p, pts, 4);
			line (p, 0.5f, 0.4f, 0.5f, 0.6f);
			disc (p, 0.5f, 0.72f, p->t * 0.7f / size);
		}
		break;

	case LRG_ICON_CLOSE:
		ring (p, 0.5f, 0.5f, 0.4f, p->t);
		line (p, 0.36f, 0.36f, 0.64f, 0.64f);
		line (p, 0.64f, 0.36f, 0.36f, 0.64f);
		break;

	case LRG_ICON_ARROW_LEFT:
		line (p, 0.8f, 0.5f, 0.2f, 0.5f);
		line (p, 0.2f, 0.5f, 0.44f, 0.26f);
		line (p, 0.2f, 0.5f, 0.44f, 0.74f);
		break;

	case LRG_ICON_ARROW_RIGHT:
		line (p, 0.2f, 0.5f, 0.8f, 0.5f);
		line (p, 0.8f, 0.5f, 0.56f, 0.26f);
		line (p, 0.8f, 0.5f, 0.56f, 0.74f);
		break;

	case LRG_ICON_FOLDER:
		{
			static const gfloat tab[] = {
				0.12f, 0.32f, 0.12f, 0.2f, 0.4f, 0.2f, 0.48f, 0.3f
			};

			polyline (p, tab, 4);
			frame (p, 0.12f, 0.3f, 0.88f, 0.82f, 0.04f);
		}
		break;

	case LRG_ICON_FILE:
		{
			static const gfloat body[] = {
				0.6f, 0.12f, 0.25f, 0.12f, 0.25f, 0.88f, 0.78f, 0.88f, 0.78f, 0.3f, 0.6f, 0.12f
			};
			static const gfloat fold[] = { 0.6f, 0.12f, 0.6f, 0.3f, 0.78f, 0.3f };

			polyline (p, body, 6);
			polyline (p, fold, 3);
		}
		break;

	case LRG_ICON_EYE:
		curve (p, 0.08f, 0.5f, 0.3f, 0.16f, 0.7f, 0.16f, 0.92f, 0.5f);
		curve (p, 0.08f, 0.5f, 0.3f, 0.84f, 0.7f, 0.84f, 0.92f, 0.5f);
		disc (p, 0.5f, 0.5f, 0.13f);
		break;

	case LRG_ICON_LOCK:
		box (p, 0.22f, 0.45f, 0.78f, 0.86f, 0.06f);
		arc (p, 0.5f, 0.42f, 0.17f, 180.0f, 360.0f);
		line (p, 0.33f, 0.42f, 0.33f, 0.47f);
		line (p, 0.67f, 0.42f, 0.67f, 0.47f);
		break;

	case LRG_ICON_HOME:
		{
			static const gfloat roof[] = { 0.12f, 0.5f, 0.5f, 0.15f, 0.88f, 0.5f };
			static const gfloat walls[] = {
				0.22f, 0.42f, 0.22f, 0.86f, 0.78f, 0.86f, 0.78f, 0.42f
			};
			static const gfloat door[] = { 0.42f, 0.86f, 0.42f, 0.62f, 0.58f, 0.62f, 0.58f, 0.86f };

			polyline (p, roof, 3);
			polyline (p, walls, 4);
			polyline (p, door, 4);
		}
		break;

	case LRG_ICON_TRASH:
		{
			static const gfloat handle[] = {
				0.4f, 0.24f, 0.4f, 0.14f, 0.6f, 0.14f, 0.6f, 0.24f
			};
			static const gfloat body[] = {
				0.25f, 0.24f, 0.3f, 0.88f, 0.7f, 0.88f, 0.75f, 0.24f
			};

			line (p, 0.16f, 0.24f, 0.84f, 0.24f);
			polyline (p, handle, 4);
			polyline (p, body, 4);
			line (p, 0.43f, 0.4f, 0.43f, 0.74f);
			line (p, 0.57f, 0.4f, 0.57f, 0.74f);
		}
		break;

	case LRG_ICON_EDIT:
		{
			/* Body along the diagonal, a triangular tip at the lower
			 * left, the eraser end squared off by the cap */
			Pen    body = pen;
			gfloat w = MAX (p->t * 2.4f, size * 0.16f);
			gfloat hw = w * 0.5f / size;
			gfloat d = 0.70710678f;

			body.t = w;
			line (&body, 0.36f, 0.64f, 0.76f, 0.24f);
			tri (p, 0.17f, 0.83f,
			     0.32f - d * hw, 0.68f - d * hw,
			     0.32f + d * hw, 0.68f + d * hw);
		}
		break;

	case LRG_ICON_REFRESH:
		{
			gfloat a = 300.0f * (gfloat) G_PI / 180.0f;
			gfloat eu = 0.5f + cosf (a) * 0.3f;
			gfloat ev = 0.5f + sinf (a) * 0.3f;
			gfloat h = MAX (p->t * 1.6f, size * 0.12f) / size;

			arc (p, 0.5f, 0.5f, 0.3f, 20.0f, 300.0f);
			/* Arrow head pointing along the arc's direction at its end */
			tri (p, eu + h * 1.2f, ev + h * 0.6f, eu - h * 0.9f, ev - h * 0.6f,
			     eu + h * 0.5f, ev - h * 1.4f);
		}
		break;

	case LRG_ICON_DOWNLOAD:
		{
			static const gfloat head[] = { 0.3f, 0.42f, 0.5f, 0.62f, 0.7f, 0.42f };
			static const gfloat tray[] = {
				0.15f, 0.66f, 0.15f, 0.86f, 0.85f, 0.86f, 0.85f, 0.66f
			};

			line (p, 0.5f, 0.14f, 0.5f, 0.62f);
			polyline (p, head, 3);
			polyline (p, tray, 4);
		}
		break;

	case LRG_ICON_UPLOAD:
		{
			static const gfloat head[] = { 0.3f, 0.34f, 0.5f, 0.14f, 0.7f, 0.34f };
			static const gfloat tray[] = {
				0.15f, 0.66f, 0.15f, 0.86f, 0.85f, 0.86f, 0.85f, 0.66f
			};

			line (p, 0.5f, 0.14f, 0.5f, 0.62f);
			polyline (p, head, 3);
			polyline (p, tray, 4);
		}
		break;

	case LRG_ICON_CALENDAR:
		{
			guint i;
			guint j;

			frame (p, 0.15f, 0.22f, 0.85f, 0.86f, 0.06f);
			line (p, 0.15f, 0.4f, 0.85f, 0.4f);
			line (p, 0.34f, 0.13f, 0.34f, 0.29f);
			line (p, 0.66f, 0.13f, 0.66f, 0.29f);
			for (j = 0; j < 2; j++)
				for (i = 0; i < 3; i++)
					box (p, 0.27f + 0.18f * (gfloat) i, 0.52f + 0.17f * (gfloat) j,
					     0.37f + 0.18f * (gfloat) i, 0.62f + 0.17f * (gfloat) j, 0.02f);
		}
		break;

	case LRG_ICON_CLOCK:
		ring (p, 0.5f, 0.5f, 0.4f, p->t);
		line (p, 0.5f, 0.5f, 0.5f, 0.27f);
		line (p, 0.5f, 0.5f, 0.66f, 0.6f);
		break;

	case LRG_ICON_USER:
		{
			LrgCornerRadii radii;

			disc (p, 0.5f, 0.33f, 0.17f);
			radii.top_left = radii.top_right = 0.3f * size;
			radii.bottom_left = radii.bottom_right = 0.05f * size;
			lrg_paint_list_rect_ex (self, px (p, 0.18f), py (p, 0.58f), 0.64f * size, 0.3f * size,
			                        &radii, color, NULL, 0.0f, NULL);
		}
		break;

	case LRG_ICON_NONE:
	default:
		break;
	}
}

LrgIcon
lrg_paint_icon_from_name (const gchar *name)
{
	g_autoptr(GEnumClass) klass = NULL;
	GEnumValue           *value;

	if (name == NULL)
		return LRG_ICON_NONE;

	klass = g_type_class_ref (LRG_TYPE_ICON);
	value = g_enum_get_value_by_nick (klass, name);
	return value != NULL ? (LrgIcon) value->value : LRG_ICON_NONE;
}

const gchar *
lrg_paint_icon_get_name (LrgIcon icon)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_ICON);
	GEnumValue           *value = g_enum_get_value (klass, (gint) icon);

	/* Nicks are static strings owned by the registered type */
	return value != NULL ? value->value_nick : NULL;
}

/* ==========================================================================
 * LrgSvgIcon
 * ========================================================================== */

/* Largest raster size accepted */
#define MAX_PIXEL_SIZE (1024)

struct _LrgSvgIcon
{
	GObject          parent_instance;

	GrlVectorShape **shapes;
	guint            n_shapes;
	GrlRectangle     bounds;    /* union of the path bounds */
	GHashTable      *textures;  /* pixel size -> GrlTexture */
};

G_DEFINE_FINAL_TYPE (LrgSvgIcon, lrg_svg_icon, G_TYPE_OBJECT)

static void
lrg_svg_icon_finalize (GObject *object)
{
	LrgSvgIcon *self = LRG_SVG_ICON (object);
	guint       i;

	for (i = 0; i < self->n_shapes; i++)
		grl_vector_shape_free (self->shapes[i]);
	g_free (self->shapes);
	g_hash_table_unref (self->textures);

	G_OBJECT_CLASS (lrg_svg_icon_parent_class)->finalize (object);
}

static void
lrg_svg_icon_class_init (LrgSvgIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_svg_icon_finalize;
}

static void
lrg_svg_icon_init (LrgSvgIcon *self)
{
	self->textures = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

/*
 * adopt_shapes:
 *
 * Takes ownership of a parsed shape array and works out the box the
 * drawing is fitted by. An SVG with no geometry is an error: there is
 * nothing to draw.
 */
static LrgSvgIcon *
adopt_shapes (GrlVectorShape **shapes,
              guint            n_shapes,
              GError         **error)
{
	g_autoptr(LrgSvgIcon) self = NULL;
	gfloat                x0 = G_MAXFLOAT;
	gfloat                y0 = G_MAXFLOAT;
	gfloat                x1 = -G_MAXFLOAT;
	gfloat                y1 = -G_MAXFLOAT;
	guint                 i;

	self = g_object_new (LRG_TYPE_SVG_ICON, NULL);
	self->shapes = shapes;
	self->n_shapes = n_shapes;

	for (i = 0; i < n_shapes; i++)
	{
		GrlRectangle r;
		gfloat       pad = 0.0f;

		if (!grl_path_get_bounds (shapes[i]->path, &r))
			continue;
		if (shapes[i]->has_stroke)
			pad = shapes[i]->stroke_width * 0.5f;
		x0 = MIN (x0, r.x - pad);
		y0 = MIN (y0, r.y - pad);
		x1 = MAX (x1, r.x + r.width + pad);
		y1 = MAX (y1, r.y + r.height + pad);
	}

	if (x0 > x1 || y0 > y1)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "SVG icon has no drawable geometry");
		return NULL;
	}

	self->bounds.x = x0;
	self->bounds.y = y0;
	self->bounds.width = MAX (x1 - x0, 1e-3f);
	self->bounds.height = MAX (y1 - y0, 1e-3f);
	return g_steal_pointer (&self);
}

LrgSvgIcon *
lrg_svg_icon_new_from_file (const gchar  *path,
                            GError      **error)
{
	GrlVectorShape **shapes;
	guint            n = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	shapes = grl_svg_load_from_file (path, 0.0f, &n, error);
	if (shapes == NULL)
		return NULL;

	return adopt_shapes (shapes, n, error);
}

LrgSvgIcon *
lrg_svg_icon_new_from_data (const gchar  *data,
                            gssize        length,
                            GError      **error)
{
	GrlVectorShape **shapes;
	guint            n = 0;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (length < 0)
		length = (gssize) strlen (data);

	shapes = grl_svg_load_from_memory (data, (gsize) length, 0.0f, &n, error);
	if (shapes == NULL)
		return NULL;

	return adopt_shapes (shapes, n, error);
}

guint
lrg_svg_icon_get_n_shapes (LrgSvgIcon *self)
{
	g_return_val_if_fail (LRG_IS_SVG_ICON (self), 0);

	return self->n_shapes;
}

GrlImage *
lrg_svg_icon_rasterize (LrgSvgIcon *self,
                        guint       pixel_size)
{
	static const GrlColor clear = { 0, 0, 0, 0 };
	g_autoptr(GrlImage)   image = NULL;
	GrlVectorShape      **fitted;
	gfloat                scale;
	gfloat                off_x;
	gfloat                off_y;
	guint                 i;

	g_return_val_if_fail (LRG_IS_SVG_ICON (self), NULL);

	pixel_size = CLAMP (pixel_size, 1u, (guint) MAX_PIXEL_SIZE);

	image = grl_image_new_color ((gint) pixel_size, (gint) pixel_size, &clear);
	grl_image_set_blend_mode (image, GRL_IMAGE_BLEND_OVER);
	grl_image_set_antialias (image, TRUE);

	/* Fit the drawing's box into the square, aspect kept, centred */
	scale = (gfloat) pixel_size / MAX (self->bounds.width, self->bounds.height);
	off_x = ((gfloat) pixel_size - self->bounds.width * scale) * 0.5f;
	off_y = ((gfloat) pixel_size - self->bounds.height * scale) * 0.5f;

	/* Transform copies, so the parsed shapes stay in SVG units */
	fitted = g_new0 (GrlVectorShape *, self->n_shapes + 1);
	for (i = 0; i < self->n_shapes; i++)
	{
		GrlVectorShape *shape = grl_vector_shape_copy (self->shapes[i]);

		grl_path_translate (shape->path, -self->bounds.x, -self->bounds.y);
		grl_path_scale (shape->path, scale, scale);
		grl_path_translate (shape->path, off_x, off_y);
		shape->stroke_width *= scale;
		fitted[i] = shape;
	}

	grl_image_draw_svg_shapes (image, fitted, self->n_shapes);

	for (i = 0; i < self->n_shapes; i++)
		grl_vector_shape_free (fitted[i]);
	g_free (fitted);

	return g_steal_pointer (&image);
}

GrlTexture *
lrg_svg_icon_get_texture (LrgSvgIcon *self,
                          guint       pixel_size)
{
	g_autoptr(GrlImage) image = NULL;
	GrlTexture         *texture;

	g_return_val_if_fail (LRG_IS_SVG_ICON (self), NULL);

	if (!IsWindowReady ())
		return NULL;

	pixel_size = CLAMP (pixel_size, 1u, (guint) MAX_PIXEL_SIZE);

	texture = g_hash_table_lookup (self->textures, GUINT_TO_POINTER (pixel_size));
	if (texture != NULL)
		return texture;

	image = lrg_svg_icon_rasterize (self, pixel_size);
	texture = grl_texture_new_from_image (image);
	if (texture == NULL)
		return NULL;

	g_hash_table_insert (self->textures, GUINT_TO_POINTER (pixel_size), texture);
	return texture;
}

void
lrg_svg_icon_clear_cache (LrgSvgIcon *self)
{
	g_return_if_fail (LRG_IS_SVG_ICON (self));

	g_hash_table_remove_all (self->textures);
}

void
lrg_paint_list_svg_icon (LrgPaintList   *self,
                         LrgSvgIcon     *icon,
                         gfloat          x,
                         gfloat          y,
                         gfloat          size,
                         const GrlColor *tint)
{
	GrlTexture *texture;
	guint       pixels;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (LRG_IS_SVG_ICON (icon));

	if (size <= 0.0f)
		return;

	pixels = (guint) MAX (floorf (size + 0.5f), 1.0f);
	texture = lrg_svg_icon_get_texture (icon, pixels);
	if (texture == NULL)
		return;

	lrg_paint_list_image (self, texture, NULL, x, y, size, size, 0.0f, tint);
}
