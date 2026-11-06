/* lrg-island.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgIsland: a pill-shaped panel that morphs between a compact and an
 * expanded size.
 *
 * The island holds two children, a compact one (a status line, a
 * timer, an icon) and an expanded one (details, controls). Toggling
 * springs the island's size and corner radius between the two
 * children's preferred sizes, fading the compact content out and the
 * expanded content in on the way, like a phone's notch that unfolds.
 * The island paints on a dark surface and gives its subtree a dark
 * twin of the surrounding theme, so it reads well over light themes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_ISLAND (lrg_island_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIsland, lrg_island, LRG, ISLAND, LrgContainer)

/**
 * lrg_island_new:
 *
 * Returns: (transfer full): a new, compact #LrgIsland
 */
LRG_AVAILABLE_IN_ALL
LrgIsland * lrg_island_new (void);

/**
 * lrg_island_set_compact_child:
 * @self: an #LrgIsland
 * @child: (nullable): the content shown while compact
 *
 * Replaces the compact content (the island takes a reference).
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_set_compact_child (LrgIsland *self,
                                   LrgWidget *child);

/**
 * lrg_island_get_compact_child:
 * @self: an #LrgIsland
 *
 * Returns: (transfer none) (nullable): the compact content
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_island_get_compact_child (LrgIsland *self);

/**
 * lrg_island_set_expanded_child:
 * @self: an #LrgIsland
 * @child: (nullable): the content shown while expanded
 *
 * Replaces the expanded content (the island takes a reference).
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_set_expanded_child (LrgIsland *self,
                                    LrgWidget *child);

/**
 * lrg_island_get_expanded_child:
 * @self: an #LrgIsland
 *
 * Returns: (transfer none) (nullable): the expanded content
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_island_get_expanded_child (LrgIsland *self);

/**
 * lrg_island_get_expanded:
 * @self: an #LrgIsland
 *
 * Returns: %TRUE if the island is (or is morphing towards) expanded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_island_get_expanded (LrgIsland *self);

/**
 * lrg_island_set_expanded:
 * @self: an #LrgIsland
 * @expanded: expand or collapse
 *
 * Morphs to the expanded or compact form with the widget's motion
 * spring and emits #LrgIsland::toggled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_set_expanded (LrgIsland *self,
                              gboolean   expanded);

/**
 * lrg_island_toggle:
 * @self: an #LrgIsland
 *
 * Flips between compact and expanded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_toggle (LrgIsland *self);

/**
 * lrg_island_get_toggle_on_click:
 * @self: an #LrgIsland
 *
 * Returns: %TRUE if a click (or activate) on the island toggles it
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_island_get_toggle_on_click (LrgIsland *self);

/**
 * lrg_island_set_toggle_on_click:
 * @self: an #LrgIsland
 * @toggle: whether clicks and activation toggle the island (default
 *   %TRUE); turn it off when the expanded content has its own buttons
 *   that should not collapse it
 */
LRG_AVAILABLE_IN_ALL
void lrg_island_set_toggle_on_click (LrgIsland *self,
                                     gboolean   toggle);

/**
 * lrg_island_get_progress:
 * @self: an #LrgIsland
 *
 * Returns: the morph, 0 (compact) to 1 (expanded); bouncy motion may
 *   overshoot briefly
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_island_get_progress (LrgIsland *self);

G_END_DECLS
