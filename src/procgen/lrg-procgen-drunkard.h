/* lrg-procgen-drunkard.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: drunkard's walk caves.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-procgen-generator.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_DRUNKARD (lrg_procgen_drunkard_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenDrunkard, lrg_procgen_drunkard, LRG, PROCGEN_DRUNKARD, LrgProcgenGenerator)

/**
 * lrg_procgen_drunkard_new:
 * @width: grid width in cells
 * @height: grid height in cells
 *
 * Creates a generator of drunkard's walk caves. A random walker that carves floor until a share of the map is open.
 *
 * Returns: (transfer full): a new #LrgProcgenDrunkard
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenDrunkard * lrg_procgen_drunkard_new (guint width,
                                               guint height);

/**
 * lrg_procgen_drunkard_get_coverage_percent:
 * @self: a generator
 *
 * Returns: the #LrgProcgenDrunkard:coverage-percent property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_drunkard_get_coverage_percent (LrgProcgenDrunkard *self);

/**
 * lrg_procgen_drunkard_set_coverage_percent:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenDrunkard:coverage-percent property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_drunkard_set_coverage_percent (LrgProcgenDrunkard *self,
                                                guint value);

/**
 * lrg_procgen_drunkard_get_max_steps:
 * @self: a generator
 *
 * Returns: the #LrgProcgenDrunkard:max-steps property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_drunkard_get_max_steps (LrgProcgenDrunkard *self);

/**
 * lrg_procgen_drunkard_set_max_steps:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenDrunkard:max-steps property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_drunkard_set_max_steps (LrgProcgenDrunkard *self,
                                         guint value);

/**
 * lrg_procgen_drunkard_get_walk_length:
 * @self: a generator
 *
 * Returns: the #LrgProcgenDrunkard:walk-length property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_drunkard_get_walk_length (LrgProcgenDrunkard *self);

/**
 * lrg_procgen_drunkard_set_walk_length:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenDrunkard:walk-length property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_drunkard_set_walk_length (LrgProcgenDrunkard *self,
                                           guint value);

G_END_DECLS
