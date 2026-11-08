/* lrg-broadphase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBroadphase: a dynamic AABB tree (bounding volume hierarchy).
 *
 * The tree scheme (surface-area cost descent for insertion, AVL-style
 * rotations on the way back up, fat leaves re-inserted only when the
 * tight box escapes) is the classic incremental BVH described by Erin
 * Catto for Box2D's b2DynamicTree (MIT). This file is written from the
 * algorithm description, not translated from that source; the layer and
 * "active body" pair rules follow the ideas of Jolt Physics' broadphase
 * (object layers, only active bodies query), without its code.
 *
 * Storage
 * -------
 * Nodes live in one growable array addressed by index; NULL_NODE marks
 * "no node". Free nodes form a stack threaded through the parent field.
 * A leaf points at its proxy, an internal node always has two children
 * and stores the exact union of their (fat) boxes plus its height.
 *
 * Proxies live in a second array addressed by proxy id. A proxy keeps
 * its tight AABB, its leaf node (whose box is the fat AABB), the filter
 * data and the caller id. Ids are the lowest free slot, which keeps the
 * same create/destroy sequence producing the same ids.
 *
 * Determinism: every operation is a pure function of the call sequence
 * (no hashing, no pointers in ordering decisions, ties broken by index),
 * and find_pairs sorts its output by proxy ids.
 */

#include "config.h"

#include "lrg-broadphase.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NULL_NODE (G_MAXUINT)

/* the fat box is re-made when it grows beyond this many margins around
 * a freshly fattened box (a body that once moved fast and then stopped
 * would otherwise keep a huge leaf forever) */
#define SHRINK_MARGINS (4.0f)

/* height difference between siblings above which the height-driven
 * (AVL) rotation runs instead of the area-driven one; see walk_up() */
#define AVL_LIMIT (3)

typedef struct
{
	gfloat min[3];
	gfloat max[3];
	guint  parent;     /* parent node; next free node when on the free list */
	guint  child1;     /* NULL_NODE for a leaf */
	guint  child2;
	gint   height;     /* 0 for a leaf, -1 when free */
	guint  proxy;      /* proxy id of a leaf */
} Node;

typedef struct
{
	gfloat  min[3];    /* tight AABB */
	gfloat  max[3];
	guint   node;      /* leaf node, NULL_NODE when the slot is free */
	guint   layer;
	guint32 mask;
	guint   flags;
	guint   user_id;
} Proxy;

struct _LrgBroadphase
{
	GObject parent_instance;

	Node   *nodes;
	guint   node_capacity;
	guint   node_count;          /* nodes in use */
	guint   free_node;           /* head of the free stack */
	guint   root;

	Proxy  *proxies;
	guint   proxy_capacity;
	guint   proxy_high;          /* one past the highest slot ever used */
	guint   proxy_count;         /* live proxies */
	guint   first_free_proxy;    /* no free slot below this index */

	gfloat  margin;
	gfloat  velocity_factor;
	LrgPhysicsLayers *layers;

	/* scratch, reused between calls to avoid allocation per frame */
	GArray *pairs;               /* guint64 (a << 32 | b) */

	/* counters */
	guint64 moves;
	guint64 reinserts;
	guint64 rotations;
	guint   last_pair_count;
	guint64 last_pair_tests;
};

