/* lrg-procgen-caves.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: cellular automaton caves.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-procgen-generator.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_CAVES (lrg_procgen_caves_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenCaves, lrg_procgen_caves, LRG, PROCGEN_CAVES, LrgProcgenGenerator)

/**
 * lrg_procgen_caves_new:
 * @width: grid width in cells
 * @height: grid height in cells
 *
 * Creates a generator of cellular automaton caves. Caves grown by a cellular automaton from random noise, then cleaned up:
 * small regions culled, and the rest either joined by corridors or reduced
 * to the largest.
 *
 * Returns: (transfer full): a new #LrgProcgenCaves
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenCaves * lrg_procgen_caves_new (guint width,
                                         guint height);

/**
 * lrg_procgen_caves_get_fill_percent:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:fill-percent property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_caves_get_fill_percent (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_fill_percent:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:fill-percent property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_fill_percent (LrgProcgenCaves *self,
                                         guint value);

/**
 * lrg_procgen_caves_get_iterations:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:iterations property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_caves_get_iterations (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_iterations:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:iterations property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_iterations (LrgProcgenCaves *self,
                                       guint value);

/**
 * lrg_procgen_caves_get_birth_limit:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:birth-limit property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_caves_get_birth_limit (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_birth_limit:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:birth-limit property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_birth_limit (LrgProcgenCaves *self,
                                        guint value);

/**
 * lrg_procgen_caves_get_survival_limit:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:survival-limit property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_caves_get_survival_limit (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_survival_limit:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:survival-limit property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_survival_limit (LrgProcgenCaves *self,
                                           guint value);

/**
 * lrg_procgen_caves_get_min_region_size:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:min-region-size property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_caves_get_min_region_size (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_min_region_size:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:min-region-size property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_min_region_size (LrgProcgenCaves *self,
                                            guint value);

/**
 * lrg_procgen_caves_get_connect:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:connect property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_caves_get_connect (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_connect:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:connect property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_connect (LrgProcgenCaves *self,
                                    gboolean value);

/**
 * lrg_procgen_caves_get_keep_largest:
 * @self: a generator
 *
 * Returns: the #LrgProcgenCaves:keep-largest property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_caves_get_keep_largest (LrgProcgenCaves *self);

/**
 * lrg_procgen_caves_set_keep_largest:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenCaves:keep-largest property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_caves_set_keep_largest (LrgProcgenCaves *self,
                                         gboolean value);

G_END_DECLS
