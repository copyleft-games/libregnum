/* lrg-debug-draw.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDebugDraw: an immediate-mode batch of debug geometry.
 *
 * Recording only stores primitives (shape, control points, pen state,
 * colour and lifetime) in an array, so it works without a graphics
 * context and tests can inspect what was recorded.  Before a draw the
 * visible primitives are tessellated on the CPU into flat segment and
 * triangle lists, one pair per depth mode, plus label and 2D lists.
 * Drawing submits each list as one rlgl run (GL_LINES or GL_TRIANGLES),
 * so a frame of debug geometry costs a handful of draw calls whatever
 * the number of shapes (rlgl splits a run only when its vertex buffer
 * fills).  Lines wider than one pixel become camera-facing quads at
 * draw time, since GL 3.3 core only guarantees 1-pixel lines.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-debug-draw.h"
#include "../lighting/lrg-shadow-renderer.h"

/* Soft cap on recorded primitives: code that records every frame into a
 * batch nobody flushes would otherwise grow without bound. */
#define DEBUG_DRAW_MAX_PRIMITIVES (262144u)

/* Depth of the pen stack (push/pop). */
#define DEBUG_DRAW_PEN_STACK (16u)

/* List indices: the always-on-top list and the depth-tested list. */
#define LIST_TOP   (0)
#define LIST_DEPTH (1)

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

G_DEFINE_ENUM_TYPE (LrgDebugShape, lrg_debug_shape,
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_LINE, "line"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_POLYLINE, "polyline"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_ARROW, "arrow"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_BOX, "box"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_ORIENTED_BOX, "oriented-box"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_SPHERE, "sphere"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_CAPSULE, "capsule"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_CYLINDER, "cylinder"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_CONE, "cone"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_FRUSTUM, "frustum"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_GRID, "grid"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_AXES, "axes"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_CIRCLE, "circle"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_POINT, "point"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_TEXT, "text"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_LINE_2D, "line-2d"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_RECT_2D, "rect-2d"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_CIRCLE_2D, "circle-2d"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_SHAPE_TEXT_2D, "text-2d"))

G_DEFINE_ENUM_TYPE (LrgDebugCategory, lrg_debug_category,
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_GENERAL, "general"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_PHYSICS, "physics"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_NAVIGATION, "navigation"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_LIGHTS, "lights"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_AUDIO, "audio"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_AI, "ai"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_ANIMATION, "animation"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_CAMERA, "camera"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_PARTICLES, "particles"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_EDITOR, "editor"),
                    G_DEFINE_ENUM_VALUE (LRG_DEBUG_CATEGORY_USER, "user"))