G_DEFINE_FINAL_TYPE (LrgBroadphase, lrg_broadphase, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_MARGIN,
	PROP_VELOCITY_FACTOR,
	PROP_LAYERS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ------------------------------------------------------------------ */
/* Boxed and flags types                                               */
/* ------------------------------------------------------------------ */

GType
lrg_broadphase_proxy_flags_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_BROADPHASE_PROXY_NONE, "LRG_BROADPHASE_PROXY_NONE", "none" },
			{ LRG_BROADPHASE_PROXY_STATIC, "LRG_BROADPHASE_PROXY_STATIC", "static" },
			{ LRG_BROADPHASE_PROXY_SLEEPING, "LRG_BROADPHASE_PROXY_SLEEPING", "sleeping" },
			{ 0, NULL, NULL }
		};
		GType type = g_flags_register_static (g_intern_static_string ("LrgBroadphaseProxyFlags"), values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

LrgBroadphaseStats *
lrg_broadphase_stats_copy (const LrgBroadphaseStats *stats)
{
	g_return_val_if_fail (stats != NULL, NULL);
	return (LrgBroadphaseStats *) g_memdup2 (stats, sizeof (LrgBroadphaseStats));
}

void
lrg_broadphase_stats_free (LrgBroadphaseStats *stats)
{
	g_free (stats);
}

G_DEFINE_BOXED_TYPE (LrgBroadphaseStats, lrg_broadphase_stats,
                     lrg_broadphase_stats_copy, lrg_broadphase_stats_free)

/* ------------------------------------------------------------------ */
/* Box helpers                                                         */
/* ------------------------------------------------------------------ */

/* box_area: half the surface area of a box; the SAH only compares
 * areas, so the constant factor does not matter */
static inline gfloat
box_area (const gfloat *min,
          const gfloat *max)
{
	gfloat dx = max[0] - min[0];
	gfloat dy = max[1] - min[1];
	gfloat dz = max[2] - min[2];

	return dx * dy + dy * dz + dz * dx;
}

/* union_area: area of the union of two boxes without building it */
static inline gfloat
union_area (const gfloat *amin, const gfloat *amax,
            const gfloat *bmin, const gfloat *bmax)
{
	gfloat lo[3];
	gfloat hi[3];
	guint  k;

	for (k = 0; k < 3; k++)
	{
		lo[k] = MIN (amin[k], bmin[k]);
		hi[k] = MAX (amax[k], bmax[k]);
	}
	return box_area (lo, hi);
}

static inline void
box_union (gfloat       *out_min,
           gfloat       *out_max,
           const gfloat *amin, const gfloat *amax,
           const gfloat *bmin, const gfloat *bmax)
{
	guint k;

	for (k = 0; k < 3; k++)
	{
		out_min[k] = MIN (amin[k], bmin[k]);
		out_max[k] = MAX (amax[k], bmax[k]);
	}
}

/* box_overlap: closed intervals, so touching boxes overlap */
static inline gboolean
box_overlap (const gfloat *amin, const gfloat *amax,
             const gfloat *bmin, const gfloat *bmax)
{
	return amin[0] <= bmax[0] && bmin[0] <= amax[0]
	    && amin[1] <= bmax[1] && bmin[1] <= amax[1]
	    && amin[2] <= bmax[2] && bmin[2] <= amax[2];
}

/* box_contains: TRUE when [imin, imax] lies inside [omin, omax] */
static inline gboolean
box_contains (const gfloat *omin, const gfloat *omax,
              const gfloat *imin, const gfloat *imax)
{
	return omin[0] <= imin[0] && omin[1] <= imin[1] && omin[2] <= imin[2]
	    && imax[0] <= omax[0] && imax[1] <= omax[1] && imax[2] <= omax[2];
}

/* box_valid: finite and not inverted */
static gboolean
box_valid (const gfloat *min,
           const gfloat *max)
{
	guint k;

	if (min == NULL || max == NULL)
		return FALSE;
	for (k = 0; k < 3; k++)
		if (!isfinite (min[k]) || !isfinite (max[k]) || min[k] > max[k])
			return FALSE;
	return TRUE;
}

/* ray_box: slab test of origin + t * dir against a box for t in
 * [0, t_max]. Writes the entry distance (0 when the origin is inside).
 * A zero direction component means the ray is parallel to that slab:
 * it hits only if the origin lies within it. */
static inline gboolean
ray_box (const gfloat *origin,
         const gfloat *dir,
         const gfloat *inv_dir,
         const gfloat *min,
         const gfloat *max,
         gfloat        t_max,
         gfloat       *out_enter)
{
	gfloat t0 = 0.0f;
	gfloat t1 = t_max;
	guint  k;

	for (k = 0; k < 3; k++)
	{
		if (dir[k] == 0.0f)
		{
			if (origin[k] < min[k] || origin[k] > max[k])
				return FALSE;
		}
		else
		{
			gfloat ta = (min[k] - origin[k]) * inv_dir[k];
			gfloat tb = (max[k] - origin[k]) * inv_dir[k];

			if (ta > tb)
			{
				gfloat tmp = ta;

				ta = tb;
				tb = tmp;
			}
			if (ta > t0)
				t0 = ta;
			if (tb < t1)
				t1 = tb;
			if (t0 > t1)
				return FALSE;
		}
	}
	*out_enter = t0;
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* Node pool                                                           */
/* ------------------------------------------------------------------ */

static inline gboolean
node_is_leaf (const Node *n)
{
	return n->child1 == NULL_NODE;
}

/* alloc_node: pops the free stack, growing the array when empty. The
 * node array may move: callers re-fetch pointers afterwards. */
static guint
alloc_node (LrgBroadphase *self)
{
	guint id;
	Node *n;

	if (self->free_node == NULL_NODE)
	{
		guint old = self->node_capacity;
		guint i;

		self->node_capacity = old == 0 ? 64u : old * 2u;
		self->nodes = g_renew (Node, self->nodes, self->node_capacity);
		/* thread the new nodes onto the free stack, lowest index on top */
		for (i = self->node_capacity; i > old; i--)
		{
			self->nodes[i - 1].parent = self->free_node;
			self->nodes[i - 1].height = -1;
			self->free_node = i - 1;
		}
	}

	id = self->free_node;
	n = &self->nodes[id];
	self->free_node = n->parent;
	n->parent = NULL_NODE;
	n->child1 = NULL_NODE;
	n->child2 = NULL_NODE;
	n->height = 0;
	n->proxy = LRG_BROADPHASE_NULL_PROXY;
	self->node_count++;
	return id;
}

static void
free_node (LrgBroadphase *self,
           guint          id)
{
	self->nodes[id].parent = self->free_node;
	self->nodes[id].height = -1;
	self->free_node = id;
	self->node_count--;
}

/* ------------------------------------------------------------------ */
/* Balancing                                                           */
/* ------------------------------------------------------------------ */

/* refit: recomputes the box and height of internal node @i from its
 * children */
static inline void
refit (LrgBroadphase *self,
       guint          i)
{
	Node *n = &self->nodes[i];
	Node *c1 = &self->nodes[n->child1];
	Node *c2 = &self->nodes[n->child2];

	box_union (n->min, n->max, c1->min, c1->max, c2->min, c2->max);
	n->height = 1 + MAX (c1->height, c2->height);
}

/* replace_child: makes @parent (or the root) point at @new_child where
 * it pointed at @old_child */
static inline void
replace_child (LrgBroadphase *self,
               guint          parent,
               guint          old_child,
               guint          new_child)
{
	if (parent == NULL_NODE)
	{
		self->root = new_child;
		return;
	}
	if (self->nodes[parent].child1 == old_child)
		self->nodes[parent].child1 = new_child;
	else
		self->nodes[parent].child2 = new_child;
}

/*
 * balance: one AVL rotation at node @ia when its children's heights
 * differ by more than one. The taller child rises to take @ia's place;
 * @ia keeps the shorter child and the shorter grandchild, the risen
 * child keeps the taller grandchild. Returns the root of the subtree.
 *
 *        A                 C
 *       / \               / \
 *      B   C     ==>     A   F      (F taller than G)
 *         / \           / \
 *        F   G         B   G
 */
static guint
balance (LrgBroadphase *self,
         guint          ia)
{
	Node  *a = &self->nodes[ia];
	guint  ib;
	guint  ic;
	gint   diff;

	if (node_is_leaf (a) || a->height < 2)
		return ia;

	ib = a->child1;
	ic = a->child2;
	diff = self->nodes[ic].height - self->nodes[ib].height;

	if (diff > AVL_LIMIT)
	{
		/* Rotate C up */
		Node  *c = &self->nodes[ic];
		guint  i_f = c->child1;
		guint  i_g = c->child2;

		c->child1 = ia;
		c->parent = a->parent;
		a->parent = ic;
		replace_child (self, c->parent, ia, ic);

		if (self->nodes[i_f].height > self->nodes[i_g].height)
		{
			c->child2 = i_f;
			a->child2 = i_g;
			self->nodes[i_g].parent = ia;
		}
		else
		{
			c->child2 = i_g;
			a->child2 = i_f;
			self->nodes[i_f].parent = ia;
		}
		refit (self, ia);
		refit (self, ic);
		self->rotations++;
		return ic;
	}

	if (diff < -AVL_LIMIT)
	{
		/* Rotate B up (mirror image) */
		Node  *b = &self->nodes[ib];
		guint  i_d = b->child1;
		guint  i_e = b->child2;

		b->child1 = ia;
		b->parent = a->parent;
		a->parent = ib;
		replace_child (self, b->parent, ia, ib);

		if (self->nodes[i_d].height > self->nodes[i_e].height)
		{
			b->child2 = i_d;
			a->child1 = i_e;
			self->nodes[i_e].parent = ia;
		}
		else
		{
			b->child2 = i_e;
			a->child1 = i_d;
			self->nodes[i_d].parent = ia;
		}
		refit (self, ia);
		refit (self, ib);
		self->rotations++;
		return ib;
	}

	return ia;
}

/* swap_children: exchanges subtree @x (child of @px) and subtree @y
 * (child of @py) */
static void
swap_subtrees (LrgBroadphase *self,
               guint          px,
               guint          x,
               guint          py,
               guint          y)
{
	if (self->nodes[px].child1 == x)
		self->nodes[px].child1 = y;
	else
		self->nodes[px].child2 = y;
	if (self->nodes[py].child1 == y)
		self->nodes[py].child1 = x;
	else
		self->nodes[py].child2 = x;
	self->nodes[x].parent = py;
	self->nodes[y].parent = px;
}

/*
 * rotate_sah: area-driven tree rotation at node @ia (after Kopta et
 * al., "Fast, Effective BVH Updates for Animated Scenes", 2012).
 *
 * With children B and C and grandchildren D, E (under B) and F, G
 * (under C), six swaps keep @ia's subtree the same set of leaves but
 * change the boxes of B and/or C:
 *
 *   B<->F, B<->G   (C changes)     C<->D, C<->E   (B changes)
 *   D<->F, D<->G   (both change)
 *
 * The swap that shrinks the summed surface area of the changed nodes
 * the most is applied; no swap is made when none shrinks it. @ia's own
 * box is unchanged by any of them. Candidates are tried in a fixed
 * order and only a strictly better one replaces the current best, so
 * the choice is deterministic.
 */
static void
rotate_sah (LrgBroadphase *self,
            guint          ia)
{
	Node   *a = &self->nodes[ia];
	guint   ib;
	guint   ic;
	Node   *b;
	Node   *c;
	gfloat  best = 0.0f;
	gint    choice = -1;
	guint   id = NULL_NODE;
	guint   ie = NULL_NODE;
	guint   i_f = NULL_NODE;
	guint   i_g = NULL_NODE;

	if (node_is_leaf (a) || a->height < 2)
		return;

	ib = a->child1;
	ic = a->child2;
	b = &self->nodes[ib];
	c = &self->nodes[ic];

	/* B<->F / B<->G: C becomes {B, G} or {F, B} */
	if (!node_is_leaf (c))
	{
		gfloat area_c = box_area (c->min, c->max);
		Node  *f;
		Node  *g;
		gfloat cost;

		i_f = c->child1;
		i_g = c->child2;
		f = &self->nodes[i_f];
		g = &self->nodes[i_g];
		cost = union_area (b->min, b->max, g->min, g->max) - area_c;
		if (cost < best)
		{
			best = cost;
			choice = 0;
		}
		cost = union_area (b->min, b->max, f->min, f->max) - area_c;
		if (cost < best)
		{
			best = cost;
			choice = 1;
		}
	}

	/* C<->D / C<->E: B becomes {C, E} or {D, C} */
	if (!node_is_leaf (b))
	{
		gfloat area_b = box_area (b->min, b->max);
		Node  *d;
		Node  *e;
		gfloat cost;

		id = b->child1;
		ie = b->child2;
		d = &self->nodes[id];
		e = &self->nodes[ie];
		cost = union_area (c->min, c->max, e->min, e->max) - area_b;
		if (cost < best)
		{
			best = cost;
			choice = 2;
		}
		cost = union_area (c->min, c->max, d->min, d->max) - area_b;
		if (cost < best)
		{
			best = cost;
			choice = 3;
		}

		/* D<->F / D<->G: B becomes {F|G, E}, C becomes {D, G|F} */
		if (!node_is_leaf (c))
		{
			gfloat base = area_b + box_area (c->min, c->max);
			Node  *f = &self->nodes[i_f];
			Node  *g = &self->nodes[i_g];

			cost = union_area (f->min, f->max, e->min, e->max)
			     + union_area (d->min, d->max, g->min, g->max) - base;
			if (cost < best)
			{
				best = cost;
				choice = 4;
			}
			cost = union_area (g->min, g->max, e->min, e->max)
			     + union_area (d->min, d->max, f->min, f->max) - base;
			if (cost < best)
			{
				best = cost;
				choice = 5;
			}
		}
	}

	switch (choice)
	{
	case 0:
		swap_subtrees (self, ia, ib, ic, i_f);
		refit (self, ic);
		break;
	case 1:
		swap_subtrees (self, ia, ib, ic, i_g);
		refit (self, ic);
		break;
	case 2:
		swap_subtrees (self, ia, ic, ib, id);
		refit (self, ib);
		break;
	case 3:
		swap_subtrees (self, ia, ic, ib, ie);
		refit (self, ib);
		break;
	case 4:
		swap_subtrees (self, ib, id, ic, i_f);
		refit (self, ib);
		refit (self, ic);
		break;
	case 5:
		swap_subtrees (self, ib, id, ic, i_g);
		refit (self, ib);
		refit (self, ic);
		break;
	default:
		return;
	}
	refit (self, ia);
	self->rotations++;
}

/*
 * walk_up: refits every ancestor from @i to the root and rotates it.
 * Area-driven rotations keep the tree tight (cheap queries); when the
 * heights of two siblings drift apart by more than AVL_LIMIT the
 * height-driven rotation runs instead, which bounds the height even
 * for adversarial insertion orders (e.g. boxes sorted along a line).
 */
static void
walk_up (LrgBroadphase *self,
         guint          i)
{
	while (i != NULL_NODE)
	{
		Node *n = &self->nodes[i];

		refit (self, i);
		if (!node_is_leaf (n))
		{
			gint diff = self->nodes[n->child2].height - self->nodes[n->child1].height;

			if (diff > AVL_LIMIT || diff < -AVL_LIMIT)
				i = balance (self, i);
			else
				rotate_sah (self, i);
		}
		refit (self, i);
		i = self->nodes[i].parent;
	}
}

/* ------------------------------------------------------------------ */
/* Insert / remove                                                     */
/* ------------------------------------------------------------------ */

/*
 * insert_leaf: links leaf @leaf into the tree.
 *
 * Sibling search: descend from the root, at each internal node compare
 * the cost of making the leaf a sibling of this whole subtree (a new
 * parent whose area is the union) against descending into either
 * child. Descending costs the area growth it causes on this node
 * ("inheritance", paid by every ancestor) plus the cost at the child.
 * Stop as soon as stopping here is cheapest. Ties go to child1, so the
 * walk is deterministic.
 */
static void
insert_leaf (LrgBroadphase *self,
             guint          leaf)
{
	gfloat lmin[3];
	gfloat lmax[3];
	guint  index;
	guint  sibling;
	guint  old_parent;
	guint  new_parent;

	if (self->root == NULL_NODE)
	{
		self->root = leaf;
		self->nodes[leaf].parent = NULL_NODE;
		return;
	}

	memcpy (lmin, self->nodes[leaf].min, sizeof (lmin));
	memcpy (lmax, self->nodes[leaf].max, sizeof (lmax));

	/* Find the best sibling */
	index = self->root;
	while (!node_is_leaf (&self->nodes[index]))
	{
		const Node *n = &self->nodes[index];
		const Node *c1 = &self->nodes[n->child1];
		const Node *c2 = &self->nodes[n->child2];
		gfloat      area = box_area (n->min, n->max);
		gfloat      combined = union_area (n->min, n->max, lmin, lmax);
		gfloat      cost = 2.0f * combined;
		gfloat      inheritance = 2.0f * (combined - area);
		gfloat      cost1;
		gfloat      cost2;

		cost1 = union_area (c1->min, c1->max, lmin, lmax) + inheritance;
		if (!node_is_leaf (c1))
			cost1 -= box_area (c1->min, c1->max);
		cost2 = union_area (c2->min, c2->max, lmin, lmax) + inheritance;
		if (!node_is_leaf (c2))
			cost2 -= box_area (c2->min, c2->max);

		if (cost < cost1 && cost < cost2)
			break;
		index = cost1 <= cost2 ? n->child1 : n->child2;
	}
	sibling = index;

	/* New parent above the sibling (alloc may move the array) */
	new_parent = alloc_node (self);
	old_parent = self->nodes[sibling].parent;
	self->nodes[new_parent].parent = old_parent;
	self->nodes[new_parent].child1 = sibling;
	self->nodes[new_parent].child2 = leaf;
	self->nodes[sibling].parent = new_parent;
	self->nodes[leaf].parent = new_parent;
	replace_child (self, old_parent, sibling, new_parent);
	refit (self, new_parent);

	/* Refit and rebalance the ancestors */
	walk_up (self, self->nodes[new_parent].parent);
}

/* remove_leaf: unlinks @leaf; its parent is freed and the sibling takes
 * the parent's place */
static void
remove_leaf (LrgBroadphase *self,
             guint          leaf)
{
	guint parent;
	guint grand;
	guint sibling;

	if (leaf == self->root)
	{
		self->root = NULL_NODE;
		return;
	}

	parent = self->nodes[leaf].parent;
	grand = self->nodes[parent].parent;
	sibling = self->nodes[parent].child1 == leaf ? self->nodes[parent].child2
	                                             : self->nodes[parent].child1;

	replace_child (self, grand, parent, sibling);
	self->nodes[sibling].parent = grand;
	free_node (self, parent);
	self->nodes[leaf].parent = NULL_NODE;

	walk_up (self, grand);
}

/* fatten: computes the fat box of a proxy from its tight box, the
 * margin (none for static proxies) and the stretched displacement */
static void
fatten (LrgBroadphase *self,
        const Proxy   *p,
        const gfloat  *displacement,
        gfloat        *out_min,
        gfloat        *out_max)
{
	gfloat m = (p->flags & LRG_BROADPHASE_PROXY_STATIC) != 0 ? 0.0f : self->margin;
	guint  k;

	for (k = 0; k < 3; k++)
	{
		out_min[k] = p->min[k] - m;
		out_max[k] = p->max[k] + m;
		if (displacement != NULL && isfinite (displacement[k]))
		{
			gfloat d = displacement[k] * self->velocity_factor;

			if (d < 0.0f)
				out_min[k] += d;
			else
				out_max[k] += d;
		}
	}
}

/* ------------------------------------------------------------------ */
/* GObject                                                             */
/* ------------------------------------------------------------------ */

static void
lrg_broadphase_finalize (GObject *object)
{
	LrgBroadphase *self = LRG_BROADPHASE (object);

	g_clear_pointer (&self->nodes, g_free);
	g_clear_pointer (&self->proxies, g_free);
	g_clear_pointer (&self->pairs, g_array_unref);
	g_clear_object (&self->layers);

	G_OBJECT_CLASS (lrg_broadphase_parent_class)->finalize (object);
}

static void
lrg_broadphase_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgBroadphase *self = LRG_BROADPHASE (object);

	switch (prop_id)
	{
	case PROP_MARGIN:
		g_value_set_float (value, self->margin);
		break;
	case PROP_VELOCITY_FACTOR:
		g_value_set_float (value, self->velocity_factor);
		break;
	case PROP_LAYERS:
		g_value_set_object (value, self->layers);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_broadphase_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgBroadphase *self = LRG_BROADPHASE (object);

	switch (prop_id)
	{
	case PROP_MARGIN:
		lrg_broadphase_set_margin (self, g_value_get_float (value));
		break;
	case PROP_VELOCITY_FACTOR:
		lrg_broadphase_set_velocity_factor (self, g_value_get_float (value));
		break;
	case PROP_LAYERS:
		lrg_broadphase_set_layers (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_broadphase_class_init (LrgBroadphaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_broadphase_finalize;
	object_class->get_property = lrg_broadphase_get_property;
	object_class->set_property = lrg_broadphase_set_property;

	/**
	 * LrgBroadphase:margin:
	 *
	 * Fat AABB margin in world units added on every side of a dynamic
	 * proxy when it (re)inserts. Static proxies get no margin.
	 */
	properties[PROP_MARGIN] =
		g_param_spec_float ("margin", NULL, NULL,
		                    0.0f, G_MAXFLOAT, LRG_BROADPHASE_DEFAULT_MARGIN,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBroadphase:velocity-factor:
	 *
	 * Multiplier on the displacement given to lrg_broadphase_move_proxy()
	 * by which a re-inserted fat AABB is stretched ahead of the body.
	 */
	properties[PROP_VELOCITY_FACTOR] =
		g_param_spec_float ("velocity-factor", NULL, NULL,
		                    0.0f, G_MAXFLOAT, 2.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBroadphase:layers:
	 *
	 * The collide matrix every pair must pass, or %NULL for none.
	 */
	properties[PROP_LAYERS] =
		g_param_spec_object ("layers", NULL, NULL,
		                     LRG_TYPE_PHYSICS_LAYERS,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_broadphase_init (LrgBroadphase *self)
{
	self->root = NULL_NODE;
	self->free_node = NULL_NODE;
	self->margin = LRG_BROADPHASE_DEFAULT_MARGIN;
	self->velocity_factor = 2.0f;
	self->pairs = g_array_sized_new (FALSE, FALSE, sizeof (guint64), 256);
}

LrgBroadphase *
lrg_broadphase_new (void)
{
	return g_object_new (LRG_TYPE_BROADPHASE, NULL);
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

void
lrg_broadphase_set_margin (LrgBroadphase *self,
                           gfloat         margin)
{
	g_return_if_fail (LRG_IS_BROADPHASE (self));
	g_return_if_fail (isfinite (margin) && margin >= 0.0f);

	if (self->margin == margin)
		return;
	self->margin = margin;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN]);
}

gfloat
lrg_broadphase_get_margin (LrgBroadphase *self)
{
	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0.0f);
	return self->margin;
}

void
lrg_broadphase_set_velocity_factor (LrgBroadphase *self,
                                    gfloat         factor)
{
	g_return_if_fail (LRG_IS_BROADPHASE (self));
	g_return_if_fail (isfinite (factor) && factor >= 0.0f);

	if (self->velocity_factor == factor)
		return;
	self->velocity_factor = factor;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VELOCITY_FACTOR]);
}

gfloat
lrg_broadphase_get_velocity_factor (LrgBroadphase *self)
{
	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0.0f);
	return self->velocity_factor;
}

void
lrg_broadphase_set_layers (LrgBroadphase    *self,
                           LrgPhysicsLayers *layers)
{
	g_return_if_fail (LRG_IS_BROADPHASE (self));
	g_return_if_fail (layers == NULL || LRG_IS_PHYSICS_LAYERS (layers));

	if (g_set_object (&self->layers, layers))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYERS]);
}

LrgPhysicsLayers *
lrg_broadphase_get_layers (LrgBroadphase *self)
{
	g_return_val_if_fail (LRG_IS_BROADPHASE (self), NULL);
	return self->layers;
}

/* ------------------------------------------------------------------ */
/* Proxies                                                             */
/* ------------------------------------------------------------------ */

/* live_proxy: the proxy for @id, or NULL when the slot is free */
static inline Proxy *
live_proxy (LrgBroadphase *self,
            guint          id)
{
	if (id >= self->proxy_high || self->proxies[id].node == NULL_NODE)
		return NULL;
	return &self->proxies[id];
}

guint
lrg_broadphase_create_proxy (LrgBroadphase           *self,
                             const gfloat            *min,
                             const gfloat            *max,
                             guint                    layer,
                             guint32                  mask,
                             LrgBroadphaseProxyFlags  flags,
                             guint                    user_id)
{
	Proxy *p;
	guint  id;
	guint  leaf;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), LRG_BROADPHASE_NULL_PROXY);

	if (!box_valid (min, max) || layer >= LRG_PHYSICS_LAYERS_MAX)
		return LRG_BROADPHASE_NULL_PROXY;

	/* Lowest free slot: scan up from the hint (no free slot below it) */
	for (id = self->first_free_proxy; id < self->proxy_high; id++)
		if (self->proxies[id].node == NULL_NODE)
			break;
	if (id == self->proxy_high)
	{
		if (self->proxy_high == self->proxy_capacity)
		{
			self->proxy_capacity = self->proxy_capacity == 0 ? 64u : self->proxy_capacity * 2u;
			self->proxies = g_renew (Proxy, self->proxies, self->proxy_capacity);
		}
		self->proxy_high++;
	}
	self->first_free_proxy = id + 1;

	p = &self->proxies[id];
	memcpy (p->min, min, sizeof (p->min));
	memcpy (p->max, max, sizeof (p->max));
	p->layer = layer;
	p->mask = mask;
	p->flags = (guint) flags;
	p->user_id = user_id;

	leaf = alloc_node (self);
	self->nodes[leaf].proxy = id;
	fatten (self, p, NULL, self->nodes[leaf].min, self->nodes[leaf].max);
	p->node = leaf;
	insert_leaf (self, leaf);
	self->proxy_count++;

	return id;
}

