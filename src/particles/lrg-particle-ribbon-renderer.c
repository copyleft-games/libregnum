/* lrg-particle-ribbon-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Trails and ribbons for particle systems.
 *
 * Trail mode keeps, per particle id, the points the particle passed:
 * update() appends a point whenever the particle moved min-distance
 * since its last one, ages every point and drops points older than
 * trail-lifetime (a dead particle's trail keeps fading until its last
 * point expires).  Connect mode needs no history: build() strings the
 * live particles together in spawn order, newest first.
 *
 * build() turns each strip into camera-facing quads headless: the side
 * vector of a point is cross (tangent, eye - point), normalized and
 * scaled to half the point's width (times the width curve at the
 * point's position along the strip), so tests check exactly the
 * geometry draw() submits through rlgl.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-particle-ribbon-renderer.h"

/* Points closer than this are merged into one strip point */
#define MERGE_EPSILON (1e-5f)

/* Size of the generated soft-edge texture */
#define EDGE_W (4)
#define EDGE_H (32)

typedef struct
{
	gfloat pos[3];
	gfloat age;       /* seconds since recorded */
	gfloat width;     /* renderer width x particle size when recorded */
	gfloat color[4];
} TrailPoint;

typedef struct
{
	guint32  id;
	gboolean live;      /* its particle is alive */
	gboolean seen;      /* touched by the current update */
	gfloat   head[3];   /* the particle's current position */
	gfloat   head_width;
	gfloat   head_color[4];
	GArray  *points;    /* TrailPoint, oldest first */
} Trail;

/* A point of a strip being built */
typedef struct
{
	gfloat pos[3];
	gfloat width;
	gfloat color[4];
} StripPoint;

struct _LrgParticleRibbonRenderer
{
	GObject                 parent_instance;

	LrgParticleRibbonMode   mode;
	LrgParticleRibbonUvMode uv_mode;
	gfloat                  uv_tile_length;
	gfloat                  width;
	LrgKeyframeCurve       *width_curve;
	LrgColorGradient       *gradient;
	guint                   max_points;
	gfloat                  min_distance;
	gfloat                  trail_lifetime;
	GrlTexture             *texture;

	GPtrArray              *trails;   /* Trail *, creation order */
	GHashTable             *by_id;    /* id -> Trail * (borrowed) */

	GArray                 *strip;    /* StripPoint scratch */
	GArray                 *scratch;  /* gfloat vertices for draw() */

	Texture2D               edge;
	gboolean                edge_loaded;
};

