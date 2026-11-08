/* lrg-mesh-simplify.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshSimplify: quadric error metric edge-collapse simplification.
 *
 * Written from the Garland-Heckbert paper ("Surface Simplification Using
 * Quadric Error Metrics", 1997); the vertex classification (manifold /
 * border / seam / locked) follows the same idea as meshoptimizer's
 * simplifier, but no code was taken from it.
 *
 * Overview (simplify_core()):
 *
 *  1. Positions are normalised into the unit cube of the mesh bounds, so
 *     errors are relative to the mesh extent.
 *  2. Vertices with bit-identical positions are grouped: remap[v] is the
 *     representative ("rep") of the position and wedge[] links every
 *     vertex of a position into a ring. Several vertices at one position
 *     are the wedges of a UV/normal seam (or a hard edge).
 *  3. One quadric per rep: the area-weighted planes of the triangles
 *     around it, plus a perpendicular "boundary plane" per open edge so
 *     borders and seams keep their shape when they slide.
 *  4. Passes until the target is met or nothing collapses:
 *     a. vertex -> triangle adjacency for the current index buffer;
 *     b. every vertex is classified (MANIFOLD, BORDER, SEAM, LOCKED);
 *     c. every edge yields its cheapest legal collapse (from -> to,
 *        error = combined quadric at the destination);
 *     d. collapses run cheapest first while under the error limit, each
 *        checked for the link condition (keeps the surface manifold) and
 *        triangle flips; a collapse locks the neighbourhood it touched
 *        for the rest of the pass, so every check reads fresh adjacency;
 *     e. the index buffer is rewritten and degenerate triangles dropped.
 *
 * Vertices are never moved, only merged onto neighbours, so the output
 * indexes a subset of the input and every attribute stays valid. A seam
 * vertex collapses together with its sibling wedge, each onto the wedge
 * of the destination it is connected to, so seams never open.
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>

#include "lrg-mesh-simplify.h"

/* Weight of the boundary planes relative to triangle planes. */
#define BOUNDARY_WEIGHT (2.0)

/* A collapse may not rotate a surrounding triangle's normal by more than
 * about 75 degrees (cos 75 = 0.26). */
#define FLIP_COS_LIMIT (0.25)

/* ==========================================================================
 * Enums and errors
 * ========================================================================== */

G_DEFINE_QUARK (lrg-mesh-simplify-error-quark, lrg_mesh_simplify_error)

