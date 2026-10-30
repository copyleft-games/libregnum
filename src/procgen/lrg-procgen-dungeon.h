/* lrg-procgen-dungeon.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A complete, YAML-definable map recipe: a generator, its post-processing,
 * spawn rules and tile rules.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-procgen-generator.h"
#include "lrg-procgen-tile-rules.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_DUNGEON (lrg_procgen_dungeon_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenDungeon, lrg_procgen_dungeon, LRG, PROCGEN_DUNGEON, GObject)

/**
 * LRG_PROCGEN_DUNGEON_FORMAT_VERSION:
 *
 * The `version` written by lrg_procgen_dungeon_to_data(); newer documents
 * are rejected.
 */
#define LRG_PROCGEN_DUNGEON_FORMAT_VERSION (1)

/**
 * lrg_procgen_dungeon_new:
 * @generator: (transfer none): the generator
 *
 * Creates a recipe around a generator with the default post-processing
 * (connect regions, place start and exit), no spawn rules and empty tile
 * rules.
 *
 * Returns: (transfer full): a new #LrgProcgenDungeon
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenDungeon * lrg_procgen_dungeon_new (LrgProcgenGenerator *generator);

/**
 * lrg_procgen_dungeon_new_from_data:
 * @data: a YAML document
 * @length: length of @data, or -1 when nul-terminated
 * @error: (nullable): return location for an error
 *
 * Parses a recipe (the format is described in
 * `docs/modules/procgen/yaml.org`). Unknown keys, unknown generator
 * parameters and out-of-range values are errors
 * (%LRG_PROCGEN_ERROR_INVALID_CONFIG).
 *
 * Returns: (transfer full) (nullable): the recipe, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenDungeon * lrg_procgen_dungeon_new_from_data (const gchar  *data,
                                                       gssize        length,
                                                       GError      **error);

/**
 * lrg_procgen_dungeon_new_from_file:
 * @path: a YAML file (e.g. `dungeon.yaml`)
 * @error: (nullable): return location for an error
 *
 * Loads a recipe from a file.
 *
 * Returns: (transfer full) (nullable): the recipe, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenDungeon * lrg_procgen_dungeon_new_from_file (const gchar  *path,
                                                       GError      **error);

/**
 * lrg_procgen_dungeon_to_data:
 * @self: a recipe
 * @error: (nullable): return location for an error
 *
 * Serializes the recipe to YAML. Loading the result gives an equal recipe
 * (the same document again, and the same maps for every seed). Learned
 * wave function collapse models are written as explicit tiles and rules.
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_procgen_dungeon_to_data (LrgProcgenDungeon  *self,
                                     GError            **error);

/**
 * lrg_procgen_dungeon_save_to_file:
 * @self: a recipe
 * @path: destination file
 * @error: (nullable): return location for an error
 *
 * Writes lrg_procgen_dungeon_to_data() to a file.
 *
 * Returns: whether the file was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_dungeon_save_to_file (LrgProcgenDungeon  *self,
                                           const gchar        *path,
                                           GError            **error);

/**
 * lrg_procgen_dungeon_get_generator:
 * @self: a recipe
 *
 * Returns: (transfer none): the generator
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGenerator * lrg_procgen_dungeon_get_generator (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_set_generator:
 * @self: a recipe
 * @generator: (transfer none): the generator
 *
 * Replaces the generator.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_set_generator (LrgProcgenDungeon   *self,
                                        LrgProcgenGenerator *generator);

/**
 * lrg_procgen_dungeon_get_tile_rules:
 * @self: a recipe
 *
 * Returns: (transfer none): the tile rules (modifiable)
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenTileRules * lrg_procgen_dungeon_get_tile_rules (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_get_seed:
 * @self: a recipe
 *
 * Returns: the #LrgProcgenDungeon:seed property
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_procgen_dungeon_get_seed (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_set_seed:
 * @self: a recipe
 * @seed: the default seed
 *
 * Sets the #LrgProcgenDungeon:seed property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_set_seed (LrgProcgenDungeon *self,
                                   guint64            seed);

/**
 * lrg_procgen_dungeon_get_cull_min_size:
 * @self: a recipe
 *
 * Returns: the #LrgProcgenDungeon:cull-min-size property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_dungeon_get_cull_min_size (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_set_cull_min_size:
 * @self: a recipe
 * @size: regions below this size are filled in (0 = off)
 *
 * Sets the #LrgProcgenDungeon:cull-min-size property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_set_cull_min_size (LrgProcgenDungeon *self,
                                            guint              size);

/**
 * lrg_procgen_dungeon_get_keep_largest:
 * @self: a recipe
 *
 * Returns: the #LrgProcgenDungeon:keep-largest property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_dungeon_get_keep_largest (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_set_keep_largest:
 * @self: a recipe
 * @keep: whether only the largest region is kept
 *
 * Sets the #LrgProcgenDungeon:keep-largest property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_set_keep_largest (LrgProcgenDungeon *self,
                                           gboolean           keep);

/**
 * lrg_procgen_dungeon_get_connect:
 * @self: a recipe
 *
 * Returns: the #LrgProcgenDungeon:connect property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_dungeon_get_connect (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_set_connect:
 * @self: a recipe
 * @connect: whether disjoint regions are joined by corridors
 *
 * Sets the #LrgProcgenDungeon:connect property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_set_connect (LrgProcgenDungeon *self,
                                      gboolean           connect);

/**
 * lrg_procgen_dungeon_get_place_start_exit:
 * @self: a recipe
 *
 * Returns: the #LrgProcgenDungeon:place-start-exit property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_dungeon_get_place_start_exit (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_set_place_start_exit:
 * @self: a recipe
 * @place: whether start and exit are placed on a farthest pair
 *
 * Sets the #LrgProcgenDungeon:place-start-exit property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_set_place_start_exit (LrgProcgenDungeon *self,
                                               gboolean           place);

/**
 * lrg_procgen_dungeon_add_spawn_rule:
 * @self: a recipe
 * @kind: what to spawn
 * @count: how many
 * @min_distance: minimum walking distance from the start
 * @rooms_only: only inside rooms
 *
 * Adds a spawn rule, applied in order after post-processing with
 * lrg_procgen_grid_place_spawns().
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_add_spawn_rule (LrgProcgenDungeon *self,
                                         const gchar       *kind,
                                         guint              count,
                                         guint              min_distance,
                                         gboolean           rooms_only);

/**
 * lrg_procgen_dungeon_get_spawn_rule_count:
 * @self: a recipe
 *
 * Returns: the number of spawn rules
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_dungeon_get_spawn_rule_count (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_get_spawn_rule:
 * @self: a recipe
 * @index: rule index
 * @out_kind: (out) (optional) (transfer none): what to spawn
 * @out_count: (out) (optional): how many
 * @out_min_distance: (out) (optional): minimum distance from the start
 * @out_rooms_only: (out) (optional): only inside rooms
 *
 * Returns: whether @index names a rule
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_dungeon_get_spawn_rule (LrgProcgenDungeon  *self,
                                             guint               index,
                                             const gchar       **out_kind,
                                             guint              *out_count,
                                             guint              *out_min_distance,
                                             gboolean           *out_rooms_only);

/**
 * lrg_procgen_dungeon_clear_spawn_rules:
 * @self: a recipe
 *
 * Removes every spawn rule.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_dungeon_clear_spawn_rules (LrgProcgenDungeon *self);

/**
 * lrg_procgen_dungeon_generate:
 * @self: a recipe
 * @seed: the seed
 * @error: (nullable): return location for an error
 *
 * Runs the whole recipe on one #LrgRandomStream seeded with @seed: the
 * generator, then culling, keep-largest, connection, start/exit placement
 * and the spawn rules, in that order.
 *
 * Returns: (transfer full) (nullable): the grid, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_procgen_dungeon_generate (LrgProcgenDungeon  *self,
                                               guint64             seed,
                                               GError            **error);

/**
 * lrg_procgen_dungeon_build_layer:
 * @self: a recipe
 * @grid: a grid it generated
 *
 * Builds the tilemap layer of @grid with the recipe's tile rules.
 *
 * Returns: (transfer full): a new #LrgTilemapLayer
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapLayer * lrg_procgen_dungeon_build_layer (LrgProcgenDungeon *self,
                                                   LrgProcgenGrid    *grid);

G_END_DECLS