/* Built-in category names by index (NULL: unnamed). */
static const gchar * const category_names[LRG_DEBUG_CATEGORY_COUNT] = {
	"general", "physics", "navigation", "lights", "audio", "ai",
	"animation", "camera", "particles", "editor", NULL, NULL,
	NULL, NULL, NULL, NULL, "user", NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

const gchar *
lrg_debug_category_get_name (guint category)
{
	if (category >= LRG_DEBUG_CATEGORY_COUNT)
		return NULL;
	return category_names[category];
}

gint
lrg_debug_category_from_name (const gchar *name)
{
	guint i;

	g_return_val_if_fail (name != NULL, -1);

	for (i = 0; i < LRG_DEBUG_CATEGORY_COUNT; i++)
		if (category_names[i] != NULL && g_strcmp0 (category_names[i], name) == 0)
			return (gint)i;
	return -1;
}

/* ==========================================================================
 * Private types
 * ========================================================================== */

/*
 * Pen:
 * The state applied to every primitive recorded next.
 */
typedef struct
{
	guint    category;
	gboolean depth_test;
	gfloat   thickness;
	gboolean solid;
	guint    segments;
	guint    text_size;
} Pen;

/*
 * Prim:
 * One recorded primitive.  @p holds up to two control points (see
 * lrg_debug_draw_get_primitive_point()), @r a radius / size / spacing,
 * @n a count (grid cells, polyline closed flag), @m a transform (or the
 * inverse view-projection of a frustum), @points a polyline's points.
 */
typedef struct
{
	LrgDebugShape shape;
	Pen           pen;
	guint8        color[4];
	gboolean      timed;
	gfloat        remaining;
	gfloat        p[2][3];
	gfloat        r;
	guint         n;
	gfloat        m[16];
	gfloat       *points;
	guint         n_points;
	gchar        *text;
} Prim;

/* Seg: one tessellated segment (2D ones have z = 0). */
typedef struct
{
	gfloat a[3];
	gfloat b[3];
	guint8 c[4];
	gfloat thickness;
} Seg;

/* Tri: one tessellated triangle. */
typedef struct
{
	gfloat v[3][3];
	guint8 c[4];
} Tri;

/* Label: one text item (3D anchor, or 2D top-left corner). */
typedef struct
{
	gfloat       p[3];
	guint8       c[4];
	guint        size;
	const gchar *text;   /* owned by the Prim */
} Label;

struct _LrgDebugDraw
{
	GObject  parent_instance;

	gboolean enabled;
	guint32  category_mask;
	Pen      pen;
	Pen      pen_stack[DEBUG_DRAW_PEN_STACK];
	guint    pen_depth;

	GArray  *prims;          /* Prim */
	gboolean warned_cap;

	/* tessellation, rebuilt when dirty */
	gboolean dirty;
	GArray  *segs[2];        /* Seg, LIST_TOP / LIST_DEPTH */
	GArray  *tris[2];        /* Tri */
	GArray  *labels;         /* Label, 3D anchors */
	GArray  *segs_2d;        /* Seg */
	GArray  *tris_2d;        /* Tri */
	GArray  *texts_2d;       /* Label */

	/* the last 3D draw: matrices and target size for the labels */
	gboolean label_valid;
	gfloat   label_view[16];
	gfloat   label_proj[16];
	gint     label_width;
	gint     label_height;
	guint    draw_calls;
};

G_DEFINE_FINAL_TYPE (LrgDebugDraw, lrg_debug_draw, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_ENABLED,
	PROP_CATEGORY_MASK,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Small vector helpers (float[3])
 * ========================================================================== */

static inline void
v3_set (gfloat *o, gfloat x, gfloat y, gfloat z)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline void
v3_sub (const gfloat *a, const gfloat *b, gfloat *o)
{
	o[0] = a[0] - b[0];
	o[1] = a[1] - b[1];
	o[2] = a[2] - b[2];
}

/* o = a + b * s */
static inline void
v3_mad (const gfloat *a, const gfloat *b, gfloat s, gfloat *o)
{
	o[0] = a[0] + b[0] * s;
	o[1] = a[1] + b[1] * s;
	o[2] = a[2] + b[2] * s;
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

	o[0] = x;
	o[1] = y;
	o[2] = z;
}

/* v3_normalize: normalises @v in place; returns its old length. */
static gfloat
v3_normalize (gfloat *v)
{
	gfloat len = sqrtf (v3_dot (v, v));

	if (len > 1e-12f)
	{
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
	return len;
}

/* basis: two unit vectors perpendicular to the unit @n and each other. */
static void
basis (const gfloat *n,
       gfloat       *u,
       gfloat       *v)
{
	gfloat helper[3];

	/* the world axis least aligned with n */
	if (fabsf (n[0]) < 0.57f)
		v3_set (helper, 1.0f, 0.0f, 0.0f);
	else if (fabsf (n[1]) < 0.57f)
		v3_set (helper, 0.0f, 1.0f, 0.0f);
	else
		v3_set (helper, 0.0f, 0.0f, 1.0f);
	v3_cross (n, helper, u);
	v3_normalize (u);
	v3_cross (n, u, v);
	v3_normalize (v);
}

/* mat_point: column-major @m x (@p, 1), divided by w (when non-zero). */
static void
mat_point (const gfloat *m,
           const gfloat *p,
           gfloat       *o)
{
	gfloat x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
	gfloat y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
	gfloat z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
	gfloat w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];

	if (fabsf (w) > 1e-12f)
	{
		x /= w;
		y /= w;
		z /= w;
	}
	v3_set (o, x, y, z);
}

/* mat_invert: the inverse of the column-major @m into @out (cofactor
 * expansion in double precision); FALSE when singular. */
static gboolean
mat_invert (const gfloat *m,
            gfloat       *out)
{
	gdouble inv[16];
	gdouble det;
	guint   i;

	inv[0] = (gdouble)m[5] * m[10] * m[15] - (gdouble)m[5] * m[11] * m[14] - (gdouble)m[9] * m[6] * m[15]
		+ (gdouble)m[9] * m[7] * m[14] + (gdouble)m[13] * m[6] * m[11] - (gdouble)m[13] * m[7] * m[10];
	inv[4] = -(gdouble)m[4] * m[10] * m[15] + (gdouble)m[4] * m[11] * m[14] + (gdouble)m[8] * m[6] * m[15]
		- (gdouble)m[8] * m[7] * m[14] - (gdouble)m[12] * m[6] * m[11] + (gdouble)m[12] * m[7] * m[10];
	inv[8] = (gdouble)m[4] * m[9] * m[15] - (gdouble)m[4] * m[11] * m[13] - (gdouble)m[8] * m[5] * m[15]
		+ (gdouble)m[8] * m[7] * m[13] + (gdouble)m[12] * m[5] * m[11] - (gdouble)m[12] * m[7] * m[9];
	inv[12] = -(gdouble)m[4] * m[9] * m[14] + (gdouble)m[4] * m[10] * m[13] + (gdouble)m[8] * m[5] * m[14]
		- (gdouble)m[8] * m[6] * m[13] - (gdouble)m[12] * m[5] * m[10] + (gdouble)m[12] * m[6] * m[9];
	inv[1] = -(gdouble)m[1] * m[10] * m[15] + (gdouble)m[1] * m[11] * m[14] + (gdouble)m[9] * m[2] * m[15]
		- (gdouble)m[9] * m[3] * m[14] - (gdouble)m[13] * m[2] * m[11] + (gdouble)m[13] * m[3] * m[10];
	inv[5] = (gdouble)m[0] * m[10] * m[15] - (gdouble)m[0] * m[11] * m[14] - (gdouble)m[8] * m[2] * m[15]
		+ (gdouble)m[8] * m[3] * m[14] + (gdouble)m[12] * m[2] * m[11] - (gdouble)m[12] * m[3] * m[10];
	inv[9] = -(gdouble)m[0] * m[9] * m[15] + (gdouble)m[0] * m[11] * m[13] + (gdouble)m[8] * m[1] * m[15]
		- (gdouble)m[8] * m[3] * m[13] - (gdouble)m[12] * m[1] * m[11] + (gdouble)m[12] * m[3] * m[9];
	inv[13] = (gdouble)m[0] * m[9] * m[14] - (gdouble)m[0] * m[10] * m[13] - (gdouble)m[8] * m[1] * m[14]
		+ (gdouble)m[8] * m[2] * m[13] + (gdouble)m[12] * m[1] * m[10] - (gdouble)m[12] * m[2] * m[9];
	inv[2] = (gdouble)m[1] * m[6] * m[15] - (gdouble)m[1] * m[7] * m[14] - (gdouble)m[5] * m[2] * m[15]
		+ (gdouble)m[5] * m[3] * m[14] + (gdouble)m[13] * m[2] * m[7] - (gdouble)m[13] * m[3] * m[6];
	inv[6] = -(gdouble)m[0] * m[6] * m[15] + (gdouble)m[0] * m[7] * m[14] + (gdouble)m[4] * m[2] * m[15]
		- (gdouble)m[4] * m[3] * m[14] - (gdouble)m[12] * m[2] * m[7] + (gdouble)m[12] * m[3] * m[6];
	inv[10] = (gdouble)m[0] * m[5] * m[15] - (gdouble)m[0] * m[7] * m[13] - (gdouble)m[4] * m[1] * m[15]
		+ (gdouble)m[4] * m[3] * m[13] + (gdouble)m[12] * m[1] * m[7] - (gdouble)m[12] * m[3] * m[5];
	inv[14] = -(gdouble)m[0] * m[5] * m[14] + (gdouble)m[0] * m[6] * m[13] + (gdouble)m[4] * m[1] * m[14]
		- (gdouble)m[4] * m[2] * m[13] - (gdouble)m[12] * m[1] * m[6] + (gdouble)m[12] * m[2] * m[5];
	inv[3] = -(gdouble)m[1] * m[6] * m[11] + (gdouble)m[1] * m[7] * m[10] + (gdouble)m[5] * m[2] * m[11]
		- (gdouble)m[5] * m[3] * m[10] - (gdouble)m[9] * m[2] * m[7] + (gdouble)m[9] * m[3] * m[6];
	inv[7] = (gdouble)m[0] * m[6] * m[11] - (gdouble)m[0] * m[7] * m[10] - (gdouble)m[4] * m[2] * m[11]
		+ (gdouble)m[4] * m[3] * m[10] + (gdouble)m[8] * m[2] * m[7] - (gdouble)m[8] * m[3] * m[6];
	inv[11] = -(gdouble)m[0] * m[5] * m[11] + (gdouble)m[0] * m[7] * m[9] + (gdouble)m[4] * m[1] * m[11]
		- (gdouble)m[4] * m[3] * m[9] - (gdouble)m[8] * m[1] * m[7] + (gdouble)m[8] * m[3] * m[5];
	inv[15] = (gdouble)m[0] * m[5] * m[10] - (gdouble)m[0] * m[6] * m[9] - (gdouble)m[4] * m[1] * m[10]
		+ (gdouble)m[4] * m[2] * m[9] + (gdouble)m[8] * m[1] * m[6] - (gdouble)m[8] * m[2] * m[5];

	det = (gdouble)m[0] * inv[0] + (gdouble)m[1] * inv[4] + (gdouble)m[2] * inv[8] + (gdouble)m[3] * inv[12];
	if (fabs (det) < 1e-20 || !isfinite (det))
		return FALSE;
	det = 1.0 / det;
	for (i = 0; i < 16; i++)
		out[i] = (gfloat)(inv[i] * det);
	return TRUE;
}

/* matrix_to_floats: a raylib Matrix into column-major floats (field by
 * field: no memcpy into or out of Matrix). */
static void
matrix_to_floats (Matrix  m,
                  gfloat *o)
{
	o[0] = m.m0; o[1] = m.m1; o[2] = m.m2; o[3] = m.m3;
	o[4] = m.m4; o[5] = m.m5; o[6] = m.m6; o[7] = m.m7;
	o[8] = m.m8; o[9] = m.m9; o[10] = m.m10; o[11] = m.m11;
	o[12] = m.m12; o[13] = m.m13; o[14] = m.m14; o[15] = m.m15;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
prim_clear (gpointer data)
{
	Prim *prim = data;

	g_clear_pointer (&prim->points, g_free);
	g_clear_pointer (&prim->text, g_free);
}

static void
lrg_debug_draw_finalize (GObject *object)
{
	LrgDebugDraw *self = LRG_DEBUG_DRAW (object);
	guint         i;

	g_clear_pointer (&self->prims, g_array_unref);
	for (i = 0; i < 2; i++)
	{
		g_clear_pointer (&self->segs[i], g_array_unref);
		g_clear_pointer (&self->tris[i], g_array_unref);
	}
	g_clear_pointer (&self->labels, g_array_unref);
	g_clear_pointer (&self->segs_2d, g_array_unref);
	g_clear_pointer (&self->tris_2d, g_array_unref);
	g_clear_pointer (&self->texts_2d, g_array_unref);

	G_OBJECT_CLASS (lrg_debug_draw_parent_class)->finalize (object);
}

static void
lrg_debug_draw_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgDebugDraw *self = LRG_DEBUG_DRAW (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		g_value_set_boolean (value, self->enabled);
		break;
	case PROP_CATEGORY_MASK:
		g_value_set_uint (value, (guint)self->category_mask);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_debug_draw_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgDebugDraw *self = LRG_DEBUG_DRAW (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		lrg_debug_draw_set_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_CATEGORY_MASK:
		lrg_debug_draw_set_category_mask (self, (guint32)g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_debug_draw_class_init (LrgDebugDrawClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_debug_draw_finalize;
	object_class->get_property = lrg_debug_draw_get_property;
	object_class->set_property = lrg_debug_draw_set_property;

	/**
	 * LrgDebugDraw:enabled:
	 *
	 * Whether the batch records and draws.  When %FALSE every recording
	 * call returns at once (so debug calls can stay in shipping code)
	 * and what was recorded is cleared.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled",
		                      "Whether the batch records and draws",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDebugDraw:category-mask:
	 *
	 * Bit N enables #LrgDebugCategory N (all by default).  The editor
	 * exposes one toggle per bit.
	 */
	properties[PROP_CATEGORY_MASK] =
		g_param_spec_uint ("category-mask", "Category mask",
		                   "Enabled debug categories",
		                   0, G_MAXUINT32, LRG_DEBUG_CATEGORY_MASK_ALL,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_debug_draw_init (LrgDebugDraw *self)
{
	guint i;

	self->enabled = TRUE;
	self->category_mask = LRG_DEBUG_CATEGORY_MASK_ALL;
	self->pen.category = LRG_DEBUG_CATEGORY_GENERAL;
	self->pen.depth_test = TRUE;
	self->pen.thickness = 1.0f;
	self->pen.solid = FALSE;
	self->pen.segments = 16;
	self->pen.text_size = 16;

	self->prims = g_array_new (FALSE, TRUE, sizeof (Prim));
	g_array_set_clear_func (self->prims, prim_clear);
	for (i = 0; i < 2; i++)
	{
		self->segs[i] = g_array_new (FALSE, FALSE, sizeof (Seg));
		self->tris[i] = g_array_new (FALSE, FALSE, sizeof (Tri));
	}
	self->labels = g_array_new (FALSE, FALSE, sizeof (Label));
	self->segs_2d = g_array_new (FALSE, FALSE, sizeof (Seg));
	self->tris_2d = g_array_new (FALSE, FALSE, sizeof (Tri));
	self->texts_2d = g_array_new (FALSE, FALSE, sizeof (Label));
	self->dirty = TRUE;
}

LrgDebugDraw *
lrg_debug_draw_new (void)
{
	return g_object_new (LRG_TYPE_DEBUG_DRAW, NULL);
}

LrgDebugDraw *
lrg_debug_draw_get_default (void)
{
	static gsize          once = 0;
	static LrgDebugDraw  *instance = NULL;

	/* Created once and kept for the life of the process. */
	if (g_once_init_enter (&once))
	{
		instance = lrg_debug_draw_new ();
		g_once_init_leave (&once, 1);
	}
	return instance;
}

/* ==========================================================================
 * Switches and pen
 * ========================================================================== */

void
lrg_debug_draw_set_enabled (LrgDebugDraw *self,
                            gboolean      enabled)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	enabled = !!enabled;
	if (self->enabled == enabled)
		return;
	self->enabled = enabled;
	if (!enabled)
		lrg_debug_draw_clear (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

gboolean
lrg_debug_draw_get_enabled (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);
	return self->enabled;
}

void
lrg_debug_draw_set_category_mask (LrgDebugDraw *self,
                                  guint32       mask)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (self->category_mask == mask)
		return;
	self->category_mask = mask;
	self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORY_MASK]);
}

guint32
lrg_debug_draw_get_category_mask (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	return self->category_mask;
}

void
lrg_debug_draw_set_category_enabled (LrgDebugDraw *self,
                                     guint         category,
                                     gboolean      enabled)
{
	guint32 mask;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (category < LRG_DEBUG_CATEGORY_COUNT);

	mask = self->category_mask;
	if (enabled)
		mask |= (guint32)1u << category;
	else
		mask &= ~((guint32)1u << category);
	lrg_debug_draw_set_category_mask (self, mask);
}

gboolean
lrg_debug_draw_is_category_enabled (LrgDebugDraw *self,
                                    guint         category)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);

	if (!self->enabled || category >= LRG_DEBUG_CATEGORY_COUNT)
		return FALSE;
	return (self->category_mask & ((guint32)1u << category)) != 0;
}

void
lrg_debug_draw_set_category (LrgDebugDraw *self,
                             guint         category)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (category < LRG_DEBUG_CATEGORY_COUNT);
	self->pen.category = category;
}

guint
lrg_debug_draw_get_category (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	return self->pen.category;
}

void
lrg_debug_draw_set_depth_test (LrgDebugDraw *self,
                               gboolean      depth_test)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	self->pen.depth_test = !!depth_test;
}

gboolean
lrg_debug_draw_get_depth_test (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), TRUE);
	return self->pen.depth_test;
}