GType
lrg_mesh_simplify_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT, "LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT", "invalid-input" },
			{ LRG_MESH_SIMPLIFY_ERROR_NO_GPU, "LRG_MESH_SIMPLIFY_ERROR_NO_GPU", "no-gpu" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgMeshSimplifyError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_mesh_vertex_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_MESH_VERTEX_KIND_MANIFOLD, "LRG_MESH_VERTEX_KIND_MANIFOLD", "manifold" },
			{ LRG_MESH_VERTEX_KIND_BORDER, "LRG_MESH_VERTEX_KIND_BORDER", "border" },
			{ LRG_MESH_VERTEX_KIND_SEAM, "LRG_MESH_VERTEX_KIND_SEAM", "seam" },
			{ LRG_MESH_VERTEX_KIND_LOCKED, "LRG_MESH_VERTEX_KIND_LOCKED", "locked" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgMeshVertexKind"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Object
 * ========================================================================== */

struct _LrgMeshSimplify
{
	GObject  parent_instance;

	gfloat   target_ratio;
	gfloat   target_error;
	gboolean lock_border;
	gboolean lock_seams;

	gfloat   result_error;
	gfloat   result_ratio;
};

G_DEFINE_FINAL_TYPE (LrgMeshSimplify, lrg_mesh_simplify, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_TARGET_RATIO,
	PROP_TARGET_ERROR,
	PROP_LOCK_BORDER,
	PROP_LOCK_SEAMS,
	PROP_RESULT_ERROR,
	PROP_RESULT_RATIO,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Quadrics
 * ========================================================================== */

/* Symmetric 3x3 A, vector b and constant c of the quadric
 * Q(p) = p^T A p + 2 b.p + c, plus the summed weight w so that Q(p) / w
 * is a weighted mean squared distance. */
typedef struct
{
	gdouble a00, a11, a22, a01, a02, a12;
	gdouble b0, b1, b2;
	gdouble c;
	gdouble w;
} Quadric;

/* Adds the plane n.p + d = 0 (n unit length) with weight @w. */
static void
quadric_add_plane (Quadric       *q,
                   const gdouble *n,
                   gdouble        d,
                   gdouble        w)
{
	q->a00 += w * n[0] * n[0];
	q->a11 += w * n[1] * n[1];
	q->a22 += w * n[2] * n[2];
	q->a01 += w * n[0] * n[1];
	q->a02 += w * n[0] * n[2];
	q->a12 += w * n[1] * n[2];
	q->b0 += w * d * n[0];
	q->b1 += w * d * n[1];
	q->b2 += w * d * n[2];
	q->c += w * d * d;
	q->w += w;
}

static void
quadric_add (Quadric       *dst,
             const Quadric *src)
{
	dst->a00 += src->a00;
	dst->a11 += src->a11;
	dst->a22 += src->a22;
	dst->a01 += src->a01;
	dst->a02 += src->a02;
	dst->a12 += src->a12;
	dst->b0 += src->b0;
	dst->b1 += src->b1;
	dst->b2 += src->b2;
	dst->c += src->c;
	dst->w += src->w;
}

/* Mean squared distance of @p to the planes in @q (never negative). */
static gdouble
quadric_error (const Quadric *q,
               const gdouble *p)
{
	gdouble x = p[0];
	gdouble y = p[1];
	gdouble z = p[2];
	gdouble e;

	e = q->a00 * x * x + q->a11 * y * y + q->a22 * z * z +
	    2.0 * (q->a01 * x * y + q->a02 * x * z + q->a12 * y * z) +
	    2.0 * (q->b0 * x + q->b1 * y + q->b2 * z) + q->c;
	if (e < 0.0)
		e = 0.0;
	return (q->w > 1e-30) ? e / q->w : e;
}

/* ==========================================================================
 * Small vector helpers (doubles)
 * ========================================================================== */

static void
vsub (const gdouble *a,
      const gdouble *b,
      gdouble       *out)
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

static void
vcross (const gdouble *a,
        const gdouble *b,
        gdouble       *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static gdouble
vdot (const gdouble *a,
      const gdouble *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static gdouble
vlen (const gdouble *a)
{
	return sqrt (vdot (a, a));
}

/* Unnormalised normal of the triangle (a, b, c). */
static void
tri_normal (const gdouble *a,
            const gdouble *b,
            const gdouble *c,
            gdouble       *out)
{
	gdouble e1[3];
	gdouble e2[3];

	vsub (b, a, e1);
	vsub (c, a, e2);
	vcross (e1, e2, out);
}

/* ==========================================================================
 * Simplifier state
 * ========================================================================== */

typedef struct
{
	guint32 from;
	guint32 to;
	gdouble error;
} Collapse;

typedef struct
{
	/* Input */
	guint32   n_vertices;
	gboolean  lock_border;
	gboolean  lock_seams;

	/* Per vertex */
	gdouble  *pos;            /* normalised xyz */
	guint32  *remap;          /* position representative */
	guint32  *wedge;          /* next vertex at the same position (ring) */
	guint8   *kind;           /* LrgMeshVertexKind */
	guint32  *open_out;       /* count of open outgoing half-edges */
	guint32  *open_in;        /* count of open incoming half-edges */
	guint32  *out_target;     /* last open outgoing half-edge's end */
	guint32  *in_source;      /* last open incoming half-edge's start */
	guint32  *collapse_remap; /* per pass: where a vertex went */

	/* Per representative (indexed by the rep's vertex id) */
	Quadric  *quadrics;
	guint8   *touched;

	/* Vertex -> triangle adjacency (CSR over the current index buffer) */
	guint32  *tri_offset;     /* n_vertices + 1 */
	guint32  *tri_list;

	/* Scratch stamps for the link condition */
	guint32  *stamp_a;
	guint32  *stamp_b;
	guint32   stamp;
} Ctx;

static void
ctx_free (Ctx *ctx)
{
	g_free (ctx->pos);
	g_free (ctx->remap);
	g_free (ctx->wedge);
	g_free (ctx->kind);
	g_free (ctx->open_out);
	g_free (ctx->open_in);
	g_free (ctx->out_target);
	g_free (ctx->in_source);
	g_free (ctx->collapse_remap);
	g_free (ctx->quadrics);
	g_free (ctx->touched);
	g_free (ctx->tri_offset);
	g_free (ctx->tri_list);
	g_free (ctx->stamp_a);
	g_free (ctx->stamp_b);
}

/* Bit pattern of a float with -0 folded into +0, for exact hashing. */
static guint32
float_bits (gfloat f)
{
	union { gfloat f; guint32 u; } v;

	v.f = (f == 0.0f) ? 0.0f : f;
	return v.u;
}

/*
 * build_position_remap:
 *
 * Groups vertices whose positions are bit-identical: remap[v] is the
 * first vertex seen at that position and wedge[] chains all vertices of
 * a position into a ring. Open addressing over a power-of-two table.
 */
static void
build_position_remap (Ctx          *ctx,
                      const gfloat *positions,
                      guint         stride)
{
	guint32  n = ctx->n_vertices;
	guint32  size = 16;
	guint32 *table;
	guint32  v;

	while (size < n * 2)
		size <<= 1;
	table = g_new (guint32, size);
	memset (table, 0xff, sizeof (guint32) * size);

	for (v = 0; v < n; v++)
	{
		const gfloat *p = positions + (gsize) v * stride;
		guint32       h;
		guint32       slot;

		/* Hash the three coordinates' bits (Murmur-style mixing). */
		h = float_bits (p[0]) * 73856093u;
		h ^= float_bits (p[1]) * 19349663u;
		h ^= float_bits (p[2]) * 83492791u;
		h ^= h >> 15;
		h *= 0x2c1b3c6du;
		h ^= h >> 12;
		slot = h & (size - 1);

		for (;;)
		{
			guint32 other = table[slot];

			if (other == G_MAXUINT32)
			{
				table[slot] = v;
				ctx->remap[v] = v;
				ctx->wedge[v] = v;
				break;
			}
			{
				const gfloat *q = positions + (gsize) other * stride;

				if (float_bits (p[0]) == float_bits (q[0]) && float_bits (p[1]) == float_bits (q[1]) &&
				    float_bits (p[2]) == float_bits (q[2]))
				{
					/* Splice v into the ring after the representative. */
					ctx->remap[v] = other;
					ctx->wedge[v] = ctx->wedge[other];
					ctx->wedge[other] = v;
					break;
				}
			}
			slot = (slot + 1) & (size - 1);
		}
	}
	g_free (table);
}

/*
 * build_adjacency:
 *
 * Vertex -> incident triangles for the current index buffer.
 */
static void
build_adjacency (Ctx           *ctx,
                 const guint32 *idx,
                 gsize          n_idx)
{
	guint32 n = ctx->n_vertices;
	gsize   i;
	guint32 v;
	guint32 *cursor;

	memset (ctx->tri_offset, 0, sizeof (guint32) * (n + 1));
	for (i = 0; i < n_idx; i++)
		ctx->tri_offset[idx[i] + 1]++;
	for (v = 0; v < n; v++)
		ctx->tri_offset[v + 1] += ctx->tri_offset[v];

	cursor = g_new (guint32, n);
	memcpy (cursor, ctx->tri_offset, sizeof (guint32) * n);
	for (i = 0; i < n_idx; i++)
		ctx->tri_list[cursor[idx[i]]++] = (guint32) (i / 3);
	g_free (cursor);
}

/* TRUE if some triangle has the directed edge a -> b. */
static gboolean
has_half_edge (const Ctx     *ctx,
               const guint32 *idx,
               guint32        a,
               guint32        b)
{
	guint32 k;

	for (k = ctx->tri_offset[a]; k < ctx->tri_offset[a + 1]; k++)
	{
		const guint32 *t = idx + (gsize) ctx->tri_list[k] * 3;

		if ((t[0] == a && t[1] == b) || (t[1] == a && t[2] == b) || (t[2] == a && t[0] == b))
			return TRUE;
	}
	return FALSE;
}

/* The vertex after (+1) or before (+2) @v in triangle @t. */
static guint32
tri_next (const guint32 *t,
          guint32        v,
          guint          step)
{
	guint k;

	for (k = 0; k < 3; k++)
		if (t[k] == v)
			return t[(k + step) % 3];
	return v;
}

/*
 * classify:
 *
 * Counts every vertex's open half-edges (edges without a twin by vertex
 * index; at a seam the two sides use different vertices, so seam edges
 * are open too) and derives its kind:
 *
 *  - one vertex at the position: no open edges -> MANIFOLD; exactly one
 *    open edge in and one out -> BORDER; otherwise LOCKED;
 *  - two vertices at the position, each with one open edge in and out,
 *    and the open edges of one matching the other's in reverse (by
 *    position) -> SEAM; otherwise LOCKED (seam meets border, two shells
 *    touching at a point);
 *  - three or more -> LOCKED (seam corners such as a cube's).
 */
static void
classify (Ctx           *ctx,
          const guint32 *idx)
{
	guint32 n = ctx->n_vertices;
	guint32 v;

	for (v = 0; v < n; v++)
	{
		guint32 k;

		ctx->open_out[v] = 0;
		ctx->open_in[v] = 0;
		ctx->out_target[v] = v;
		ctx->in_source[v] = v;
		for (k = ctx->tri_offset[v]; k < ctx->tri_offset[v + 1]; k++)
		{
			const guint32 *t = idx + (gsize) ctx->tri_list[k] * 3;
			guint32        next = tri_next (t, v, 1);
			guint32        prev = tri_next (t, v, 2);

			if (!has_half_edge (ctx, idx, next, v))
			{
				ctx->open_out[v]++;
				ctx->out_target[v] = next;
			}
			if (!has_half_edge (ctx, idx, v, prev))
			{
				ctx->open_in[v]++;
				ctx->in_source[v] = prev;
			}
		}
	}

	for (v = 0; v < n; v++)
	{
		guint32 ring = 1;
		guint32 s = ctx->wedge[v];
		guint8  kind;

		while (s != v)
		{
			ring++;
			s = ctx->wedge[s];
		}

		if (ring == 1)
		{
			if (ctx->open_out[v] == 0 && ctx->open_in[v] == 0)
				kind = LRG_MESH_VERTEX_KIND_MANIFOLD;
			else if (ctx->open_out[v] == 1 && ctx->open_in[v] == 1)
				kind = ctx->lock_border ? LRG_MESH_VERTEX_KIND_LOCKED : LRG_MESH_VERTEX_KIND_BORDER;
			else
				kind = LRG_MESH_VERTEX_KIND_LOCKED;
		}
		else if (ring == 2)
		{
			s = ctx->wedge[v];
			if (ctx->open_out[v] == 1 && ctx->open_in[v] == 1 && ctx->open_out[s] == 1 &&
			    ctx->open_in[s] == 1 &&
			    ctx->remap[ctx->out_target[v]] == ctx->remap[ctx->in_source[s]] &&
			    ctx->remap[ctx->in_source[v]] == ctx->remap[ctx->out_target[s]])
				kind = ctx->lock_seams ? LRG_MESH_VERTEX_KIND_LOCKED : LRG_MESH_VERTEX_KIND_SEAM;
			else
				kind = LRG_MESH_VERTEX_KIND_LOCKED;
		}
		else
			kind = LRG_MESH_VERTEX_KIND_LOCKED;

		ctx->kind[v] = kind;
	}
}

/*
 * compute_quadrics:
 *
 * Area-weighted triangle planes, accumulated per position, plus for every
 * open half-edge a plane through the edge perpendicular to its triangle,
 * weighted by the squared edge length (keeps borders and seams in place
 * while they slide). Needs the adjacency of the input.
 */
static void
compute_quadrics (Ctx           *ctx,
                  const guint32 *idx,
                  gsize          n_idx)
{
	gsize t;

	for (t = 0; t < n_idx; t += 3)
	{
		const guint32 *tri = idx + t;
		gdouble        n[3];
		gdouble        len;
		gdouble        d;
		guint          k;

		tri_normal (ctx->pos + tri[0] * 3, ctx->pos + tri[1] * 3, ctx->pos + tri[2] * 3, n);
		len = vlen (n);
		if (len <= 1e-30)
			continue;
		n[0] /= len;
		n[1] /= len;
		n[2] /= len;
		d = -vdot (n, ctx->pos + tri[0] * 3);
		for (k = 0; k < 3; k++)
			quadric_add_plane (&ctx->quadrics[ctx->remap[tri[k]]], n, d, len * 0.5);

		/* Boundary planes of the open edges of this triangle */
		for (k = 0; k < 3; k++)
		{
			guint32 a = tri[k];
			guint32 b = tri[(k + 1) % 3];
			gdouble e[3];
			gdouble m[3];
			gdouble elen;
			gdouble mlen;
			gdouble md;

			if (has_half_edge (ctx, idx, b, a))
				continue;
			vsub (ctx->pos + b * 3, ctx->pos + a * 3, e);
			elen = vlen (e);
			vcross (e, n, m);
			mlen = vlen (m);
			if (mlen <= 1e-30)
				continue;
			m[0] /= mlen;
			m[1] /= mlen;
			m[2] /= mlen;
			md = -vdot (m, ctx->pos + a * 3);
			quadric_add_plane (&ctx->quadrics[ctx->remap[a]], m, md, elen * elen * BOUNDARY_WEIGHT);
			quadric_add_plane (&ctx->quadrics[ctx->remap[b]], m, md, elen * elen * BOUNDARY_WEIGHT);
		}
	}
}

/* May @from collapse onto @to over an edge that is open (@open) or not? */
static gboolean
can_collapse (const Ctx *ctx,
              guint32    from,
              guint32    to,
              gboolean   open)
{
	guint8 kf = ctx->kind[from];
	guint8 kt = ctx->kind[to];

	switch (kf)
	{
	case LRG_MESH_VERTEX_KIND_MANIFOLD:
		return TRUE;
	case LRG_MESH_VERTEX_KIND_BORDER:
		return open && (kt == LRG_MESH_VERTEX_KIND_BORDER || kt == LRG_MESH_VERTEX_KIND_LOCKED);
	case LRG_MESH_VERTEX_KIND_SEAM:
		return open && (kt == LRG_MESH_VERTEX_KIND_SEAM || kt == LRG_MESH_VERTEX_KIND_LOCKED);
	case LRG_MESH_VERTEX_KIND_LOCKED:
	default:
		return FALSE;
	}
}

static gint
collapse_compare (gconstpointer a,
                  gconstpointer b)
{
	const Collapse *ca = (const Collapse *) a;
	const Collapse *cb = (const Collapse *) b;

	if (ca->error < cb->error)
		return -1;
	if (ca->error > cb->error)
		return 1;
	/* Stable order for identical errors: deterministic output. */
	if (ca->from != cb->from)
		return (ca->from < cb->from) ? -1 : 1;
	if (ca->to != cb->to)
		return (ca->to < cb->to) ? -1 : 1;
	return 0;
}

/*
 * pick_collapses:
 *
 * One candidate per edge: the cheaper legal direction. Edges with a twin
 * are visited once (from the half-edge with the smaller start index).
 */
static GArray *
pick_collapses (Ctx           *ctx,
                const guint32 *idx,
                gsize          n_idx)
{
	GArray *out = g_array_sized_new (FALSE, FALSE, sizeof (Collapse), (guint) (n_idx / 2));
	gsize   i;

	for (i = 0; i < n_idx; i++)
	{
		guint32  i0 = idx[i];
		guint32  i1 = idx[(i % 3 == 2) ? i - 2 : i + 1];
		guint32  r0 = ctx->remap[i0];
		guint32  r1 = ctx->remap[i1];
		gboolean twin;
		gboolean ok01;
		gboolean ok10;
		Quadric  q;
		gdouble  e01 = G_MAXDOUBLE;
		gdouble  e10 = G_MAXDOUBLE;
		Collapse c;

		if (r0 == r1)
			continue;
		twin = has_half_edge (ctx, idx, i1, i0);
		if (twin && i0 > i1)
			continue;
		ok01 = can_collapse (ctx, i0, i1, !twin);
		ok10 = can_collapse (ctx, i1, i0, !twin);
		if (!ok01 && !ok10)
			continue;

		q = ctx->quadrics[r0];
		quadric_add (&q, &ctx->quadrics[r1]);
		if (ok01)
			e01 = quadric_error (&q, ctx->pos + i1 * 3);
		if (ok10)
			e10 = quadric_error (&q, ctx->pos + i0 * 3);

		if (e01 <= e10)
		{
			c.from = i0;
			c.to = i1;
			c.error = e01;
		}
		else
		{
			c.from = i1;
			c.to = i0;
			c.error = e10;
		}
		g_array_append_val (out, c);
	}
	g_array_sort (out, collapse_compare);
	return out;
}

/*
 * link_condition:
 *
 * Collapsing the edge between positions r0 and r1 keeps the surface
 * manifold only if the positions adjacent to both are exactly the
 * opposite corners of the triangles on the edge. Also returns how many
 * triangles disappear (@out_removed).
 */
static gboolean
link_condition (Ctx           *ctx,
                const guint32 *idx,
                guint32        r0,
                guint32        r1,
                guint32       *out_removed)
{
	guint32 common = 0;
	guint32 opposite = 0;
	guint32 removed = 0;
	guint32 s;
	guint32 sa;
	guint32 sb;

	/* stamp_a[x] == sa: x is adjacent to r0; stamp_b[x] == sb: counted.
	 * stamp_b[x] == sb + 1 marks a corner opposite the edge. */
	ctx->stamp += 3;
	sa = ctx->stamp;
	sb = ctx->stamp + 1;

	s = r0;
	do
	{
		guint32 k;

		for (k = ctx->tri_offset[s]; k < ctx->tri_offset[s + 1]; k++)
		{
			const guint32 *t = idx + (gsize) ctx->tri_list[k] * 3;
			guint32        a = ctx->remap[t[0]];
			guint32        b = ctx->remap[t[1]];
			guint32        c = ctx->remap[t[2]];

			ctx->stamp_a[a] = sa;
			ctx->stamp_a[b] = sa;
			ctx->stamp_a[c] = sa;
			if (a == r1 || b == r1 || c == r1)
			{
				guint32 third = (a != r0 && a != r1) ? a : ((b != r0 && b != r1) ? b : c);

				removed++;
				if (ctx->stamp_b[third] != sb + 1)
				{
					ctx->stamp_b[third] = sb + 1;
					opposite++;
				}
			}
		}
		s = ctx->wedge[s];
	}
	while (s != r0);

	/* Positions adjacent to r1 that are also adjacent to r0. */
	s = r1;
	do
	{
		guint32 k;

		for (k = ctx->tri_offset[s]; k < ctx->tri_offset[s + 1]; k++)
		{
			const guint32 *t = idx + (gsize) ctx->tri_list[k] * 3;
			guint          j;

			for (j = 0; j < 3; j++)
			{
				guint32 x = ctx->remap[t[j]];

				if (x == r0 || x == r1 || ctx->stamp_a[x] != sa)
					continue;
				if (ctx->stamp_b[x] == sb || ctx->stamp_b[x] == sb + 2)
					continue;
				/* sb + 2 = counted and opposite; sb = counted only */
				ctx->stamp_b[x] = (ctx->stamp_b[x] == sb + 1) ? sb + 2 : sb;
				common++;
			}
		}
		s = ctx->wedge[s];
	}
	while (s != r1);

	*out_removed = removed;
	return common == opposite && removed > 0;
}

/* TRUE if moving position r0 to @dest flips or folds a triangle that
 * survives the collapse onto r1. */
static gboolean
collapse_flips (const Ctx     *ctx,
                const guint32 *idx,
                guint32        r0,
                guint32        r1,
                const gdouble *dest)
{
	guint32 s = r0;

	do
	{
		guint32 k;

		for (k = ctx->tri_offset[s]; k < ctx->tri_offset[s + 1]; k++)
		{
			const guint32 *t = idx + (gsize) ctx->tri_list[k] * 3;
			const gdouble *p[3];
			const gdouble *q[3];
			gdouble        n0[3];
			gdouble        n1[3];
			guint          j;
			gboolean       has_r1 = FALSE;

			for (j = 0; j < 3; j++)
			{
				guint32 rj = ctx->remap[t[j]];

				if (rj == r1)
					has_r1 = TRUE;
				p[j] = ctx->pos + t[j] * 3;
				q[j] = (rj == r0) ? dest : p[j];
			}
			if (has_r1)
				continue;

			tri_normal (p[0], p[1], p[2], n0);
			tri_normal (q[0], q[1], q[2], n1);
			if (vdot (n0, n1) < FLIP_COS_LIMIT * vlen (n0) * vlen (n1))
				return TRUE;
		}
		s = ctx->wedge[s];
	}
	while (s != r0);
	return FALSE;
}

/* The vertex at position @r1 that @s shares an edge with, or G_MAXUINT32. */
static guint32
connected_wedge (const Ctx     *ctx,
                 const guint32 *idx,
                 guint32        s,
                 guint32        r1)
{
	guint32 t = r1;

	do
	{
		if (has_half_edge (ctx, idx, s, t) || has_half_edge (ctx, idx, t, s))
			return t;
		t = ctx->wedge[t];
	}
	while (t != r1);
	return G_MAXUINT32;
}

/* Marks the positions of every triangle around position @r. */
static void
touch_neighbourhood (Ctx           *ctx,
                     const guint32 *idx,
                     guint32        r)
{
	guint32 s = r;

	do
	{
		guint32 k;

		for (k = ctx->tri_offset[s]; k < ctx->tri_offset[s + 1]; k++)
		{
			const guint32 *t = idx + (gsize) ctx->tri_list[k] * 3;

			ctx->touched[ctx->remap[t[0]]] = 1;
			ctx->touched[ctx->remap[t[1]]] = 1;
			ctx->touched[ctx->remap[t[2]]] = 1;
		}
		s = ctx->wedge[s];
	}
	while (s != r);
}

/*
 * perform_collapses:
 *
 * Runs the sorted candidates while they stay under @limit (squared,
 * normalised) and fewer than @want triangles have gone. Returns the
 * number of collapses and raises *max_error.
 */
static guint
perform_collapses (Ctx           *ctx,
                   const guint32 *idx,
                   GArray        *candidates,
                   gdouble        limit,
                   gsize          want,
                   gdouble       *max_error)
{
	gsize  removed_total = 0;
	guint  done = 0;
	guint  i;

	memset (ctx->touched, 0, ctx->n_vertices);
	for (i = 0; i < ctx->n_vertices; i++)
		ctx->collapse_remap[i] = i;

	for (i = 0; i < candidates->len && removed_total < want; i++)
	{
		const Collapse *c = &g_array_index (candidates, Collapse, i);
		guint32         r0 = ctx->remap[c->from];
		guint32         r1 = ctx->remap[c->to];
		guint32         removed = 0;
		guint32         s;
		gboolean        ok = TRUE;

		if (c->error > limit)
			break;
		if (ctx->touched[r0] || ctx->touched[r1])
			continue;
		if (!link_condition (ctx, idx, r0, r1, &removed))
			continue;
		if (collapse_flips (ctx, idx, r0, r1, ctx->pos + c->to * 3))
			continue;

		/* Every wedge of r0 goes to the wedge of r1 it is connected to
		 * (manifold and border vertices have a single wedge). */
		s = c->from;
		do
		{
			guint32 t = (s == c->from) ? c->to : connected_wedge (ctx, idx, s, r1);

			if (t == G_MAXUINT32)
			{
				ok = FALSE;
				break;
			}
			ctx->collapse_remap[s] = t;
			s = ctx->wedge[s];
		}
		while (s != c->from);
		if (!ok)
		{
			s = c->from;
			do
			{
				ctx->collapse_remap[s] = s;
				s = ctx->wedge[s];
			}
			while (s != c->from);
			continue;
		}

		quadric_add (&ctx->quadrics[r1], &ctx->quadrics[r0]);
		if (c->error > *max_error)
			*max_error = c->error;
		touch_neighbourhood (ctx, idx, r0);
		ctx->touched[r1] = 1;
		removed_total += removed;
		done++;
	}
	return done;
}

/*
 * rewrite_indices:
 *
 * Applies collapse_remap and drops triangles that became degenerate (two
 * corners at one position). Returns the new index count.
 */
static gsize
rewrite_indices (Ctx     *ctx,
                 guint32 *idx,
                 gsize    n_idx)
{
	gsize out = 0;
	gsize t;

	for (t = 0; t < n_idx; t += 3)
	{
		guint32 a = ctx->collapse_remap[idx[t]];
		guint32 b = ctx->collapse_remap[idx[t + 1]];
		guint32 c = ctx->collapse_remap[idx[t + 2]];
		guint32 ra = ctx->remap[a];
		guint32 rb = ctx->remap[b];
		guint32 rc = ctx->remap[c];

		if (ra == rb || rb == rc || ra == rc)
			continue;
		idx[out++] = a;
		idx[out++] = b;
		idx[out++] = c;
	}
	return out;
}

/* Validates input and prepares the context (positions, remap, wedges,
 * buffers). Returns FALSE with @error set on bad data. */
static gboolean
ctx_init (Ctx            *ctx,
          const guint32  *indices,
          gsize           n_indices,
          const gfloat   *positions,
          gsize           n_floats,
          guint           stride,
          GError        **error)
{
	gsize   n_vertices;
	gdouble mn[3];
	gdouble mx[3];
	gdouble extent;
	gsize   v;
	gsize   i;

	memset (ctx, 0, sizeof (*ctx));
	if (stride < 3 || positions == NULL || (indices == NULL && n_indices > 0) || n_indices % 3 != 0)
	{
		g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
		                     "Index count must be a multiple of 3, positions set and stride >= 3");
		return FALSE;
	}
	n_vertices = n_floats / stride;
	if (n_vertices == 0 || n_vertices >= G_MAXUINT32)
	{
		g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
		                     "No vertices (or too many)");
		return FALSE;
	}
	for (i = 0; i < n_indices; i++)
		if (indices[i] >= n_vertices)
		{
			g_set_error (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
			             "Index %u at %" G_GSIZE_FORMAT " is out of range (%" G_GSIZE_FORMAT " vertices)",
			             indices[i], i, n_vertices);
			return FALSE;
		}

	ctx->n_vertices = (guint32) n_vertices;
	ctx->pos = g_new (gdouble, n_vertices * 3);
	ctx->remap = g_new (guint32, n_vertices);
	ctx->wedge = g_new (guint32, n_vertices);
	ctx->kind = g_new0 (guint8, n_vertices);
	ctx->open_out = g_new (guint32, n_vertices);
	ctx->open_in = g_new (guint32, n_vertices);
	ctx->out_target = g_new (guint32, n_vertices);
	ctx->in_source = g_new (guint32, n_vertices);
	ctx->collapse_remap = g_new (guint32, n_vertices);
	ctx->quadrics = g_new0 (Quadric, n_vertices);
	ctx->touched = g_new0 (guint8, n_vertices);
	ctx->tri_offset = g_new0 (guint32, n_vertices + 1);
	ctx->tri_list = g_new (guint32, MAX (n_indices, 1));
	ctx->stamp_a = g_new0 (guint32, n_vertices);
	ctx->stamp_b = g_new0 (guint32, n_vertices);

	/* Normalise into the unit cube of the bounds. */
	mn[0] = mn[1] = mn[2] = G_MAXDOUBLE;
	mx[0] = mx[1] = mx[2] = -G_MAXDOUBLE;
	for (v = 0; v < n_vertices; v++)
	{
		const gfloat *p = positions + v * stride;
		guint         k;

		for (k = 0; k < 3; k++)
		{
			if (p[k] < mn[k])
				mn[k] = p[k];
			if (p[k] > mx[k])
				mx[k] = p[k];
		}
	}
	extent = MAX (MAX (mx[0] - mn[0], mx[1] - mn[1]), mx[2] - mn[2]);
	if (!(extent > 0.0))
		extent = 1.0;
	for (v = 0; v < n_vertices; v++)
	{
		const gfloat *p = positions + v * stride;

		ctx->pos[v * 3 + 0] = (p[0] - mn[0]) / extent;
		ctx->pos[v * 3 + 1] = (p[1] - mn[1]) / extent;
		ctx->pos[v * 3 + 2] = (p[2] - mn[2]) / extent;
	}

	build_position_remap (ctx, positions, stride);
	return TRUE;
}

/* Drops input triangles whose corners share a position. */
static gsize
drop_degenerate (const Ctx *ctx,
                 guint32   *idx,
                 gsize      n_idx)
{
	gsize out = 0;
	gsize t;

	for (t = 0; t < n_idx; t += 3)
	{
		guint32 ra = ctx->remap[idx[t]];
		guint32 rb = ctx->remap[idx[t + 1]];
		guint32 rc = ctx->remap[idx[t + 2]];

		if (ra == rb || rb == rc || ra == rc)
			continue;
		idx[out++] = idx[t];
		idx[out++] = idx[t + 1];
		idx[out++] = idx[t + 2];
	}
	return out;
}

/*
 * simplify_core:
 *
 * The pass loop described at the top of the file. @idx is rewritten in
 * place; returns the new index count and stores the largest collapse
 * error (relative distance) in @out_error.
 */
static gsize
simplify_core (Ctx      *ctx,
               guint32  *idx,
               gsize     n_idx,
               gsize     target_idx,
               gdouble   target_error,
               gdouble  *out_error)
{
	gdouble limit = target_error * target_error;
	gdouble max_error = 0.0;
	gsize   count;

	count = drop_degenerate (ctx, idx, n_idx);

	/* Quadrics come from the input surface once and then accumulate. */
	build_adjacency (ctx, idx, count);
	compute_quadrics (ctx, idx, count);

	while (count > target_idx)
	{
		GArray *candidates;
		guint   done;

		build_adjacency (ctx, idx, count);
		classify (ctx, idx);
		candidates = pick_collapses (ctx, idx, count);
		done = perform_collapses (ctx, idx, candidates, limit, (count - target_idx) / 3, &max_error);
		g_array_unref (candidates);
		if (done == 0)
			break;
		count = rewrite_indices (ctx, idx, count);
	}

	*out_error = sqrt (max_error);
	return count;
}

/* ==========================================================================
 * Public API: settings
 * ========================================================================== */

LrgMeshSimplify *
lrg_mesh_simplify_new (void)
{
	return g_object_new (LRG_TYPE_MESH_SIMPLIFY, NULL);
}

gfloat
lrg_mesh_simplify_get_target_ratio (LrgMeshSimplify *self)
{
	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), 0.0f);
	return self->target_ratio;
}