gboolean
lrg_broadphase_destroy_proxy (LrgBroadphase *self,
                              guint          proxy)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);

	p = live_proxy (self, proxy);
	if (p == NULL)
		return FALSE;

	remove_leaf (self, p->node);
	free_node (self, p->node);
	p->node = NULL_NODE;
	self->proxy_count--;
	if (proxy < self->first_free_proxy)
		self->first_free_proxy = proxy;
	/* trim the high-water mark so ids restart low after a mass destroy */
	while (self->proxy_high > 0 && self->proxies[self->proxy_high - 1].node == NULL_NODE)
		self->proxy_high--;
	if (self->first_free_proxy > self->proxy_high)
		self->first_free_proxy = self->proxy_high;
	return TRUE;
}

gboolean
lrg_broadphase_move_proxy (LrgBroadphase *self,
                           guint          proxy,
                           const gfloat  *min,
                           const gfloat  *max,
                           const gfloat  *displacement)
{
	Proxy  *p;
	Node   *leaf;
	gfloat  fmin[3];
	gfloat  fmax[3];

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);

	p = live_proxy (self, proxy);
	if (p == NULL || !box_valid (min, max))
		return FALSE;

	memcpy (p->min, min, sizeof (p->min));
	memcpy (p->max, max, sizeof (p->max));
	self->moves++;

	/* Fast path: the tight box is still inside the fat leaf, and the
	 * fat leaf is not grossly oversized for the current motion */
	leaf = &self->nodes[p->node];
	if (box_contains (leaf->min, leaf->max, p->min, p->max))
	{
		gfloat hmin[3];
		gfloat hmax[3];
		guint  k;

		fatten (self, p, displacement, fmin, fmax);
		for (k = 0; k < 3; k++)
		{
			hmin[k] = fmin[k] - SHRINK_MARGINS * self->margin;
			hmax[k] = fmax[k] + SHRINK_MARGINS * self->margin;
		}
		if (box_contains (hmin, hmax, leaf->min, leaf->max))
			return FALSE;
	}
	else
		fatten (self, p, displacement, fmin, fmax);

	/* Slow path: re-insert with a fresh fat box */
	remove_leaf (self, p->node);
	leaf = &self->nodes[p->node];
	memcpy (leaf->min, fmin, sizeof (fmin));
	memcpy (leaf->max, fmax, sizeof (fmax));
	insert_leaf (self, p->node);
	self->reinserts++;
	return TRUE;
}