G_DEFINE_FINAL_TYPE (LrgParticleRibbonRenderer, lrg_particle_ribbon_renderer, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_MODE,
	PROP_UV_MODE,
	PROP_UV_TILE_LENGTH,
	PROP_WIDTH,
	PROP_WIDTH_CURVE,
	PROP_COLOR_GRADIENT,
	PROP_MAX_POINTS,
	PROP_MIN_DISTANCE,
	PROP_TRAIL_LIFETIME,
	PROP_TEXTURE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

GType
lrg_particle_ribbon_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PARTICLE_RIBBON_TRAIL, "LRG_PARTICLE_RIBBON_TRAIL", "trail" },
			{ LRG_PARTICLE_RIBBON_CONNECT, "LRG_PARTICLE_RIBBON_CONNECT", "connect" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgParticleRibbonMode", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_particle_ribbon_uv_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PARTICLE_RIBBON_UV_STRETCH, "LRG_PARTICLE_RIBBON_UV_STRETCH", "stretch" },
			{ LRG_PARTICLE_RIBBON_UV_TILE, "LRG_PARTICLE_RIBBON_UV_TILE", "tile" },
			{ LRG_PARTICLE_RIBBON_UV_PER_SEGMENT, "LRG_PARTICLE_RIBBON_UV_PER_SEGMENT", "per-segment" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgParticleRibbonUvMode", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Trails
 * ========================================================================== */

static void
trail_free (gpointer data)
{
	Trail *t = data;

	g_array_unref (t->points);
	g_free (t);
}

static gfloat
dist3 (const gfloat *a,
       const gfloat *b)
{
	gfloat dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];

	return sqrtf (dx * dx + dy * dy + dz * dz);
}

typedef struct
{
	LrgParticleRibbonRenderer *self;
} RecordData;

/* One live particle during update(): find its trail, move the head and
 * record a point when it travelled far enough */
static void
record_particle (const LrgParticle *p,
                 gpointer           user_data)
{
	LrgParticleRibbonRenderer *self = ((RecordData *) user_data)->self;
	Trail                     *t = g_hash_table_lookup (self->by_id, GUINT_TO_POINTER (p->id));
	TrailPoint                 pt;

	if (t == NULL)
	{
		t = g_new0 (Trail, 1);
		t->id = p->id;
		t->points = g_array_new (FALSE, FALSE, sizeof (TrailPoint));
		g_ptr_array_add (self->trails, t);
		g_hash_table_insert (self->by_id, GUINT_TO_POINTER (p->id), t);
	}
	t->seen = TRUE;
	t->live = TRUE;
	t->head[0] = p->position_x;
	t->head[1] = p->position_y;
	t->head[2] = p->position_z;
	t->head_width = self->width * p->size;
	t->head_color[0] = p->color_r;
	t->head_color[1] = p->color_g;
	t->head_color[2] = p->color_b;
	t->head_color[3] = p->color_a;

	if (t->points->len > 0 &&
	    dist3 (g_array_index (t->points, TrailPoint, t->points->len - 1).pos, t->head) < self->min_distance)
		return;
	memcpy (pt.pos, t->head, sizeof pt.pos);
	pt.age = 0.0f;
	pt.width = t->head_width;
	memcpy (pt.color, t->head_color, sizeof pt.color);
	g_array_append_val (t->points, pt);
	if (t->points->len > self->max_points)
		g_array_remove_range (t->points, 0, t->points->len - self->max_points);
}

void
lrg_particle_ribbon_renderer_update (LrgParticleRibbonRenderer *self,
                                     LrgParticleSystem         *system,
                                     gfloat                     delta_time)
{
	RecordData rd;
	guint      i, w;

	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (system));

	if (self->mode != LRG_PARTICLE_RIBBON_TRAIL)
		return;

	/* age every point; expired ones are the oldest, at the front */
	for (i = 0; i < self->trails->len; i++)
	{
		Trail *t = g_ptr_array_index (self->trails, i);
		guint  drop = 0, k;

		t->seen = FALSE;
		for (k = 0; k < t->points->len; k++)
		{
			TrailPoint *pt = &g_array_index (t->points, TrailPoint, k);

			pt->age += delta_time;
			if (pt->age >= self->trail_lifetime)
				drop = k + 1;
		}
		if (drop > 0)
			g_array_remove_range (t->points, 0, drop);
	}

	rd.self = self;
	lrg_particle_system_foreach_active (system, record_particle, &rd);

	/* orphan trails of dead particles fade out; empty orphans go */
	for (i = 0, w = 0; i < self->trails->len; i++)
	{
		Trail *t = g_ptr_array_index (self->trails, i);

		if (!t->seen)
			t->live = FALSE;
		if (!t->live && t->points->len == 0)
		{
			g_hash_table_remove (self->by_id, GUINT_TO_POINTER (t->id));
			trail_free (t);
			continue;
		}
		g_ptr_array_index (self->trails, w++) = t;
	}
	self->trails->len = w;
}

void
lrg_particle_ribbon_renderer_clear (LrgParticleRibbonRenderer *self)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));

	g_hash_table_remove_all (self->by_id);
	for (i = 0; i < self->trails->len; i++)
		trail_free (g_ptr_array_index (self->trails, i));
	self->trails->len = 0;
}

guint
lrg_particle_ribbon_renderer_get_n_trails (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0);
	return self->trails->len;
}

