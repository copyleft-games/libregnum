/* lrg-bake-bvh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBakeBvh: a binned-SAH triangle BVH for the lightmap baker's CPU
 * ray tracer.  See the header for the overview.
 *
 * Layout
 *   nodes[]  32-byte nodes.  An interior node stores the index of its
 *            first child (the second is first + 1) and count 0; a leaf
 *            stores the first triangle of its run and the run length.
 *   tris[]   9 floats per triangle in leaf order: v0, edge1 = v1 - v0,
 *            edge2 = v2 - v0 (what Moller-Trumbore needs, nothing else).
 *   ids[]    the caller's triangle index of each reordered triangle.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-bake-bvh.h"

/* bins per axis of the SAH sweep */
#define BVH_BINS       (16)
/* traversal stack; a SAH tree over 2^32 triangles stays far below */
#define BVH_STACK      (96)
/* relative cost of a node visit against a triangle test */
#define BVH_TRAVERSAL_COST (1.0f)

typedef struct
{
	gfloat bmin[3];
	gint32 first;        /* first child (interior) or first triangle (leaf) */
	gfloat bmax[3];
	gint32 count;        /* 0 = interior */
} BvhNode;

struct _LrgBakeBvh
{
	GObject  parent_instance;

	BvhNode *nodes;
	guint    n_nodes;
	gfloat  *tris;       /* 9 floats per triangle, leaf order */
	guint   *ids;        /* caller triangle index per leaf-order triangle */
	guint    n_tris;
};

G_DEFINE_FINAL_TYPE (LrgBakeBvh, lrg_bake_bvh, G_TYPE_OBJECT)

/* ==========================================================================
 * LrgBakeHit (boxed)
 * ========================================================================== */

LrgBakeHit *
lrg_bake_hit_copy (const LrgBakeHit *hit)
{
	g_return_val_if_fail (hit != NULL, NULL);

	return (LrgBakeHit *)g_memdup2 (hit, sizeof (LrgBakeHit));
}

void
lrg_bake_hit_free (LrgBakeHit *hit)
{
	g_free (hit);
}

G_DEFINE_BOXED_TYPE (LrgBakeHit, lrg_bake_hit, lrg_bake_hit_copy, lrg_bake_hit_free)

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_bake_bvh_clear (LrgBakeBvh *self)
{
	g_clear_pointer (&self->nodes, g_free);
	g_clear_pointer (&self->tris, g_free);
	g_clear_pointer (&self->ids, g_free);
	self->n_nodes = 0;
	self->n_tris = 0;
}

static void
lrg_bake_bvh_finalize (GObject *object)
{
	lrg_bake_bvh_clear (LRG_BAKE_BVH (object));
	G_OBJECT_CLASS (lrg_bake_bvh_parent_class)->finalize (object);
}

static void
lrg_bake_bvh_class_init (LrgBakeBvhClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_bake_bvh_finalize;
}

static void
lrg_bake_bvh_init (LrgBakeBvh *self)
{
}

LrgBakeBvh *
lrg_bake_bvh_new (void)
{
	return g_object_new (LRG_TYPE_BAKE_BVH, NULL);
}

/* ==========================================================================
 * Build
 * ========================================================================== */

/* Per-triangle build record: bounds and centroid. */
typedef struct
{
	gfloat bmin[3];
	gfloat bmax[3];
	gfloat c[3];
} BuildTri;

typedef struct
{
	const BuildTri *recs;
	guint          *order;    /* triangle indices, partitioned in place */
	BvhNode        *nodes;
	guint           n_nodes;
	guint           cap_nodes;
} BuildCtx;

static void
box_reset (gfloat *bmin,
           gfloat *bmax)
{
	bmin[0] = bmin[1] = bmin[2] = G_MAXFLOAT;
	bmax[0] = bmax[1] = bmax[2] = -G_MAXFLOAT;
}

static void
box_grow_box (gfloat       *bmin,
              gfloat       *bmax,
              const gfloat *omin,
              const gfloat *omax)
{
	gint k;

	for (k = 0; k < 3; k++)
	{
		if (omin[k] < bmin[k])
			bmin[k] = omin[k];
		if (omax[k] > bmax[k])
			bmax[k] = omax[k];
	}
}

static gfloat
box_area (const gfloat *bmin,
          const gfloat *bmax)
{
	gfloat dx = bmax[0] - bmin[0];
	gfloat dy = bmax[1] - bmin[1];
	gfloat dz = bmax[2] - bmin[2];

	if (dx < 0.0f || dy < 0.0f || dz < 0.0f)
		return 0.0f;
	return 2.0f * (dx * dy + dy * dz + dz * dx);
}