void
lrg_debug_draw_set_thickness (LrgDebugDraw *self,
                              gfloat        thickness)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (!isfinite (thickness))
		thickness = 1.0f;
	self->pen.thickness = CLAMP (thickness, 0.5f, 64.0f);
}

gfloat
lrg_debug_draw_get_thickness (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 1.0f);
	return self->pen.thickness;
}

void
lrg_debug_draw_set_solid (LrgDebugDraw *self,
                          gboolean      solid)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	self->pen.solid = !!solid;
}

gboolean
lrg_debug_draw_get_solid (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);
	return self->pen.solid;
}

void
lrg_debug_draw_set_segments (LrgDebugDraw *self,
                             guint         segments)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	self->pen.segments = CLAMP (segments, 4u, 128u);
}

guint
lrg_debug_draw_get_segments (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 16);
	return self->pen.segments;
}

void
lrg_debug_draw_set_text_size (LrgDebugDraw *self,
                              guint         size)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	self->pen.text_size = CLAMP (size, 4u, 256u);
}

guint
lrg_debug_draw_get_text_size (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 16);
	return self->pen.text_size;
}

void
lrg_debug_draw_push (LrgDebugDraw *self)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (self->pen_depth >= DEBUG_DRAW_PEN_STACK)
	{
		g_warning ("LrgDebugDraw: pen stack overflow (more than %u pushes)",
		           DEBUG_DRAW_PEN_STACK);
		return;
	}
	self->pen_stack[self->pen_depth++] = self->pen;
}

void
lrg_debug_draw_pop (LrgDebugDraw *self)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (self->pen_depth == 0)
	{
		g_warning ("LrgDebugDraw: pop without a matching push");
		return;
	}
	self->pen = self->pen_stack[--self->pen_depth];
}

/* ==========================================================================
 * Recording
 * ========================================================================== */

/* prim_begin:
 * Appends a primitive with the pen, colour and lifetime filled in, or
 * returns NULL when the batch is disabled, the pen's category is off or
 * the cap is reached.  The returned pointer is valid until the next
 * append. */
static Prim *
prim_begin (LrgDebugDraw   *self,
            LrgDebugShape   shape,
            const GrlColor *color,
            gfloat          duration)
{
	Prim *prim;

	if (!lrg_debug_draw_is_category_enabled (self, self->pen.category))
		return NULL;
	if (self->prims->len >= DEBUG_DRAW_MAX_PRIMITIVES)
	{
		if (!self->warned_cap)
			g_warning ("LrgDebugDraw: %u primitives recorded and never flushed; "
			           "dropping new ones (call lrg_debug_draw_flush() or "
			           "lrg_debug_draw_end_frame() every frame)",
			           DEBUG_DRAW_MAX_PRIMITIVES);
		self->warned_cap = TRUE;
		return NULL;
	}

	g_array_set_size (self->prims, self->prims->len + 1);
	prim = &g_array_index (self->prims, Prim, self->prims->len - 1);
	memset (prim, 0, sizeof *prim);
	prim->shape = shape;
	prim->pen = self->pen;
	if (color != NULL)
	{
		prim->color[0] = color->r;
		prim->color[1] = color->g;
		prim->color[2] = color->b;
		prim->color[3] = color->a;
	}
	else
		memset (prim->color, 255, sizeof prim->color);
	prim->timed = isfinite (duration) && duration > 0.0f;
	prim->remaining = prim->timed ? duration : 0.0f;
	self->dirty = TRUE;
	return prim;
}

static inline void
copy_v3 (gfloat *o, const GrlVector3 *v)
{
	o[0] = v->x;
	o[1] = v->y;
	o[2] = v->z;
}

void
lrg_debug_draw_line (LrgDebugDraw     *self,
                     const GrlVector3 *from,
                     const GrlVector3 *to,
                     const GrlColor   *color,
                     gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (from != NULL && to != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_LINE, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], from);
	copy_v3 (prim->p[1], to);
}

void
lrg_debug_draw_polyline (LrgDebugDraw     *self,
                         const GrlVector3 *points,
                         guint             n_points,
                         gboolean          closed,
                         const GrlColor   *color,
                         gfloat            duration)
{
	Prim *prim;
	guint i;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (points != NULL || n_points == 0);

	if (n_points < 2)
		return;
	prim = prim_begin (self, LRG_DEBUG_SHAPE_POLYLINE, color, duration);
	if (prim == NULL)
		return;
	prim->points = g_new (gfloat, (gsize)n_points * 3);
	for (i = 0; i < n_points; i++)
		copy_v3 (&prim->points[i * 3], &points[i]);
	prim->n_points = n_points;
	prim->n = closed ? 1 : 0;
	copy_v3 (prim->p[0], &points[0]);
	copy_v3 (prim->p[1], &points[n_points - 1]);
}

void
lrg_debug_draw_arrow (LrgDebugDraw     *self,
                      const GrlVector3 *from,
                      const GrlVector3 *to,
                      gfloat            head_size,
                      const GrlColor   *color,
                      gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (from != NULL && to != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_ARROW, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], from);
	copy_v3 (prim->p[1], to);
	prim->r = head_size;
}

void
lrg_debug_draw_box (LrgDebugDraw     *self,
                    const GrlVector3 *min,
                    const GrlVector3 *max,
                    const GrlColor   *color,
                    gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (min != NULL && max != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_BOX, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], min);
	copy_v3 (prim->p[1], max);
}

void
lrg_debug_draw_oriented_box (LrgDebugDraw     *self,
                             const gfloat     *transform,
                             const GrlVector3 *half_extents,
                             const GrlColor   *color,
                             gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (transform != NULL && half_extents != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_ORIENTED_BOX, color, duration);
	if (prim == NULL)
		return;
	memcpy (prim->m, transform, sizeof prim->m);
	v3_set (prim->p[0], transform[12], transform[13], transform[14]);
	copy_v3 (prim->p[1], half_extents);
}

/* record_round: the shared body of the two-point round shapes. */
static void
record_round (LrgDebugDraw     *self,
              LrgDebugShape     shape,
              const GrlVector3 *a,
              const GrlVector3 *b,
              gfloat            radius,
              const GrlColor   *color,
              gfloat            duration)
{
	Prim *prim;

	prim = prim_begin (self, shape, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], a);
	if (b != NULL)
		copy_v3 (prim->p[1], b);
	else
		memcpy (prim->p[1], prim->p[0], sizeof prim->p[1]);
	prim->r = radius;
}

void
lrg_debug_draw_sphere (LrgDebugDraw     *self,
                       const GrlVector3 *center,
                       gfloat            radius,
                       const GrlColor   *color,
                       gfloat            duration)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (center != NULL);

	record_round (self, LRG_DEBUG_SHAPE_SPHERE, center, NULL, radius, color, duration);
}

void
lrg_debug_draw_capsule (LrgDebugDraw     *self,
                        const GrlVector3 *a,
                        const GrlVector3 *b,
                        gfloat            radius,
                        const GrlColor   *color,
                        gfloat            duration)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (a != NULL && b != NULL);

	record_round (self, LRG_DEBUG_SHAPE_CAPSULE, a, b, radius, color, duration);
}

