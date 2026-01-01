/* lrg-deckbuilder-manager.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDeckbuilderManager - Central coordinator for deckbuilder systems.
 *
 * This singleton manages:
 * - Character registry
 * - Unlock system
 * - Player profile
 * - Ascension configuration
 * - Run lifecycle coordination
 *
 * It provides the main entry point for deckbuilder game functionality.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECKBUILDER_MANAGER (lrg_deckbuilder_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDeckbuilderManager, lrg_deckbuilder_manager, LRG, DECKBUILDER_MANAGER, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_default:
 *
 * Gets the default deckbuilder manager singleton.
 *
 * Returns: (transfer none): the default #LrgDeckbuilderManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckbuilderManager * lrg_deckbuilder_manager_get_default (void);

/* ==========================================================================
 * Player Profile
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_profile:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets the current player profile.
 *
 * Returns: (transfer none): the #LrgPlayerProfile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerProfile * lrg_deckbuilder_manager_get_profile (LrgDeckbuilderManager *self);

/**
 * lrg_deckbuilder_manager_set_profile:
 * @self: a #LrgDeckbuilderManager
 * @profile: (transfer none): the profile to use
 *
 * Sets the current player profile.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_deckbuilder_manager_set_profile (LrgDeckbuilderManager *self,
                                           LrgPlayerProfile      *profile);

/* ==========================================================================
 * Character Registry
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_register_character:
 * @self: a #LrgDeckbuilderManager
 * @character: (transfer none): character to register
 *
 * Registers a character definition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_deckbuilder_manager_register_character (LrgDeckbuilderManager *self,
                                                  LrgCharacterDef       *character);

/**
 * lrg_deckbuilder_manager_get_character:
 * @self: a #LrgDeckbuilderManager
 * @id: character ID
 *
 * Gets a character by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgCharacterDef, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCharacterDef * lrg_deckbuilder_manager_get_character (LrgDeckbuilderManager *self,
                                                          const gchar           *id);

/**
 * lrg_deckbuilder_manager_get_characters:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets all registered characters.
 *
 * Returns: (transfer none) (element-type LrgCharacterDef): characters
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deckbuilder_manager_get_characters (LrgDeckbuilderManager *self);

/**
 * lrg_deckbuilder_manager_get_unlocked_characters:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets characters unlocked for the current profile.
 *
 * Returns: (transfer full) (element-type LrgCharacterDef): unlocked characters
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deckbuilder_manager_get_unlocked_characters (LrgDeckbuilderManager *self);

/* ==========================================================================
 * Unlock Registry
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_register_unlock:
 * @self: a #LrgDeckbuilderManager
 * @unlock: (transfer none): unlock definition to register
 *
 * Registers an unlock definition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_deckbuilder_manager_register_unlock (LrgDeckbuilderManager *self,
                                               LrgUnlockDef          *unlock);

/**
 * lrg_deckbuilder_manager_get_unlock:
 * @self: a #LrgDeckbuilderManager
 * @id: unlock ID
 *
 * Gets an unlock by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgUnlockDef, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockDef * lrg_deckbuilder_manager_get_unlock (LrgDeckbuilderManager *self,
                                                    const gchar           *id);

/**
 * lrg_deckbuilder_manager_check_unlocks:
 * @self: a #LrgDeckbuilderManager
 *
 * Checks all unlock conditions and grants any met unlocks.
 *
 * Returns: (transfer full) (element-type LrgUnlockDef): newly granted unlocks
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deckbuilder_manager_check_unlocks (LrgDeckbuilderManager *self);

/* ==========================================================================
 * Ascension
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_ascension:
 * @self: a #LrgDeckbuilderManager
 * @level: ascension level (0-20)
 *
 * Gets or creates an ascension configuration for a level.
 *
 * Returns: (transfer none): the #LrgAscension for that level
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAscension * lrg_deckbuilder_manager_get_ascension (LrgDeckbuilderManager *self,
                                                       gint                   level);

/**
 * lrg_deckbuilder_manager_get_max_ascension:
 * @self: a #LrgDeckbuilderManager
 * @character_id: character ID
 *
 * Gets the max unlocked ascension for a character.
 *
 * Returns: max ascension level
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_deckbuilder_manager_get_max_ascension (LrgDeckbuilderManager *self,
                                                 const gchar           *character_id);

/* ==========================================================================
 * Run Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_current_run:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets the current active run.
 *
 * Returns: (transfer none) (nullable): the current #LrgRun, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRun * lrg_deckbuilder_manager_get_current_run (LrgDeckbuilderManager *self);

/**
 * lrg_deckbuilder_manager_start_run:
 * @self: a #LrgDeckbuilderManager
 * @character_id: character to play
 * @ascension_level: ascension level (0 = normal)
 * @seed: (nullable): seed for RNG, or %NULL for random
 *
 * Starts a new run.
 *
 * Returns: (transfer none): the new #LrgRun
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRun * lrg_deckbuilder_manager_start_run (LrgDeckbuilderManager *self,
                                             const gchar           *character_id,
                                             gint                   ascension_level,
                                             const gchar           *seed);

/**
 * lrg_deckbuilder_manager_end_run:
 * @self: a #LrgDeckbuilderManager
 * @victory: whether the run was won
 *
 * Ends the current run.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_deckbuilder_manager_end_run (LrgDeckbuilderManager *self,
                                       gboolean               victory);

/**
 * lrg_deckbuilder_manager_abandon_run:
 * @self: a #LrgDeckbuilderManager
 *
 * Abandons the current run without completing it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_deckbuilder_manager_abandon_run (LrgDeckbuilderManager *self);

/* ==========================================================================
 * Statistics
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_run_count:
 * @self: a #LrgDeckbuilderManager
 * @character_id: (nullable): character ID, or %NULL for total
 *
 * Gets run count.
 *
 * Returns: number of runs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_deckbuilder_manager_get_run_count (LrgDeckbuilderManager *self,
                                             const gchar           *character_id);

/**
 * lrg_deckbuilder_manager_get_win_count:
 * @self: a #LrgDeckbuilderManager
 * @character_id: (nullable): character ID, or %NULL for total
 *
 * Gets win count.
 *
 * Returns: number of wins
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_deckbuilder_manager_get_win_count (LrgDeckbuilderManager *self,
                                             const gchar           *character_id);

/**
 * lrg_deckbuilder_manager_get_win_rate:
 * @self: a #LrgDeckbuilderManager
 * @character_id: (nullable): character ID, or %NULL for total
 *
 * Gets win rate as a percentage.
 *
 * Returns: win rate (0.0 - 100.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_deckbuilder_manager_get_win_rate (LrgDeckbuilderManager *self,
                                              const gchar           *character_id);

/* ==========================================================================
 * Persistence
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_save:
 * @self: a #LrgDeckbuilderManager
 * @error: (nullable): return location for error
 *
 * Saves all profile data.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_deckbuilder_manager_save (LrgDeckbuilderManager  *self,
                                        GError                **error);

/**
 * lrg_deckbuilder_manager_load:
 * @self: a #LrgDeckbuilderManager
 * @error: (nullable): return location for error
 *
 * Loads profile data.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_deckbuilder_manager_load (LrgDeckbuilderManager  *self,
                                        GError                **error);

G_END_DECLS