void
lrg_mesh_simplify_set_target_ratio (LrgMeshSimplify *self,
                                    gfloat           ratio)
{
	g_return_if_fail (LRG_IS_MESH_SIMPLIFY (self));

	ratio = CLAMP (ratio, 0.0f, 1.0f);
	if (self->target_ratio == ratio)
		return;
	self->target_ratio = ratio;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_RATIO]);
}

gfloat
lrg_mesh_simplify_get_target_error (LrgMeshSimplify *self)
{
	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), 0.0f);
	return self->target_error;
}

void
lrg_mesh_simplify_set_target_error (LrgMeshSimplify *self,
                                    gfloat           error)
{
	g_return_if_fail (LRG_IS_MESH_SIMPLIFY (self));

	error = MAX (error, 0.0f);
	if (self->target_error == error)
		return;
	self->target_error = error;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_ERROR]);
}

gboolean
lrg_mesh_simplify_get_lock_border (LrgMeshSimplify *self)
{
	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), FALSE);
	return self->lock_border;
}

void
lrg_mesh_simplify_set_lock_border (LrgMeshSimplify *self,
                                   gboolean         lock)
{
	g_return_if_fail (LRG_IS_MESH_SIMPLIFY (self));

	lock = !!lock;
	if (self->lock_border == lock)
		return;
	self->lock_border = lock;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCK_BORDER]);
}

