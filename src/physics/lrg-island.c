/* lrg-island.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 * (Jolt's IslandBuilder links bodies through a lock-free union-find;
 * this is the single-threaded form of the same idea.)
 *
 * Union-find with path halving. Linking always hangs the larger root
 * under the smaller one, so the representative of an island is its
 * smallest element and the result does not depend on link order.
 */

#include "config.h"

#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-island.h"

struct _LrgIslandBuilder
{
	GObject parent_instance;

	guint  *parent;     /* union-find parent per element */
	guint  *island_of;  /* island index per element after finish */
	guint  *offsets;    /* island start offsets into members (count+1) */
	guint  *members;    /* elements grouped by island */
	guint  *root_island; /* scratch: island of each root */
	guint   n_elements;
	guint   capacity;
	guint   n_islands;
	guint   offsets_capacity;
};

G_DEFINE_FINAL_TYPE (LrgIslandBuilder, lrg_island_builder, G_TYPE_OBJECT)

static void
lrg_island_builder_finalize (GObject *object)
{
	LrgIslandBuilder *self;

	self = LRG_ISLAND_BUILDER (object);
	g_free (self->parent);
	g_free (self->island_of);
	g_free (self->offsets);
	g_free (self->members);
	g_free (self->root_island);
	G_OBJECT_CLASS (lrg_island_builder_parent_class)->finalize (object);
}

static void
lrg_island_builder_class_init (LrgIslandBuilderClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_island_builder_finalize;
}

static void
lrg_island_builder_init (LrgIslandBuilder *self)
{
	(void) self;
}

LrgIslandBuilder *
lrg_island_builder_new (void)
{
	return g_object_new (LRG_TYPE_ISLAND_BUILDER, NULL);
}

void
lrg_island_builder_reset (LrgIslandBuilder *self,
                          guint             n_elements)
{
	guint i;

	g_return_if_fail (LRG_IS_ISLAND_BUILDER (self));

	/* Grow the arrays geometrically; they are reused every substep. */
	if (n_elements > self->capacity)
	{
		guint cap;

		cap = MAX (64u, self->capacity);
		while (cap < n_elements)
			cap *= 2u;
		self->parent = g_renew (guint, self->parent, cap);
		self->island_of = g_renew (guint, self->island_of, cap);
		self->members = g_renew (guint, self->members, cap);
		self->root_island = g_renew (guint, self->root_island, cap);
		self->capacity = cap;
	}
	self->n_elements = n_elements;
	self->n_islands = 0;
	for (i = 0; i < n_elements; i++)
	{
		self->parent[i] = i;
		self->island_of[i] = G_MAXUINT;
	}
}

/* Root of @x with path halving. */
static guint
island_root (LrgIslandBuilder *self,
             guint             x)
{
	while (self->parent[x] != x)
	{
		self->parent[x] = self->parent[self->parent[x]];
		x = self->parent[x];
	}
	return x;
}

void
lrg_island_builder_link (LrgIslandBuilder *self,
                         guint             a,
                         guint             b)
{
	guint ra;
	guint rb;

	g_return_if_fail (LRG_IS_ISLAND_BUILDER (self));
	if (a >= self->n_elements || b >= self->n_elements)
		return;
	ra = island_root (self, a);
	rb = island_root (self, b);
	if (ra == rb)
		return;
	/* The smaller root wins so representatives are deterministic. */
	if (ra < rb)
		self->parent[rb] = ra;
	else
		self->parent[ra] = rb;
}

guint
lrg_island_builder_find (LrgIslandBuilder *self,
                         guint             element)
{
	g_return_val_if_fail (LRG_IS_ISLAND_BUILDER (self), G_MAXUINT);
	if (element >= self->n_elements)
		return G_MAXUINT;
	return island_root (self, element);
}

guint
lrg_island_builder_finish (LrgIslandBuilder *self,
                           const gboolean   *active,
                           guint             n_active)
{
	guint i;
	guint root;
	guint n;
	guint *count;

	g_return_val_if_fail (LRG_IS_ISLAND_BUILDER (self), 0);
	g_return_val_if_fail (active == NULL || n_active == self->n_elements, 0);

	/* Pass 1: number the islands in order of their smallest active
	 * element. Links through inactive elements still join islands. */
	n = 0;
	for (i = 0; i < self->n_elements; i++)
	{
		self->island_of[i] = G_MAXUINT;
		self->root_island[i] = G_MAXUINT;
	}
	for (i = 0; i < self->n_elements; i++)
	{
		if (active != NULL && !active[i])
			continue;
		root = island_root (self, i);
		if (self->root_island[root] == G_MAXUINT)
			self->root_island[root] = n++;
		self->island_of[i] = self->root_island[root];
	}
	self->n_islands = n;

	/* Pass 2: counting sort of the members by island. */
	if (n + 1u > self->offsets_capacity)
	{
		self->offsets_capacity = MAX (n + 1u, self->offsets_capacity * 2u);
		self->offsets = g_renew (guint, self->offsets, self->offsets_capacity);
	}
	memset (self->offsets, 0, sizeof (guint) * (n + 1u));
	for (i = 0; i < self->n_elements; i++)
		if (self->island_of[i] != G_MAXUINT)
			self->offsets[self->island_of[i] + 1u]++;
	for (i = 0; i < n; i++)
		self->offsets[i + 1u] += self->offsets[i];
	count = g_new0 (guint, MAX (n, 1u));
	for (i = 0; i < self->n_elements; i++)
	{
		guint isl;

		isl = self->island_of[i];
		if (isl == G_MAXUINT)
			continue;
		self->members[self->offsets[isl] + count[isl]] = i;
		count[isl]++;
	}
	g_free (count);
	return n;
}

guint
lrg_island_builder_get_island_count (LrgIslandBuilder *self)
{
	g_return_val_if_fail (LRG_IS_ISLAND_BUILDER (self), 0);
	return self->n_islands;
}

guint
lrg_island_builder_get_island_of (LrgIslandBuilder *self,
                                  guint             element)
{
	g_return_val_if_fail (LRG_IS_ISLAND_BUILDER (self), G_MAXUINT);
	if (element >= self->n_elements)
		return G_MAXUINT;
	return self->island_of[element];
}

const guint *
lrg_island_builder_get_island (LrgIslandBuilder *self,
                               guint             island,
                               guint            *n_elements)
{
	g_return_val_if_fail (LRG_IS_ISLAND_BUILDER (self), NULL);
	g_return_val_if_fail (n_elements != NULL, NULL);
	if (island >= self->n_islands)
	{
		*n_elements = 0;
		return NULL;
	}
	*n_elements = self->offsets[island + 1u] - self->offsets[island];
	return self->members + self->offsets[island];
}