void
lrg_debug_draw_cylinder (LrgDebugDraw     *self,
                         const GrlVector3 *a,
                         const GrlVector3 *b,
                         gfloat            radius,
                         const GrlColor   *color,
                         gfloat            duration)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (a != NULL && b != NULL);

	record_round (self, LRG_DEBUG_SHAPE_CYLINDER, a, b, radius, color, duration);
}

void
lrg_debug_draw_cone (LrgDebugDraw     *self,
                     const GrlVector3 *apex,
                     const GrlVector3 *base,
                     gfloat            radius,
                     const GrlColor   *color,
                     gfloat            duration)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (apex != NULL && base != NULL);

	record_round (self, LRG_DEBUG_SHAPE_CONE, apex, base, radius, color, duration);
}

void
lrg_debug_draw_frustum (LrgDebugDraw   *self,
                        const gfloat   *view_projection,
                        const GrlColor *color,
                        gfloat          duration)
{
	gfloat inverse[16];
	gfloat near_center[3] = { 0.0f, 0.0f, -1.0f };
	Prim  *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (view_projection != NULL);

	/* The corners are the clip cube under the inverse; a singular
	 * matrix has no volume to show. */
	if (!mat_invert (view_projection, inverse))
		return;
	prim = prim_begin (self, LRG_DEBUG_SHAPE_FRUSTUM, color, duration);
	if (prim == NULL)
		return;
	memcpy (prim->m, inverse, sizeof prim->m);
	mat_point (inverse, near_center, prim->p[0]);
}

void
lrg_debug_draw_camera_frustum (LrgDebugDraw   *self,
                               LrgCamera3D    *camera,
                               gfloat          aspect,
                               gfloat          near_plane,
                               gfloat          far_plane,
                               const GrlColor *color,
                               gfloat          duration)
{
	g_autoptr(GrlVector3) position = NULL;
	g_autoptr(GrlVector3) target = NULL;
	g_autoptr(GrlVector3) up = NULL;
	gfloat eye[3];
	gfloat at[3];
	gfloat upv[3];
	gfloat view[16];
	gfloat proj[16];
	gfloat view_proj[16];

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (LRG_IS_CAMERA3D (camera));

	if (!lrg_debug_draw_is_category_enabled (self, self->pen.category))
		return;

	/* Defaults: the camera's own aspect, else 16:9; rlgl's cull
	 * distances (what BeginMode3D() uses). */
	if (aspect <= 0.0f)
		aspect = lrg_camera3d_get_aspect (camera);
	if (aspect <= 0.0f)
		aspect = 16.0f / 9.0f;
	if (near_plane <= 0.0f)
		near_plane = (gfloat)rlGetCullDistanceNear ();
	if (far_plane <= near_plane)
		far_plane = MAX ((gfloat)rlGetCullDistanceFar (), near_plane * 2.0f);

	position = lrg_camera3d_get_position (camera);
	target = lrg_camera3d_get_target (camera);
	up = lrg_camera3d_get_up (camera);
	copy_v3 (eye, position);
	copy_v3 (at, target);
	copy_v3 (upv, up);

	lrg_shadow_look_at (eye, at, upv, view);
	if (lrg_camera3d_get_projection (camera) == LRG_PROJECTION_ORTHOGRAPHIC)
	{
		/* raylib: fovy is the height of the orthographic view */
		gfloat top = lrg_camera3d_get_fovy (camera) * 0.5f;
		gfloat right = top * aspect;

		lrg_shadow_ortho (-right, right, -top, top, near_plane, far_plane, proj);
	}
	else
		lrg_shadow_perspective (lrg_camera3d_get_fovy (camera), aspect,
		                        near_plane, far_plane, proj);
	lrg_shadow_multiply (proj, view, view_proj);
	lrg_debug_draw_frustum (self, view_proj, color, duration);
}

void
lrg_debug_draw_grid (LrgDebugDraw     *self,
                     const GrlVector3 *center,
                     gfloat            spacing,
                     guint             cells,
                     const GrlColor   *color,
                     gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (center != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_GRID, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], center);
	prim->r = spacing;
	prim->n = MAX (cells, 1u);
}

void
lrg_debug_draw_axes (LrgDebugDraw *self,
                     const gfloat *transform,
                     gfloat        size,
                     gfloat        duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (transform != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_AXES, NULL, duration);
	if (prim == NULL)
		return;
	memcpy (prim->m, transform, sizeof prim->m);
	v3_set (prim->p[0], transform[12], transform[13], transform[14]);
	prim->r = size;
}

void
lrg_debug_draw_circle (LrgDebugDraw     *self,
                       const GrlVector3 *center,
                       const GrlVector3 *normal,
                       gfloat            radius,
                       const GrlColor   *color,
                       gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (center != NULL && normal != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_CIRCLE, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], center);
	copy_v3 (prim->p[1], normal);
	if (v3_normalize (prim->p[1]) <= 1e-12f)
		v3_set (prim->p[1], 0.0f, 1.0f, 0.0f);
	prim->r = radius;
}

void
lrg_debug_draw_point (LrgDebugDraw     *self,
                      const GrlVector3 *position,
                      gfloat            size,
                      const GrlColor   *color,
                      gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (position != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_POINT, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], position);
	prim->r = size;
}

void
lrg_debug_draw_text (LrgDebugDraw     *self,
                     const GrlVector3 *position,
                     const gchar      *text,
                     const GrlColor   *color,
                     gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (position != NULL);
	g_return_if_fail (text != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_TEXT, color, duration);
	if (prim == NULL)
		return;
	copy_v3 (prim->p[0], position);
	prim->text = g_strdup (text);
}

void
lrg_debug_draw_line_2d (LrgDebugDraw     *self,
                        const GrlVector2 *from,
                        const GrlVector2 *to,
                        const GrlColor   *color,
                        gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (from != NULL && to != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_LINE_2D, color, duration);
	if (prim == NULL)
		return;
	v3_set (prim->p[0], from->x, from->y, 0.0f);
	v3_set (prim->p[1], to->x, to->y, 0.0f);
}

void
lrg_debug_draw_rect_2d (LrgDebugDraw   *self,
                        gfloat          x,
                        gfloat          y,
                        gfloat          width,
                        gfloat          height,
                        const GrlColor *color,
                        gfloat          duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	prim = prim_begin (self, LRG_DEBUG_SHAPE_RECT_2D, color, duration);
	if (prim == NULL)
		return;
	v3_set (prim->p[0], x, y, 0.0f);
	v3_set (prim->p[1], width, height, 0.0f);
}

void
lrg_debug_draw_circle_2d (LrgDebugDraw     *self,
                          const GrlVector2 *center,
                          gfloat            radius,
                          const GrlColor   *color,
                          gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (center != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_CIRCLE_2D, color, duration);
	if (prim == NULL)
		return;
	v3_set (prim->p[0], center->x, center->y, 0.0f);
	prim->r = radius;
}

void
lrg_debug_draw_text_2d (LrgDebugDraw     *self,
                        const GrlVector2 *position,
                        const gchar      *text,
                        const GrlColor   *color,
                        gfloat            duration)
{
	Prim *prim;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (position != NULL);
	g_return_if_fail (text != NULL);

	prim = prim_begin (self, LRG_DEBUG_SHAPE_TEXT_2D, color, duration);
	if (prim == NULL)
		return;
	v3_set (prim->p[0], position->x, position->y, 0.0f);
	prim->text = g_strdup (text);
}

/* ==========================================================================
 * Lifetime
 * ========================================================================== */

/* remove_where: drops every primitive @drop accepts, keeping order. */
static void
remove_where (LrgDebugDraw *self,
              gboolean    (*drop) (const Prim *prim, gpointer data),
              gpointer      data)
{
	guint read;
	guint write = 0;

	for (read = 0; read < self->prims->len; read++)
	{
		Prim *prim = &g_array_index (self->prims, Prim, read);

		if (drop (prim, data))
		{
			prim_clear (prim);
			continue;
		}
		if (write != read)
			g_array_index (self->prims, Prim, write) = *prim;
		write++;
	}
	if (write != self->prims->len)
	{
		/* the tail entries were moved or cleared already: shrink
		 * without the clear func running on them again */
		self->prims->len = write;
		self->dirty = TRUE;
	}
	if (self->prims->len < DEBUG_DRAW_MAX_PRIMITIVES)
		self->warned_cap = FALSE;
}

static gboolean
drop_expired (const Prim *prim,
              gpointer    data)
{
	(void)data;
	return prim->timed && prim->remaining <= 0.0f;
}

static gboolean
drop_one_frame (const Prim *prim,
                gpointer    data)
{
	(void)data;
	return !prim->timed;
}

static gboolean
drop_all (const Prim *prim,
          gpointer    data)
{
	(void)prim;
	(void)data;
	return TRUE;
}

static gboolean
drop_category (const Prim *prim,
               gpointer    data)
{
	return prim->pen.category == GPOINTER_TO_UINT (data);
}