gboolean
lrg_mesh_simplify_get_lock_seams (LrgMeshSimplify *self)
{
	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), FALSE);
	return self->lock_seams;
}

void
lrg_mesh_simplify_set_lock_seams (LrgMeshSimplify *self,
                                  gboolean         lock)
{
	g_return_if_fail (LRG_IS_MESH_SIMPLIFY (self));

	lock = !!lock;
	if (self->lock_seams == lock)
		return;
	self->lock_seams = lock;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCK_SEAMS]);
}

gfloat
lrg_mesh_simplify_get_result_error (LrgMeshSimplify *self)
{
	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), 0.0f);
	return self->result_error;
}

gfloat
lrg_mesh_simplify_get_result_ratio (LrgMeshSimplify *self)
{
	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), 1.0f);
	return self->result_ratio;
}

/* Stores the result of a run and notifies. */
static void
set_result (LrgMeshSimplify *self,
            gdouble          error,
            gsize            n_in,
            gsize            n_out)
{
	self->result_error = (gfloat) error;
	self->result_ratio = (n_in > 0) ? (gfloat) ((gdouble) n_out / (gdouble) n_in) : 1.0f;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESULT_ERROR]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESULT_RATIO]);
}

/* ==========================================================================
 * Public API: headless simplification
 * ========================================================================== */