gboolean
lrg_broadphase_is_proxy_valid (LrgBroadphase *self,
                               guint          proxy)
{
	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);
	return live_proxy (self, proxy) != NULL;
}

guint
lrg_broadphase_get_proxy_count (LrgBroadphase *self)
{
	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);
	return self->proxy_count;
}

gboolean
lrg_broadphase_set_proxy_layer (LrgBroadphase *self,
                                guint          proxy,
                                guint          layer,
                                guint32        mask)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);

	p = live_proxy (self, proxy);
	if (p == NULL || layer >= LRG_PHYSICS_LAYERS_MAX)
		return FALSE;
	p->layer = layer;
	p->mask = mask;
	return TRUE;
}

guint
lrg_broadphase_get_proxy_layer (LrgBroadphase *self,
                                guint          proxy)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);
	p = live_proxy (self, proxy);
	return p != NULL ? p->layer : 0;
}

guint32
lrg_broadphase_get_proxy_mask (LrgBroadphase *self,
                               guint          proxy)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);
	p = live_proxy (self, proxy);
	return p != NULL ? p->mask : 0;
}

gboolean
lrg_broadphase_set_proxy_flags (LrgBroadphase           *self,
                                guint                    proxy,
                                LrgBroadphaseProxyFlags  flags)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);
	p = live_proxy (self, proxy);
	if (p == NULL)
		return FALSE;
	p->flags = (guint) flags;
	return TRUE;
}