void
lrg_debug_draw_tick (LrgDebugDraw *self,
                     gfloat        delta)
{
	guint i;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (!isfinite (delta) || delta < 0.0f)
		delta = 0.0f;
	for (i = 0; i < self->prims->len; i++)
	{
		Prim *prim = &g_array_index (self->prims, Prim, i);

		if (prim->timed)
			prim->remaining -= delta;
	}
	remove_where (self, drop_expired, NULL);
}

void
lrg_debug_draw_end_frame (LrgDebugDraw *self)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	remove_where (self, drop_one_frame, NULL);
}

void
lrg_debug_draw_clear (LrgDebugDraw *self)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	remove_where (self, drop_all, NULL);
}

void
lrg_debug_draw_clear_category (LrgDebugDraw *self,
                               guint         category)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	remove_where (self, drop_category, GUINT_TO_POINTER (category));
}

/* ==========================================================================
 * Tessellation
 * ========================================================================== */

/* Emit context: the lists one primitive tessellates into. */
typedef struct
{
	GArray       *segs;
	GArray       *tris;
	const guint8 *color;
	gfloat        thickness;
} Emit;

static void
emit_seg (Emit         *e,
          const gfloat *a,
          const gfloat *b)
{
	Seg seg;

	memcpy (seg.a, a, sizeof seg.a);
	memcpy (seg.b, b, sizeof seg.b);
	memcpy (seg.c, e->color, sizeof seg.c);
	seg.thickness = e->thickness;
	g_array_append_val (e->segs, seg);
}

static void
emit_tri (Emit         *e,
          const gfloat *a,
          const gfloat *b,
          const gfloat *c)
{
	Tri tri;

	memcpy (tri.v[0], a, sizeof tri.v[0]);
	memcpy (tri.v[1], b, sizeof tri.v[1]);
	memcpy (tri.v[2], c, sizeof tri.v[2]);
	memcpy (tri.c, e->color, sizeof tri.c);
	g_array_append_val (e->tris, tri);
}

/* ring_point: @center + (@u cos t + @v sin t) * @r */
static void
ring_point (const gfloat *center,
            const gfloat *u,
            const gfloat *v,
            gfloat        r,
            gfloat        t,
            gfloat       *o)
{
	gfloat c = cosf (t) * r;
	gfloat s = sinf (t) * r;

	o[0] = center[0] + u[0] * c + v[0] * s;
	o[1] = center[1] + u[1] * c + v[1] * s;
	o[2] = center[2] + u[2] * c + v[2] * s;
}

/* emit_ring: a circle (or, @fill, a triangle fan) in the plane of @u, @v. */
static void
emit_ring (Emit         *e,
           const gfloat *center,
           const gfloat *u,
           const gfloat *v,
           gfloat        r,
           guint         segments,
           gboolean      fill)
{
	gfloat prev[3];
	gfloat cur[3];
	guint  i;

	ring_point (center, u, v, r, 0.0f, prev);
	for (i = 1; i <= segments; i++)
	{
		ring_point (center, u, v, r, (gfloat)(2.0 * G_PI * i / segments), cur);
		if (fill)
			emit_tri (e, center, prev, cur);
		else
			emit_seg (e, prev, cur);
		memcpy (prev, cur, sizeof prev);
	}
}

/* emit_arc: half a circle from +@u through @v to -@u (wire only). */
static void
emit_arc (Emit         *e,
          const gfloat *center,
          const gfloat *u,
          const gfloat *v,
          gfloat        r,
          guint         segments)
{
	gfloat prev[3];
	gfloat cur[3];
	guint  half = MAX (segments / 2, 2u);
	guint  i;

	ring_point (center, u, v, r, 0.0f, prev);
	for (i = 1; i <= half; i++)
	{
		ring_point (center, u, v, r, (gfloat)(G_PI * i / half), cur);
		emit_seg (e, prev, cur);
		memcpy (prev, cur, sizeof prev);
	}
}

/* dome_point: a point of the hemisphere over @center towards @axis at
 * elevation @lat (0 at the equator) and azimuth @lon. */
static void
dome_point (const gfloat *center,
            const gfloat *axis,
            const gfloat *u,
            const gfloat *v,
            gfloat        r,
            gfloat        lat,
            gfloat        lon,
            gfloat       *o)
{
	gfloat ring[3];
	gfloat zero[3] = { 0.0f, 0.0f, 0.0f };

	ring_point (zero, u, v, r * cosf (lat), lon, ring);
	o[0] = center[0] + ring[0] + axis[0] * r * sinf (lat);
	o[1] = center[1] + ring[1] + axis[1] * r * sinf (lat);
	o[2] = center[2] + ring[2] + axis[2] * r * sinf (lat);
}

/* emit_dome: a solid hemisphere (triangles) over @center towards @axis. */
static void
emit_dome (Emit         *e,
           const gfloat *center,
           const gfloat *axis,
           const gfloat *u,
           const gfloat *v,
           gfloat        r,
           guint         segments)
{
	guint rings = MAX (segments / 4, 2u);
	guint i;
	guint j;

	for (i = 0; i < rings; i++)
	{
		gfloat lat0 = (gfloat)(G_PI * 0.5 * i / rings);
		gfloat lat1 = (gfloat)(G_PI * 0.5 * (i + 1) / rings);

		for (j = 0; j < segments; j++)
		{
			gfloat lon0 = (gfloat)(2.0 * G_PI * j / segments);
			gfloat lon1 = (gfloat)(2.0 * G_PI * (j + 1) / segments);
			gfloat p00[3], p01[3], p10[3], p11[3];

			dome_point (center, axis, u, v, r, lat0, lon0, p00);
			dome_point (center, axis, u, v, r, lat0, lon1, p01);
			dome_point (center, axis, u, v, r, lat1, lon0, p10);
			dome_point (center, axis, u, v, r, lat1, lon1, p11);
			emit_tri (e, p00, p01, p11);
			if (i + 1 < rings)
				emit_tri (e, p00, p11, p10);
		}
	}
}

/* emit_tube_side: the side of a cylinder between two cap centres; wire
 * draws four lines along it, solid a band of quads. */
static void
emit_tube_side (Emit         *e,
                const gfloat *a,
                const gfloat *b,
                const gfloat *u,
                const gfloat *v,
                gfloat        r,
                guint         segments,
                gboolean      fill)
{
	guint i;
	guint steps = fill ? segments : 4;

	for (i = 0; i < steps; i++)
	{
		gfloat t0 = (gfloat)(2.0 * G_PI * i / steps);
		gfloat t1 = (gfloat)(2.0 * G_PI * (i + 1) / steps);
		gfloat a0[3], b0[3], a1[3], b1[3];

		ring_point (a, u, v, r, t0, a0);
		ring_point (b, u, v, r, t0, b0);
		if (!fill)
		{
			emit_seg (e, a0, b0);
			continue;
		}
		ring_point (a, u, v, r, t1, a1);
		ring_point (b, u, v, r, t1, b1);
		emit_tri (e, a0, a1, b1);
		emit_tri (e, a0, b1, b0);
	}
}