gboolean
lrg_mesh_simplify_indices (LrgMeshSimplify  *self,
                           const guint32    *indices,
                           gsize             n_indices,
                           const gfloat     *positions,
                           gsize             n_floats,
                           guint             stride,
                           guint32         **out_indices,
                           gsize            *out_n_indices,
                           GError          **error)
{
	Ctx      ctx;
	guint32 *idx;
	gsize    target;
	gsize    count;
	gdouble  result_error = 0.0;

	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), FALSE);
	g_return_val_if_fail (out_indices != NULL, FALSE);
	g_return_val_if_fail (out_n_indices != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	*out_indices = NULL;
	*out_n_indices = 0;
	if (!ctx_init (&ctx, indices, n_indices, positions, n_floats, stride, error))
	{
		ctx_free (&ctx);
		return FALSE;
	}
	ctx.lock_border = self->lock_border;
	ctx.lock_seams = self->lock_seams;

	idx = g_new (guint32, MAX (n_indices, 1));
	if (n_indices > 0)
		memcpy (idx, indices, sizeof (guint32) * n_indices);

	/* Target in whole triangles, at least one. */
	target = (gsize) floor ((gdouble) (n_indices / 3) * (gdouble) self->target_ratio + 0.5) * 3;
	if (target < 3)
		target = 3;

	count = simplify_core (&ctx, idx, n_indices, target, (gdouble) self->target_error, &result_error);
	ctx_free (&ctx);

	set_result (self, result_error, n_indices, count);
	*out_indices = idx;
	*out_n_indices = count;
	return TRUE;
}