guint
lrg_particle_ribbon_renderer_get_n_points (LrgParticleRibbonRenderer *self)
{
	guint i, n = 0;

	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0);

	for (i = 0; i < self->trails->len; i++)
		n += ((Trail *) g_ptr_array_index (self->trails, i))->points->len;
	return n;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static void
put_vertex (gfloat       *v,
            const gfloat *pos,
            const gfloat *side,
            gfloat        sign,
            gfloat        u,
            gfloat        vcoord,
            const gfloat *color)
{
	v[0] = pos[0] + side[0] * sign;
	v[1] = pos[1] + side[1] * sign;
	v[2] = pos[2] + side[2] * sign;
	v[3] = u;
	v[4] = vcoord;
	memcpy (v + 5, color, 4 * sizeof (gfloat));
}

/*
 * Turns self->strip (head first) into quads appended to @out.  Returns
 * the number of quads.
 */
static guint
emit_strip (LrgParticleRibbonRenderer *self,
            const gfloat              *eye,
            GArray                    *out)
{
	const StripPoint *sp = (const StripPoint *) (const void *) self->strip->data;
	guint             n = self->strip->len;
	g_autofree gfloat *cum = NULL;
	g_autofree gfloat *side = NULL;
	g_autofree gfloat *col = NULL;
	g_autofree gfloat *uu = NULL;
	gfloat            total;
	gfloat            prev_side[3] = { 0.0f, 0.0f, 0.0f };
	guint             i;

	if (n < 2)
		return 0;

	/* distance from the head to every point */
	cum = g_new (gfloat, n);
	cum[0] = 0.0f;
	for (i = 1; i < n; i++)
		cum[i] = cum[i - 1] + dist3 (sp[i].pos, sp[i - 1].pos);
	total = cum[n - 1];
	if (!(total > MERGE_EPSILON))
		return 0;

	side = g_new (gfloat, (gsize) n * 3);
	col = g_new (gfloat, (gsize) n * 4);
	uu = g_new (gfloat, n);
	for (i = 0; i < n; i++)
	{
		const gfloat *a = sp[i > 0 ? i - 1 : 0].pos;
		const gfloat *b = sp[i + 1 < n ? i + 1 : n - 1].pos;
		gfloat        t[3], view[3], s[3], len, half, f;
		gfloat        g[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		gint          k;

		f = cum[i] / total;   /* 0 head .. 1 tail */
		for (k = 0; k < 3; k++)
		{
			t[k] = b[k] - a[k];
			view[k] = eye[k] - sp[i].pos[k];
		}
		s[0] = t[1] * view[2] - t[2] * view[1];
		s[1] = t[2] * view[0] - t[0] * view[2];
		s[2] = t[0] * view[1] - t[1] * view[0];
		len = sqrtf (s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
		half = sp[i].width * 0.5f;
		if (self->width_curve != NULL && lrg_keyframe_curve_get_key_count (self->width_curve) > 0)
			half *= lrg_keyframe_curve_sample (self->width_curve, f);
		if (len > 1e-8f)
		{
			for (k = 0; k < 3; k++)
				prev_side[k] = s[k] / len;
		}
		/* degenerate (looking along the strip): keep the last side */
		for (k = 0; k < 3; k++)
			side[i * 3 + (guint) k] = prev_side[k] * half;

		if (self->gradient != NULL)
			lrg_color_gradient_evaluate_fast (self->gradient, f, g);
		for (k = 0; k < 4; k++)
			col[i * 4 + (guint) k] = sp[i].color[k] * g[k];

		switch (self->uv_mode)
		{
		case LRG_PARTICLE_RIBBON_UV_TILE:
			uu[i] = cum[i] / self->uv_tile_length;
			break;
		case LRG_PARTICLE_RIBBON_UV_PER_SEGMENT:
			uu[i] = 0.0f;   /* set per quad below */
			break;
		case LRG_PARTICLE_RIBBON_UV_STRETCH:
		default:
			uu[i] = f;
			break;
		}
	}

	for (i = 0; i + 1 < n; i++)
	{
		gfloat *q;
		gfloat  u0 = uu[i], u1 = uu[i + 1];

		if (self->uv_mode == LRG_PARTICLE_RIBBON_UV_PER_SEGMENT)
		{
			u0 = 0.0f;
			u1 = 1.0f;
		}
		g_array_set_size (out, out->len + LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD);
		q = &g_array_index (out, gfloat, out->len - LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD);
		put_vertex (q, sp[i].pos, &side[i * 3], 1.0f, u0, 0.0f, &col[i * 4]);
		put_vertex (q + 9, sp[i].pos, &side[i * 3], -1.0f, u0, 1.0f, &col[i * 4]);
		put_vertex (q + 18, sp[i + 1].pos, &side[(i + 1) * 3], -1.0f, u1, 1.0f, &col[(i + 1) * 4]);
		put_vertex (q + 27, sp[i + 1].pos, &side[(i + 1) * 3], 1.0f, u1, 0.0f, &col[(i + 1) * 4]);
	}
	return n - 1;
}

/* Appends a strip point unless it sits on the previous one */
static void
strip_add (GArray       *strip,
           const gfloat *pos,
           gfloat        width,
           const gfloat *color)
{
	StripPoint sp;

	if (strip->len > 0 &&
	    dist3 (g_array_index (strip, StripPoint, strip->len - 1).pos, pos) < MERGE_EPSILON)
		return;
	memcpy (sp.pos, pos, sizeof sp.pos);
	sp.width = width;
	memcpy (sp.color, color, sizeof sp.color);
	g_array_append_val (strip, sp);
}

typedef struct
{
	const LrgParticle *p;
} Ordered;

static void
collect_particle (const LrgParticle *p,
                  gpointer           user_data)
{
	Ordered o;

	o.p = p;
	g_array_append_val ((GArray *) user_data, o);
}

/* newest (largest id) first */
static gint
by_id_desc (gconstpointer a,
            gconstpointer b)
{
	guint32 ia = ((const Ordered *) a)->p->id;
	guint32 ib = ((const Ordered *) b)->p->id;

	return ia < ib ? 1 : ia > ib ? -1 : 0;
}

guint
lrg_particle_ribbon_renderer_build (LrgParticleRibbonRenderer *self,
                                    LrgParticleSystem         *system,
                                    const gfloat              *eye,
                                    GArray                    *out_vertices)
{
	guint quads = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0);
	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (system), 0);
	g_return_val_if_fail (eye != NULL && out_vertices != NULL, 0);
	g_return_val_if_fail (g_array_get_element_size (out_vertices) == sizeof (gfloat), 0);

	if (self->mode == LRG_PARTICLE_RIBBON_CONNECT)
	{
		g_autoptr(GArray) order = g_array_new (FALSE, FALSE, sizeof (Ordered));

		lrg_particle_system_foreach_active (system, collect_particle, order);
		g_array_sort (order, by_id_desc);
		g_array_set_size (self->strip, 0);
		for (i = 0; i < order->len; i++)
		{
			const LrgParticle *p = g_array_index (order, Ordered, i).p;
			gfloat             pos[3] = { p->position_x, p->position_y, p->position_z };
			gfloat             c[4] = { p->color_r, p->color_g, p->color_b, p->color_a };

			strip_add (self->strip, pos, self->width * p->size, c);
		}
		return emit_strip (self, eye, out_vertices);
	}

	/* trails: head (live particle) first, then the points newest first */
	for (i = 0; i < self->trails->len; i++)
	{
		Trail *t = g_ptr_array_index (self->trails, i);
		guint  k;

		g_array_set_size (self->strip, 0);
		if (t->live)
			strip_add (self->strip, t->head, t->head_width, t->head_color);
		for (k = t->points->len; k > 0; k--)
		{
			const TrailPoint *pt = &g_array_index (t->points, TrailPoint, k - 1);

			strip_add (self->strip, pt->pos, pt->width, pt->color);
		}
		quads += emit_strip (self, eye, out_vertices);
	}
	return quads;
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

static void
ensure_edge (LrgParticleRibbonRenderer *self)
{
	Image image;
	gint  x, y;

	if (self->edge_loaded)
		return;
	/* opaque along the centre line (v = 0.5), fading to both sides */
	image = GenImageColor (EDGE_W, EDGE_H, (Color){ 255, 255, 255, 255 });
	for (y = 0; y < EDGE_H; y++)
	{
		gfloat v = ((gfloat) y + 0.5f) / (gfloat) EDGE_H;
		gfloat d = fabsf (v * 2.0f - 1.0f);
		guint8 a = (guint8) lrintf (CLAMP (1.0f - d * d, 0.0f, 1.0f) * 255.0f);

		for (x = 0; x < EDGE_W; x++)
			ImageDrawPixel (&image, x, y, (Color){ 255, 255, 255, a });
	}
	self->edge = LoadTextureFromImage (image);
	UnloadImage (image);
	self->edge_loaded = self->edge.id != 0;
	if (self->edge_loaded)
		SetTextureFilter (self->edge, TEXTURE_FILTER_BILINEAR);
}

static gint
blend_of (LrgParticleBlendMode mode)
{
	switch (mode)
	{
	case LRG_PARTICLE_BLEND_ADDITIVE: return BLEND_ADDITIVE;
	case LRG_PARTICLE_BLEND_MULTIPLY: return BLEND_MULTIPLIED;
	case LRG_PARTICLE_BLEND_ALPHA:
	default:                          return BLEND_ALPHA;
	}
}

static guint8
byte_of (gfloat v)
{
	return (guint8) (CLAMP (v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void
lrg_particle_ribbon_renderer_draw (LrgParticleRibbonRenderer *self,
                                   LrgParticleSystem         *system,
                                   const gfloat              *eye)
{
	guint quads, i;
	guint texture_id;

	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (system));
	g_return_if_fail (eye != NULL);

	if (!lrg_particle_system_is_layer_visible (system) || !IsWindowReady ())
		return;
	g_array_set_size (self->scratch, 0);
	quads = lrg_particle_ribbon_renderer_build (self, system, eye, self->scratch);
	if (quads == 0)
		return;

	if (self->texture != NULL)
		texture_id = ((Texture2D *) grl_texture_get_handle (self->texture))->id;
	else
	{
		ensure_edge (self);
		texture_id = self->edge_loaded ? self->edge.id : rlGetTextureIdDefault ();
	}

	/* blended strips: depth tested, no depth writes, both faces */
	rlDrawRenderBatchActive ();
	rlDisableDepthMask ();
	rlDisableBackfaceCulling ();
	BeginBlendMode (blend_of (lrg_particle_system_get_blend_mode (system)));
	rlSetTexture (texture_id);
	rlBegin (RL_QUADS);
	for (i = 0; i < quads; i++)
	{
		const gfloat *q = &g_array_index (self->scratch, gfloat, (gsize) i * LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD);
		guint         k;

		for (k = 0; k < 4; k++)
		{
			const gfloat *v = q + k * LRG_PARTICLE_RIBBON_FLOATS_PER_VERTEX;

			rlColor4ub (byte_of (v[5]), byte_of (v[6]), byte_of (v[7]), byte_of (v[8]));
			rlTexCoord2f (v[3], v[4]);
			rlVertex3f (v[0], v[1], v[2]);
		}
	}
	rlEnd ();
	rlSetTexture (0);
	EndBlendMode ();
	rlDrawRenderBatchActive ();
	rlEnableBackfaceCulling ();
	rlEnableDepthMask ();
}

void
lrg_particle_ribbon_renderer_release_gpu (LrgParticleRibbonRenderer *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));

	if (self->edge_loaded && IsWindowReady ())
		UnloadTexture (self->edge);
	self->edge_loaded = FALSE;
	memset (&self->edge, 0, sizeof self->edge);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_particle_ribbon_renderer_finalize (GObject *object)
{
	LrgParticleRibbonRenderer *self = LRG_PARTICLE_RIBBON_RENDERER (object);

	lrg_particle_ribbon_renderer_release_gpu (self);
	lrg_particle_ribbon_renderer_clear (self);
	g_clear_pointer (&self->trails, g_ptr_array_unref);
	g_clear_pointer (&self->by_id, g_hash_table_unref);
	g_clear_pointer (&self->strip, g_array_unref);
	g_clear_pointer (&self->scratch, g_array_unref);
	g_clear_object (&self->width_curve);
	g_clear_object (&self->gradient);
	g_clear_object (&self->texture);
	G_OBJECT_CLASS (lrg_particle_ribbon_renderer_parent_class)->finalize (object);
}

static void
lrg_particle_ribbon_renderer_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
	LrgParticleRibbonRenderer *self = LRG_PARTICLE_RIBBON_RENDERER (object);

	switch (prop_id)
	{
	case PROP_MODE:           g_value_set_enum (value, self->mode); break;
	case PROP_UV_MODE:        g_value_set_enum (value, self->uv_mode); break;
	case PROP_UV_TILE_LENGTH: g_value_set_float (value, self->uv_tile_length); break;
	case PROP_WIDTH:          g_value_set_float (value, self->width); break;
	case PROP_WIDTH_CURVE:    g_value_set_object (value, self->width_curve); break;
	case PROP_COLOR_GRADIENT: g_value_set_object (value, self->gradient); break;
	case PROP_MAX_POINTS:     g_value_set_uint (value, self->max_points); break;
	case PROP_MIN_DISTANCE:   g_value_set_float (value, self->min_distance); break;
	case PROP_TRAIL_LIFETIME: g_value_set_float (value, self->trail_lifetime); break;
	case PROP_TEXTURE:        g_value_set_object (value, self->texture); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_ribbon_renderer_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
	LrgParticleRibbonRenderer *self = LRG_PARTICLE_RIBBON_RENDERER (object);

	switch (prop_id)
	{
	case PROP_MODE:
		lrg_particle_ribbon_renderer_set_mode (self, g_value_get_enum (value));
		break;
	case PROP_UV_MODE:
		lrg_particle_ribbon_renderer_set_uv_mode (self, g_value_get_enum (value));
		break;
	case PROP_UV_TILE_LENGTH:
		lrg_particle_ribbon_renderer_set_uv_tile_length (self, g_value_get_float (value));
		break;
	case PROP_WIDTH:
		lrg_particle_ribbon_renderer_set_width (self, g_value_get_float (value));
		break;
	case PROP_WIDTH_CURVE:
		lrg_particle_ribbon_renderer_set_width_curve (self, g_value_get_object (value));
		break;
	case PROP_COLOR_GRADIENT:
		lrg_particle_ribbon_renderer_set_color_gradient (self, g_value_get_object (value));
		break;
	case PROP_MAX_POINTS:
		lrg_particle_ribbon_renderer_set_max_points (self, g_value_get_uint (value));
		break;
	case PROP_MIN_DISTANCE:
		lrg_particle_ribbon_renderer_set_min_distance (self, g_value_get_float (value));
		break;
	case PROP_TRAIL_LIFETIME:
		lrg_particle_ribbon_renderer_set_trail_lifetime (self, g_value_get_float (value));
		break;
	case PROP_TEXTURE:
		lrg_particle_ribbon_renderer_set_texture (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_ribbon_renderer_class_init (LrgParticleRibbonRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_particle_ribbon_renderer_finalize;
	object_class->get_property = lrg_particle_ribbon_renderer_get_property;
	object_class->set_property = lrg_particle_ribbon_renderer_set_property;

	/**
	 * LrgParticleRibbonRenderer:mode:
	 *
	 * Trails per particle, or one ribbon through all particles.
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "Trails or a connecting ribbon", LRG_TYPE_PARTICLE_RIBBON_MODE,
		                   LRG_PARTICLE_RIBBON_TRAIL, rw);

	/**
	 * LrgParticleRibbonRenderer:uv-mode:
	 *
	 * How the texture's u coordinate runs along a strip.
	 */
	properties[PROP_UV_MODE] =
		g_param_spec_enum ("uv-mode", "UV Mode", "How u runs along a strip", LRG_TYPE_PARTICLE_RIBBON_UV_MODE,
		                   LRG_PARTICLE_RIBBON_UV_STRETCH, rw);

	/**
	 * LrgParticleRibbonRenderer:uv-tile-length:
	 *
	 * World units per texture repeat with %LRG_PARTICLE_RIBBON_UV_TILE.
	 */
	properties[PROP_UV_TILE_LENGTH] =
		g_param_spec_float ("uv-tile-length", "UV Tile Length", "World units per texture repeat",
		                    0.0001f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgParticleRibbonRenderer:width:
	 *
	 * Strip width at particle size 1.
	 */
	properties[PROP_WIDTH] =
		g_param_spec_float ("width", "Width", "Strip width at particle size 1", 0.0f, G_MAXFLOAT, 0.2f, rw);

	/**
	 * LrgParticleRibbonRenderer:width-curve:
	 *
	 * Width multiplier along the strip (head 0, tail 1), or %NULL.
	 */
	properties[PROP_WIDTH_CURVE] =
		g_param_spec_object ("width-curve", "Width Curve", "Width multiplier along the strip",
		                     LRG_TYPE_KEYFRAME_CURVE, rw);

	/**
	 * LrgParticleRibbonRenderer:color-gradient:
	 *
	 * Colour multiplier along the strip (head 0, tail 1), or %NULL.
	 */
	properties[PROP_COLOR_GRADIENT] =
		g_param_spec_object ("color-gradient", "Color Gradient", "Colour multiplier along the strip",
		                     LRG_TYPE_COLOR_GRADIENT, rw);

	/**
	 * LrgParticleRibbonRenderer:max-points:
	 *
	 * The most points a trail keeps.
	 */
	properties[PROP_MAX_POINTS] =
		g_param_spec_uint ("max-points", "Max Points", "Most points per trail", 2, 1024, 32, rw);

	/**
	 * LrgParticleRibbonRenderer:min-distance:
	 *
	 * Travel before a trail records a new point.
	 */
	properties[PROP_MIN_DISTANCE] =
		g_param_spec_float ("min-distance", "Min Distance", "Spacing of trail points", 0.0f, G_MAXFLOAT, 0.05f, rw);

	/**
	 * LrgParticleRibbonRenderer:trail-lifetime:
	 *
	 * Seconds a trail point lives.
	 */
	properties[PROP_TRAIL_LIFETIME] =
		g_param_spec_float ("trail-lifetime", "Trail Lifetime", "Seconds a trail point lives",
		                    0.001f, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgParticleRibbonRenderer:texture:
	 *
	 * The strip texture, or %NULL for the generated soft edge.
	 */
	properties[PROP_TEXTURE] =
		g_param_spec_object ("texture", "Texture", "Strip texture", GRL_TYPE_TEXTURE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_particle_ribbon_renderer_init (LrgParticleRibbonRenderer *self)
{
	self->mode = LRG_PARTICLE_RIBBON_TRAIL;
	self->uv_mode = LRG_PARTICLE_RIBBON_UV_STRETCH;
	self->uv_tile_length = 1.0f;
	self->width = 0.2f;
	self->max_points = 32;
	self->min_distance = 0.05f;
	self->trail_lifetime = 0.5f;
	self->trails = g_ptr_array_new ();
	self->by_id = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->strip = g_array_new (FALSE, FALSE, sizeof (StripPoint));
	self->scratch = g_array_new (FALSE, FALSE, sizeof (gfloat));
}

LrgParticleRibbonRenderer *
lrg_particle_ribbon_renderer_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_RIBBON_RENDERER, NULL);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

LrgParticleRibbonMode
lrg_particle_ribbon_renderer_get_mode (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), LRG_PARTICLE_RIBBON_TRAIL);
	return self->mode;
}

void
lrg_particle_ribbon_renderer_set_mode (LrgParticleRibbonRenderer *self,
                                       LrgParticleRibbonMode      mode)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	if (self->mode == mode)
		return;
	self->mode = mode;
	lrg_particle_ribbon_renderer_clear (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

LrgParticleRibbonUvMode
lrg_particle_ribbon_renderer_get_uv_mode (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), LRG_PARTICLE_RIBBON_UV_STRETCH);
	return self->uv_mode;
}

void
lrg_particle_ribbon_renderer_set_uv_mode (LrgParticleRibbonRenderer *self,
                                          LrgParticleRibbonUvMode    mode)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	if (self->uv_mode == mode)
		return;
	self->uv_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UV_MODE]);
}

gfloat
lrg_particle_ribbon_renderer_get_uv_tile_length (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 1.0f);
	return self->uv_tile_length;
}

void
lrg_particle_ribbon_renderer_set_uv_tile_length (LrgParticleRibbonRenderer *self,
                                                 gfloat                     length)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	length = MAX (length, 0.0001f);
	if (self->uv_tile_length == length)
		return;
	self->uv_tile_length = length;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UV_TILE_LENGTH]);
}