static guint
alloc_node (BuildCtx *ctx)
{
	if (ctx->n_nodes == ctx->cap_nodes)
	{
		ctx->cap_nodes = ctx->cap_nodes * 2u + 16u;
		ctx->nodes = g_renew (BvhNode, ctx->nodes, ctx->cap_nodes);
	}
	return ctx->n_nodes++;
}

/*
 * Builds the subtree of the triangles order[start .. end) into node
 * @ni.  Splits with a 16-bin SAH sweep on the axis and plane of lowest
 * cost; a range that cannot be split profitably (or whose centroids all
 * coincide) becomes a leaf, chopped at the median when it is still too
 * big for one leaf.
 */
static void
build_node (BuildCtx *ctx,
            guint     ni,
            guint     start,
            guint     end,
            guint     depth)
{
	gfloat bmin[3];
	gfloat bmax[3];
	gfloat cmin[3];
	gfloat cmax[3];
	guint  count = end - start;
	guint  i;
	gint   axis;
	gint   best_axis = -1;
	gint   best_split = -1;
	gfloat best_cost;
	gfloat leaf_cost;
	guint  mid;
	guint  left;

	/* bounds of the triangles and of their centroids */
	box_reset (bmin, bmax);
	box_reset (cmin, cmax);
	for (i = start; i < end; i++)
	{
		const BuildTri *r = &ctx->recs[ctx->order[i]];

		box_grow_box (bmin, bmax, r->bmin, r->bmax);
		box_grow_box (cmin, cmax, r->c, r->c);
	}
	memcpy (ctx->nodes[ni].bmin, bmin, sizeof bmin);
	memcpy (ctx->nodes[ni].bmax, bmax, sizeof bmax);

	if (count <= 1u || depth >= (guint)BVH_STACK - 8u)
	{
		ctx->nodes[ni].first = (gint32)start;
		ctx->nodes[ni].count = (gint32)count;
		return;
	}

	/* SAH sweep: cost = traversal + (A_l n_l + A_r n_r) / A_parent */
	leaf_cost = (gfloat)count;
	best_cost = G_MAXFLOAT;
	for (axis = 0; axis < 3; axis++)
	{
		gfloat extent = cmax[axis] - cmin[axis];
		gfloat bin_min[BVH_BINS][3];
		gfloat bin_max[BVH_BINS][3];
		guint  bin_n[BVH_BINS];
		gfloat right_area[BVH_BINS];
		guint  right_n[BVH_BINS];
		gfloat amin[3];
		gfloat amax[3];
		gfloat scale;
		guint  acc;
		gint   b;

		if (extent <= 1e-12f)
			continue;
		scale = (gfloat)BVH_BINS / extent;
		for (b = 0; b < BVH_BINS; b++)
		{
			box_reset (bin_min[b], bin_max[b]);
			bin_n[b] = 0;
		}
		for (i = start; i < end; i++)
		{
			const BuildTri *r = &ctx->recs[ctx->order[i]];

			b = (gint)((r->c[axis] - cmin[axis]) * scale);
			b = CLAMP (b, 0, BVH_BINS - 1);
			bin_n[b]++;
			box_grow_box (bin_min[b], bin_max[b], r->bmin, r->bmax);
		}
		/* suffix sweep: area and count right of each plane */
		box_reset (amin, amax);
		acc = 0;
		for (b = BVH_BINS - 1; b > 0; b--)
		{
			box_grow_box (amin, amax, bin_min[b], bin_max[b]);
			acc += bin_n[b];
			right_area[b] = box_area (amin, amax);
			right_n[b] = acc;
		}
		/* prefix sweep: evaluate the plane between bin b-1 and b */
		box_reset (amin, amax);
		acc = 0;
		for (b = 1; b < BVH_BINS; b++)
		{
			gfloat cost;

			box_grow_box (amin, amax, bin_min[b - 1], bin_max[b - 1]);
			acc += bin_n[b - 1];
			if (acc == 0 || right_n[b] == 0)
				continue;
			cost = box_area (amin, amax) * (gfloat)acc + right_area[b] * (gfloat)right_n[b];
			if (cost < best_cost)
			{
				best_cost = cost;
				best_axis = axis;
				best_split = b;
			}
		}
	}

	if (best_axis >= 0)
	{
		gfloat parent_area = box_area (bmin, bmax);

		if (parent_area > 0.0f)
			best_cost = BVH_TRAVERSAL_COST + best_cost / parent_area;
	}

	if (best_axis < 0 || (best_cost >= leaf_cost && count <= LRG_BAKE_BVH_MAX_LEAF_TRIANGLES))
	{
		if (count <= LRG_BAKE_BVH_MAX_LEAF_TRIANGLES)
		{
			ctx->nodes[ni].first = (gint32)start;
			ctx->nodes[ni].count = (gint32)count;
			return;
		}
		/* identical centroids (or no gain) but too many: median split */
		mid = start + count / 2u;
	}
	else
	{
		/* partition by the chosen bin plane */
		gfloat scale = (gfloat)BVH_BINS / (cmax[best_axis] - cmin[best_axis]);
		guint  lo = start;
		guint  hi = end;

		while (lo < hi)
		{
			const BuildTri *r = &ctx->recs[ctx->order[lo]];
			gint b = (gint)((r->c[best_axis] - cmin[best_axis]) * scale);

			b = CLAMP (b, 0, BVH_BINS - 1);
			if (b < best_split)
				lo++;
			else
			{
				guint tmp = ctx->order[lo];

				hi--;
				ctx->order[lo] = ctx->order[hi];
				ctx->order[hi] = tmp;
			}
		}
		mid = lo;
		if (mid == start || mid == end)
			mid = start + count / 2u;
	}

	/* two children side by side; the node array may move, so index */
	left = alloc_node (ctx);
	(void)alloc_node (ctx);
	ctx->nodes[ni].first = (gint32)left;
	ctx->nodes[ni].count = 0;
	build_node (ctx, left, start, mid, depth + 1u);
	build_node (ctx, left + 1u, mid, end, depth + 1u);
}