gboolean
lrg_mesh_simplify_classify (LrgMeshSimplify  *self,
                            const guint32    *indices,
                            gsize             n_indices,
                            const gfloat     *positions,
                            gsize             n_floats,
                            guint             stride,
                            guint8          **out_kinds,
                            gsize            *out_n_kinds,
                            GError          **error)
{
	Ctx      ctx;
	guint32 *idx;
	gsize    count;

	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), FALSE);
	g_return_val_if_fail (out_kinds != NULL, FALSE);
	g_return_val_if_fail (out_n_kinds != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	*out_kinds = NULL;
	*out_n_kinds = 0;
	if (!ctx_init (&ctx, indices, n_indices, positions, n_floats, stride, error))
	{
		ctx_free (&ctx);
		return FALSE;
	}
	ctx.lock_border = self->lock_border;
	ctx.lock_seams = self->lock_seams;

	idx = g_new (guint32, MAX (n_indices, 1));
	if (n_indices > 0)
		memcpy (idx, indices, sizeof (guint32) * n_indices);
	count = drop_degenerate (&ctx, idx, n_indices);
	build_adjacency (&ctx, idx, count);
	classify (&ctx, idx);

	*out_n_kinds = ctx.n_vertices;
	*out_kinds = (guint8 *) g_steal_pointer (&ctx.kind);
	g_free (idx);
	ctx_free (&ctx);
	return TRUE;
}

/* ==========================================================================
 * raylib Mesh helpers
 * ========================================================================== */

/* Byte size of one vertex across every attribute @m carries (welding key). */
static gsize
vertex_key_size (const Mesh *m)
{
	gsize s = 3 * sizeof (gfloat);

	if (m->texcoords != NULL)
		s += 2 * sizeof (gfloat);
	if (m->texcoords2 != NULL)
		s += 2 * sizeof (gfloat);
	if (m->normals != NULL)
		s += 3 * sizeof (gfloat);
	if (m->tangents != NULL)
		s += 4 * sizeof (gfloat);
	if (m->colors != NULL)
		s += 4;
	if (m->boneIndices != NULL)
		s += 4;
	if (m->boneWeights != NULL)
		s += 4 * sizeof (gfloat);
	return s;
}

/* Packs vertex @v of @m into @out (vertex_key_size() bytes); -0 becomes 0
 * in the float attributes so they compare equal. */
static void
vertex_key (const Mesh *m,
            gsize       v,
            guint8     *out)
{
	gsize  o = 0;
	guint  k;
	gfloat f;

#define PUT_FLOATS(arr, n) \
	for (k = 0; k < (n); k++) \
	{ \
		f = (arr)[v * (n) + k]; \
		if (f == 0.0f) \
			f = 0.0f; \
		memcpy (out + o, &f, sizeof (gfloat)); \
		o += sizeof (gfloat); \
	}

	PUT_FLOATS (m->vertices, 3);
	if (m->texcoords != NULL)
	{
		PUT_FLOATS (m->texcoords, 2);
	}
	if (m->texcoords2 != NULL)
	{
		PUT_FLOATS (m->texcoords2, 2);
	}
	if (m->normals != NULL)
	{
		PUT_FLOATS (m->normals, 3);
	}
	if (m->tangents != NULL)
	{
		PUT_FLOATS (m->tangents, 4);
	}
	if (m->colors != NULL)
	{
		memcpy (out + o, m->colors + v * 4, 4);
		o += 4;
	}
	if (m->boneIndices != NULL)
	{
		memcpy (out + o, m->boneIndices + v * 4, 4);
		o += 4;
	}
	if (m->boneWeights != NULL)
	{
		PUT_FLOATS (m->boneWeights, 4);
	}
#undef PUT_FLOATS
}

