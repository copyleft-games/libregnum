/* lrg-island.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 *
 * LrgIslandBuilder: groups bodies that touch or are constrained together
 * into islands (union-find with path halving and union by index, so the
 * result only depends on the order links are added). #LrgDynamicsWorld
 * uses islands to put whole groups to sleep and wake them together.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ISLAND_BUILDER (lrg_island_builder_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIslandBuilder, lrg_island_builder, LRG, ISLAND_BUILDER, GObject)

/**
 * lrg_island_builder_new:
 *
 * An empty builder. Call lrg_island_builder_reset() with the element
 * count before linking.
 *
 * Returns: (transfer full): a new #LrgIslandBuilder
 */
LRG_AVAILABLE_IN_ALL
LrgIslandBuilder *lrg_island_builder_new (void);

/**
 * lrg_island_builder_reset:
 * @self: an island builder
 * @n_elements: how many elements (bodies) take part; each starts alone
 *
 * Forgets every link and the last finished result.
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_builder_reset (LrgIslandBuilder *self,
                               guint             n_elements);

/**
 * lrg_island_builder_link:
 * @self: an island builder
 * @a: an element below the reset count
 * @b: another element below the reset count
 *
 * Puts @a and @b in the same island. Out-of-range elements are ignored,
 * so a link to a static body (pass %G_MAXUINT) is a no-op.
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_builder_link (LrgIslandBuilder *self,
                              guint             a,
                              guint             b);

/**
 * lrg_island_builder_find:
 * @self: an island builder
 * @element: an element below the reset count
 *
 * Returns: the representative of @element's island (the smallest
 *   element in it once lrg_island_builder_finish() ran), or %G_MAXUINT
 *   when @element is out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_island_builder_find (LrgIslandBuilder *self,
                               guint             element);

/**
 * lrg_island_builder_finish:
 * @self: an island builder
 * @active: (array length=n_active) (nullable): whether each element
 *   takes part; %NULL means all of them. Inactive elements get no island
 * @n_active: length of @active (must equal the reset count when
 *   @active is not %NULL)
 *
 * Numbers the islands 0 .. n-1 in order of their smallest element and
 * fills the per-island element lists (ascending order).
 *
 * Returns: the island count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_island_builder_finish (LrgIslandBuilder *self,
                                 const gboolean   *active,
                                 guint             n_active);

/**
 * lrg_island_builder_get_island_count:
 * @self: an island builder
 *
 * Returns: islands numbered by the last lrg_island_builder_finish()
 */
LRG_AVAILABLE_IN_ALL
guint lrg_island_builder_get_island_count (LrgIslandBuilder *self);

/**
 * lrg_island_builder_get_island_of:
 * @self: an island builder
 * @element: an element below the reset count
 *
 * Returns: the island index of @element, or %G_MAXUINT when it took no
 *   part
 */
LRG_AVAILABLE_IN_ALL
guint lrg_island_builder_get_island_of (LrgIslandBuilder *self,
                                        guint             element);

/**
 * lrg_island_builder_get_island:
 * @self: an island builder
 * @island: an island index below the island count
 * @n_elements: (out): how many elements the island has
 *
 * Returns: (array length=n_elements) (transfer none): the island's
 *   elements in ascending order, owned by @self until the next reset
 */
LRG_AVAILABLE_IN_ALL
const guint *lrg_island_builder_get_island (LrgIslandBuilder *self,
                                            guint             island,
                                            guint            *n_elements);

G_END_DECLS