LrgBroadphaseProxyFlags
lrg_broadphase_get_proxy_flags (LrgBroadphase *self,
                                guint          proxy)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), LRG_BROADPHASE_PROXY_NONE);
	p = live_proxy (self, proxy);
	return p != NULL ? (LrgBroadphaseProxyFlags) p->flags : LRG_BROADPHASE_PROXY_NONE;
}

guint
lrg_broadphase_get_proxy_user_id (LrgBroadphase *self,
                                  guint          proxy)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), G_MAXUINT);
	p = live_proxy (self, proxy);
	return p != NULL ? p->user_id : G_MAXUINT;
}

gboolean
lrg_broadphase_set_proxy_user_id (LrgBroadphase *self,
                                  guint          proxy,
                                  guint          user_id)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);
	p = live_proxy (self, proxy);
	if (p == NULL)
		return FALSE;
	p->user_id = user_id;
	return TRUE;
}

gboolean
lrg_broadphase_get_proxy_aabb (LrgBroadphase *self,
                               guint          proxy,
                               gfloat        *out_min,
                               gfloat        *out_max)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

	p = live_proxy (self, proxy);
	if (p == NULL)
		return FALSE;
	memcpy (out_min, p->min, sizeof (p->min));
	memcpy (out_max, p->max, sizeof (p->max));
	return TRUE;
}

gboolean
lrg_broadphase_get_fat_aabb (LrgBroadphase *self,
                             guint          proxy,
                             gfloat        *out_min,
                             gfloat        *out_max)
{
	Proxy *p;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

	p = live_proxy (self, proxy);
	if (p == NULL)
		return FALSE;
	memcpy (out_min, self->nodes[p->node].min, 3 * sizeof (gfloat));
	memcpy (out_max, self->nodes[p->node].max, 3 * sizeof (gfloat));
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* Pair filter and pair search                                         */
/* ------------------------------------------------------------------ */

/* pair_allowed: both masks accept the other's layer and the matrix row
 * (snapshot in @rows, all ones without a matrix) allows the pair */
static inline gboolean
pair_allowed (const Proxy   *a,
              const Proxy   *b,
              const guint32 *rows)
{
	return (a->mask & LRG_PHYSICS_LAYER_BIT (b->layer)) != 0
	    && (b->mask & LRG_PHYSICS_LAYER_BIT (a->layer)) != 0
	    && (rows[a->layer] & LRG_PHYSICS_LAYER_BIT (b->layer)) != 0;
}

/* snapshot_rows: copies the matrix rows once per search */
static void
snapshot_rows (LrgBroadphase *self,
               guint32       *rows)
{
	guint i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		rows[i] = self->layers != NULL
		          ? lrg_physics_layers_get_collide_mask (self->layers, i)
		          : LRG_PHYSICS_LAYERS_ALL;
}

gboolean
lrg_broadphase_layers_allow (LrgBroadphase *self,
                             guint          proxy_a,
                             guint          proxy_b)
{
	Proxy *a;
	Proxy *b;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);

	a = live_proxy (self, proxy_a);
	b = live_proxy (self, proxy_b);
	if (a == NULL || b == NULL)
		return FALSE;
	return lrg_physics_layers_should_collide (self->layers, a->layer, a->mask, b->layer, b->mask);
}

static inline gboolean
proxy_active (const Proxy *p)
{
	return (p->flags & (LRG_BROADPHASE_PROXY_STATIC | LRG_BROADPHASE_PROXY_SLEEPING)) == 0;
}

static gint
compare_pair (gconstpointer a,
              gconstpointer b)
{
	guint64 x = *(const guint64 *) a;
	guint64 y = *(const guint64 *) b;

	return x < y ? -1 : (x > y ? 1 : 0);
}

