/* lrg-procgen-generator.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class of the procedural map generators.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../core/lrg-random-stream.h"
#include "../core/lrg-registry.h"
#include "lrg-procgen-grid.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_GENERATOR (lrg_procgen_generator_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgProcgenGenerator, lrg_procgen_generator, LRG, PROCGEN_GENERATOR, GObject)

/**
 * LRG_PROCGEN_STREAM:
 *
 * The #LrgRandomStream sequence selector lrg_procgen_generator_generate()
 * uses, so a game can derive other streams from the same seed without
 * overlapping the generator's draws.
 */
#define LRG_PROCGEN_STREAM (G_GUINT64_CONSTANT (0x70726f6367656e))

/**
 * LrgProcgenGeneratorClass:
 * @parent_class: the parent class
 * @generate: fills @grid (already sized and filled with
 *   %LRG_PROCGEN_CELL_WALL) drawing only from @rng; returns %FALSE and
 *   sets @error on failure
 *
 * The class of procedural map generators. Every tunable parameter of a
 * generator is a GObject property, which is what YAML configurations set
 * (see #LrgProcgenDungeon).
 */
struct _LrgProcgenGeneratorClass
{
	GObjectClass parent_class;

	gboolean (*generate) (LrgProcgenGenerator  *self,
	                      LrgProcgenGrid       *grid,
	                      LrgRandomStream      *rng,
	                      GError              **error);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_procgen_generator_get_width:
 * @self: a generator
 *
 * Returns: the width of the grids it makes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_generator_get_width (LrgProcgenGenerator *self);

/**
 * lrg_procgen_generator_set_width:
 * @self: a generator
 * @width: grid width in cells
 *
 * Sets the width of the grids it makes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_generator_set_width (LrgProcgenGenerator *self,
                                      guint                width);

/**
 * lrg_procgen_generator_get_height:
 * @self: a generator
 *
 * Returns: the height of the grids it makes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_generator_get_height (LrgProcgenGenerator *self);

/**
 * lrg_procgen_generator_set_height:
 * @self: a generator
 * @height: grid height in cells
 *
 * Sets the height of the grids it makes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_generator_set_height (LrgProcgenGenerator *self,
                                       guint                height);

/**
 * lrg_procgen_generator_generate:
 * @self: a generator
 * @seed: the seed
 * @error: (nullable): return location for an error
 *
 * Generates a grid from a seed with a fresh #LrgRandomStream
 * (`lrg_random_stream_new (seed, LRG_PROCGEN_STREAM)`). The same seed and
 * parameters always give the same grid, on every platform.
 *
 * Returns: (transfer full) (nullable): the grid, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_procgen_generator_generate (LrgProcgenGenerator  *self,
                                                 guint64               seed,
                                                 GError              **error);

/**
 * lrg_procgen_generator_generate_with_rng:
 * @self: a generator
 * @rng: the random stream to draw from (advanced)
 * @error: (nullable): return location for an error
 *
 * Generates a grid drawing from an existing stream (e.g. a saveable game
 * stream).
 *
 * Returns: (transfer full) (nullable): the grid, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_procgen_generator_generate_with_rng (LrgProcgenGenerator  *self,
                                                          LrgRandomStream      *rng,
                                                          GError              **error);

/**
 * lrg_procgen_generator_type_from_name:
 * @name: a generator name: `bsp`, `caves`, `wfc`, `drunkard`, `noise`,
 *   or the GType name of any #LrgProcgenGenerator subclass
 *
 * Returns: the generator type, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType lrg_procgen_generator_type_from_name (const gchar *name);

/**
 * lrg_procgen_generator_type_to_name:
 * @type: an #LrgProcgenGenerator type
 *
 * Returns: the short name of a built-in generator type, or the GType
 *   name of any other
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_procgen_generator_type_to_name (GType type);

/**
 * lrg_procgen_register_types:
 * @registry: a registry
 *
 * Registers the generators and the recipe type under the names
 * `procgen-bsp`, `procgen-caves`, `procgen-wfc`, `procgen-drunkard`,
 * `procgen-noise` and `procgen-dungeon`, so an #LrgDataLoader using
 * @registry loads a bare generator definition (`type: procgen-caves` plus
 * its properties) like any other def. Full recipes with spawn and tile
 * rules use lrg_procgen_dungeon_new_from_file().
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_register_types (LrgRegistry *registry);

G_END_DECLS
