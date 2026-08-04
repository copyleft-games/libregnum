/* lrg-wave-director.h - Data-driven enemy wave/spawn sequencing
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgWaveDirector sequences enemy waves for shooters, shmups, tower
 * defense, survival arenas, and any other genre that spawns groups of
 * entities over time. The director owns NO entity types - it emits
 * "spawn" signals carrying a string enemy id plus a position, and the
 * game decides how to instantiate each enemy.
 *
 * Waves run strictly in order. Each wave has a trigger (time elapsed,
 * external progress value, or all-enemies-cleared) and a list of spawn
 * entries. When a wave's trigger fires its entries begin spawning
 * (respecting per-entry intervals); when every entry has finished
 * spawning the director advances to the next wave. After the last wave
 * finishes spawning, the "finished" signal is emitted.
 *
 * Waves can be built programmatically or loaded from YAML:
 *
 * ```yaml
 * waves:
 *   - trigger: {type: time, value: 2.0}
 *     entries:
 *       - {enemy: grunt, count: 6, pattern: edge-random, interval: 0.4}
 *       - {enemy: turret, count: 2, pattern: point, x: 40.0, y: 60.0}
 * ```
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_WAVE_DIRECTOR (lrg_wave_director_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWaveDirector, lrg_wave_director,
                      LRG, WAVE_DIRECTOR, GObject)

/**
 * LrgWaveTriggerType:
 * @LRG_WAVE_TRIGGER_TIME: Fires when the wave-local elapsed time since
 *   the wave became current reaches the trigger value (in seconds).
 * @LRG_WAVE_TRIGGER_PROGRESS: Fires when the externally-supplied
 *   progress value passed to lrg_wave_director_update() reaches the
 *   trigger value (e.g. shmup scroll position).
 * @LRG_WAVE_TRIGGER_CLEARED: Fires when the externally-supplied active
 *   enemy count passed to lrg_wave_director_update() is zero. The
 *   trigger value is ignored.
 *
 * Conditions that start a wave once it has become the current wave.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_WAVE_TRIGGER_TIME,
    LRG_WAVE_TRIGGER_PROGRESS,
    LRG_WAVE_TRIGGER_CLEARED
} LrgWaveTriggerType;

/**
 * LrgWavePattern:
 * @LRG_WAVE_PATTERN_POINT: Spawn exactly at (x, y).
 * @LRG_WAVE_PATTERN_EDGE_TOP: Random point along the top edge of the
 *   bounds rectangle.
 * @LRG_WAVE_PATTERN_EDGE_BOTTOM: Random point along the bottom edge of
 *   the bounds rectangle.
 * @LRG_WAVE_PATTERN_EDGE_LEFT: Random point along the left edge of the
 *   bounds rectangle.
 * @LRG_WAVE_PATTERN_EDGE_RIGHT: Random point along the right edge of
 *   the bounds rectangle.
 * @LRG_WAVE_PATTERN_EDGE_RANDOM: Random point along a randomly chosen
 *   edge of the bounds rectangle.
 * @LRG_WAVE_PATTERN_RING: Random angles on a circle of the given radius
 *   centered at (x, y).
 * @LRG_WAVE_PATTERN_LINE: Evenly spaced along a horizontal line centered
 *   at (x, y) with spacing equal to the radius parameter.
 *
 * Placement patterns for spawn entries. Edge patterns require bounds to
 * be configured with lrg_wave_director_set_bounds().
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_WAVE_PATTERN_POINT,
    LRG_WAVE_PATTERN_EDGE_TOP,
    LRG_WAVE_PATTERN_EDGE_BOTTOM,
    LRG_WAVE_PATTERN_EDGE_LEFT,
    LRG_WAVE_PATTERN_EDGE_RIGHT,
    LRG_WAVE_PATTERN_EDGE_RANDOM,
    LRG_WAVE_PATTERN_RING,
    LRG_WAVE_PATTERN_LINE
} LrgWavePattern;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_wave_director_new:
 *
 * Creates a new, empty wave director.
 *
 * Returns: (transfer full): a new #LrgWaveDirector
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWaveDirector *
lrg_wave_director_new (void);

/* ==========================================================================
 * Loading
 * ========================================================================== */

/**
 * lrg_wave_director_load_from_file:
 * @self: an #LrgWaveDirector
 * @path: path to a YAML wave definition file
 * @error: (nullable): return location for a #GError
 *
 * Loads wave definitions from a YAML file and appends them to the
 * director's wave list. On failure no waves are added.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wave_director_load_from_file (LrgWaveDirector  *self,
                                  const gchar      *path,
                                  GError          **error);

/**
 * lrg_wave_director_load_from_data:
 * @self: an #LrgWaveDirector
 * @data: YAML wave definition data
 * @length: length of @data, or -1 if null-terminated
 * @error: (nullable): return location for a #GError
 *
 * Loads wave definitions from an in-memory YAML string and appends
 * them to the director's wave list. On failure no waves are added.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wave_director_load_from_data (LrgWaveDirector  *self,
                                  const gchar      *data,
                                  gssize            length,
                                  GError          **error);

/* ==========================================================================
 * Programmatic Builders
 * ========================================================================== */