/* NodeStack: the traversal stack. It starts in a fixed local buffer
 * (enough for any tree of height < 60) and only moves to the heap for
 * deeper trees, so a query does not allocate. Each traversal owns its
 * stack, so callbacks may run nested queries. */
typedef struct
{
	guint *data;
	guint  len;
	guint  cap;
	guint  local[128];
} NodeStack;

static inline void
stack_init (NodeStack *s)
{
	s->data = s->local;
	s->len = 0;
	s->cap = G_N_ELEMENTS (s->local);
}

static void
stack_grow (NodeStack *s)
{
	guint cap = s->cap * 2u;

	if (s->data == s->local)
	{
		s->data = g_new (guint, cap);
		memcpy (s->data, s->local, sizeof (s->local));
	}
	else
		s->data = g_renew (guint, s->data, cap);
	s->cap = cap;
}

static inline void
stack_push (NodeStack *s,
            guint      v)
{
	if (G_UNLIKELY (s->len == s->cap))
		stack_grow (s);
	s->data[s->len++] = v;
}

static inline guint
stack_pop (NodeStack *s)
{
	return s->data[--s->len];
}

static inline void
stack_free (NodeStack *s)
{
	if (s->data != s->local)
		g_free (s->data);
	s->data = s->local;
}

/*
 * find_pairs: every active proxy queries the tree with its tight box.
 * A pair of two active proxies is found from both sides; it is kept only
 * from the lower id. A pair of an active and an inactive proxy is only
 * ever found from the active side. So each pair is collected once, and
 * the sort gives the final order.
 */
guint
lrg_broadphase_find_pairs (LrgBroadphase         *self,
                           LrgBroadphasePairFunc  func,
                           gpointer               user_data)
{
	guint32 rows[LRG_PHYSICS_LAYERS_MAX];
	NodeStack stack;
	guint64 tests = 0;
	guint   i;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);

	snapshot_rows (self, rows);
	g_array_set_size (self->pairs, 0);
	stack_init (&stack);

	for (i = 0; i < self->proxy_high && self->root != NULL_NODE; i++)
	{
		const Proxy *pi = &self->proxies[i];

		if (pi->node == NULL_NODE || !proxy_active (pi))
			continue;

		stack.len = 0;
		stack_push (&stack, self->root);
		while (stack.len > 0)
		{
			guint        ni = stack_pop (&stack);
			const Node  *n = &self->nodes[ni];
			const Proxy *pj;
			guint        j;

			if (!box_overlap (n->min, n->max, pi->min, pi->max))
				continue;
			if (!node_is_leaf (n))
			{
				stack_push (&stack, n->child2);
				stack_push (&stack, n->child1);
				continue;
			}

			j = n->proxy;
			if (j == i)
				continue;
			pj = &self->proxies[j];
			if (proxy_active (pj) && j < i)
				continue;   /* already found from j */
			tests++;
			if (!box_overlap (pi->min, pi->max, pj->min, pj->max))
				continue;
			if (!pair_allowed (pi, pj, rows))
				continue;
			{
				guint64 key = i < j ? (((guint64) i << 32) | j) : (((guint64) j << 32) | i);

				g_array_append_val (self->pairs, key);
			}
		}
	}
	stack_free (&stack);

	g_array_sort (self->pairs, compare_pair);
	self->last_pair_count = self->pairs->len;
	self->last_pair_tests = tests;

	if (func != NULL)
	{
		for (i = 0; i < self->pairs->len; i++)
		{
			guint64 key = g_array_index (self->pairs, guint64, i);
			guint   a = (guint) (key >> 32);
			guint   b = (guint) (key & 0xFFFFFFFFu);

			func (a, self->proxies[a].user_id, b, self->proxies[b].user_id, user_data);
		}
	}
	return self->pairs->len;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

/* Every overlap query below shares one traversal; the volume test is a
 * small tagged union so internal nodes (fat boxes) and leaves (tight
 * boxes) use the same predicate. */
typedef enum
{
	VOLUME_BOX,
	VOLUME_SPHERE,
	VOLUME_FRUSTUM
} VolumeKind;

typedef struct
{
	VolumeKind    kind;
	const gfloat *min;
	const gfloat *max;
	const gfloat *center;
	gfloat        radius_sq;
	const gfloat *planes;
} Volume;

static gboolean
volume_test (const Volume *v,
             const gfloat *min,
             const gfloat *max)
{
	guint k;

	switch (v->kind)
	{
	case VOLUME_BOX:
		return box_overlap (v->min, v->max, min, max);

	case VOLUME_SPHERE:
		{
			gfloat d2 = 0.0f;

			/* squared distance from the center to the box */
			for (k = 0; k < 3; k++)
			{
				gfloat c = v->center[k];
				gfloat e = 0.0f;

				if (c < min[k])
					e = min[k] - c;
				else if (c > max[k])
					e = c - max[k];
				d2 += e * e;
			}
			return d2 <= v->radius_sq;
		}

	case VOLUME_FRUSTUM:
		{
			guint p;

			/* outside when the box corner furthest along a plane
			 * normal (the "positive vertex") is behind that plane */
			for (p = 0; p < 6; p++)
			{
				const gfloat *pl = v->planes + p * 4;
				gfloat        px = pl[0] >= 0.0f ? max[0] : min[0];
				gfloat        py = pl[1] >= 0.0f ? max[1] : min[1];
				gfloat        pz = pl[2] >= 0.0f ? max[2] : min[2];

				if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.0f)
					return FALSE;
			}
			return TRUE;
		}

	default:
		return FALSE;
	}
}

static guint
query_volume (LrgBroadphase          *self,
              const Volume           *v,
              guint32                 layer_mask,
              LrgBroadphaseQueryFunc  func,
              gpointer                user_data)
{
	NodeStack stack;
	guint             hits = 0;

	if (self->root == NULL_NODE)
		return 0;

	/* A private stack: the callback may legally run another query */
	stack_init (&stack);
	stack_push (&stack, self->root);
	while (stack.len > 0)
	{
		guint        ni = stack_pop (&stack);
		const Node  *n = &self->nodes[ni];
		const Proxy *p;

		if (!volume_test (v, n->min, n->max))
			continue;
		if (!node_is_leaf (n))
		{
			stack_push (&stack, n->child2);
			stack_push (&stack, n->child1);
			continue;
		}
		p = &self->proxies[n->proxy];
		if ((layer_mask & LRG_PHYSICS_LAYER_BIT (p->layer)) == 0)
			continue;
		if (!volume_test (v, p->min, p->max))
			continue;
		hits++;
		if (func != NULL && !func (n->proxy, p->user_id, user_data))
			break;
	}
	stack_free (&stack);
	return hits;
}

guint
lrg_broadphase_query_aabb (LrgBroadphase          *self,
                           const gfloat           *min,
                           const gfloat           *max,
                           guint32                 layer_mask,
                           LrgBroadphaseQueryFunc  func,
                           gpointer                user_data)
{
	Volume v = { VOLUME_BOX, NULL, NULL, NULL, 0.0f, NULL };

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);
	g_return_val_if_fail (min != NULL && max != NULL, 0);

	v.min = min;
	v.max = max;
	return query_volume (self, &v, layer_mask, func, user_data);
}