/* box_corners: the eight corners of a box, bit 0 = x, 1 = y, 2 = z. */
static void
emit_box_corners (Emit         *e,
                  gfloat        corners[8][3],
                  gboolean      fill)
{
	static const guint edges[12][2] = {
		{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
		{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
	};
	static const guint faces[6][4] = {
		{ 0, 2, 3, 1 }, { 4, 5, 7, 6 },  /* -z, +z */
		{ 0, 1, 5, 4 }, { 2, 6, 7, 3 },  /* -y, +y */
		{ 0, 4, 6, 2 }, { 1, 3, 7, 5 }   /* -x, +x */
	};
	guint i;

	if (!fill)
	{
		for (i = 0; i < 12; i++)
			emit_seg (e, corners[edges[i][0]], corners[edges[i][1]]);
		return;
	}
	for (i = 0; i < 6; i++)
	{
		emit_tri (e, corners[faces[i][0]], corners[faces[i][1]], corners[faces[i][2]]);
		emit_tri (e, corners[faces[i][0]], corners[faces[i][2]], corners[faces[i][3]]);
	}
}

/* tessellate_prim: one primitive into @e (3D and 2D geometry alike; the
 * caller picks the lists). */
static void
tessellate_prim (const Prim *prim,
                 Emit       *e)
{
	const gfloat *pa = prim->p[0];
	const gfloat *pb = prim->p[1];
	gboolean      fill = prim->pen.solid;
	guint         seg = prim->pen.segments;
	gfloat        axis[3];
	gfloat        u[3];
	gfloat        v[3];
	gfloat        corners[8][3];
	guint         i;

	switch (prim->shape)
	{
	case LRG_DEBUG_SHAPE_LINE:
	case LRG_DEBUG_SHAPE_LINE_2D:
		emit_seg (e, pa, pb);
		break;

	case LRG_DEBUG_SHAPE_POLYLINE:
		for (i = 0; i + 1 < prim->n_points; i++)
			emit_seg (e, &prim->points[i * 3], &prim->points[(i + 1) * 3]);
		if (prim->n != 0 && prim->n_points > 2)
			emit_seg (e, &prim->points[(prim->n_points - 1) * 3], &prim->points[0]);
		break;

	case LRG_DEBUG_SHAPE_ARROW:
	{
		gfloat len;
		gfloat head;
		gfloat back[3];
		gfloat barb[3];

		emit_seg (e, pa, pb);
		v3_sub (pb, pa, axis);
		len = v3_normalize (axis);
		if (len <= 1e-6f)
			break;
		head = prim->r > 0.0f ? prim->r : len * 0.2f;
		basis (axis, u, v);
		v3_mad (pb, axis, -head, back);
		/* four barbs, half as wide as long */
		v3_mad (back, u, head * 0.5f, barb);
		emit_seg (e, pb, barb);
		v3_mad (back, u, -head * 0.5f, barb);
		emit_seg (e, pb, barb);
		v3_mad (back, v, head * 0.5f, barb);
		emit_seg (e, pb, barb);
		v3_mad (back, v, -head * 0.5f, barb);
		emit_seg (e, pb, barb);
		break;
	}

	case LRG_DEBUG_SHAPE_BOX:
		for (i = 0; i < 8; i++)
			v3_set (corners[i], (i & 1) ? pb[0] : pa[0], (i & 2) ? pb[1] : pa[1],
			        (i & 4) ? pb[2] : pa[2]);
		emit_box_corners (e, corners, fill);
		break;

	case LRG_DEBUG_SHAPE_ORIENTED_BOX:
		for (i = 0; i < 8; i++)
		{
			gfloat local[3];

			v3_set (local, (i & 1) ? pb[0] : -pb[0], (i & 2) ? pb[1] : -pb[1],
			        (i & 4) ? pb[2] : -pb[2]);
			mat_point (prim->m, local, corners[i]);
		}
		emit_box_corners (e, corners, fill);
		break;

	case LRG_DEBUG_SHAPE_SPHERE:
	{
		gfloat x[3] = { 1.0f, 0.0f, 0.0f };
		gfloat y[3] = { 0.0f, 1.0f, 0.0f };
		gfloat z[3] = { 0.0f, 0.0f, 1.0f };
		gfloat ny[3] = { 0.0f, -1.0f, 0.0f };

		if (fill)
		{
			/* two domes: +Y and -Y */
			emit_dome (e, pa, y, x, z, prim->r, seg);
			emit_dome (e, pa, ny, z, x, prim->r, seg);
			break;
		}
		emit_ring (e, pa, x, y, prim->r, seg, FALSE);
		emit_ring (e, pa, x, z, prim->r, seg, FALSE);
		emit_ring (e, pa, y, z, prim->r, seg, FALSE);
		break;
	}

	case LRG_DEBUG_SHAPE_CAPSULE:
	case LRG_DEBUG_SHAPE_CYLINDER:
	{
		gfloat neg[3];

		v3_sub (pb, pa, axis);
		if (v3_normalize (axis) <= 1e-6f)
			v3_set (axis, 0.0f, 1.0f, 0.0f);
		basis (axis, u, v);
		v3_set (neg, -axis[0], -axis[1], -axis[2]);
		emit_tube_side (e, pa, pb, u, v, prim->r, seg, fill);
		if (prim->shape == LRG_DEBUG_SHAPE_CYLINDER)
		{
			emit_ring (e, pa, u, v, prim->r, seg, fill);
			emit_ring (e, pb, u, v, prim->r, seg, fill);
			break;
		}
		if (fill)
		{
			emit_dome (e, pb, axis, u, v, prim->r, seg);
			emit_dome (e, pa, neg, v, u, prim->r, seg);
			break;
		}
		/* wire capsule: both equators and two half circles per end */
		emit_ring (e, pa, u, v, prim->r, seg, FALSE);
		emit_ring (e, pb, u, v, prim->r, seg, FALSE);
		emit_arc (e, pb, u, axis, prim->r, seg);
		emit_arc (e, pb, v, axis, prim->r, seg);
		emit_arc (e, pa, u, neg, prim->r, seg);
		emit_arc (e, pa, v, neg, prim->r, seg);
		break;
	}

	case LRG_DEBUG_SHAPE_CONE:
	{
		gfloat rim0[3];
		gfloat rim1[3];

		v3_sub (pb, pa, axis);
		if (v3_normalize (axis) <= 1e-6f)
			v3_set (axis, 0.0f, -1.0f, 0.0f);
		basis (axis, u, v);
		emit_ring (e, pb, u, v, prim->r, seg, fill);
		for (i = 0; i < (fill ? seg : 4u); i++)
		{
			guint steps = fill ? seg : 4u;

			ring_point (pb, u, v, prim->r, (gfloat)(2.0 * G_PI * i / steps), rim0);
			if (!fill)
			{
				emit_seg (e, pa, rim0);
				continue;
			}
			ring_point (pb, u, v, prim->r, (gfloat)(2.0 * G_PI * (i + 1) / steps), rim1);
			emit_tri (e, pa, rim0, rim1);
		}
		break;
	}

	case LRG_DEBUG_SHAPE_FRUSTUM:
		/* the clip cube's corners under the stored inverse */
		for (i = 0; i < 8; i++)
		{
			gfloat ndc[3];

			v3_set (ndc, (i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f,
			        (i & 4) ? 1.0f : -1.0f);
			mat_point (prim->m, ndc, corners[i]);
		}
		emit_box_corners (e, corners, FALSE);
		break;

	case LRG_DEBUG_SHAPE_GRID:
	{
		gfloat half = prim->r * (gfloat)prim->n * 0.5f;
		gfloat a[3];
		gfloat b[3];

		for (i = 0; i <= prim->n; i++)
		{
			gfloat off = -half + prim->r * (gfloat)i;

			v3_set (a, pa[0] + off, pa[1], pa[2] - half);
			v3_set (b, pa[0] + off, pa[1], pa[2] + half);
			emit_seg (e, a, b);
			v3_set (a, pa[0] - half, pa[1], pa[2] + off);
			v3_set (b, pa[0] + half, pa[1], pa[2] + off);
			emit_seg (e, a, b);
		}
		break;
	}

	case LRG_DEBUG_SHAPE_AXES:
	{
		static const guint8 axis_colors[3][4] = {
			{ 230, 41, 55, 255 }, { 0, 228, 48, 255 }, { 0, 121, 241, 255 }
		};
		const guint8 *saved = e->color;

		for (i = 0; i < 3; i++)
		{
			gfloat dir[3];
			gfloat tip[3];

			/* the matrix's column i, normalised: scale is ignored */
			v3_set (dir, prim->m[i * 4], prim->m[i * 4 + 1], prim->m[i * 4 + 2]);
			if (v3_normalize (dir) <= 1e-12f)
				continue;
			v3_mad (pa, dir, prim->r, tip);
			e->color = axis_colors[i];
			emit_seg (e, pa, tip);
		}
		e->color = saved;
		break;
	}

	case LRG_DEBUG_SHAPE_CIRCLE:
		basis (pb, u, v);
		emit_ring (e, pa, u, v, prim->r, seg, fill);
		break;

	case LRG_DEBUG_SHAPE_POINT:
	{
		gfloat h = prim->r * 0.5f;
		gfloat a[3];
		gfloat b[3];

		for (i = 0; i < 3; i++)
		{
			memcpy (a, pa, sizeof a);
			memcpy (b, pa, sizeof b);
			a[i] -= h;
			b[i] += h;
			emit_seg (e, a, b);
		}
		break;
	}

	case LRG_DEBUG_SHAPE_RECT_2D:
	{
		gfloat c[4][3];

		v3_set (c[0], pa[0], pa[1], 0.0f);
		v3_set (c[1], pa[0] + pb[0], pa[1], 0.0f);
		v3_set (c[2], pa[0] + pb[0], pa[1] + pb[1], 0.0f);
		v3_set (c[3], pa[0], pa[1] + pb[1], 0.0f);
		if (fill)
		{
			/* counter-clockwise on screen (y down) as raylib's own */
			emit_tri (e, c[0], c[3], c[2]);
			emit_tri (e, c[0], c[2], c[1]);
			break;
		}
		for (i = 0; i < 4; i++)
			emit_seg (e, c[i], c[(i + 1) % 4]);
		break;
	}

	case LRG_DEBUG_SHAPE_CIRCLE_2D:
	{
		gfloat x[3] = { 1.0f, 0.0f, 0.0f };
		gfloat y[3] = { 0.0f, -1.0f, 0.0f };

		/* -y keeps the fan counter-clockwise on a y-down screen */
		emit_ring (e, pa, x, y, prim->r, seg, fill);
		break;
	}

	case LRG_DEBUG_SHAPE_TEXT:
	case LRG_DEBUG_SHAPE_TEXT_2D:
	default:
		/* labels are collected by the caller */
		break;
	}
}

/* is_2d: whether @shape is drawn by the 2D pass. */
static inline gboolean
is_2d (LrgDebugShape shape)
{
	return shape >= LRG_DEBUG_SHAPE_LINE_2D;
}

/* build: re-tessellates every visible primitive when anything changed. */
static void
build (LrgDebugDraw *self)
{
	guint i;

	if (!self->dirty)
		return;
	for (i = 0; i < 2; i++)
	{
		g_array_set_size (self->segs[i], 0);
		g_array_set_size (self->tris[i], 0);
	}
	g_array_set_size (self->labels, 0);
	g_array_set_size (self->segs_2d, 0);
	g_array_set_size (self->tris_2d, 0);
	g_array_set_size (self->texts_2d, 0);

	for (i = 0; i < self->prims->len; i++)
	{
		const Prim *prim = &g_array_index (self->prims, Prim, i);
		Emit        e;
		guint       list;

		/* timed primitives of a category switched off since stay
		 * recorded but are not drawn */
		if ((self->category_mask & ((guint32)1u << prim->pen.category)) == 0)
			continue;

		if (prim->shape == LRG_DEBUG_SHAPE_TEXT || prim->shape == LRG_DEBUG_SHAPE_TEXT_2D)
		{
			Label label;

			memcpy (label.p, prim->p[0], sizeof label.p);
			memcpy (label.c, prim->color, sizeof label.c);
			label.size = prim->pen.text_size;
			label.text = prim->text;
			g_array_append_val (prim->shape == LRG_DEBUG_SHAPE_TEXT
			                    ? self->labels : self->texts_2d, label);
			continue;
		}

		list = prim->pen.depth_test ? LIST_DEPTH : LIST_TOP;
		e.segs = is_2d (prim->shape) ? self->segs_2d : self->segs[list];
		e.tris = is_2d (prim->shape) ? self->tris_2d : self->tris[list];
		e.color = prim->color;
		e.thickness = prim->pen.thickness;
		tessellate_prim (prim, &e);
	}
	self->dirty = FALSE;
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/* view_eye: the camera position and forward direction of a column-major
 * view matrix (rigid: the eye is -R^T t, forward is -row 2 of R). */
static void
view_eye (const gfloat *view,
          gfloat       *eye,
          gfloat       *forward)
{
	guint j;

	for (j = 0; j < 3; j++)
		eye[j] = -(view[j * 4 + 0] * view[12] + view[j * 4 + 1] * view[13]
		           + view[j * 4 + 2] * view[14]);
	v3_set (forward, -view[2], -view[6], -view[10]);
	v3_normalize (forward);
}

/* thick_quad: the two triangles covering a segment @thickness pixels
 * wide, facing the camera.  @perspective selects how a pixel converts
 * to world units at a point: 2 * depth / (proj[5] * height) for a
 * perspective projection, 2 / (proj[5] * height) for an orthographic
 * one.  @to_viewer is used for the orthographic case. Returns FALSE
 * for a degenerate segment. */
static gboolean
thick_quad (const Seg    *seg,
            const gfloat *eye,
            const gfloat *forward,
            gboolean      perspective,
            gfloat        proj5,
            gfloat        height,
            gfloat        quad[4][3])
{
	gfloat dir[3];
	gfloat mid[3];
	gfloat look[3];
	gfloat side[3];
	gfloat half[2];
	guint  k;

	v3_sub (seg->b, seg->a, dir);
	for (k = 0; k < 3; k++)
		mid[k] = (seg->a[k] + seg->b[k]) * 0.5f;
	if (perspective)
		v3_sub (eye, mid, look);
	else
		v3_set (look, -forward[0], -forward[1], -forward[2]);
	v3_cross (dir, look, side);
	if (v3_normalize (side) <= 1e-12f)
		return FALSE;

	for (k = 0; k < 2; k++)
	{
		const gfloat *p = k == 0 ? seg->a : seg->b;
		gfloat        world_per_pixel;

		if (perspective)
		{
			gfloat rel[3];
			gfloat depth;

			v3_sub (p, eye, rel);
			depth = MAX (v3_dot (rel, forward), 1e-4f);
			world_per_pixel = 2.0f * depth / (proj5 * height);
		}
		else
			world_per_pixel = 2.0f / (proj5 * height);
		half[k] = fabsf (seg->thickness * 0.5f * world_per_pixel);
	}
	v3_mad (seg->a, side, half[0], quad[0]);
	v3_mad (seg->a, side, -half[0], quad[1]);
	v3_mad (seg->b, side, -half[1], quad[2]);
	v3_mad (seg->b, side, half[1], quad[3]);
	return TRUE;
}

static inline void
put_vertex (const gfloat *p,
            const guint8 *c)
{
	rlColor4ub (c[0], c[1], c[2], c[3]);
	rlVertex3f (p[0], p[1], p[2]);
}

/* draw_list: one list as at most one GL_LINES run and one GL_TRIANGLES
 * run.  @thick_3d tells how wide segments become quads (3D: facing the
 * camera; 2D: perpendicular on screen). */
static void
draw_list (LrgDebugDraw *self,
           GArray       *segs,
           GArray       *tris,
           gboolean      thick_3d,
           const gfloat *eye,
           const gfloat *forward,
           gboolean      perspective,
           gfloat        proj5,
           gfloat        height)
{
	gboolean any_thick = FALSE;
	gboolean any_thin = FALSE;
	gboolean blended = FALSE;
	guint    i;

	for (i = 0; i < segs->len; i++)
	{
		if (g_array_index (segs, Seg, i).thickness > 1.0f)
			any_thick = TRUE;
		else
			any_thin = TRUE;
	}

	if (any_thin)
	{
		rlBegin (RL_LINES);
		for (i = 0; i < segs->len; i++)
		{
			const Seg *seg = &g_array_index (segs, Seg, i);

			if (seg->thickness > 1.0f)
				continue;
			put_vertex (seg->a, seg->c);
			put_vertex (seg->b, seg->c);
		}
		rlEnd ();
		self->draw_calls++;
	}

	if (tris->len == 0 && !any_thick)
		return;

	/* Translucent fills must not hide what lies behind them. */
	for (i = 0; i < tris->len && !blended; i++)
		blended = g_array_index (tris, Tri, i).c[3] < 255;
	rlDrawRenderBatchActive ();
	rlDisableBackfaceCulling ();
	if (blended)
		rlDisableDepthMask ();

	rlBegin (RL_TRIANGLES);
	for (i = 0; i < tris->len; i++)
	{
		const Tri *tri = &g_array_index (tris, Tri, i);

		put_vertex (tri->v[0], tri->c);
		put_vertex (tri->v[1], tri->c);
		put_vertex (tri->v[2], tri->c);
	}
	for (i = 0; any_thick && i < segs->len; i++)
	{
		const Seg *seg = &g_array_index (segs, Seg, i);
		gfloat     quad[4][3];

		if (seg->thickness <= 1.0f)
			continue;
		if (thick_3d)
		{
			if (!thick_quad (seg, eye, forward, perspective, proj5, height, quad))
				continue;
		}
		else
		{
			gfloat dx = seg->b[0] - seg->a[0];
			gfloat dy = seg->b[1] - seg->a[1];
			gfloat len = sqrtf (dx * dx + dy * dy);
			gfloat nx;
			gfloat ny;

			if (len <= 1e-6f)
				continue;
			nx = -dy / len * seg->thickness * 0.5f;
			ny = dx / len * seg->thickness * 0.5f;
			v3_set (quad[0], seg->a[0] + nx, seg->a[1] + ny, 0.0f);
			v3_set (quad[1], seg->a[0] - nx, seg->a[1] - ny, 0.0f);
			v3_set (quad[2], seg->b[0] - nx, seg->b[1] - ny, 0.0f);
			v3_set (quad[3], seg->b[0] + nx, seg->b[1] + ny, 0.0f);
		}
		put_vertex (quad[0], seg->c);
		put_vertex (quad[1], seg->c);
		put_vertex (quad[2], seg->c);
		put_vertex (quad[0], seg->c);
		put_vertex (quad[2], seg->c);
		put_vertex (quad[3], seg->c);
	}
	rlEnd ();
	self->draw_calls++;

	rlDrawRenderBatchActive ();
	if (blended)
		rlEnableDepthMask ();
	rlEnableBackfaceCulling ();
}

void
lrg_debug_draw_draw_3d (LrgDebugDraw *self)
{
	gfloat eye[3];
	gfloat forward[3];
	gfloat height;
	gboolean perspective;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	self->draw_calls = 0;
	if (!self->enabled || !IsWindowReady ())
		return;
	build (self);

	/* What the labels and the thick lines need: the live matrices and
	 * the target's size. */
	matrix_to_floats (rlGetMatrixModelview (), self->label_view);
	matrix_to_floats (rlGetMatrixProjection (), self->label_proj);
	self->label_width = rlGetFramebufferWidth ();
	self->label_height = rlGetFramebufferHeight ();
	self->label_valid = self->label_width > 0 && self->label_height > 0;
	view_eye (self->label_view, eye, forward);
	perspective = fabsf (self->label_proj[11]) > 1e-6f;
	height = (gfloat)MAX (self->label_height, 1);

	if (self->segs[LIST_DEPTH]->len + self->tris[LIST_DEPTH]->len
	    + self->segs[LIST_TOP]->len + self->tris[LIST_TOP]->len == 0)
		return;

	/* the depth-tested list, then the always-on-top list */
	rlDrawRenderBatchActive ();
	rlEnableDepthTest ();
	draw_list (self, self->segs[LIST_DEPTH], self->tris[LIST_DEPTH], TRUE,
	           eye, forward, perspective, self->label_proj[5], height);
	rlDrawRenderBatchActive ();
	rlDisableDepthTest ();
	draw_list (self, self->segs[LIST_TOP], self->tris[LIST_TOP], TRUE,
	           eye, forward, perspective, self->label_proj[5], height);
	rlDrawRenderBatchActive ();
	/* a 3D mode runs with the depth test on */
	rlEnableDepthTest ();
}

void
lrg_debug_draw_draw_labels (LrgDebugDraw *self)
{
	gfloat view_proj[16];
	guint  i;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (!self->enabled || !IsWindowReady () || !self->label_valid)
		return;
	build (self);

	lrg_shadow_multiply (self->label_proj, self->label_view, view_proj);
	for (i = 0; i < self->labels->len; i++)
	{
		const Label *label = &g_array_index (self->labels, Label, i);
		const gfloat *m = view_proj;
		const gfloat *p = label->p;
		gfloat        cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
		gfloat        cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
		gfloat        cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
		gfloat        sx;
		gfloat        sy;
		gint          width;

		/* anchors behind the camera have no place on screen */
		if (cw <= 1e-6f)
			continue;
		sx = (cx / cw * 0.5f + 0.5f) * (gfloat)self->label_width;
		sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * (gfloat)self->label_height;
		width = MeasureText (label->text, (gint)label->size);
		DrawText (label->text, (gint)(sx - (gfloat)width * 0.5f),
		          (gint)(sy - (gfloat)label->size * 0.5f), (gint)label->size,
		          (Color){ label->c[0], label->c[1], label->c[2], label->c[3] });
	}
}

void
lrg_debug_draw_draw_2d (LrgDebugDraw *self)
{
	guint i;

	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));

	if (!self->enabled || !IsWindowReady ())
		return;
	build (self);

	if (self->segs_2d->len + self->tris_2d->len > 0)
	{
		rlDrawRenderBatchActive ();
		draw_list (self, self->segs_2d, self->tris_2d, FALSE, NULL, NULL, FALSE, 1.0f, 1.0f);
	}
	for (i = 0; i < self->texts_2d->len; i++)
	{
		const Label *label = &g_array_index (self->texts_2d, Label, i);

		DrawText (label->text, (gint)label->p[0], (gint)label->p[1], (gint)label->size,
		          (Color){ label->c[0], label->c[1], label->c[2], label->c[3] });
	}
}

void
lrg_debug_draw_flush (LrgDebugDraw *self,
                      LrgCamera    *camera)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (camera == NULL || LRG_IS_CAMERA (camera));

	if (self->enabled && IsWindowReady ())
	{
		if (camera != NULL && LRG_IS_CAMERA3D (camera))
		{
			lrg_camera_begin (camera);
			lrg_debug_draw_draw_3d (self);
			lrg_camera_end (camera);
			lrg_debug_draw_draw_labels (self);
			lrg_debug_draw_draw_2d (self);
		}
		else if (camera != NULL)
		{
			lrg_camera_begin (camera);
			lrg_debug_draw_draw_2d (self);
			lrg_camera_end (camera);
		}
		else
			lrg_debug_draw_draw_2d (self);
	}
	lrg_debug_draw_end_frame (self);
}

void
lrg_debug_draw_flush_matrices (LrgDebugDraw *self,
                               const gfloat *view,
                               const gfloat *projection)
{
	g_return_if_fail (LRG_IS_DEBUG_DRAW (self));
	g_return_if_fail (view != NULL && projection != NULL);

	if (self->enabled && IsWindowReady ())
	{
		/* as BeginMode3D(), with the given matrices */
		rlDrawRenderBatchActive ();
		rlMatrixMode (RL_PROJECTION);
		rlPushMatrix ();
		rlLoadIdentity ();
		rlMultMatrixf (projection);
		/* no push here: rlgl routes a pushed modelview into its CPU
		 * transform, which rlGetMatrixModelview() would not see */
		rlMatrixMode (RL_MODELVIEW);
		rlLoadIdentity ();
		rlMultMatrixf (view);
		rlEnableDepthTest ();

		lrg_debug_draw_draw_3d (self);

		/* as EndMode3D() */
		rlDrawRenderBatchActive ();
		rlMatrixMode (RL_PROJECTION);
		rlPopMatrix ();
		rlMatrixMode (RL_MODELVIEW);
		rlLoadIdentity ();
		rlDisableDepthTest ();

		lrg_debug_draw_draw_labels (self);
		lrg_debug_draw_draw_2d (self);
	}
	lrg_debug_draw_end_frame (self);
}

/* ==========================================================================
 * Inspection
 * ========================================================================== */

static const Prim *
prim_at (LrgDebugDraw *self,
         guint         index)
{
	if (index >= self->prims->len)
		return NULL;
	return &g_array_index (self->prims, Prim, index);
}

guint
lrg_debug_draw_get_count (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	return self->prims->len;
}

LrgDebugShape
lrg_debug_draw_get_shape (LrgDebugDraw *self,
                          guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), LRG_DEBUG_SHAPE_LINE);
	prim = prim_at (self, index);
	return prim != NULL ? prim->shape : LRG_DEBUG_SHAPE_LINE;
}