gfloat
lrg_particle_ribbon_renderer_get_width (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0.0f);
	return self->width;
}

void
lrg_particle_ribbon_renderer_set_width (LrgParticleRibbonRenderer *self,
                                        gfloat                     width)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	width = MAX (width, 0.0f);
	if (self->width == width)
		return;
	self->width = width;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
}

LrgKeyframeCurve *
lrg_particle_ribbon_renderer_get_width_curve (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), NULL);
	return self->width_curve;
}

void
lrg_particle_ribbon_renderer_set_width_curve (LrgParticleRibbonRenderer *self,
                                              LrgKeyframeCurve          *curve)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	if (g_set_object (&self->width_curve, curve))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH_CURVE]);
}

LrgColorGradient *
lrg_particle_ribbon_renderer_get_color_gradient (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), NULL);
	return self->gradient;
}

void
lrg_particle_ribbon_renderer_set_color_gradient (LrgParticleRibbonRenderer *self,
                                                 LrgColorGradient          *gradient)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	if (g_set_object (&self->gradient, gradient))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_GRADIENT]);
}

guint
lrg_particle_ribbon_renderer_get_max_points (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0);
	return self->max_points;
}

void
lrg_particle_ribbon_renderer_set_max_points (LrgParticleRibbonRenderer *self,
                                             guint                      max_points)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	max_points = CLAMP (max_points, 2u, 1024u);
	if (self->max_points == max_points)
		return;
	self->max_points = max_points;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_POINTS]);
}