/**
 * lrg_wave_director_add_wave:
 * @self: an #LrgWaveDirector
 * @trigger: the trigger condition for this wave
 * @trigger_value: the trigger threshold (seconds for time triggers,
 *   progress units for progress triggers, ignored for cleared triggers)
 *
 * Appends a new empty wave to the director. Populate it with
 * lrg_wave_director_wave_add_entry().
 *
 * Returns: the index of the new wave
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wave_director_add_wave (LrgWaveDirector    *self,
                            LrgWaveTriggerType  trigger,
                            gfloat              trigger_value);

/**
 * lrg_wave_director_wave_add_entry:
 * @self: an #LrgWaveDirector
 * @wave_index: index of the wave to add the entry to
 * @enemy_id: string identifier for the enemy type to spawn
 * @count: number of enemies this entry spawns
 * @pattern: placement pattern for the spawns
 * @x: pattern X coordinate (point/ring/line patterns)
 * @y: pattern Y coordinate (point/ring/line patterns)
 * @radius: ring radius, or line spacing (unused by other patterns)
 * @interval: seconds between successive spawns of this entry
 *   (0 = all at once)
 *
 * Adds a spawn entry to an existing wave.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wave_director_wave_add_entry (LrgWaveDirector *self,
                                  guint            wave_index,
                                  const gchar     *enemy_id,
                                  guint            count,
                                  LrgWavePattern   pattern,
                                  gfloat           x,
                                  gfloat           y,
                                  gfloat           radius,
                                  gfloat           interval);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_wave_director_set_bounds:
 * @self: an #LrgWaveDirector
 * @min_x: left edge of the spawn bounds
 * @min_y: top edge of the spawn bounds
 * @max_x: right edge of the spawn bounds
 * @max_y: bottom edge of the spawn bounds
 *
 * Sets the bounds rectangle used by the edge spawn patterns.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wave_director_set_bounds (LrgWaveDirector *self,
                              gfloat           min_x,
                              gfloat           min_y,
                              gfloat           max_x,
                              gfloat           max_y);

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

/**
 * lrg_wave_director_start:
 * @self: an #LrgWaveDirector
 *
 * Starts the director. Wave 0 becomes current and its trigger begins
 * evaluating on subsequent lrg_wave_director_update() calls. If the
 * director has no waves it finishes immediately.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wave_director_start (LrgWaveDirector *self);

/**
 * lrg_wave_director_reset:
 * @self: an #LrgWaveDirector
 *
 * Returns the director to its pre-start state. The configured waves
 * are kept; all runtime progress (current wave, timers, pending
 * spawns) is discarded.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wave_director_reset (LrgWaveDirector *self);

/**
 * lrg_wave_director_update:
 * @self: an #LrgWaveDirector
 * @delta: time step in seconds
 * @progress: externally-supplied progress value (e.g. scroll position),
 *   used by %LRG_WAVE_TRIGGER_PROGRESS triggers
 * @active_count: externally-supplied count of live enemies, used by
 *   %LRG_WAVE_TRIGGER_CLEARED triggers
 *
 * Advances the director. Call once per frame while the director is
 * running. Fires wave triggers, dispatches "spawn" signals, and
 * advances through the wave list.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wave_director_update (LrgWaveDirector *self,
                          gfloat           delta,
                          gfloat           progress,
                          guint            active_count);

/**
 * lrg_wave_director_skip_to_wave:
 * @self: an #LrgWaveDirector
 * @index: index of the wave to make current
 *
 * Skips to the given wave, e.g. when restoring a checkpoint. Pending
 * spawns of the current wave are discarded and wave @index becomes
 * current with its trigger state reset (time triggers measure from
 * this moment).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wave_director_skip_to_wave (LrgWaveDirector *self,
                                guint            index);

/* ==========================================================================
 * State Queries
 * ========================================================================== */

/**
 * lrg_wave_director_get_current_wave:
 * @self: an #LrgWaveDirector
 *
 * Gets the index of the current wave.
 *
 * Returns: the current wave index
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wave_director_get_current_wave (LrgWaveDirector *self);

/**
 * lrg_wave_director_get_wave_count:
 * @self: an #LrgWaveDirector
 *
 * Gets the number of waves configured on the director.
 *
 * Returns: the wave count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wave_director_get_wave_count (LrgWaveDirector *self);

/**
 * lrg_wave_director_is_started:
 * @self: an #LrgWaveDirector
 *
 * Checks whether the director has been started.
 *
 * Returns: %TRUE if lrg_wave_director_start() has been called and the
 *   director has not been reset
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wave_director_is_started (LrgWaveDirector *self);

/**
 * lrg_wave_director_is_finished:
 * @self: an #LrgWaveDirector
 *
 * Checks whether all spawns of all waves have been dispatched. Note
 * that this means the director is done spawning, not that all enemies
 * are dead - track live enemies in the game.
 *
 * Returns: %TRUE if every wave has finished spawning
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wave_director_is_finished (LrgWaveDirector *self);

G_END_DECLS