guint
lrg_debug_draw_get_primitive_category (LrgDebugDraw *self,
                                       guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	prim = prim_at (self, index);
	return prim != NULL ? prim->pen.category : 0;
}

gboolean
lrg_debug_draw_get_primitive_depth_test (LrgDebugDraw *self,
                                         guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);
	prim = prim_at (self, index);
	return prim != NULL && prim->pen.depth_test;
}

gboolean
lrg_debug_draw_get_primitive_solid (LrgDebugDraw *self,
                                    guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);
	prim = prim_at (self, index);
	return prim != NULL && prim->pen.solid;
}

gfloat
lrg_debug_draw_get_primitive_thickness (LrgDebugDraw *self,
                                        guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0.0f);
	prim = prim_at (self, index);
	return prim != NULL ? prim->pen.thickness : 0.0f;
}

gfloat
lrg_debug_draw_get_primitive_remaining (LrgDebugDraw *self,
                                        guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0.0f);
	prim = prim_at (self, index);
	return prim != NULL && prim->timed ? prim->remaining : 0.0f;
}

gboolean
lrg_debug_draw_get_primitive_color (LrgDebugDraw *self,
                                    guint         index,
                                    GrlColor     *out_color)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);
	g_return_val_if_fail (out_color != NULL, FALSE);

	prim = prim_at (self, index);
	if (prim == NULL)
		return FALSE;
	out_color->r = prim->color[0];
	out_color->g = prim->color[1];
	out_color->b = prim->color[2];
	out_color->a = prim->color[3];
	return TRUE;
}