/*
 * weld_vertices:
 *
 * Index buffer for an unindexed mesh: vertices with byte-identical
 * attributes share one index. @out_source maps each welded vertex to its
 * first source vertex. Returns the number of welded vertices.
 */
static gsize
weld_vertices (const Mesh *m,
               guint32    *out_indices,
               guint32    *out_source)
{
	gsize    n = (gsize) m->vertexCount;
	gsize    key_size = vertex_key_size (m);
	guint8  *keys = g_malloc (key_size * MAX (n, 1));
	gsize    size = 16;
	guint32 *table;
	gsize    unique = 0;
	gsize    v;

	while (size < n * 2)
		size <<= 1;
	table = g_new (guint32, size);
	memset (table, 0xff, sizeof (guint32) * size);

	for (v = 0; v < n; v++)
	{
		guint8 *key = keys + v * key_size;
		guint32 h = 2166136261u;
		gsize   b;
		gsize   slot;

		vertex_key (m, v, key);
		for (b = 0; b < key_size; b++)
			h = (h ^ key[b]) * 16777619u;
		slot = h & (size - 1);
		for (;;)
		{
			guint32 w = table[slot];

			if (w == G_MAXUINT32)
			{
				table[slot] = (guint32) unique;
				out_source[unique] = (guint32) v;
				out_indices[v] = (guint32) unique;
				unique++;
				break;
			}
			if (memcmp (keys + (gsize) out_source[w] * key_size, key, key_size) == 0)
			{
				out_indices[v] = w;
				break;
			}
			slot = (slot + 1) & (size - 1);
		}
	}
	g_free (table);
	g_free (keys);
	return unique;
}


/* Allocates every attribute array @s has, for @n vertices, in @d. */
static void
alloc_attributes (const Mesh *s,
                  Mesh       *d,
                  gsize       n)
{
	d->vertices = (float *) RL_MALLOC (n * 3 * sizeof (float));
	d->texcoords = (s->texcoords != NULL) ? (float *) RL_MALLOC (n * 2 * sizeof (float)) : NULL;
	d->texcoords2 = (s->texcoords2 != NULL) ? (float *) RL_MALLOC (n * 2 * sizeof (float)) : NULL;
	d->normals = (s->normals != NULL) ? (float *) RL_MALLOC (n * 3 * sizeof (float)) : NULL;
	d->tangents = (s->tangents != NULL) ? (float *) RL_MALLOC (n * 4 * sizeof (float)) : NULL;
	d->colors = (s->colors != NULL) ? (unsigned char *) RL_MALLOC (n * 4) : NULL;
	d->boneIndices = (s->boneIndices != NULL) ? (unsigned char *) RL_MALLOC (n * 4) : NULL;
	d->boneWeights = (s->boneWeights != NULL) ? (float *) RL_MALLOC (n * 4 * sizeof (float)) : NULL;
}

/* Copies every attribute of source vertex @from to output vertex @to. */
static void
copy_vertex (const Mesh *s,
             Mesh       *d,
             gsize       from,
             gsize       to)
{
	memcpy (d->vertices + to * 3, s->vertices + from * 3, 3 * sizeof (float));
	if (s->texcoords != NULL)
		memcpy (d->texcoords + to * 2, s->texcoords + from * 2, 2 * sizeof (float));
	if (s->texcoords2 != NULL)
		memcpy (d->texcoords2 + to * 2, s->texcoords2 + from * 2, 2 * sizeof (float));
	if (s->normals != NULL)
		memcpy (d->normals + to * 3, s->normals + from * 3, 3 * sizeof (float));
	if (s->tangents != NULL)
		memcpy (d->tangents + to * 4, s->tangents + from * 4, 4 * sizeof (float));
	if (s->colors != NULL)
		memcpy (d->colors + to * 4, s->colors + from * 4, 4);
	if (s->boneIndices != NULL)
		memcpy (d->boneIndices + to * 4, s->boneIndices + from * 4, 4);
	if (s->boneWeights != NULL)
		memcpy (d->boneWeights + to * 4, s->boneWeights + from * 4, 4 * sizeof (float));
}

gboolean
lrg_mesh_simplify_raylib_mesh (LrgMeshSimplify  *self,
                               gconstpointer     src,
                               gpointer          dst,
                               GError          **error)
{
	const Mesh        *s = (const Mesh *) src;
	Mesh              *d = (Mesh *) dst;
	g_autofree guint32 *indices = NULL;
	g_autofree guint32 *source = NULL;
	g_autofree gfloat  *gathered = NULL;
	g_autofree guint32 *out = NULL;
	g_autofree guint32 *new_id = NULL;
	const gfloat       *positions;
	gsize               n_idx;
	gsize               n_welded;
	gsize               n_out = 0;
	gsize               n_used = 0;
	gsize               i;

	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), FALSE);
	g_return_val_if_fail (src != NULL, FALSE);
	g_return_val_if_fail (dst != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (s->vertices == NULL || s->vertexCount <= 0 || s->triangleCount <= 0)
	{
		g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
		                     "The mesh has no CPU-side vertex data");
		return FALSE;
	}

	/* 1. A 32-bit index buffer over (welded) vertices. */
	if (s->indices != NULL)
	{
		n_idx = (gsize) s->triangleCount * 3;
		n_welded = (gsize) s->vertexCount;
		indices = g_new (guint32, n_idx);
		source = g_new (guint32, n_welded);
		for (i = 0; i < n_idx; i++)
			indices[i] = s->indices[i];
		for (i = 0; i < n_welded; i++)
			source[i] = (guint32) i;
		positions = s->vertices;
	}
	else
	{
		if (s->vertexCount % 3 != 0)
		{
			g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
			                     "Unindexed mesh with a vertex count that is not a multiple of 3");
			return FALSE;
		}
		n_idx = (gsize) s->vertexCount;
		indices = g_new (guint32, n_idx);
		source = g_new (guint32, n_idx);
		n_welded = weld_vertices (s, indices, source);
		gathered = g_new (gfloat, n_welded * 3);
		for (i = 0; i < n_welded; i++)
			memcpy (gathered + i * 3, s->vertices + (gsize) source[i] * 3, 3 * sizeof (gfloat));
		positions = gathered;
	}

	/* 2. Simplify. */
	if (!lrg_mesh_simplify_indices (self, indices, n_idx, positions, n_welded * 3, 3, &out, &n_out, error))
		return FALSE;
	if (n_out == 0)
	{
		g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
		                     "The mesh has no non-degenerate triangles");
		return FALSE;
	}

	/* 3. Compact the used vertices, in first-use order. */
	new_id = g_new (guint32, n_welded);
	memset (new_id, 0xff, sizeof (guint32) * n_welded);
	for (i = 0; i < n_out; i++)
		if (new_id[out[i]] == G_MAXUINT32)
			new_id[out[i]] = (guint32) n_used++;

	/* 4. Fill @dst: indexed when 16-bit indices suffice, else expanded. */
	memset (d, 0, sizeof (Mesh));
	d->boneCount = s->boneCount;
	d->triangleCount = (int) (n_out / 3);
	if (n_used <= 65535)
	{
		alloc_attributes (s, d, n_used);
		for (i = 0; i < n_welded; i++)
			if (new_id[i] != G_MAXUINT32)
				copy_vertex (s, d, source[i], new_id[i]);
		d->indices = (unsigned short *) RL_MALLOC (n_out * sizeof (unsigned short));
		for (i = 0; i < n_out; i++)
			d->indices[i] = (unsigned short) new_id[out[i]];
		d->vertexCount = (int) n_used;
	}
	else
	{
		alloc_attributes (s, d, n_out);
		for (i = 0; i < n_out; i++)
			copy_vertex (s, d, source[out[i]], i);
		d->vertexCount = (int) n_out;
	}
	return TRUE;
}