gboolean
lrg_bake_bvh_build (LrgBakeBvh    *self,
                    const gfloat  *positions,
                    gsize          n_floats,
                    const guint32 *indices,
                    gsize          n_indices)
{
	g_autofree BuildTri *recs = NULL;
	g_autofree guint    *order = NULL;
	BuildCtx             ctx;
	gsize                n_verts;
	gsize                n_tris;
	gsize                t;

	g_return_val_if_fail (LRG_IS_BAKE_BVH (self), FALSE);
	g_return_val_if_fail (positions != NULL || n_floats == 0, FALSE);

	lrg_bake_bvh_clear (self);

	n_verts = n_floats / 3u;
	n_tris = indices != NULL ? n_indices / 3u : n_verts / 3u;
	if (n_tris == 0)
		return TRUE;
	if (n_tris > (gsize)G_MAXINT32)
	{
		g_warning ("lrg_bake_bvh_build: %" G_GSIZE_FORMAT " triangles is too many", n_tris);
		return FALSE;
	}

	/* validate the indices before anything is allocated for the tree */
	if (indices != NULL)
	{
		for (t = 0; t < n_tris * 3u; t++)
		{
			if ((gsize)indices[t] >= n_verts)
			{
				g_warning ("lrg_bake_bvh_build: index %u out of range (%" G_GSIZE_FORMAT
				           " vertices)", indices[t], n_verts);
				return FALSE;
			}
		}
	}

	/* per-triangle bounds and centroid */
	recs = g_new (BuildTri, n_tris);
	order = g_new (guint, n_tris);
	for (t = 0; t < n_tris; t++)
	{
		const gfloat *v[3];
		gint          j;
		gint          k;

		for (j = 0; j < 3; j++)
		{
			gsize vi = indices != NULL ? (gsize)indices[t * 3u + (gsize)j] : t * 3u + (gsize)j;

			v[j] = positions + vi * 3u;
		}
		for (k = 0; k < 3; k++)
		{
			recs[t].bmin[k] = MIN (v[0][k], MIN (v[1][k], v[2][k]));
			recs[t].bmax[k] = MAX (v[0][k], MAX (v[1][k], v[2][k]));
			recs[t].c[k] = (recs[t].bmin[k] + recs[t].bmax[k]) * 0.5f;
		}
		order[t] = (guint)t;
	}

	/* the tree; at most 2n - 1 nodes */
	memset (&ctx, 0, sizeof ctx);
	ctx.recs = recs;
	ctx.order = order;
	ctx.cap_nodes = (guint)MIN (n_tris * 2u, (gsize)G_MAXUINT / 2u);
	ctx.nodes = g_new (BvhNode, ctx.cap_nodes);
	(void)alloc_node (&ctx);
	build_node (&ctx, 0, 0, (guint)n_tris, 0);

	/* triangles in leaf order, pre-transformed for the hit test */
	self->tris = g_new (gfloat, n_tris * 9u);
	self->ids = g_new (guint, n_tris);
	for (t = 0; t < n_tris; t++)
	{
		guint         src = order[t];
		const gfloat *v[3];
		gfloat       *dst = self->tris + t * 9u;
		gint          j;
		gint          k;

		for (j = 0; j < 3; j++)
		{
			gsize vi = indices != NULL ? (gsize)indices[(gsize)src * 3u + (gsize)j]
			                           : (gsize)src * 3u + (gsize)j;

			v[j] = positions + vi * 3u;
		}
		for (k = 0; k < 3; k++)
		{
			dst[k] = v[0][k];
			dst[3 + k] = v[1][k] - v[0][k];
			dst[6 + k] = v[2][k] - v[0][k];
		}
		self->ids[t] = src;
	}

	self->nodes = g_renew (BvhNode, ctx.nodes, MAX (ctx.n_nodes, 1u));
	self->n_nodes = ctx.n_nodes;
	self->n_tris = (guint)n_tris;
	return TRUE;
}