guint
lrg_broadphase_query_sphere (LrgBroadphase          *self,
                             const gfloat           *center,
                             gfloat                  radius,
                             guint32                 layer_mask,
                             LrgBroadphaseQueryFunc  func,
                             gpointer                user_data)
{
	Volume v = { VOLUME_SPHERE, NULL, NULL, NULL, 0.0f, NULL };

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);
	g_return_val_if_fail (center != NULL, 0);
	g_return_val_if_fail (radius >= 0.0f, 0);

	v.center = center;
	v.radius_sq = radius * radius;
	return query_volume (self, &v, layer_mask, func, user_data);
}

guint
lrg_broadphase_query_frustum (LrgBroadphase          *self,
                              const gfloat           *planes,
                              guint32                 layer_mask,
                              LrgBroadphaseQueryFunc  func,
                              gpointer                user_data)
{
	Volume v = { VOLUME_FRUSTUM, NULL, NULL, NULL, 0.0f, NULL };

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);
	g_return_val_if_fail (planes != NULL, 0);

	v.planes = planes;
	return query_volume (self, &v, layer_mask, func, user_data);
}

/*
 * cast_core: ray traversal against boxes grown by @half (0 for a ray,
 * the half extents of the moving box for a box cast: sweeping a box
 * against a box equals casting its center against the Minkowski sum).
 * The maximum distance shrinks as the callback reports closer hits, so
 * subtrees beyond it are pruned.
 */
static gfloat
cast_core (LrgBroadphase        *self,
           const gfloat         *origin,
           const gfloat         *direction,
           const gfloat         *half,
           gfloat                max_distance,
           guint32               layer_mask,
           LrgBroadphaseRayFunc  func,
           gpointer              user_data)
{
	NodeStack stack;
	gfloat            inv[3];
	gfloat            t_max = max_distance;
	guint             k;

	if (self->root == NULL_NODE || !(max_distance > 0.0f))
		return max_distance;
	for (k = 0; k < 3; k++)
		inv[k] = direction[k] != 0.0f ? 1.0f / direction[k] : 0.0f;

	stack_init (&stack);
	stack_push (&stack, self->root);
	while (stack.len > 0)
	{
		guint        ni = stack_pop (&stack);
		const Node  *n = &self->nodes[ni];
		const Proxy *p;
		gfloat       bmin[3];
		gfloat       bmax[3];
		gfloat       t_enter;
		gfloat       next;

		for (k = 0; k < 3; k++)
		{
			bmin[k] = n->min[k] - half[k];
			bmax[k] = n->max[k] + half[k];
		}
		if (!ray_box (origin, direction, inv, bmin, bmax, t_max, &t_enter))
			continue;
		if (!node_is_leaf (n))
		{
			stack_push (&stack, n->child2);
			stack_push (&stack, n->child1);
			continue;
		}

		p = &self->proxies[n->proxy];
		if ((layer_mask & LRG_PHYSICS_LAYER_BIT (p->layer)) == 0)
			continue;
		for (k = 0; k < 3; k++)
		{
			bmin[k] = p->min[k] - half[k];
			bmax[k] = p->max[k] + half[k];
		}
		if (!ray_box (origin, direction, inv, bmin, bmax, t_max, &t_enter))
			continue;
		if (func == NULL)
			continue;
		next = func (n->proxy, p->user_id, t_enter, user_data);
		if (!(next > 0.0f))
		{
			t_max = next;
			break;
		}
		if (next < t_max)
			t_max = next;
	}
	stack_free (&stack);
	return t_max;
}

gfloat
lrg_broadphase_cast_ray (LrgBroadphase        *self,
                         const gfloat         *origin,
                         const gfloat         *direction,
                         gfloat                max_distance,
                         guint32               layer_mask,
                         LrgBroadphaseRayFunc  func,
                         gpointer              user_data)
{
	static const gfloat zero[3] = { 0.0f, 0.0f, 0.0f };

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0.0f);
	g_return_val_if_fail (origin != NULL && direction != NULL, 0.0f);

	return cast_core (self, origin, direction, zero, max_distance, layer_mask, func, user_data);
}

gfloat
lrg_broadphase_cast_aabb (LrgBroadphase        *self,
                          const gfloat         *min,
                          const gfloat         *max,
                          const gfloat         *direction,
                          gfloat                max_distance,
                          guint32               layer_mask,
                          LrgBroadphaseRayFunc  func,
                          gpointer              user_data)
{
	gfloat center[3];
	gfloat half[3];
	guint  k;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0.0f);
	g_return_val_if_fail (min != NULL && max != NULL && direction != NULL, 0.0f);

	for (k = 0; k < 3; k++)
	{
		center[k] = 0.5f * (min[k] + max[k]);
		half[k] = 0.5f * (max[k] - min[k]);
	}
	return cast_core (self, center, direction, half, max_distance, layer_mask, func, user_data);
}

/* ------------------------------------------------------------------ */
/* Inspection                                                          */
/* ------------------------------------------------------------------ */

void
lrg_broadphase_foreach_node (LrgBroadphase         *self,
                             LrgBroadphaseNodeFunc  func,
                             gpointer               user_data)
{
	NodeStack stack;

	g_return_if_fail (LRG_IS_BROADPHASE (self));
	g_return_if_fail (func != NULL);

	if (self->root == NULL_NODE)
		return;

	/* the stack holds (node, depth) pairs, pre-order, child1 first */
	stack_init (&stack);
	stack_push (&stack, 0u);
	stack_push (&stack, self->root);
	while (stack.len > 0)
	{
		guint       ni = stack_pop (&stack);
		guint       depth = stack_pop (&stack);
		const Node *n = &self->nodes[ni];
		gfloat      bmin[3];
		gfloat      bmax[3];
		guint       proxy;
		guint       c1;
		guint       c2;

		/* copy out: the callback must not see storage that could move */
		memcpy (bmin, n->min, sizeof (bmin));
		memcpy (bmax, n->max, sizeof (bmax));
		proxy = node_is_leaf (n) ? n->proxy : LRG_BROADPHASE_NULL_PROXY;
		c1 = n->child1;
		c2 = n->child2;
		func (depth, bmin, bmax, proxy, user_data);
		if (c1 != NULL_NODE)
		{
			stack_push (&stack, depth + 1);
			stack_push (&stack, c2);
			stack_push (&stack, depth + 1);
			stack_push (&stack, c1);
		}
	}
	stack_free (&stack);
}

guint
lrg_broadphase_get_height (LrgBroadphase *self)
{
	g_return_val_if_fail (LRG_IS_BROADPHASE (self), 0);

	if (self->root == NULL_NODE)
		return 0;
	return (guint) self->nodes[self->root].height;
}

void
lrg_broadphase_get_stats (LrgBroadphase      *self,
                          LrgBroadphaseStats *out_stats)
{
	gfloat internal_area = 0.0f;
	guint  i;

	g_return_if_fail (LRG_IS_BROADPHASE (self));
	g_return_if_fail (out_stats != NULL);

	memset (out_stats, 0, sizeof (*out_stats));
	out_stats->proxy_count = self->proxy_count;
	out_stats->node_count = self->node_count;
	out_stats->height = lrg_broadphase_get_height (self);
	out_stats->moves = self->moves;
	out_stats->reinserts = self->reinserts;
	out_stats->rotations = self->rotations;
	out_stats->pair_count = self->last_pair_count;
	out_stats->pair_tests = self->last_pair_tests;

	for (i = 0; i < self->proxy_high; i++)
		if (self->proxies[i].node != NULL_NODE && proxy_active (&self->proxies[i]))
			out_stats->active_count++;

	/* shape of the tree: every in-use internal node */
	for (i = 0; i < self->node_capacity; i++)
	{
		const Node *n = &self->nodes[i];
		gint        diff;

		if (n->height <= 0)
			continue;
		diff = self->nodes[n->child1].height - self->nodes[n->child2].height;
		if ((guint) ABS (diff) > out_stats->max_balance)
			out_stats->max_balance = (guint) ABS (diff);
		internal_area += box_area (n->min, n->max);
	}
	if (self->root != NULL_NODE)
	{
		gfloat root_area = box_area (self->nodes[self->root].min, self->nodes[self->root].max);

		out_stats->area_ratio = root_area > 0.0f ? internal_area / root_area : 0.0f;
	}
}