/* ==========================================================================
 * Public API: GPU objects
 * ========================================================================== */

GrlMesh *
lrg_mesh_simplify_wrap_raylib_mesh (gpointer   mesh,
                                    GError   **error)
{
	static const gfloat  tri_vertices[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f };
	static const guint16 tri_indices[3] = { 0, 1, 2 };
	Mesh                *m = (Mesh *) mesh;
	GrlMesh             *wrapper;
	Mesh                *handle;

	g_return_val_if_fail (mesh != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!IsWindowReady () || m->vertices == NULL || m->vertexCount <= 0)
	{
		gboolean no_gpu = !IsWindowReady ();

		/* CPU arrays only (vaoId 0): UnloadMesh() just frees them. */
		m->vaoId = 0;
		m->vboId = NULL;
		UnloadMesh (*m);
		memset (m, 0, sizeof (Mesh));
		if (no_gpu)
			g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_NO_GPU,
			                     "A graphics context is required to upload a mesh");
		else
			g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
			                     "The mesh has no vertices");
		return NULL;
	}

	/*
	 * graylib has no constructor that adopts a raylib Mesh with every
	 * attribute, so build a one-triangle GrlMesh and swap its contents:
	 * the GrlMesh owns whatever Mesh its handle points at and unloads it
	 * when finalized.
	 */
	wrapper = grl_mesh_new_custom (tri_vertices, 3, NULL, tri_indices, 3);
	if (wrapper == NULL)
	{
		m->vaoId = 0;
		m->vboId = NULL;
		UnloadMesh (*m);
		memset (m, 0, sizeof (Mesh));
		g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_NO_GPU,
		                     "Could not create a mesh");
		return NULL;
	}
	handle = (Mesh *) grl_mesh_get_handle (wrapper);
	UnloadMesh (*handle);
	*handle = *m;
	handle->vaoId = 0;
	handle->vboId = NULL;
	memset (m, 0, sizeof (Mesh));
	UploadMesh (handle, false);
	return wrapper;
}

GrlMesh *
lrg_mesh_simplify_mesh (LrgMeshSimplify  *self,
                        GrlMesh          *mesh,
                        GError          **error)
{
	Mesh  out;
	Mesh *src;

	g_return_val_if_fail (LRG_IS_MESH_SIMPLIFY (self), NULL);
	g_return_val_if_fail (GRL_IS_MESH (mesh), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	src = (Mesh *) grl_mesh_get_handle (mesh);
	if (!lrg_mesh_simplify_raylib_mesh (self, src, &out, error))
		return NULL;
	return lrg_mesh_simplify_wrap_raylib_mesh (&out, error);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_mesh_simplify_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgMeshSimplify *self = LRG_MESH_SIMPLIFY (object);

	switch (prop_id)
	{
	case PROP_TARGET_RATIO:
		g_value_set_float (value, self->target_ratio);
		break;
	case PROP_TARGET_ERROR:
		g_value_set_float (value, self->target_error);
		break;
	case PROP_LOCK_BORDER:
		g_value_set_boolean (value, self->lock_border);
		break;
	case PROP_LOCK_SEAMS:
		g_value_set_boolean (value, self->lock_seams);
		break;
	case PROP_RESULT_ERROR:
		g_value_set_float (value, self->result_error);
		break;
	case PROP_RESULT_RATIO:
		g_value_set_float (value, self->result_ratio);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_mesh_simplify_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgMeshSimplify *self = LRG_MESH_SIMPLIFY (object);

	switch (prop_id)
	{
	case PROP_TARGET_RATIO:
		lrg_mesh_simplify_set_target_ratio (self, g_value_get_float (value));
		break;
	case PROP_TARGET_ERROR:
		lrg_mesh_simplify_set_target_error (self, g_value_get_float (value));
		break;
	case PROP_LOCK_BORDER:
		lrg_mesh_simplify_set_lock_border (self, g_value_get_boolean (value));
		break;
	case PROP_LOCK_SEAMS:
		lrg_mesh_simplify_set_lock_seams (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_mesh_simplify_class_init (LrgMeshSimplifyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_mesh_simplify_get_property;
	object_class->set_property = lrg_mesh_simplify_set_property;

	/**
	 * LrgMeshSimplify:target-ratio:
	 *
	 * Fraction of the triangles to keep (0..1). Simplification stops
	 * once the triangle count is at or below it.
	 */
	properties[PROP_TARGET_RATIO] =
		g_param_spec_float ("target-ratio", "Target ratio", "Fraction of triangles to keep",
		                    0.0f, 1.0f, 0.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMeshSimplify:target-error:
	 *
	 * Largest allowed geometric error as a fraction of the mesh extent.
	 * No collapse above it runs, even if the ratio is not reached.
	 */
	properties[PROP_TARGET_ERROR] =
		g_param_spec_float ("target-error", "Target error", "Largest relative error allowed",
		                    0.0f, G_MAXFLOAT, 0.01f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMeshSimplify:lock-border:
	 *
	 * Pins vertices on open boundaries so separately simplified pieces
	 * still meet.
	 */
	properties[PROP_LOCK_BORDER] =
		g_param_spec_boolean ("lock-border", "Lock border", "Pin open-boundary vertices",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMeshSimplify:lock-seams:
	 *
	 * Pins UV/normal seam vertices instead of letting both wedges slide
	 * along the seam. Seams never open either way.
	 */
	properties[PROP_LOCK_SEAMS] =
		g_param_spec_boolean ("lock-seams", "Lock seams", "Pin UV/normal seam vertices",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMeshSimplify:result-error:
	 *
	 * Relative error of the last run (largest collapse error).
	 */
	properties[PROP_RESULT_ERROR] =
		g_param_spec_float ("result-error", "Result error", "Relative error of the last run",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMeshSimplify:result-ratio:
	 *
	 * Output triangles over input triangles of the last run.
	 */
	properties[PROP_RESULT_RATIO] =
		g_param_spec_float ("result-ratio", "Result ratio", "Triangle ratio of the last run",
		                    0.0f, 1.0f, 1.0f,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_mesh_simplify_init (LrgMeshSimplify *self)
{
	self->target_ratio = 0.5f;
	self->target_error = 0.01f;
	self->lock_border = TRUE;
	self->lock_seams = FALSE;
	self->result_error = 0.0f;
	self->result_ratio = 1.0f;
}