guint
lrg_bake_bvh_get_n_triangles (LrgBakeBvh *self)
{
	g_return_val_if_fail (LRG_IS_BAKE_BVH (self), 0);
	return self->n_tris;
}

guint
lrg_bake_bvh_get_n_nodes (LrgBakeBvh *self)
{
	g_return_val_if_fail (LRG_IS_BAKE_BVH (self), 0);
	return self->n_nodes;
}

gboolean
lrg_bake_bvh_get_bounds (LrgBakeBvh *self,
                         gfloat     *out_min,
                         gfloat     *out_max)
{
	g_return_val_if_fail (LRG_IS_BAKE_BVH (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

	if (self->n_nodes == 0)
	{
		memset (out_min, 0, 3 * sizeof (gfloat));
		memset (out_max, 0, 3 * sizeof (gfloat));
		return FALSE;
	}
	memcpy (out_min, self->nodes[0].bmin, 3 * sizeof (gfloat));
	memcpy (out_max, self->nodes[0].bmax, 3 * sizeof (gfloat));
	return TRUE;
}

/* ==========================================================================
 * Traversal
 * ========================================================================== */

/* A ray prepared for slab tests. */
typedef struct
{
	gfloat o[3];
	gfloat d[3];
	gfloat inv[3];
} Ray;

static void
ray_init (Ray          *r,
          const gfloat *origin,
          const gfloat *direction)
{
	gint k;

	for (k = 0; k < 3; k++)
	{
		gfloat d = direction[k];

		r->o[k] = origin[k];
		r->d[k] = d;
		/* a zero component gets a huge finite inverse: no 0 * inf NaN */
		if (fabsf (d) < 1e-20f)
			d = d < 0.0f ? -1e-20f : 1e-20f;
		r->inv[k] = 1.0f / d;
	}
}

/* Entry distance into a node's box within (t_min, t_max), or +inf. */
static inline gfloat
slab (const Ray     *r,
      const BvhNode *n,
      gfloat         t_min,
      gfloat         t_max)
{
	gfloat tx1 = (n->bmin[0] - r->o[0]) * r->inv[0];
	gfloat tx2 = (n->bmax[0] - r->o[0]) * r->inv[0];
	gfloat ty1 = (n->bmin[1] - r->o[1]) * r->inv[1];
	gfloat ty2 = (n->bmax[1] - r->o[1]) * r->inv[1];
	gfloat tz1 = (n->bmin[2] - r->o[2]) * r->inv[2];
	gfloat tz2 = (n->bmax[2] - r->o[2]) * r->inv[2];
	gfloat tn = MAX (MAX (MIN (tx1, tx2), MIN (ty1, ty2)), MAX (MIN (tz1, tz2), t_min));
	gfloat tf = MIN (MIN (MAX (tx1, tx2), MAX (ty1, ty2)), MIN (MAX (tz1, tz2), t_max));

	return tn <= tf ? tn : G_MAXFLOAT;
}

/*
 * Moller-Trumbore on a pre-transformed triangle.  Returns TRUE with
 * t / u / v / det when the ray crosses it inside (t_min, t_max).
 */
static inline gboolean
hit_tri (const Ray    *r,
         const gfloat *tri,
         gfloat        t_min,
         gfloat        t_max,
         gfloat       *out_t,
         gfloat       *out_u,
         gfloat       *out_v,
         gfloat       *out_det)
{
	const gfloat *e1 = tri + 3;
	const gfloat *e2 = tri + 6;
	gfloat p[3];
	gfloat s[3];
	gfloat q[3];
	gfloat det;
	gfloat inv;
	gfloat u;
	gfloat v;
	gfloat t;

	p[0] = r->d[1] * e2[2] - r->d[2] * e2[1];
	p[1] = r->d[2] * e2[0] - r->d[0] * e2[2];
	p[2] = r->d[0] * e2[1] - r->d[1] * e2[0];
	det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
	if (fabsf (det) < 1e-20f)
		return FALSE;
	inv = 1.0f / det;
	s[0] = r->o[0] - tri[0];
	s[1] = r->o[1] - tri[1];
	s[2] = r->o[2] - tri[2];
	u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
	if (u < 0.0f || u > 1.0f)
		return FALSE;
	q[0] = s[1] * e1[2] - s[2] * e1[1];
	q[1] = s[2] * e1[0] - s[0] * e1[2];
	q[2] = s[0] * e1[1] - s[1] * e1[0];
	v = (r->d[0] * q[0] + r->d[1] * q[1] + r->d[2] * q[2]) * inv;
	if (v < 0.0f || u + v > 1.0f)
		return FALSE;
	t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
	if (t <= t_min || t >= t_max)
		return FALSE;
	*out_t = t;
	*out_u = u;
	*out_v = v;
	*out_det = det;
	return TRUE;
}

/*
 * The shared traversal: near child first, far child on the stack.
 * @any stops at the first hit (shadow rays).
 */
static gboolean
traverse (LrgBakeBvh   *self,
          const gfloat *origin,
          const gfloat *direction,
          gfloat        t_min,
          gfloat        t_max,
          gboolean      any,
          LrgBakeHit   *out_hit)
{
	Ray     r;
	guint   stack[BVH_STACK];
	guint   sp = 0;
	guint   ni = 0;
	gfloat  best = t_max;
	gint    best_tri = -1;
	gfloat  best_u = 0.0f;
	gfloat  best_v = 0.0f;
	gfloat  best_det = 0.0f;

	if (self->n_nodes == 0)
		return FALSE;
	ray_init (&r, origin, direction);
	if (slab (&r, &self->nodes[0], t_min, best) == G_MAXFLOAT)
		return FALSE;

	for (;;)
	{
		const BvhNode *n = &self->nodes[ni];

		if (n->count > 0)
		{
			gint i;

			for (i = 0; i < n->count; i++)
			{
				gint   ti = n->first + i;
				gfloat t;
				gfloat u;
				gfloat v;
				gfloat det;

				if (hit_tri (&r, self->tris + (gsize)ti * 9u, t_min, best, &t, &u, &v, &det))
				{
					best = t;
					best_tri = ti;
					best_u = u;
					best_v = v;
					best_det = det;
					if (any)
						goto done;
				}
			}
			if (sp == 0)
				break;
			ni = stack[--sp];
		}
		else
		{
			guint  c0 = (guint)n->first;
			guint  c1 = c0 + 1u;
			gfloat t0 = slab (&r, &self->nodes[c0], t_min, best);
			gfloat t1 = slab (&r, &self->nodes[c1], t_min, best);

			if (t1 < t0)
			{
				gfloat tt = t0;
				guint  tc = c0;

				t0 = t1;
				t1 = tt;
				c0 = c1;
				c1 = tc;
			}
			if (t0 == G_MAXFLOAT)
			{
				if (sp == 0)
					break;
				ni = stack[--sp];
			}
			else
			{
				ni = c0;
				if (t1 != G_MAXFLOAT && sp < BVH_STACK)
					stack[sp++] = c1;
			}
		}
	}

done:
	if (best_tri < 0)
		return FALSE;
	if (out_hit != NULL)
	{
		out_hit->t = best;
		out_hit->triangle = self->ids[best_tri];
		out_hit->u = best_u;
		out_hit->v = best_v;
		/* det < 0 <=> the ray runs along the face normal (e1 x e2) */
		out_hit->backface = best_det < 0.0f;
	}
	return TRUE;
}

gboolean
lrg_bake_bvh_intersect (LrgBakeBvh   *self,
                        const gfloat *origin,
                        const gfloat *direction,
                        gfloat        t_min,
                        gfloat        t_max,
                        LrgBakeHit   *out_hit)
{
	g_return_val_if_fail (LRG_IS_BAKE_BVH (self), FALSE);
	g_return_val_if_fail (origin != NULL && direction != NULL, FALSE);

	return traverse (self, origin, direction, t_min, t_max, FALSE, out_hit);
}

gboolean
lrg_bake_bvh_occluded (LrgBakeBvh   *self,
                       const gfloat *origin,
                       const gfloat *direction,
                       gfloat        t_min,
                       gfloat        t_max)
{
	g_return_val_if_fail (LRG_IS_BAKE_BVH (self), FALSE);
	g_return_val_if_fail (origin != NULL && direction != NULL, FALSE);

	return traverse (self, origin, direction, t_min, t_max, TRUE, NULL);
}