gfloat
lrg_particle_ribbon_renderer_get_min_distance (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0.0f);
	return self->min_distance;
}

void
lrg_particle_ribbon_renderer_set_min_distance (LrgParticleRibbonRenderer *self,
                                               gfloat                     distance)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	distance = MAX (distance, 0.0f);
	if (self->min_distance == distance)
		return;
	self->min_distance = distance;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_DISTANCE]);
}

gfloat
lrg_particle_ribbon_renderer_get_trail_lifetime (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), 0.0f);
	return self->trail_lifetime;
}

void
lrg_particle_ribbon_renderer_set_trail_lifetime (LrgParticleRibbonRenderer *self,
                                                 gfloat                     lifetime)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	lifetime = MAX (lifetime, 0.001f);
	if (self->trail_lifetime == lifetime)
		return;
	self->trail_lifetime = lifetime;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRAIL_LIFETIME]);
}

GrlTexture *
lrg_particle_ribbon_renderer_get_texture (LrgParticleRibbonRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self), NULL);
	return self->texture;
}

void
lrg_particle_ribbon_renderer_set_texture (LrgParticleRibbonRenderer *self,
                                          GrlTexture                *texture)
{
	g_return_if_fail (LRG_IS_PARTICLE_RIBBON_RENDERER (self));
	if (g_set_object (&self->texture, texture))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
}