gboolean
lrg_debug_draw_get_primitive_point (LrgDebugDraw *self,
                                    guint         index,
                                    guint         which,
                                    GrlVector3   *out_point)
{
	const Prim   *prim;
	const gfloat *src = NULL;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), FALSE);
	g_return_val_if_fail (out_point != NULL, FALSE);

	prim = prim_at (self, index);
	if (prim == NULL)
		return FALSE;

	switch (prim->shape)
	{
	case LRG_DEBUG_SHAPE_POLYLINE:
		if (which < prim->n_points)
			src = &prim->points[which * 3];
		break;
	case LRG_DEBUG_SHAPE_LINE:
	case LRG_DEBUG_SHAPE_ARROW:
	case LRG_DEBUG_SHAPE_BOX:
	case LRG_DEBUG_SHAPE_CAPSULE:
	case LRG_DEBUG_SHAPE_CYLINDER:
	case LRG_DEBUG_SHAPE_CONE:
	case LRG_DEBUG_SHAPE_LINE_2D:
	case LRG_DEBUG_SHAPE_RECT_2D:
		if (which < 2)
			src = prim->p[which];
		break;
	case LRG_DEBUG_SHAPE_ORIENTED_BOX:
	case LRG_DEBUG_SHAPE_SPHERE:
	case LRG_DEBUG_SHAPE_FRUSTUM:
	case LRG_DEBUG_SHAPE_GRID:
	case LRG_DEBUG_SHAPE_AXES:
	case LRG_DEBUG_SHAPE_CIRCLE:
	case LRG_DEBUG_SHAPE_POINT:
	case LRG_DEBUG_SHAPE_TEXT:
	case LRG_DEBUG_SHAPE_CIRCLE_2D:
	case LRG_DEBUG_SHAPE_TEXT_2D:
	default:
		if (which == 0)
			src = prim->p[0];
		break;
	}
	if (src == NULL)
		return FALSE;
	out_point->x = src[0];
	out_point->y = src[1];
	out_point->z = src[2];
	return TRUE;
}

const gchar *
lrg_debug_draw_get_primitive_text (LrgDebugDraw *self,
                                   guint         index)
{
	const Prim *prim;

	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), NULL);
	prim = prim_at (self, index);
	return prim != NULL ? prim->text : NULL;
}

guint
lrg_debug_draw_get_line_count (LrgDebugDraw *self,
                               gboolean      depth_tested)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	build (self);
	return self->segs[depth_tested ? LIST_DEPTH : LIST_TOP]->len;
}

guint
lrg_debug_draw_get_triangle_count (LrgDebugDraw *self,
                                   gboolean      depth_tested)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	build (self);
	return self->tris[depth_tested ? LIST_DEPTH : LIST_TOP]->len;
}

guint
lrg_debug_draw_get_label_count (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	build (self);
	return self->labels->len;
}

guint
lrg_debug_draw_get_draw_calls (LrgDebugDraw *self)
{
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (self), 0);
	return self->draw_calls;
}