void
lrg_broadphase_reset_counters (LrgBroadphase *self)
{
	g_return_if_fail (LRG_IS_BROADPHASE (self));

	self->moves = 0;
	self->reinserts = 0;
	self->rotations = 0;
	self->last_pair_count = 0;
	self->last_pair_tests = 0;
}

/* validate_node: recursive structural check; returns the leaves seen
 * below @i, or G_MAXUINT on a broken invariant */
static guint
validate_node (LrgBroadphase *self,
               guint          i,
               guint          parent)
{
	const Node *n;
	guint       l1;
	guint       l2;
	gfloat      umin[3];
	gfloat      umax[3];

	if (i >= self->node_capacity)
		return G_MAXUINT;
	n = &self->nodes[i];
	if (n->height < 0 || n->parent != parent)
		return G_MAXUINT;

	if (node_is_leaf (n))
	{
		const Proxy *p;

		if (n->height != 0 || n->child2 != NULL_NODE || n->proxy >= self->proxy_high)
			return G_MAXUINT;
		p = &self->proxies[n->proxy];
		if (p->node != i || !box_contains (n->min, n->max, p->min, p->max))
			return G_MAXUINT;
		return 1;
	}

	if (n->child2 == NULL_NODE)
		return G_MAXUINT;
	l1 = validate_node (self, n->child1, i);
	l2 = validate_node (self, n->child2, i);
	if (l1 == G_MAXUINT || l2 == G_MAXUINT)
		return G_MAXUINT;
	if (n->height != 1 + MAX (self->nodes[n->child1].height, self->nodes[n->child2].height))
		return G_MAXUINT;
	box_union (umin, umax,
	           self->nodes[n->child1].min, self->nodes[n->child1].max,
	           self->nodes[n->child2].min, self->nodes[n->child2].max);
	if (memcmp (umin, n->min, sizeof (umin)) != 0 || memcmp (umax, n->max, sizeof (umax)) != 0)
		return G_MAXUINT;
	return l1 + l2;
}

gboolean
lrg_broadphase_validate (LrgBroadphase *self)
{
	guint leaves;
	guint free_count = 0;
	guint live = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_BROADPHASE (self), FALSE);

	/* leaves reachable from the root equal live proxies */
	leaves = self->root == NULL_NODE ? 0 : validate_node (self, self->root, NULL_NODE);
	if (leaves == G_MAXUINT || leaves != self->proxy_count)
		return FALSE;

	for (i = 0; i < self->proxy_high; i++)
		if (self->proxies[i].node != NULL_NODE)
			live++;
	if (live != self->proxy_count)
		return FALSE;

	/* every node is either in use or on the free stack */
	for (i = self->free_node; i != NULL_NODE; i = self->nodes[i].parent)
	{
		if (self->nodes[i].height != -1 || ++free_count > self->node_capacity)
			return FALSE;
	}
	if (free_count + self->node_count != self->node_capacity)
		return FALSE;
	if (self->proxy_count > 0 && self->node_count != 2 * self->proxy_count - 1)
		return FALSE;
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* Rebuild                                                             */
/* ------------------------------------------------------------------ */

typedef struct
{
	gfloat center;
	guint  leaf;
	guint  proxy;
} SortLeaf;

static gint
compare_sort_leaf (const void *a,
                   const void *b)
{
	const SortLeaf *x = (const SortLeaf *) a;
	const SortLeaf *y = (const SortLeaf *) b;

	if (x->center < y->center)
		return -1;
	if (x->center > y->center)
		return 1;
	/* ties by proxy id: qsort is not stable, the order must be total */
	return x->proxy < y->proxy ? -1 : (x->proxy > y->proxy ? 1 : 0);
}

/* build_range: top-down build over leaves [first, first + count):
 * split at the median along the longest axis of the leaf centers */
static guint
build_range (LrgBroadphase *self,
             SortLeaf      *leaves,
             guint          count)
{
	gfloat cmin[3] = { G_MAXFLOAT, G_MAXFLOAT, G_MAXFLOAT };
	gfloat cmax[3] = { -G_MAXFLOAT, -G_MAXFLOAT, -G_MAXFLOAT };
	guint  axis = 0;
	guint  half;
	guint  i;
	guint  k;
	guint  left;
	guint  right;
	guint  parent;

	if (count == 1)
		return leaves[0].leaf;

	for (i = 0; i < count; i++)
	{
		const Node *n = &self->nodes[leaves[i].leaf];

		for (k = 0; k < 3; k++)
		{
			gfloat c = 0.5f * (n->min[k] + n->max[k]);

			cmin[k] = MIN (cmin[k], c);
			cmax[k] = MAX (cmax[k], c);
		}
	}
	for (k = 1; k < 3; k++)
		if (cmax[k] - cmin[k] > cmax[axis] - cmin[axis])
			axis = k;
	for (i = 0; i < count; i++)
	{
		const Node *n = &self->nodes[leaves[i].leaf];

		leaves[i].center = 0.5f * (n->min[axis] + n->max[axis]);
	}
	qsort (leaves, count, sizeof (SortLeaf), compare_sort_leaf);

	half = count / 2;
	left = build_range (self, leaves, half);
	right = build_range (self, leaves + half, count - half);

	parent = alloc_node (self);
	self->nodes[parent].child1 = left;
	self->nodes[parent].child2 = right;
	self->nodes[left].parent = parent;
	self->nodes[right].parent = parent;
	refit (self, parent);
	return parent;
}

void
lrg_broadphase_rebuild (LrgBroadphase *self)
{
	g_autofree SortLeaf *leaves = NULL;
	guint                n = 0;
	guint                i;

	g_return_if_fail (LRG_IS_BROADPHASE (self));

	if (self->proxy_count < 3)
		return;

	/* free every internal node; keep the leaves (fat boxes) */
	leaves = g_new (SortLeaf, self->proxy_count);
	for (i = 0; i < self->node_capacity; i++)
	{
		Node *node = &self->nodes[i];

		if (node->height > 0)
			free_node (self, i);
	}
	for (i = 0; i < self->proxy_high; i++)
	{
		if (self->proxies[i].node == NULL_NODE)
			continue;
		leaves[n].leaf = self->proxies[i].node;
		leaves[n].proxy = i;
		leaves[n].center = 0.0f;
		self->nodes[leaves[n].leaf].parent = NULL_NODE;
		n++;
	}

	self->root = build_range (self, leaves, n);
	self->nodes[self->root].parent = NULL_NODE;
}

void
lrg_broadphase_clear (LrgBroadphase *self)
{
	g_return_if_fail (LRG_IS_BROADPHASE (self));

	g_clear_pointer (&self->nodes, g_free);
	g_clear_pointer (&self->proxies, g_free);
	self->node_capacity = 0;
	self->node_count = 0;
	self->free_node = NULL_NODE;
	self->root = NULL_NODE;
	self->proxy_capacity = 0;
	self->proxy_high = 0;
	self->proxy_count = 0;
	self->first_free_proxy = 0;
	lrg_broadphase_reset_counters (self);
}
